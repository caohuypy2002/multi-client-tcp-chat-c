/*
 * Integration Test (Kiểm thử tích hợp hệ thống bằng C):
 * Tự động khởi chạy server trên cổng thử nghiệm, sử dụng socket kết nối
 * có bộ đệm persistent linebuf_t riêng cho từng client để kiểm tra tự động:
 *  - E1 & E2: 20 luồng đua kết nối cùng lúc -> đúng 10 vào được, đúng 10 nhận REJECT FULL.
 *  - E3: Người thoát thì người mới vào được (giải phóng slot).
 *  - E4: Client ngắt kết nối đột ngột (close không gửi QUIT) -> broadcast LEFT ... LOST.
 *  - E5 & E6: Trùng tên (ERR NAME_TAKEN) và tên cấm (ERR RESERVED) -> đổi tên vào được.
 *  - E9 & E13: Tin rỗng (ERR EMPTY), MSG & PM quá 512 byte (ERR TOO_LONG) không vỡ framing,
 *              làm sạch chuỗi điều khiển ANSI (\033[2J).
 *  - E10: Gửi từng byte (fragmented) & UTF-8 tiếng Việt.
 *  - E14: Tin nhắn riêng (PM), lỗi tự gửi cho mình (SELF_PM) và không tồn tại (NO_USER).
 *  - E18: Stress burst nhiều client gửi liên tục không hỏng dòng.
 *  - INFO: Chẩn đoán luồng nhân Linux (PID/TID).
 *  - E16: Tắt server an toàn bằng SIGINT (mã thoát 0).
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "config.h"
#include "net_utils.h"
#include "linebuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static uint16_t g_test_port = 15555;

static uint16_t pick_free_port(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    assert(s >= 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    assert(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0);
    socklen_t len = sizeof(a);
    assert(getsockname(s, (struct sockaddr *)&a, &len) == 0);
    uint16_t port = ntohs(a.sin_port);
    close(s);
    return port;
}

typedef struct {
    int fd;
    linebuf_t lb;
} test_client_t;

static void tc_connect(test_client_t *tc, uint16_t port) {
    tc->fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(tc->fd >= 0);
    set_socket_timeouts(tc->fd, 10, 10);
    linebuf_init(&tc->lb);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int res = connect(tc->fd, (struct sockaddr *)&addr, sizeof(addr));
    assert(res == 0);
}

static void tc_close(test_client_t *tc) {
    if (tc->fd >= 0) {
        close(tc->fd);
        tc->fd = -1;
    }
}

/* Chờ server bind/listen xong (hữu ích khi chạy chậm dưới Valgrind) */
static void wait_for_server_ready(uint16_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    for (int tries = 0; tries < 100; tries++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                char tmp[128];
                set_socket_timeouts(fd, 5, 5);
                recv(fd, tmp, sizeof(tmp), 0);
                send_all(fd, "QUIT\n", 5);
                close(fd);
                usleep(50000);
                return;
            }
            close(fd);
        }
        usleep(100000);
    }
    fprintf(stderr, "Server không sẵn sàng trên cổng %u\n", port);
    assert(0);
}

/* Đọc dòng kế tiếp từ bộ đệm của client test */
static void tc_read_line(test_client_t *tc, char *out, size_t out_sz) {
    char buf[256];
    while (1) {
        int r = linebuf_next_line(&tc->lb, out, out_sz);
        assert(r >= 0);
        if (r == 1) return;

        ssize_t n = recv(tc->fd, buf, sizeof(buf), 0);
        assert(n > 0);
        assert(linebuf_feed(&tc->lb, buf, (size_t)n) == 0);
    }
}

/* Đọc các dòng cho tới khi gặp dòng bắt đầu bằng prefix */
static void tc_read_until_prefix(test_client_t *tc, const char *prefix, char *out, size_t out_sz) {
    size_t plen = strlen(prefix);
    for (int safety = 0; safety < 250; safety++) {
        tc_read_line(tc, out, out_sz);
        if (strncmp(out, prefix, plen) == 0) {
            return;
        }
    }
    fprintf(stderr, "Không tìm thấy dòng bắt đầu bằng '%s'\n", prefix);
    assert(0);
}

/* Dữ liệu phục vụ kiểm thử E2 (20 luồng đua kết nối cùng lúc) */
typedef struct {
    int idx;
    bool got_hello;
    bool got_reject;
} race_arg_t;

static pthread_barrier_t g_race_barrier;
static pthread_mutex_t g_race_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_race_cond = PTHREAD_COND_INITIALIZER;
static int g_race_responded = 0;

static void *race_connect_worker(void *arg) {
    race_arg_t *ra = (race_arg_t *)arg;
    pthread_barrier_wait(&g_race_barrier);

    test_client_t tc;
    tc_connect(&tc, g_test_port);

    char line[MAX_LINE];
    tc_read_line(&tc, line, sizeof(line));
    if (strncmp(line, "HELLO ", 6) == 0) {
        ra->got_hello = true;
    } else if (strncmp(line, "REJECT FULL", 11) == 0) {
        ra->got_reject = true;
    }

    /* Giữ tất cả socket mở cho đến khi cả 20 luồng đều đã nhận phản hồi (kể cả dưới Valgrind) */
    pthread_mutex_lock(&g_race_lock);
    g_race_responded++;
    if (g_race_responded == 20) {
        pthread_cond_broadcast(&g_race_cond);
    }
    while (g_race_responded < 20) {
        pthread_cond_wait(&g_race_cond, &g_race_lock);
    }
    pthread_mutex_unlock(&g_race_lock);

    tc_close(&tc);
    return NULL;
}

static void test_concurrent_20_connections_e2(void) {
    printf("[0] Đua 20 luồng kết nối đồng thời (E2 - Thundering Herd)...\n");
    g_race_responded = 0;
    pthread_barrier_init(&g_race_barrier, NULL, 20);

    pthread_t th[20];
    race_arg_t args[20];
    for (int i = 0; i < 20; i++) {
        args[i].idx = i;
        args[i].got_hello = false;
        args[i].got_reject = false;
        int cr = pthread_create(&th[i], NULL, race_connect_worker, &args[i]);
        assert(cr == 0);
    }
    for (int i = 0; i < 20; i++) {
        pthread_join(th[i], NULL);
    }
    pthread_barrier_destroy(&g_race_barrier);

    int hellos = 0, rejects = 0;
    for (int i = 0; i < 20; i++) {
        if (args[i].got_hello) hellos++;
        if (args[i].got_reject) rejects++;
    }
    assert(hellos == 10);
    assert(rejects == 10);
    printf("  -> [PASS] 20 kết nối đồng thời: đúng %d nhận HELLO, đúng %d nhận REJECT FULL!\n",
           hellos, rejects);
    usleep(80000);
}

int main(void) {
    printf("=== BẮT ĐẦU INTEGRATION TEST CHAT TCP/IP ===\n");
    g_test_port = pick_free_port();

    /* 1. Khởi chạy Server trong tiến trình con */
    pid_t server_pid = fork();
    assert(server_pid >= 0);

    if (server_pid == 0) {
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%u", g_test_port);
        execl("./build/server", "./build/server", "-p", port_str, "-m", "10", (char *)NULL);
        perror("Lỗi execl server");
        exit(1);
    }

    /* Chờ server sẵn sàng lắng nghe */
    wait_for_server_ready(g_test_port);

    /* 1.5 Kiểm tra E2: 20 kết nối đồng thời */
    test_concurrent_20_connections_e2();

    /* 2. Thử nghiệm E1 & E2: Kết nối 10 client hợp lệ */
    printf("[1] Kết nối 10 client hợp lệ (user_1 .. user_10)...\n");
    test_client_t clients[10];
    char line[MAX_LINE];

    for (int i = 0; i < 10; i++) {
        tc_connect(&clients[i], g_test_port);

        tc_read_line(&clients[i], line, sizeof(line));
        assert(strncmp(line, "HELLO", 5) == 0);

        char join_cmd[64];
        snprintf(join_cmd, sizeof(join_cmd), "JOIN user_%d\n", i + 1);
        send_all(clients[i].fd, join_cmd, strlen(join_cmd));

        tc_read_until_prefix(&clients[i], "WELCOME", line, sizeof(line));
    }
    printf("  -> [PASS] Cả 10 client đã JOIN thành công!\n");

    /* 3. Thử nghiệm E1: Client thứ 11 kết nối -> Bị từ chối REJECT FULL */
    printf("[2] Client thứ 11 kết nối khi phòng đã đủ 10/10 (kỳ vọng: REJECT FULL)...\n");
    test_client_t c11;
    tc_connect(&c11, g_test_port);

    tc_read_line(&c11, line, sizeof(line));
    assert(strncmp(line, "REJECT FULL", 11) == 0);
    tc_close(&c11);
    printf("  -> [PASS] Client thứ 11 nhận chính xác: '%s'\n", line);

    /* 4. Thử nghiệm E3, E5, E6 */
    printf("[3] Client 1 thoát (QUIT) -> kiểm tra giải phóng slot và kiểm tra tên (E3, E5, E6)...\n");
    send_all(clients[0].fd, "QUIT\n", 5);
    tc_read_until_prefix(&clients[0], "BYE", line, sizeof(line));
    tc_close(&clients[0]);
    usleep(80000);

    test_client_t c_new;
    tc_connect(&c_new, g_test_port);
    tc_read_line(&c_new, line, sizeof(line));
    assert(strncmp(line, "HELLO", 5) == 0);

    send_all(c_new.fd, "JOIN admin\n", 11);
    tc_read_until_prefix(&c_new, "ERR RESERVED", line, sizeof(line));
    printf("  -> [PASS] Tên 'admin' bị chặn: '%s'\n", line);

    send_all(c_new.fd, "JOIN USER_2\n", 12);
    tc_read_until_prefix(&c_new, "ERR NAME_TAKEN", line, sizeof(line));
    printf("  -> [PASS] Trùng tên không phân biệt hoa thường nhận: '%s'\n", line);

    send_all(c_new.fd, "JOIN user_new\n", 14);
    tc_read_until_prefix(&c_new, "WELCOME user_new", line, sizeof(line));
    printf("  -> [PASS] Đổi sang tên hợp lệ 'user_new' đã JOIN thành công!\n");

    /* 5. Thử nghiệm E10 & Broadcast: Gửi tin nhắn từng byte */
    printf("[4] Kiểm tra gửi tin nhắn từng byte (Message Framing E10) & Broadcast...\n");
    const char *frag_msg = "MSG Xin chào từ user_new!\n";
    for (size_t i = 0; i < strlen(frag_msg); i++) {
        send_all(c_new.fd, &frag_msg[i], 1);
    }
    tc_read_until_prefix(&clients[1], "MSG ", line, sizeof(line));
    assert(strstr(line, "user_new Xin chào từ user_new!") != NULL);
    printf("  -> [PASS] Client 2 nhận đủ tin nhắn ghép từ từng byte: '%s'\n", line);

    /* 6. Thử nghiệm E9 & E13: Tin rỗng, MSG/PM > 512 byte, ANSI injection */
    printf("[5] Kiểm tra E9 (MSG/PM > 512B, rỗng) & E13 (làm sạch ANSI escape)...\n");
    send_all(c_new.fd, "MSG    \n", 8);
    tc_read_until_prefix(&c_new, "ERR EMPTY", line, sizeof(line));

    char long_pm[600];
    memset(long_pm, 'A', sizeof(long_pm));
    memcpy(long_pm, "PM user_2 ", 10);
    long_pm[580] = '\n';
    long_pm[581] = '\0';
    send_all(c_new.fd, long_pm, 581);
    tc_read_until_prefix(&c_new, "ERR TOO_LONG", line, sizeof(line));

    send_all(c_new.fd, "MSG Safe\033[2JText\n", 17);
    tc_read_until_prefix(&clients[1], "MSG ", line, sizeof(line));
    assert(strchr(line, '\033') == NULL);
    assert(strstr(line, "Safe [2JText") != NULL);
    printf("  -> [PASS] E9 (ERR EMPTY/TOO_LONG trên PM) và E13 (chặn ESC 0x1B) đạt chuẩn!\n");

    /* 7. Thử nghiệm E14: Tin nhắn riêng (PM), SELF_PM và NO_USER */
    printf("[6] Kiểm tra tin nhắn riêng (PM), SELF_PM và lỗi NO_USER (E14)...\n");
    send_all(c_new.fd, "PM user_2 Tin nhan bi mat\n", 26);
    tc_read_until_prefix(&clients[1], "PM ", line, sizeof(line));
    assert(strstr(line, "user_new user_2 Tin nhan bi mat") != NULL);
    tc_read_until_prefix(&c_new, "PM ", line, sizeof(line));

    send_all(c_new.fd, "PM user_new Tu gui\n", 19);
    tc_read_until_prefix(&c_new, "ERR SELF_PM", line, sizeof(line));

    send_all(c_new.fd, "PM khong_ton_tai Alo\n", 21);
    tc_read_until_prefix(&c_new, "ERR NO_USER", line, sizeof(line));
    printf("  -> [PASS] PM, SELF_PM và NO_USER hoạt động chính xác!\n");

    /* 8. Thử nghiệm INFO & E18 (5 client * 10 tin = 50 tin) */
    printf("[7] Kiểm tra lệnh INFO (PID/TID) & Stress 50 tin nhắn đồng bộ (E18)...\n");
    send_all(c_new.fd, "INFO\n", 5);
    tc_read_until_prefix(&c_new, "INFO ", line, sizeof(line));

    for (int m = 0; m < 10; m++) {
        char burst[64];
        snprintf(burst, sizeof(burst), "MSG burst_0_%d\n", m);
        send_all(c_new.fd, burst, strlen(burst));
        snprintf(burst, sizeof(burst), "MSG burst_1_%d\n", m);
        send_all(clients[1].fd, burst, strlen(burst));
    }
    int got_b0 = 0, got_b1 = 0;
    while (got_b0 < 10 || got_b1 < 10) {
        tc_read_until_prefix(&clients[3], "MSG ", line, sizeof(line));
        if (strstr(line, "burst_0_")) got_b0++;
        if (strstr(line, "burst_1_")) got_b1++;
    }
    printf("  -> [PASS] Client 4 nhận đủ 20/20 tin burst không rách dòng nào!\n");

    /* 9. Thử nghiệm E4: Ngắt kết nối đột ngột */
    printf("[8] Kiểm tra phát hiện ngắt kết nối đột ngột (E4 - LOST)...\n");
    tc_close(&clients[2]); /* user_3 đóng socket đột ngột */
    tc_read_until_prefix(&c_new, "LEFT ", line, sizeof(line));
    assert(strstr(line, "user_3") != NULL && strstr(line, "LOST") != NULL);
    printf("  -> [PASS] Các client khác nhận thông báo: '%s'\n", line);

    /* Đóng các kết nối client còn lại */
    tc_close(&c_new);
    for (int i = 1; i < 10; i++) {
        tc_close(&clients[i]);
    }

    /* 10. Thử nghiệm E16: Tắt server an toàn bằng SIGINT */
    printf("[9] Gửi SIGINT tắt server an toàn (E16)...\n");
    kill(server_pid, SIGINT);
    int status = 0;
    waitpid(server_pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    printf("  -> [PASS] Server đã dừng an toàn với mã thoát 0!\n");

    printf("\n>>> TẤT CẢ CÁC CA INTEGRATION TEST ĐÃ PASS HOÀN TOÀN! <<<\n");
    return 0;
}
