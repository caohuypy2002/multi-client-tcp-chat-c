/*
 * Trách nhiệm: Quản lý danh sách kết nối client, khoá đồng bộ g_lock,
 * cấp phát và giải phóng slot nguyên tử, broadcast tin nhắn, gửi PM,
 * kiểm tra heartbeat và bảo vệ thứ tự khoá (g_lock -> send_lock).
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "clients.h"
#include "protocol.h"
#include "net_utils.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>

client_t g_clients[MAX_CLIENTS];
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_broadcast_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_max_clients = MAX_CLIENTS;

static void clients_broadcast_system_left(const char *name, const char *reason);

void clients_init(int max_clients) {
    pthread_mutex_lock(&g_lock);
    s_max_clients = (max_clients > 0 && max_clients <= MAX_CLIENTS) ? max_clients : MAX_CLIENTS;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&g_clients[i], 0, sizeof(client_t));
        g_clients[i].fd = -1;
        g_clients[i].tid = 0;
        pthread_mutex_init(&g_clients[i].send_lock, NULL);
    }
    pthread_mutex_unlock(&g_lock);
}

void clients_destroy(void) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].in_use) {
            if (g_clients[i].fd >= 0) {
                close(g_clients[i].fd);
                g_clients[i].fd = -1;
            }
            g_clients[i].in_use = false;
            g_clients[i].joined = false;
        }
        pthread_mutex_destroy(&g_clients[i].send_lock);
    }
    pthread_mutex_unlock(&g_lock);
}

int clients_get_max_clients(void) {
    int max_c;
    pthread_mutex_lock(&g_lock);
    max_c = s_max_clients;
    pthread_mutex_unlock(&g_lock);
    return max_c;
}

int clients_reserve_slot(int fd, const char *ip, uint16_t port) {
    pthread_mutex_lock(&g_lock);
    int slot = -1;
    for (int i = 0; i < s_max_clients; i++) {
        if (!g_clients[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot != -1) {
        g_clients[slot].in_use = true;
        g_clients[slot].joined = false;
        g_clients[slot].fd = fd;
        g_clients[slot].tid = 0;
        g_clients[slot].port = port;
        g_clients[slot].last_seen = time(NULL);
        g_clients[slot].bad_count = 0;
        g_clients[slot].name_tries = 0;
        g_clients[slot].name[0] = '\0';
        snprintf(g_clients[slot].ip, sizeof(g_clients[slot].ip), "%s", ip);
        linebuf_init(&g_clients[slot].rbuf);
    }
    pthread_mutex_unlock(&g_lock);
    return slot;
}

bool clients_register_name(int slot, const char *name, char *err_code, char *err_msg, size_t err_sz) {
    if (slot < 0 || slot >= MAX_CLIENTS) return false;

    const char *code = NULL;
    const char *msg = NULL;
    if (!protocol_validate_nickname(name, &code, &msg)) {
        if (err_code) snprintf(err_code, err_sz, "%s", code);
        if (err_msg) snprintf(err_msg, err_sz, "%s", msg);
        return false;
    }

    pthread_mutex_lock(&g_lock);

    /* Kiểm tra trùng tên (không phân biệt hoa thường) nguyên tử dưới g_lock */
    for (int i = 0; i < s_max_clients; i++) {
        if (i != slot && g_clients[i].in_use && g_clients[i].joined) {
            if (strcasecmp(g_clients[i].name, name) == 0) {
                pthread_mutex_unlock(&g_lock);
                if (err_code) snprintf(err_code, err_sz, "NAME_TAKEN");
                if (err_msg) snprintf(err_msg, err_sz, "Tên '%s' đã có người sử dụng", name);
                return false;
            }
        }
    }

    /* Đăng ký thành công */
    snprintf(g_clients[slot].name, sizeof(g_clients[slot].name), "%s", name);
    g_clients[slot].joined = true;
    g_clients[slot].last_seen = time(NULL);

    pthread_mutex_unlock(&g_lock);
    return true;
}

void clients_cleanup_slot(int slot, const char *reason) {
    if (slot < 0 || slot >= MAX_CLIENTS) return;

    char saved_name[MAX_NAME_LEN + 1];
    bool was_joined = false;
    int fd_to_close = -1;

    pthread_mutex_lock(&g_lock);
    if (!g_clients[slot].in_use) {
        pthread_mutex_unlock(&g_lock);
        return;
    }

    /* Giữ send_lock theo thứ tự (g_lock -> send_lock) để chờ gửi xong mới đóng fd */
    pthread_mutex_lock(&g_clients[slot].send_lock);

    was_joined = g_clients[slot].joined;
    snprintf(saved_name, sizeof(saved_name), "%s", g_clients[slot].name);
    fd_to_close = g_clients[slot].fd;

    g_clients[slot].in_use = false;
    g_clients[slot].joined = false;
    g_clients[slot].fd = -1;
    g_clients[slot].tid = 0;
    g_clients[slot].name[0] = '\0';

    /* Chỉ luồng sở hữu slot mới được đóng fd tại đây (sau khi đã gỡ khỏi mảng in_use) */
    if (fd_to_close >= 0) {
        close(fd_to_close);
    }

    pthread_mutex_unlock(&g_clients[slot].send_lock);
    pthread_mutex_unlock(&g_lock);

    /* Thông báo cho mọi người nếu client này đã từng JOIN */
    if (was_joined) {
        clients_broadcast_system_left(saved_name, reason ? reason : "QUIT");
    }
}

static bool send_slot_internal(int slot, const char *line, bool require_joined, bool warn_on_fail) {
    if (slot < 0 || slot >= MAX_CLIENTS) return false;

    int fd = -1;
    char cname[MAX_NAME_LEN + 1] = "";
    pthread_mutex_lock(&g_lock);
    if (!g_clients[slot].in_use || g_clients[slot].fd < 0 ||
        (require_joined && !g_clients[slot].joined)) {
        pthread_mutex_unlock(&g_lock);
        return false;
    }
    fd = g_clients[slot].fd;
    if (warn_on_fail) {
        snprintf(cname, sizeof(cname), "%s", g_clients[slot].name);
    }

    /* Khoá gửi riêng của client (thứ tự khoá: g_lock -> send_lock), nhả g_lock trước I/O */
    pthread_mutex_lock(&g_clients[slot].send_lock);
    pthread_mutex_unlock(&g_lock);

    size_t len = strlen(line);
    ssize_t sent = send_all(fd, line, len);

    if (sent < 0) {
        if (warn_on_fail) {
            log_warn("Broadcast thất bại tới client %s, đánh thức để dọn dẹp", cname);
        }
        /* Gọi shutdown trước khi nhả send_lock để fd không bị tái sử dụng */
        shutdown(fd, SHUT_RDWR);
    }

    pthread_mutex_unlock(&g_clients[slot].send_lock);
    return (sent >= 0);
}

bool clients_send_raw(int slot, const char *line) {
    return send_slot_internal(slot, line, false, false);
}

static void broadcast_with_lock_held(const char *line, int exclude_slot) {
    bool targets[MAX_CLIENTS] = {false};
    int max_c = MAX_CLIENTS;

    pthread_mutex_lock(&g_lock);
    max_c = s_max_clients;
    for (int i = 0; i < max_c; i++) {
        if (i != exclude_slot && g_clients[i].in_use && g_clients[i].joined && g_clients[i].fd >= 0) {
            targets[i] = true;
        }
    }
    pthread_mutex_unlock(&g_lock);

    for (int i = 0; i < max_c; i++) {
        if (targets[i]) {
            send_slot_internal(i, line, true, true);
        }
    }
}

void clients_broadcast(const char *line, int exclude_slot) {
    pthread_mutex_lock(&s_broadcast_lock);
    broadcast_with_lock_held(line, exclude_slot);
    pthread_mutex_unlock(&s_broadcast_lock);
}

static void build_users_line_locked(char *out, size_t out_sz) {
    char user_names[MAX_LINE / 2] = "";
    int count = 0;

    for (int i = 0; i < s_max_clients; i++) {
        if (g_clients[i].in_use && g_clients[i].joined) {
            if (count > 0) {
                strncat(user_names, ",", sizeof(user_names) - strlen(user_names) - 1);
            }
            strncat(user_names, g_clients[i].name, sizeof(user_names) - strlen(user_names) - 1);
            count++;
        }
    }
    snprintf(out, out_sz, "USERS %d %d %s\n", count, s_max_clients, user_names);
}

void clients_broadcast_system_join(int slot) {
    if (slot < 0 || slot >= MAX_CLIENTS) return;

    char time_str[16];
    protocol_get_time_str(time_str, sizeof(time_str));

    char joined_line[MAX_LINE];
    char users_line[MAX_LINE];

    pthread_mutex_lock(&s_broadcast_lock);
    pthread_mutex_lock(&g_lock);
    int count = 0;
    for (int i = 0; i < s_max_clients; i++) {
        if (g_clients[i].in_use && g_clients[i].joined) count++;
    }
    snprintf(joined_line, sizeof(joined_line), "JOINED %s %s %d %d\n",
             time_str, g_clients[slot].name, count, s_max_clients);
    build_users_line_locked(users_line, sizeof(users_line));
    pthread_mutex_unlock(&g_lock);

    broadcast_with_lock_held(joined_line, -1);
    broadcast_with_lock_held(users_line, -1);
    pthread_mutex_unlock(&s_broadcast_lock);
}

static void clients_broadcast_system_left(const char *name, const char *reason) {
    char time_str[16];
    protocol_get_time_str(time_str, sizeof(time_str));

    char left_line[MAX_LINE];
    char users_line[MAX_LINE];

    pthread_mutex_lock(&s_broadcast_lock);
    pthread_mutex_lock(&g_lock);
    int count = 0;
    for (int i = 0; i < s_max_clients; i++) {
        if (g_clients[i].in_use && g_clients[i].joined) count++;
    }
    snprintf(left_line, sizeof(left_line), "LEFT %s %s %d %d %s\n",
             time_str, name, count, s_max_clients, reason ? reason : "QUIT");
    build_users_line_locked(users_line, sizeof(users_line));
    pthread_mutex_unlock(&g_lock);

    broadcast_with_lock_held(left_line, -1);
    broadcast_with_lock_held(users_line, -1);
    pthread_mutex_unlock(&s_broadcast_lock);
}

void clients_send_user_list(int target_slot) {
    if (target_slot < 0 || target_slot >= MAX_CLIENTS) return;

    char line[MAX_LINE];
    pthread_mutex_lock(&g_lock);
    build_users_line_locked(line, sizeof(line));
    pthread_mutex_unlock(&g_lock);

    clients_send_raw(target_slot, line);
}

void clients_send_thread_info(int target_slot) {
    if (target_slot < 0 || target_slot >= MAX_CLIENTS) return;

    char details[512] = "";
    int count = 0;
    int max_c = MAX_CLIENTS;
    pid_t my_tid = 0;
    pid_t srv_pid = getpid();

    pthread_mutex_lock(&g_lock);
    max_c = s_max_clients;
    my_tid = g_clients[target_slot].tid;
    for (int i = 0; i < s_max_clients; i++) {
        if (g_clients[i].in_use && g_clients[i].joined) {
            char item[64];
            snprintf(item, sizeof(item), "%s%s[TID:%d,FD:%d]",
                     (count > 0) ? "," : "",
                     g_clients[i].name, (int)g_clients[i].tid, g_clients[i].fd);
            strncat(details, item, sizeof(details) - strlen(details) - 1);
            count++;
        }
    }
    pthread_mutex_unlock(&g_lock);

    char line[MAX_LINE];
    snprintf(line, sizeof(line), "INFO %d %d %d %d %s\n",
             (int)srv_pid, (int)my_tid, count, max_c, details);
    clients_send_raw(target_slot, line);
}

bool clients_send_pm(int from_slot, const char *to_name, const char *text) {
    if (from_slot < 0 || from_slot >= MAX_CLIENTS) return false;

    char time_str[16];
    protocol_get_time_str(time_str, sizeof(time_str));

    int target_slot = -1;
    char from_name[MAX_NAME_LEN + 1];
    char canonical_to[MAX_NAME_LEN + 1];

    pthread_mutex_lock(&g_lock);
    snprintf(from_name, sizeof(from_name), "%s", g_clients[from_slot].name);

    for (int i = 0; i < s_max_clients; i++) {
        if (g_clients[i].in_use && g_clients[i].joined) {
            if (strcasecmp(g_clients[i].name, to_name) == 0) {
                target_slot = i;
                snprintf(canonical_to, sizeof(canonical_to), "%s", g_clients[i].name);
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);

    if (target_slot == -1) {
        return false; /* Người nhận không tồn tại hoặc đã offline */
    }

    char line[MAX_LINE];
    snprintf(line, sizeof(line), "PM %s %s %s %s\n", time_str, from_name, canonical_to, text);

    /* Gửi cho người nhận */
    clients_send_raw(target_slot, line);

    /* Gửi bản sao echo lại cho người gửi */
    clients_send_raw(from_slot, line);

    return true;
}

int clients_get_joined_count(void) {
    int count = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < s_max_clients; i++) {
        if (g_clients[i].in_use && g_clients[i].joined) {
            count++;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return count;
}

int clients_get_in_use_count(void) {
    int count = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < s_max_clients; i++) {
        if (g_clients[i].in_use) {
            count++;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return count;
}

void clients_shutdown_all(const char *bye_msg) {
    char line[MAX_LINE];
    snprintf(line, sizeof(line), "BYE %s\n", bye_msg ? bye_msg : "Server dang dong.");

    bool active[MAX_CLIENTS] = {false};
    int max_c = MAX_CLIENTS;
    pthread_mutex_lock(&g_lock);
    max_c = s_max_clients;
    for (int i = 0; i < max_c; i++) {
        if (g_clients[i].in_use && g_clients[i].fd >= 0) {
            active[i] = true;
        }
    }
    pthread_mutex_unlock(&g_lock);

    for (int i = 0; i < max_c; i++) {
        if (!active[i]) continue;
        pthread_mutex_lock(&g_lock);
        if (g_clients[i].in_use && g_clients[i].fd >= 0) {
            int fd = g_clients[i].fd;
            pthread_mutex_lock(&g_clients[i].send_lock);
            pthread_mutex_unlock(&g_lock);

            send_all(fd, line, strlen(line));
            /* Đánh thức handler thread để nó thoát vòng lặp và cleanup */
            shutdown(fd, SHUT_RDWR);
            pthread_mutex_unlock(&g_clients[i].send_lock);
        } else {
            pthread_mutex_unlock(&g_lock);
        }
    }
}

void clients_check_heartbeat(void) {
    time_t now = time(NULL);
    bool need_ping[MAX_CLIENTS] = {false};
    int max_c = MAX_CLIENTS;

    pthread_mutex_lock(&g_lock);
    max_c = s_max_clients;
    for (int i = 0; i < max_c; i++) {
        if (g_clients[i].in_use && g_clients[i].joined && g_clients[i].fd >= 0) {
            double idle = difftime(now, g_clients[i].last_seen);
            if (idle >= IDLE_TIMEOUT_SEC) {
                log_warn("Client %s het han im lang (%.0fs), ngat ket noi", g_clients[i].name, idle);
                shutdown(g_clients[i].fd, SHUT_RDWR);
            } else if (idle >= PING_INTERVAL_SEC) {
                need_ping[i] = true;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);

    for (int i = 0; i < max_c; i++) {
        if (need_ping[i]) {
            send_slot_internal(i, "PING\n", true, false);
        }
    }
}
