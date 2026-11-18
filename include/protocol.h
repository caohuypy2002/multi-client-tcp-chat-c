#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "config.h"
#include <stdbool.h>
#include <stddef.h>

/* Các loại lệnh từ Client gửi lên Server */
typedef enum {
    CMD_UNKNOWN = 0,
    CMD_JOIN,
    CMD_MSG,
    CMD_PM,
    CMD_LIST,
    CMD_INFO,
    CMD_PONG,
    CMD_QUIT
} client_cmd_type_t;

/* Cấu trúc chứa lệnh đã phân tích từ Client */
typedef struct {
    client_cmd_type_t type;
    char target[MAX_NAME_LEN + 1]; /* Dùng cho PM: người nhận */
    char text[MAX_LINE];           /* Dùng cho JOIN (tên), MSG (nội dung), PM (nội dung) */
} parsed_cmd_t;

/* Kiểm tra tính hợp lệ của nickname:
 *  - 1 đến 16 ký tự: chỉ gồm [A-Za-z0-9_]
 *  - Không nằm trong danh sách tên dành riêng (server, system, admin, all)
 * Trả về true nếu hợp lệ, false nếu không.
 * Nếu không hợp lệ, ghi mã lỗi vào err_code ("BAD_NAME" hoặc "RESERVED").
 */
bool protocol_validate_nickname(const char *name, const char **err_code, const char **err_msg);

/* Làm sạch nội dung văn bản:
 *  - Thay các ký tự điều khiển (< 0x20 và 0x7F) thành khoảng trắng ' '
 *  - Giữ nguyên các byte UTF-8 (>= 0x80) để hiển thị đúng tiếng Việt có dấu
 *  - Trả về độ dài chuỗi sau khi làm sạch
 */
size_t protocol_sanitize_text(char *text);

/* Phân tích một dòng nhận được từ Client */
/* Trả về: true nếu cú pháp hợp lệ, false nếu lỗi cú pháp */
bool protocol_parse_client_line(const char *line, parsed_cmd_t *out_cmd);

/* Lấy thời gian hiện tại định dạng HH:MM:SS (an toàn luồng qua localtime_r) */
void protocol_get_time_str(char *out, size_t out_sz);

/* Cắt khoảng trắng ở đầu và cuối chuỗi (in-place) */
char *protocol_trim_whitespace(char *str);

#endif /* PROTOCOL_H */
