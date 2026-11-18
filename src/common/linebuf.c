/*
 * Trách nhiệm: Giải quyết vấn đề "TCP là luồng byte" (Message Framing).
 * Tích luỹ byte nhận được, tách dòng theo ký tự '\n', loại bỏ '\r',
 * bảo vệ chống tràn bộ đệm khi gặp dòng quá dài.
 */

#include "linebuf.h"
#include <string.h>

void linebuf_init(linebuf_t *lb) {
    if (lb == NULL) return;
    lb->len = 0;
    lb->discarding = false;
}

int linebuf_feed(linebuf_t *lb, const char *data, size_t len) {
    if (lb == NULL || data == NULL) return -1;
    if (lb->len + len > LINEBUF_CAPACITY) {
        return -1; /* Bộ đệm bị tràn */
    }
    memcpy(lb->buf + lb->len, data, len);
    lb->len += len;
    return 0;
}

int linebuf_next_line(linebuf_t *lb, char *out, size_t outcap) {
    if (lb == NULL || out == NULL || outcap == 0) return 0;

    while (lb->len > 0) {
        /* Tìm ký tự phân cách dòng '\n' */
        char *newline_pos = (char *)memchr(lb->buf, '\n', lb->len);

        if (lb->discarding) {
            if (newline_pos != NULL) {
                /* Đã tìm thấy điểm kết thúc của dòng quá dài, loại bỏ nó */
                size_t consumed = (size_t)(newline_pos - lb->buf) + 1;
                size_t remaining = lb->len - consumed;
                if (remaining > 0) {
                    memmove(lb->buf, newline_pos + 1, remaining);
                }
                lb->len = remaining;
                lb->discarding = false;
                /* Tiếp tục kiểm tra dòng kế tiếp */
                continue;
            } else {
                /* Chưa thấy '\n', xoá toàn bộ dữ liệu hiện tại để tránh đầy buffer */
                lb->len = 0;
                return 0;
            }
        }

        if (newline_pos != NULL) {
            size_t line_len = (size_t)(newline_pos - lb->buf);

            /* Kiểm tra nếu dòng vượt quá giới hạn MAX_LINE */
            if (line_len >= MAX_LINE) {
                size_t consumed = line_len + 1;
                size_t remaining = lb->len - consumed;
                if (remaining > 0) {
                    memmove(lb->buf, newline_pos + 1, remaining);
                }
                lb->len = remaining;
                return -1; /* Báo lỗi dòng quá dài */
            }

            /* Cắt bỏ ký tự '\r' nếu có trước '\n' */
            size_t copy_len = line_len;
            if (copy_len > 0 && lb->buf[copy_len - 1] == '\r') {
                copy_len--;
            }

            if (copy_len >= outcap) {
                copy_len = outcap - 1;
            }

            memcpy(out, lb->buf, copy_len);
            out[copy_len] = '\0';

            /* Dịch chuyển phần byte còn lại trong buffer */
            size_t consumed = line_len + 1;
            size_t remaining = lb->len - consumed;
            if (remaining > 0) {
                memmove(lb->buf, newline_pos + 1, remaining);
            }
            lb->len = remaining;
            return 1;
        } else {
            /* Chưa tìm thấy '\n', nếu độ dài tích luỹ đã >= MAX_LINE thì dòng này vi phạm */
            if (lb->len >= MAX_LINE) {
                lb->discarding = true;
                lb->len = 0;
                return -1; /* Báo lỗi dòng quá dài */
            }
            return 0; /* Chưa đủ dữ liệu để tạo 1 dòng */
        }
    }

    return 0;
}
