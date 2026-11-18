/*
 * Trách nhiệm: Điểm vào của Server: xử lý tham số dòng lệnh (-p, -m, -v),
 * khởi tạo socket, in IP nội bộ, quản lý tín hiệu (SIGINT/SIGTERM qua sigwait),
 * vòng lặp accept(), từ chối ngay lập tức khi đầy (E1/E2), tắt server an toàn.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "config.h"
#include "clients.h"
#include "handler.h"
#include "net_utils.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

static atomic_int g_running = 1;
static int g_listen_fd = -1;
static pthread_mutex_t g_hb_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_hb_cond = PTHREAD_COND_INITIALIZER;

/* Luồng chuyên biệt đón bắt tín hiệu SIGINT/SIGTERM */
static void *signal_handler_thread(void *arg) {
    sigset_t *set = (sigset_t *)arg;
    int sig = 0;
    while (atomic_load(&g_running)) {
        if (sigwait(set, &sig) == 0) {
            if (sig == SIGINT || sig == SIGTERM) {
                log_info("Nhận tín hiệu dừng (signal %d), bắt đầu quy trình tắt server...", sig);
                atomic_store(&g_running, 0);

                /* Đánh thức luồng heartbeat và socket accept() dưới khoá bảo vệ fd */
                pthread_mutex_lock(&g_hb_lock);
                pthread_cond_broadcast(&g_hb_cond);
                if (g_listen_fd >= 0) {
                    shutdown(g_listen_fd, SHUT_RDWR);
                }
                pthread_mutex_unlock(&g_hb_lock);
                break;
            }
        }
    }
    return NULL;
}

/* Luồng kiểm tra heartbeat định kỳ mỗi 5 giây (thức dậy ngay khi g_running = 0) */
static void *heartbeat_thread(void *arg) {
    (void)arg;
    while (atomic_load(&g_running)) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;

        pthread_mutex_lock(&g_hb_lock);
        if (atomic_load(&g_running)) {
            pthread_cond_timedwait(&g_hb_cond, &g_hb_lock, &ts);
        }
        pthread_mutex_unlock(&g_hb_lock);

        if (!atomic_load(&g_running)) break;
        clients_check_heartbeat();
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    uint16_t port = DEFAULT_PORT;
    int max_clients = MAX_CLIENTS;
    bool verbose = false;

    /* Phân tích tham số dòng lệnh nghiêm ngặt */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 >= argc || !parse_port(argv[++i], &port)) {
                fprintf(stderr, "Cổng không hợp lệ (phải là số nguyên 1..65535)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-m") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Thiếu giá trị cho -m\n");
                return 1;
            }
            char *endp = NULL;
            errno = 0;
            long val = strtol(argv[++i], &endp, 10);
            if (errno != 0 || endp == argv[i] || *endp != '\0' || val < 1 || val > MAX_CLIENTS) {
                fprintf(stderr, "Số client tối đa phải từ 1 đến %d\n", MAX_CLIENTS);
                return 1;
            }
            max_clients = (int)val;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Sử dụng: %s [-p CỔNG] [-m MAX_CLIENTS (1..%d)] [-v]\n", argv[0], MAX_CLIENTS);
            return 0;
        } else {
            fprintf(stderr, "Tham số không hợp lệ: %s\n", argv[i]);
            return 1;
        }
    }

    /* Bỏ qua SIGPIPE để tránh server bị crash khi gửi dữ liệu vào socket đã đóng */
    signal(SIGPIPE, SIG_IGN);

    /* Chặn SIGINT và SIGTERM trên luồng chính để luồng phụ bắt bằng sigwait */
    sigset_t sig_set;
    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGINT);
    sigaddset(&sig_set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sig_set, NULL);

    log_init("logs/server.log", verbose);
    clients_init(max_clients);

    /* Tạo socket lắng nghe */
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        log_error("Không thể tạo socket: %s", strerror(errno));
        log_close();
        return 1;
    }

    if (set_reuse_addr(g_listen_fd) < 0) {
        log_warn("Không thể bật SO_REUSEADDR: %s", strerror(errno));
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(g_listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Lỗi: Cổng %u đang được sử dụng hoặc không có quyền truy cập! (%s)\n",
                port, strerror(errno));
        log_error("Bind thất bại trên cổng %u: %s", port, strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        clients_destroy();
        log_close();
        return 2;
    }

    if (listen(g_listen_fd, LISTEN_BACKLOG) < 0) {
        log_error("Listen thất bại: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        clients_destroy();
        log_close();
        return 1;
    }

    /* Tạo luồng xử lý tín hiệu tắt */
    pthread_t sig_tid;
    if (pthread_create(&sig_tid, NULL, signal_handler_thread, &sig_set) != 0) {
        log_error("Không thể tạo luồng quản lý tín hiệu");
        close(g_listen_fd);
        g_listen_fd = -1;
        clients_destroy();
        log_close();
        return 1;
    }

    /* Khởi động luồng kiểm tra heartbeat */
    pthread_t hb_tid;
    if (pthread_create(&hb_tid, NULL, heartbeat_thread, NULL) != 0) {
        log_error("Không thể tạo luồng heartbeat");
        pthread_kill(sig_tid, SIGTERM);
        pthread_join(sig_tid, NULL);
        close(g_listen_fd);
        g_listen_fd = -1;
        clients_destroy();
        log_close();
        return 1;
    }

    pid_t srv_pid = getpid();
    pid_t listener_tid = (pid_t)syscall(SYS_gettid);
    print_local_ips(port);
    log_info("Server PID (TGID): %d | Main Listener TID (LWP): %d", (int)srv_pid, (int)listener_tid);
    log_info("Lệnh kiểm tra luồng nhân Linux: ps -eLf -q %d", (int)srv_pid);
    log_info("Server đã sẵn sàng (Giới hạn tối đa: %d client đồng thời)", max_clients);

    int listen_fd = g_listen_fd;

    /* Vòng lặp chính tiếp nhận kết nối */
    while (atomic_load(&g_running)) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!atomic_load(&g_running)) break;
            if (errno == EINTR || errno == ECONNABORTED) continue;
            if (errno == EMFILE || errno == ENFILE) {
                log_warn("Hết file descriptor khả dụng, tạm dừng 100ms...");
                usleep(100000);
                continue;
            }
            log_error("Lỗi accept: %s", strerror(errno));
            break;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        uint16_t client_port = ntohs(client_addr.sin_port);

        log_info("Kết nối mới đến từ %s:%u (fd=%d)", ip_str, client_port, client_fd);

        /* Giữ slot NGAY LẬP TỨC dưới khoá g_lock */
        int slot = clients_reserve_slot(client_fd, ip_str, client_port);

        if (slot == -1) {
            /* Phòng đã đủ người -> Từ chối ngay mà không tạo luồng xử lý */
            log_warn("Từ chối kết nối từ %s:%u vì phòng chat đã đầy (%d/%d)",
                     ip_str, client_port, max_clients, max_clients);

            char reject_buf[MAX_LINE];
            snprintf(reject_buf, sizeof(reject_buf),
                     "REJECT FULL Phong chat da du %d/%d nguoi, vui long thu lai sau.\n",
                     max_clients, max_clients);

            set_socket_timeouts(client_fd, 1, 1);
            send_all(client_fd, reject_buf, strlen(reject_buf));
            shutdown(client_fd, SHUT_WR);
            close(client_fd);
            continue;
        }

        /* Tăng bộ đếm TRƯỚC pthread_create để shutdown không bỏ sót luồng vừa tạo */
        handler_inc_active_threads();
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler_thread, (void *)(intptr_t)slot) != 0) {
            log_error("Không thể tạo luồng cho slot %d", slot);
            handler_dec_active_threads();
            clients_cleanup_slot(slot, "SYS_ERR");
            continue;
        }
        pthread_detach(tid);
    }

    /* === QUY TRÌNH SHUTDOWN SERVER AN TOÀN === */
    log_info("Đang thông báo cho các client và dọn dẹp tài nguyên...");
    if (atomic_load(&g_running) != 0) {
        pthread_kill(sig_tid, SIGTERM);
    }

    /* Đợi signal_handler_thread kết thúc trước khi đóng listen_fd (strict FD ownership) */
    pthread_join(sig_tid, NULL);

    pthread_mutex_lock(&g_hb_lock);
    int fd_to_close = g_listen_fd;
    g_listen_fd = -1;
    atomic_store(&g_running, 0);
    pthread_cond_broadcast(&g_hb_cond);
    pthread_mutex_unlock(&g_hb_lock);
    if (fd_to_close >= 0) {
        close(fd_to_close);
    }

    clients_shutdown_all("Server dang tat.");

    /* Chờ các luồng client hoàn tất dọn dẹp (tối đa 3 giây) */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += SHUTDOWN_WAIT_SEC;

    pthread_mutex_lock(&g_threads_lock);
    while (g_active_threads > 0) {
        int wait_res = pthread_cond_timedwait(&g_threads_cond, &g_threads_lock, &ts);
        if (wait_res == ETIMEDOUT) {
            log_warn("Hết thời gian chờ 3 giây, còn %d luồng đang chạy", g_active_threads);
            break;
        }
    }
    pthread_mutex_unlock(&g_threads_lock);

    pthread_join(hb_tid, NULL);

    clients_destroy();
    log_info("Server đã dừng hoàn tất. Tạm biệt!");
    log_close();

    return 0;
}
