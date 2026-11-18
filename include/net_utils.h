#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Phân tích và kiểm tra cổng hợp lệ (1..65535) */
bool parse_port(const char *str, uint16_t *out_port);

/* Gửi toàn bộ dữ liệu qua socket, xử lý gửi từng phần, EINTR, MSG_NOSIGNAL */
/* Trả về tổng số byte gửi được, hoặc -1 nếu lỗi */
ssize_t send_all(int fd, const void *buf, size_t len);

/* Thiết lập timeout đọc/ghi cho socket */
int set_socket_timeouts(int fd, int rcv_sec, int snd_sec);

/* Bật SO_REUSEADDR để khởi động lại server nhanh chóng */
int set_reuse_addr(int fd);

/* In ra các địa chỉ IP IPv4 nội bộ của máy (dùng getifaddrs) */
void print_local_ips(uint16_t port);

/* Kết nối tới server kèm thời gian chờ (connect non-blocking + poll) */
/* Trả về socket fd nếu thành công, -1 nếu thất bại */
int connect_with_timeout(const char *host, uint16_t port, int timeout_sec);

#endif /* NET_UTILS_H */
