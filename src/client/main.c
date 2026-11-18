/*
 * Trách nhiệm: Điểm vào của Client: kết nối socket có timeout,
 * handshake an toàn (chờ HELLO, gửi JOIN, hỗ trợ đổi tên khi trùng),
 * vòng lặp sự kiện đơn luồng sử dụng poll() kết hợp read() + linebuf trên STDIN và socket,
 * xử lý các lệnh /help, /users (/list), /threads (/info), /w (/msg), /quit (/exit)
 * và phím tắt Ctrl+C, Ctrl+D.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "config.h"
#include "protocol.h"
#include "linebuf.h"
#include "net_utils.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <sys/socket.h>

static volatile sig_atomic_t g_client_running = 1;

/* Signal handler hoàn toàn async-signal-safe: chỉ gán biến cờ nguyên tử */
static void sigint_handler(int sig) {
    (void)sig;
    g_client_running = 0;
}

/* Trả về con trỏ phần nội dung sau tiền tố nếu khớp nguyên vẹn, NULL nếu không khớp */
static const char *after_prefix(const char *line, const char *prefix) {
    size_t plen = strlen(prefix);
    if (strncmp(line, prefix, plen) != 0) return NULL;
    if (line[plen] == '\0') return line + plen;
    if (line[plen] == ' ') return line + plen + 1;
    return NULL;
}

/* Đọc 1 dòng từ fd bằng read() + linebuf để đồng bộ hoàn hảo với poll() */
static int read_fd_line(int fd, linebuf_t *lb, char *out, size_t out_sz) {
    char chunk[256];
    while (g_client_running) {
        int r = linebuf_next_line(lb, out, out_sz);
        if (r != 0) return r;

        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -2;
        }
        if (n == 0) {
            /* Nếu gặp EOF mà vẫn còn chữ chưa có \n, trả về nốt phần còn lại */
            if (lb->len > 0) {
                size_t copy_len = (lb->len < out_sz) ? lb->len : out_sz - 1;
                memcpy(out, lb->buf, copy_len);
                out[copy_len] = '\0';
                lb->len = 0;
                return 1;
            }
            return 0; /* EOF */
        }
        if (linebuf_feed(lb, chunk, (size_t)n) != 0) {
            linebuf_init(lb);
            return -1;
        }
    }
    return 0;
}

/* Định dạng và gửi lệnh an toàn: không bao giờ để snprintf làm rơi '\n' */
static void send_formatted_cmd(int sock_fd, const char *fmt_prefix, const char *arg1, const char *arg2) {
    char buf[MAX_LINE];
    int written = 0;
    if (arg2 != NULL) {
        written = snprintf(buf, sizeof(buf), "%s %s %s\n", fmt_prefix, arg1, arg2);
    } else if (arg1 != NULL) {
        written = snprintf(buf, sizeof(buf), "%s %s\n", fmt_prefix, arg1);
    } else {
        written = snprintf(buf, sizeof(buf), "%s\n", fmt_prefix);
    }

    if (written < 0 || (size_t)written >= sizeof(buf)) {
        ui_print_error("Nội dung quá dài, vượt giới hạn khung truyền (1024 byte).");
        return;
    }
    send_all(sock_fd, buf, (size_t)written);
}

/* Xử lý một dòng nhận được từ Server; trả về false nếu gặp BYE cần ngắt kết nối */
static bool handle_server_line(char *line, int sock_fd, const char *nickname) {
    if (strcmp(line, "PING") == 0) {
        send_all(sock_fd, "PONG\n", 5);
        return true;
    }
    if (strncmp(line, "MSG ", 4) == 0) {
        char *saveptr = NULL;
        const char *t_tok = strtok_r(line + 4, " ", &saveptr);
        const char *from = strtok_r(NULL, " ", &saveptr);
        if (t_tok != NULL && from != NULL) {
            const char *msg_text = saveptr ? saveptr : "";
            bool is_me = (strcasecmp(from, nickname) == 0);
            ui_print_msg(t_tok, from, msg_text, is_me);
        }
        return true;
    }
    if (strncmp(line, "PM ", 3) == 0) {
        char *saveptr = NULL;
        const char *t_tok = strtok_r(line + 3, " ", &saveptr);
        const char *from = strtok_r(NULL, " ", &saveptr);
        const char *to = strtok_r(NULL, " ", &saveptr);
        if (t_tok != NULL && from != NULL && to != NULL) {
            const char *text = saveptr ? saveptr : "";
            bool is_out = (strcasecmp(from, nickname) == 0);
            ui_print_pm(t_tok, from, to, text, is_out);
        }
        return true;
    }
    if (strncmp(line, "JOINED ", 7) == 0) {
        char time_str[16] = {0}, name[MAX_NAME_LEN + 1] = {0};
        int count = 0, max = 0;
        sscanf(line + 7, "%15s %16s %d %d", time_str, name, &count, &max);
        char sys_msg[MAX_LINE];
        snprintf(sys_msg, sizeof(sys_msg), "[%s] %s đã tham gia phòng chat (%d/%d)",
                 time_str, name, count, max);
        ui_print_system(sys_msg);
        return true;
    }
    if (strncmp(line, "LEFT ", 5) == 0) {
        char time_str[16] = {0}, name[MAX_NAME_LEN + 1] = {0}, reason[32] = {0};
        int count = 0, max = 0;
        sscanf(line + 5, "%15s %16s %d %d %31s", time_str, name, &count, &max, reason);
        char sys_msg[MAX_LINE];
        snprintf(sys_msg, sizeof(sys_msg), "[%s] %s đã rời phòng chat (%d/%d) [%s]",
                 time_str, name, count, max, reason);
        ui_print_system(sys_msg);
        return true;
    }
    if (strncmp(line, "USERS ", 6) == 0) {
        int count = 0, max = 0;
        char users_list[MAX_LINE] = {0};
        sscanf(line + 6, "%d %d %1023s", &count, &max, users_list);
        ui_print_user_list(count, max, users_list);
        return true;
    }
    if (strncmp(line, "INFO ", 5) == 0) {
        int srv_pid = 0, my_tid = 0, count = 0, max = 0;
        char details[MAX_LINE] = {0};
        sscanf(line + 5, "%d %d %d %d %1023s", &srv_pid, &my_tid, &count, &max, details);
        ui_print_thread_info(srv_pid, my_tid, count, max, details);
        return true;
    }

    const char *err_body = after_prefix(line, "ERR");
    if (err_body != NULL) {
        ui_print_error(err_body);
        return true;
    }

    const char *bye_body = after_prefix(line, "BYE");
    if (bye_body != NULL) {
        ui_print_system(*bye_body ? bye_body : "Đã ngắt kết nối.");
        return false;
    }
    return true;
}

/* Xử lý 1 dòng nhập từ người dùng; trả về true nếu gửi QUIT */
static bool process_user_line(char *raw_line, int sock_fd) {
    char *input = protocol_trim_whitespace(raw_line);
    if (input == NULL || *input == '\0') {
        ui_print_prompt();
        return false;
    }

    if (input[0] == '/') {
        if (strcasecmp(input, "/help") == 0) {
            ui_print_help();
        } else if (strcasecmp(input, "/users") == 0 || strcasecmp(input, "/list") == 0) {
            send_all(sock_fd, "LIST\n", 5);
        } else if (strcasecmp(input, "/threads") == 0 || strcasecmp(input, "/info") == 0) {
            send_all(sock_fd, "INFO\n", 5);
        } else if (strcasecmp(input, "/quit") == 0 || strcasecmp(input, "/exit") == 0) {
            send_all(sock_fd, "QUIT\n", 5);
            return true;
        } else if (strncasecmp(input, "/w ", 3) == 0 || strncasecmp(input, "/msg ", 5) == 0) {
            char *rest = (strncasecmp(input, "/w ", 3) == 0) ? (input + 3) : (input + 5);
            char *save_ptr = NULL;
            const char *target_tok = strtok_r(rest, " \t", &save_ptr);
            if (target_tok != NULL && save_ptr != NULL) {
                const char *pm_text = protocol_trim_whitespace(save_ptr);
                if (pm_text != NULL && *pm_text != '\0') {
                    send_formatted_cmd(sock_fd, "PM", target_tok, pm_text);
                } else {
                    ui_print_error("Cú pháp: /w <tên> <nội dung tin nhắn>");
                }
            } else {
                ui_print_error("Cú pháp: /w <tên> <nội dung tin nhắn>");
            }
        } else {
            ui_print_error("Lệnh không hợp lệ. Gõ /help để xem trợ giúp.");
        }
    } else {
        send_formatted_cmd(sock_fd, "MSG", input, NULL);
    }
    return false;
}

int main(int argc, char *argv[]) {
    char host[128] = "127.0.0.1";
    uint16_t port = DEFAULT_PORT;
    char nickname[MAX_NAME_LEN + 1] = {0};
    bool has_host = false;
    bool has_port = false;
    bool no_color = false;

    /* Đọc tham số dòng lệnh */
    int arg_idx = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-color") == 0) {
            no_color = true;
        } else if (arg_idx == 1) {
            snprintf(host, sizeof(host), "%s", argv[i]);
            has_host = true;
            arg_idx++;
        } else if (arg_idx == 2) {
            if (!parse_port(argv[i], &port)) {
                fprintf(stderr, "Cổng không hợp lệ: '%s' (phải là 1..65535)\n", argv[i]);
                return 1;
            }
            has_port = true;
            arg_idx++;
        } else if (arg_idx == 3) {
            snprintf(nickname, sizeof(nickname), "%s", argv[i]);
            arg_idx++;
        }
    }

    ui_init(no_color);
    signal(SIGPIPE, SIG_IGN);

    /* Đăng ký SIGINT không có SA_RESTART để đánh thức poll/read/recv ngay lập tức */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    linebuf_t stdin_lb;
    linebuf_init(&stdin_lb);
    char input_line[MAX_LINE];

    /* Hỏi tương tác Host và Port nếu thiếu tham số (M2) */
    if (!has_host) {
        printf("Nhập IP/Hostname server [127.0.0.1]: ");
        fflush(stdout);
        int rr = read_fd_line(STDIN_FILENO, &stdin_lb, input_line, sizeof(input_line));
        if (rr <= 0) return 0;
        const char *trimmed = protocol_trim_whitespace(input_line);
        if (trimmed != NULL && *trimmed != '\0') {
            snprintf(host, sizeof(host), "%s", trimmed);
        }
    }

    while (!has_port && g_client_running) {
        printf("Nhập cổng server [%d]: ", DEFAULT_PORT);
        fflush(stdout);
        int rr = read_fd_line(STDIN_FILENO, &stdin_lb, input_line, sizeof(input_line));
        if (rr <= 0) return 0;
        const char *trimmed = protocol_trim_whitespace(input_line);
        if (trimmed == NULL || *trimmed == '\0') {
            port = DEFAULT_PORT;
            has_port = true;
        } else if (parse_port(trimmed, &port)) {
            has_port = true;
        } else {
            printf("Cổng không hợp lệ, vui lòng nhập số từ 1 đến 65535.\n");
        }
    }

    if (!g_client_running) return 0;

    printf("Đang kết nối tới server %s:%u (thời gian chờ %ds)...\n",
           host, port, CONNECT_TIMEOUT_SEC);

    int sock_fd = connect_with_timeout(host, port, CONNECT_TIMEOUT_SEC);
    if (sock_fd < 0) {
        fprintf(stderr, "Không thể kết nối tới server tại %s:%u: %s\n",
                host, port, strerror(errno));
        return 1;
    }

    linebuf_t s_rbuf;
    linebuf_init(&s_rbuf);

    char line[MAX_LINE];
    char recv_buf[512];

    /* --- BƯỚC 1: CHỜ LỜI CHÀO HELLO HOẶC REJECT FULL TỪ SERVER --- */
    bool greeted = false;
    while (!greeted && g_client_running) {
        ssize_t n = recv(sock_fd, recv_buf, sizeof(recv_buf), 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR && !g_client_running) {
                close(sock_fd);
                return 0;
            }
            fprintf(stderr, "Lỗi: Server đã đóng kết nối trước khi bắt tay.\n");
            close(sock_fd);
            return 1;
        }
        linebuf_feed(&s_rbuf, recv_buf, (size_t)n);
        int lr = linebuf_next_line(&s_rbuf, line, sizeof(line));
        if (lr == 1) {
            const char *rej_full = after_prefix(line, "REJECT FULL");
            if (rej_full != NULL) {
                printf("\n*** THÔNG BÁO TỪ SERVER ***\n%s\n",
                       *rej_full ? rej_full : "Phòng chat đã đầy.");
                close(sock_fd);
                return 0; /* Thoát êm khi phòng đầy */
            } else if (strncmp(line, "HELLO", 5) == 0) {
                int count = 0, max = 0;
                sscanf(line, "HELLO %d %d", &count, &max);
                printf("Kết nối thành công! (Hiện có %d/%d người trong phòng)\n", count, max);
                greeted = true;
            } else {
                fprintf(stderr, "Phản hồi không xác định từ server: %s\n", line);
                close(sock_fd);
                return 1;
            }
        }
    }

    /* --- BƯỚC 2: NHẬP VÀ ĐĂNG KÝ NICKNAME (JOIN) --- */
    bool joined = false;
    while (!joined && g_client_running) {
        if (nickname[0] == '\0') {
            printf("Nhập nickname của bạn (1-16 ký tự, chỉ chữ/số/_): ");
            fflush(stdout);
            if (read_fd_line(STDIN_FILENO, &stdin_lb, input_line, sizeof(input_line)) <= 0) {
                close(sock_fd);
                return 0;
            }
            const char *trimmed = protocol_trim_whitespace(input_line);
            snprintf(nickname, sizeof(nickname), "%s", trimmed);
        }

        const char *v_code = NULL;
        const char *v_msg = NULL;
        if (!protocol_validate_nickname(nickname, &v_code, &v_msg)) {
            printf("Nickname không hợp lệ (%s): %s\n", v_code, v_msg);
            nickname[0] = '\0';
            continue;
        }

        char join_cmd[MAX_LINE];
        snprintf(join_cmd, sizeof(join_cmd), "JOIN %s\n", nickname);
        send_all(sock_fd, join_cmd, strlen(join_cmd));

        /* Đợi phản hồi WELCOME hoặc ERR từ server */
        bool responded = false;
        while (!responded && g_client_running) {
            ssize_t n = recv(sock_fd, recv_buf, sizeof(recv_buf), 0);
            if (n <= 0) {
                if (n < 0 && errno == EINTR && !g_client_running) break;
                fprintf(stderr, "Mất kết nối tới server trong lúc gửi tên.\n");
                close(sock_fd);
                return 1;
            }
            linebuf_feed(&s_rbuf, recv_buf, (size_t)n);
            while (linebuf_next_line(&s_rbuf, line, sizeof(line)) == 1) {
                const char *err_body = NULL;
                const char *rej_body = NULL;
                if (strncmp(line, "WELCOME", 7) == 0) {
                    char w_name[MAX_NAME_LEN + 1] = {0};
                    int w_count = 0, w_max = 0;
                    sscanf(line, "WELCOME %16s %d %d", w_name, &w_count, &w_max);
                    printf("\n=======================================================\n");
                    printf(" Chào mừng %s đã tham gia phòng chat! (%d/%d người)\n",
                           w_name, w_count, w_max);
                    printf(" Lệnh nhanh: /help, /users (/list), /threads, /w, /quit\n");
                    printf("=======================================================\n\n");
                    joined = true;
                    responded = true;
                    break;
                } else if ((err_body = after_prefix(line, "ERR")) != NULL) {
                    printf("Từ chối tên: %s\n", err_body);
                    nickname[0] = '\0'; /* Yêu cầu nhập lại tên khác */
                    responded = true;
                    break;
                } else if ((rej_body = after_prefix(line, "REJECT")) != NULL) {
                    printf("Bị từ chối: %s\n", rej_body);
                    close(sock_fd);
                    return 1;
                }
            }
        }
    }

    if (!joined) {
        close(sock_fd);
        return 0;
    }

    ui_print_prompt();

    /* Xử lý nốt mọi dòng còn sẵn trong s_rbuf sau WELCOME (JOINED, USERS, MSG, PM...) */
    int pre_res;
    while ((pre_res = linebuf_next_line(&s_rbuf, line, sizeof(line))) != 0) {
        if (pre_res < 0) continue;
        if (!handle_server_line(line, sock_fd, nickname)) {
            close(sock_fd);
            return 0;
        }
    }

    /* --- BƯỚC 3: VÒNG LẶP SỰ KIỆN CHAT SỬ DỤNG POLL() --- */
    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;

    pfds[1].fd = sock_fd;
    pfds[1].events = POLLIN;

    char in_chunk[512];
    bool sent_quit = false;
    bool closing = false;

    /* Nếu người dùng đã nhập/pipe sẵn dòng lệnh từ Bước 2, xử lý ngay */
    while (!closing) {
        int pre_in = linebuf_next_line(&stdin_lb, input_line, sizeof(input_line));
        if (pre_in == 0) break;
        if (pre_in < 0) {
            ui_print_error("Dòng nhập quá dài (> 1024 byte).");
            continue;
        }
        if (process_user_line(input_line, sock_fd)) {
            sent_quit = true;
            closing = true;
            pfds[0].fd = -1;
        }
    }

    while (g_client_running) {
        /* Khi đang đóng sau /quit, chờ tối đa 1000ms để nhận hết phản hồi và BYE từ server */
        int poll_res = poll(pfds, 2, closing ? 1000 : -1);
        if (poll_res < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (poll_res == 0 && closing) {
            break;
        }

        /* 1. Có dữ liệu từ bàn phím người dùng (dùng read() đồng bộ với poll) */
        if (pfds[0].fd >= 0 && (pfds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t rn = read(STDIN_FILENO, in_chunk, sizeof(in_chunk));
            if (rn == 0) {
                /* Ctrl+D (EOF): xử lý nốt phần chữ đệm nếu có, rồi gửi QUIT */
                if (stdin_lb.len > 0) {
                    size_t copy_len = (stdin_lb.len < sizeof(input_line)) ? stdin_lb.len : sizeof(input_line) - 1;
                    memcpy(input_line, stdin_lb.buf, copy_len);
                    input_line[copy_len] = '\0';
                    stdin_lb.len = 0;
                    process_user_line(input_line, sock_fd);
                }
                if (g_client_running) {
                    printf("\nĐang rời phòng chat...\n");
                }
                send_all(sock_fd, "QUIT\n", 5);
                sent_quit = true;
                closing = true;
                pfds[0].fd = -1;
            } else if (rn < 0) {
                if (errno != EINTR) break;
            } else {
                if (linebuf_feed(&stdin_lb, in_chunk, (size_t)rn) != 0) {
                    linebuf_init(&stdin_lb);
                    ui_print_error("Dữ liệu nhập vượt quá bộ đệm (2048 byte).");
                } else {
                    int lr;
                    while ((lr = linebuf_next_line(&stdin_lb, input_line, sizeof(input_line))) != 0) {
                        if (lr < 0) {
                            ui_print_error("Dòng nhập quá dài (> 1024 byte).");
                            continue;
                        }
                        if (process_user_line(input_line, sock_fd)) {
                            sent_quit = true;
                            closing = true;
                            pfds[0].fd = -1;
                            break;
                        }
                    }
                }
            }
        }

        /* 2. Có dữ liệu từ Server */
        if (pfds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = recv(sock_fd, recv_buf, sizeof(recv_buf), 0);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                if (!closing) {
                    ui_print_system("Mất kết nối tới máy chủ.");
                }
                sent_quit = true;
                break;
            }

            if (linebuf_feed(&s_rbuf, recv_buf, (size_t)n) != 0) {
                linebuf_init(&s_rbuf);
            }
            int lr;
            while ((lr = linebuf_next_line(&s_rbuf, line, sizeof(line))) != 0) {
                if (lr < 0) continue;
                if (!handle_server_line(line, sock_fd, nickname)) {
                    sent_quit = true;
                    g_client_running = 0;
                    break;
                }
            }
        }
    }

    if (!sent_quit && sock_fd >= 0) {
        send_all(sock_fd, "QUIT\n", 5);
    }

    printf("\nĐóng ứng dụng chat. Hẹn gặp lại!\n");
    close(sock_fd);
    return 0;
}