/*
 * Trách nhiệm: Tiện ích mạng socket: gửi toàn bộ (send_all), đặt timeout,
 * kết nối có timeout, lấy danh sách IP máy chủ.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "net_utils.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>

bool parse_port(const char *str, uint16_t *out_port) {
    if (str == NULL || *str == '\0' || out_port == NULL) return false;
    errno = 0;
    char *endptr = NULL;
    long val = strtol(str, &endptr, 10);
    if (errno != 0 || endptr == str || *endptr != '\0') return false;
    if (val < 1 || val > 65535) return false;
    *out_port = (uint16_t)val;
    return true;
}

ssize_t send_all(int fd, const void *buf, size_t len) {
    const char *ptr = (const char *)buf;
    size_t total_sent = 0;

    while (total_sent < len) {
        /* MSG_NOSIGNAL ngăn chặn việc phát sinh SIGPIPE khi ghi vào socket đã đóng */
        ssize_t sent = send(fd, ptr + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue; /* Bị ngắt bởi tín hiệu, tiếp tục gửi */
            }
            return -1; /* Lỗi socket thực sự */
        }
        if (sent == 0) {
            break; /* Không thể gửi thêm */
        }
        total_sent += (size_t)sent;
    }

    return (ssize_t)total_sent;
}

int set_socket_timeouts(int fd, int rcv_sec, int snd_sec) {
    struct timeval tv_rcv;
    tv_rcv.tv_sec = rcv_sec;
    tv_rcv.tv_usec = 0;

    struct timeval tv_snd;
    tv_snd.tv_sec = snd_sec;
    tv_snd.tv_usec = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv_rcv, sizeof(tv_rcv)) < 0) {
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv_snd, sizeof(tv_snd)) < 0) {
        return -1;
    }
    return 0;
}

int set_reuse_addr(int fd) {
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

void print_local_ips(uint16_t port) {
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) == -1) {
        log_warn("Không thể lấy danh sách card mạng (getifaddrs)");
        return;
    }

    log_info("=== Server đang lắng nghe trên cổng %u ===", port);
    log_info("Các địa chỉ IP bạn bè có thể kết nối:");

    int count = 0;
    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char ip_str[INET_ADDRSTRLEN];
            struct sockaddr_in *p_in = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(p_in->sin_addr), ip_str, sizeof(ip_str));

            /* Nhận diện card mạng và in ra */
            log_info("  - Giao diện %s: %s:%u", ifa->ifa_name, ip_str, port);
            count++;
        }
    }

    if (count == 0) {
        log_info("  - 127.0.0.1:%u (localhost)", port);
    }
    log_info("==========================================");

    freeifaddrs(ifaddr);
}

int connect_with_timeout(const char *host, uint16_t port, int timeout_sec) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int s = getaddrinfo(host, port_str, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "Lỗi phân giải tên miền/IP '%s': %s\n", host, gai_strerror(s));
        return -1;
    }

    int sock_fd = -1;
    int last_err = ETIMEDOUT;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock_fd < 0) {
            last_err = errno;
            continue;
        }

        /* Chuyển sang non-blocking để connect kèm timeout */
        int flags = fcntl(sock_fd, F_GETFL, 0);
        if (flags < 0) {
            last_err = errno;
            close(sock_fd);
            sock_fd = -1;
            continue;
        }
        fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

        int res = connect(sock_fd, rp->ai_addr, rp->ai_addrlen);
        if (res == 0) {
            /* Kết nối thành công ngay lập tức */
            fcntl(sock_fd, F_SETFL, flags);
            break;
        }

        if (errno == EINPROGRESS) {
            struct pollfd pfd;
            pfd.fd = sock_fd;
            pfd.events = POLLOUT;

            int poll_res = poll(&pfd, 1, timeout_sec * 1000);
            if (poll_res > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    /* Kết nối thành công sau khi poll */
                    fcntl(sock_fd, F_SETFL, flags);
                    break;
                }
                if (err != 0) last_err = err;
            } else if (poll_res == 0) {
                last_err = ETIMEDOUT;
            } else {
                last_err = errno;
            }
        } else {
            last_err = errno;
        }

        close(sock_fd);
        sock_fd = -1;
    }

    freeaddrinfo(result);
    if (sock_fd < 0) {
        errno = last_err;
    }
    return sock_fd;
}
