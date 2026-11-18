/*
 * Trách nhiệm: Quản lý vòng đời kết nối của một client trong một luồng riêng biệt:
 * handshake ban đầu (chào HELLO, nhận JOIN), xử lý các lệnh MSG, PM, LIST, INFO, PONG, QUIT,
 * thực thi quy tắc FD OWNERSHIP (chỉ luồng này mới gọi close fd qua clients_cleanup_slot).
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "handler.h"
#include "clients.h"
#include "protocol.h"
#include "net_utils.h"
#include "log.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/syscall.h>

int g_active_threads = 0;
pthread_mutex_t g_threads_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_threads_cond = PTHREAD_COND_INITIALIZER;

void handler_inc_active_threads(void) {
    pthread_mutex_lock(&g_threads_lock);
    g_active_threads++;
    pthread_mutex_unlock(&g_threads_lock);
}

void handler_dec_active_threads(void) {
    pthread_mutex_lock(&g_threads_lock);
    g_active_threads--;
    if (g_active_threads == 0) {
        pthread_cond_broadcast(&g_threads_cond);
    }
    pthread_mutex_unlock(&g_threads_lock);
}

/* Làm sạch và kiểm tra độ dài nội dung dùng chung cho cả MSG và PM */
static const char *validate_chat_text(int slot, char *buf) {
    protocol_sanitize_text(buf);
    const char *trimmed = protocol_trim_whitespace(buf);
    if (trimmed == NULL || *trimmed == '\0') {
        clients_send_raw(slot, "ERR EMPTY Noi dung tin nhan khong duoc de trong.\n");
        return NULL;
    }
    if (strlen(trimmed) > MAX_MSG_LEN) {
        clients_send_raw(slot, "ERR TOO_LONG Tin nhan vuot qua 512 byte.\n");
        return NULL;
    }
    return trimmed;
}

void *client_handler_thread(void *arg) {
    int slot = (int)(intptr_t)arg;

    int client_fd = -1;
    char client_ip[INET_ADDRSTRLEN] = {0};
    uint16_t client_port = 0;
    pid_t my_tid = (pid_t)syscall(SYS_gettid);

    pthread_mutex_lock(&g_lock);
    client_fd = g_clients[slot].fd;
    g_clients[slot].tid = my_tid;
    snprintf(client_ip, sizeof(client_ip), "%s", g_clients[slot].ip);
    client_port = g_clients[slot].port;
    pthread_mutex_unlock(&g_lock);

    log_info("[THREAD SPAWNED] Slot %d | Worker TID=%d (LWP) | FD=%d | Server PID=%d",
             slot, (int)my_tid, client_fd, (int)getpid());

    /* Đặt timeout nhận trong giai đoạn handshake là 10 giây */
    set_socket_timeouts(client_fd, HANDSHAKE_TIMEOUT_SEC, SEND_TIMEOUT_SEC);

    int in_use_cnt = clients_get_in_use_count();
    int max_c = clients_get_max_clients();
    char hello_buf[MAX_LINE];
    snprintf(hello_buf, sizeof(hello_buf), "HELLO %d %d\n", in_use_cnt, max_c);
    clients_send_raw(slot, hello_buf);

    char line[MAX_LINE];
    char recv_buf[512];

    const char *exit_reason = "LOST";
    bool handshake_done = false;
    int name_tries = 0;

    while (1) {
        ssize_t n = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        if (n == 0) {
            /* Client đóng kết nối */
            exit_reason = "LOST";
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!handshake_done) {
                    /* Quá 10s không gửi JOIN */
                    clients_send_raw(slot, "REJECT TIMEOUT Ban khong gui JOIN trong 10 giay.\n");
                    exit_reason = "TIMEOUT";
                    break;
                } else {
                    /* Tiếp tục đợi tin nhắn hoặc xử lý ping */
                    continue;
                }
            }
            /* Lỗi socket hoặc socket bị shutdown từ xa */
            exit_reason = "LOST";
            break;
        }

        /* Nạp dữ liệu vào buffer framing của client */
        pthread_mutex_lock(&g_lock);
        int feed_res = linebuf_feed(&g_clients[slot].rbuf, recv_buf, (size_t)n);
        pthread_mutex_unlock(&g_lock);

        if (feed_res != 0) {
            clients_send_raw(slot, "ERR TOO_LONG Bo dem bi tran.\n");
            exit_reason = "KICKED";
            break;
        }

        /* Tách và xử lý từng dòng hoàn chỉnh */
        while (1) {
            pthread_mutex_lock(&g_lock);
            int parse_res = linebuf_next_line(&g_clients[slot].rbuf, line, sizeof(line));
            pthread_mutex_unlock(&g_lock);

            if (parse_res == 0) break; /* Chưa đủ 1 dòng hoàn chỉnh */

            if (parse_res == -1) {
                clients_send_raw(slot, "ERR TOO_LONG Dong vuot qua 1024 byte.\n");
                continue;
            }

            pthread_mutex_lock(&g_lock);
            g_clients[slot].last_seen = time(NULL);
            pthread_mutex_unlock(&g_lock);

            parsed_cmd_t cmd;
            bool ok = protocol_parse_client_line(line, &cmd);

            if (!handshake_done) {
                /* Trong giai đoạn handshake, chỉ chấp nhận lệnh JOIN hoặc QUIT */
                if (!ok || (cmd.type != CMD_JOIN && cmd.type != CMD_QUIT)) {
                    clients_send_raw(slot, "ERR NOT_JOINED Ban phai gui JOIN <nickname> truoc.\n");
                    continue;
                }

                if (cmd.type == CMD_QUIT) {
                    clients_send_raw(slot, "BYE Tam biet!\n");
                    exit_reason = "QUIT";
                    goto cleanup;
                }

                char err_code[32] = {0};
                char err_msg[128] = {0};
                if (!clients_register_name(slot, cmd.text, err_code, err_msg, sizeof(err_msg))) {
                    name_tries++;
                    if (name_tries >= MAX_NAME_TRIES) {
                        clients_send_raw(slot, "REJECT TOO_MANY_TRIES Thu ten sai qua 3 lan.\n");
                        exit_reason = "KICKED";
                        goto cleanup;
                    }

                    char rep[MAX_LINE];
                    snprintf(rep, sizeof(rep), "ERR %s %s\n", err_code, err_msg);
                    clients_send_raw(slot, rep);
                    continue;
                }

                /* Đăng ký tên thành công */
                handshake_done = true;

                /* Chuyển timeout đọc sang 5 giây để kiểm tra heartbeat linh hoạt */
                set_socket_timeouts(client_fd, 5, SEND_TIMEOUT_SEC);

                int joined_cnt = clients_get_joined_count();
                int cur_max = clients_get_max_clients();
                char welcome[MAX_LINE];
                snprintf(welcome, sizeof(welcome), "WELCOME %.16s %d %d\n", cmd.text, joined_cnt, cur_max);
                clients_send_raw(slot, welcome);

                log_info("Client [%s:%u | TID=%d] da dang ky nickname: '%s' (%d/%d)",
                         client_ip, client_port, (int)my_tid, cmd.text, joined_cnt, cur_max);

                /* Thông báo phòng chat có thành viên mới */
                clients_broadcast_system_join(slot);
                continue;
            }

            /* --- TRẠNG THÁI CHAT: ĐÃ THAM GIA PHÒNG CHAT (JOINED) --- */
            if (!ok) {
                pthread_mutex_lock(&g_lock);
                g_clients[slot].bad_count++;
                int cur_bad = g_clients[slot].bad_count;
                pthread_mutex_unlock(&g_lock);

                if (cur_bad >= MAX_BAD_COMMANDS) {
                    clients_send_raw(slot, "BYE Ban bi ngat ket noi vi gui qua nhieu lenh sai.\n");
                    exit_reason = "KICKED";
                    goto cleanup;
                }
                clients_send_raw(slot, "ERR BAD_CMD Cu phap lenh khong hop le.\n");
                continue;
            }

            pthread_mutex_lock(&g_lock);
            g_clients[slot].bad_count = 0;
            char my_name[MAX_NAME_LEN + 1];
            snprintf(my_name, sizeof(my_name), "%s", g_clients[slot].name);
            pthread_mutex_unlock(&g_lock);

            if (cmd.type == CMD_MSG) {
                const char *trimmed = validate_chat_text(slot, cmd.text);
                if (trimmed == NULL) continue;

                char time_str[16];
                protocol_get_time_str(time_str, sizeof(time_str));

                char broadcast_buf[MAX_LINE];
                snprintf(broadcast_buf, sizeof(broadcast_buf), "MSG %s %s %s\n",
                         time_str, my_name, trimmed);

                log_msg("[%s | TID=%d] %s: %s", time_str, (int)my_tid, my_name, trimmed);
                clients_broadcast(broadcast_buf, -1);
            } else if (cmd.type == CMD_PM) {
                const char *trimmed = validate_chat_text(slot, cmd.text);
                if (trimmed == NULL) continue;

                if (strcasecmp(cmd.target, my_name) == 0) {
                    clients_send_raw(slot, "ERR SELF_PM Khong the gui tin nhan rieng cho chinh minh.\n");
                    continue;
                }

                if (!clients_send_pm(slot, cmd.target, trimmed)) {
                    char err_rep[MAX_LINE];
                    snprintf(err_rep, sizeof(err_rep),
                             "ERR NO_USER Nguoi dung '%s' khong ton tai hoac da offline.\n", cmd.target);
                    clients_send_raw(slot, err_rep);
                }
            } else if (cmd.type == CMD_LIST) {
                clients_send_user_list(slot);
            } else if (cmd.type == CMD_INFO) {
                clients_send_thread_info(slot);
            } else if (cmd.type == CMD_PONG) {
                /* Ghi nhận hoạt động, last_seen đã được cập nhật */
            } else if (cmd.type == CMD_QUIT) {
                clients_send_raw(slot, "BYE Tam biet ban!\n");
                exit_reason = "QUIT";
                goto cleanup;
            } else if (cmd.type == CMD_JOIN) {
                clients_send_raw(slot, "ERR ALREADY_JOINED Ban da o trong phong chat roi.\n");
            }
        }
    }

cleanup:
    log_info("[THREAD EXITING] Slot %d | Worker TID=%d | Ly do=%s", slot, (int)my_tid, exit_reason);

    /* Chỉ duy nhất luồng này gọi cleanup_slot để đóng fd */
    clients_cleanup_slot(slot, exit_reason);

    handler_dec_active_threads();
    return NULL;
}
