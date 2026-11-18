#ifndef CLIENTS_H
#define CLIENTS_H

#include "config.h"
#include "linebuf.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <netinet/in.h>

typedef struct {
    bool in_use;                    /* Slot đã được cấp phát khi accept */
    bool joined;                    /* Đã hoàn thành handshake JOIN */
    int fd;                         /* File descriptor socket */
    pid_t tid;                      /* Linux Kernel Thread ID (LWP) qua SYS_gettid */
    char name[MAX_NAME_LEN + 1];    /* Nickname */
    char ip[INET_ADDRSTRLEN];       /* Địa chỉ IP client */
    uint16_t port;                  /* Cổng client */
    pthread_mutex_t send_lock;      /* Khoá gửi an toàn luồng cho từng socket */
    time_t last_seen;               /* Thời điểm cuối nhận được dữ liệu (heartbeat) */
    int bad_count;                  /* Số lệnh sai liên tiếp */
    int name_tries;                 /* Số lần thử đặt tên */
    linebuf_t rbuf;                 /* Bộ đệm framing dòng */
} client_t;

extern client_t g_clients[MAX_CLIENTS];
extern pthread_mutex_t g_lock;

/* Khởi tạo cấu trúc quản lý client và các khoá mutex */
void clients_init(int max_clients);

/* Huỷ tài nguyên khi server tắt */
void clients_destroy(void);

/* Lấy giới hạn số client tối đa hiện hành */
int clients_get_max_clients(void);

/* Cố gắng giữ 1 slot trống ngay khi accept() thành công */
/* Trả về chỉ số slot (0..MAX-1) nếu còn chỗ, hoặc -1 nếu đầy */
int clients_reserve_slot(int fd, const char *ip, uint16_t port);

/* Kiểm tra và đăng ký nickname (nguyên tử dưới g_lock) */
/* Trả về true nếu thành công, false nếu tên đã tồn tại hoặc không hợp lệ */
bool clients_register_name(int slot, const char *name, char *err_code, char *err_msg, size_t err_sz);

/* Dọn dẹp slot khi client rời đi hoặc ngắt kết nối (chạy đúng 1 lần từ handler thread) */
void clients_cleanup_slot(int slot, const char *reason);

/* Gửi một dòng văn bản tới 1 client cụ thể (có send_lock, timeout) */
/* Trả về true nếu gửi thành công, false nếu lỗi */
bool clients_send_raw(int slot, const char *line);

/* Broadcast một thông điệp tới TẤT CẢ client đã JOIN */
/* exclude_slot: -1 nếu gửi cho tất cả; hoặc chỉ số slot cần loại trừ */
void clients_broadcast(const char *line, int exclude_slot);

/* Broadcast thông báo người mới vào phòng (JOINED) kèm USERS */
void clients_broadcast_system_join(int slot);

/* Gửi danh sách người online (USERS ...) tới 1 client */
void clients_send_user_list(int target_slot);

/* Gửi thông tin chẩn đoán luồng nhân Linux (PID/TGID, Worker TID/LWP) tới 1 client */
void clients_send_thread_info(int target_slot);

/* Gửi tin nhắn riêng (PM) từ 1 client tới 1 client khác theo tên */
bool clients_send_pm(int from_slot, const char *to_name, const char *text);

/* Lấy số lượng người dùng hiện tại đã JOIN thành công */
int clients_get_joined_count(void);

/* Lấy số lượng slot đang được giữ (kể cả đang handshake) */
int clients_get_in_use_count(void);

/* Gửi thông điệp BYE và đóng/shutdown toàn bộ kết nối client */
void clients_shutdown_all(const char *bye_msg);

/* Kiểm tra heartbeat cho các client, gửi PING hoặc ngắt kết nối quá hạn */
void clients_check_heartbeat(void);

#endif /* CLIENTS_H */
