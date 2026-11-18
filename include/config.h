#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

/* Cấu hình cổng và kết nối */
#define DEFAULT_HOST            "0.0.0.0"
#define DEFAULT_PORT            5555
#define LISTEN_BACKLOG          16

/* Giới hạn số lượng client đồng thời theo đề bài */
#define MAX_CLIENTS             10

/* Giới hạn bộ đệm và độ dài thông điệp */
#define MAX_LINE                1024       /* Kích thước dòng tối đa (gồm cả \n và \0) */
#define MAX_MSG_LEN             512        /* Nội dung tin nhắn tối đa */
#define MAX_NAME_LEN            16         /* Độ dài tối đa của nickname */
#define LINEBUF_CAPACITY        (MAX_LINE * 2)

/* Thời gian chờ (giây) */
#define HANDSHAKE_TIMEOUT_SEC   10         /* Hết hạn gửi lệnh JOIN sau khi nhận HELLO */
#define PING_INTERVAL_SEC       15         /* Chu kỳ kiểm tra im lặng để gửi PING */
#define IDLE_TIMEOUT_SEC        45         /* Hết hạn im lặng ngắt kết nối */
#define SEND_TIMEOUT_SEC        2          /* Timeout gửi socket tránh nghẽn luồng broadcast */
#define CONNECT_TIMEOUT_SEC     5          /* Timeout kết nối client -> server */
#define SHUTDOWN_WAIT_SEC       3          /* Thời gian chờ các luồng kết thúc khi tắt server */

/* Ngưỡng bảo vệ */
#define MAX_NAME_TRIES          3          /* Số lần thử tên tối đa trước khi bị ngắt */
#define MAX_BAD_COMMANDS        5          /* Số lệnh sai liên tiếp tối đa trước khi bị kick */

#endif /* CONFIG_H */
