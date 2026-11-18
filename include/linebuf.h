#ifndef LINEBUF_H
#define LINEBUF_H

#include "config.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char buf[LINEBUF_CAPACITY];
    size_t len;
    bool discarding; /* Cờ đánh dấu đang bỏ qua dòng vượt quá kích thước */
} linebuf_t;

/* Khởi tạo lại bộ đệm dòng */
void linebuf_init(linebuf_t *lb);

/* Nạp dữ liệu nhận được từ recv/read vào bộ đệm */
/* Trả về 0 nếu thành công, -1 nếu tràn bộ đệm */
int linebuf_feed(linebuf_t *lb, const char *data, size_t len);

/* Lấy ra một dòng hoàn chỉnh (bỏ \n và \r ở cuối, thêm \0) */
/* Trả về:
 *  1: Đã lấy được 1 dòng thành công vào out.
 *  0: Chưa đủ 1 dòng hoàn chỉnh trong buffer.
 * -1: Dòng quá dài vượt quá giới hạn (ERR TOO_LONG).
 */
int linebuf_next_line(linebuf_t *lb, char *out, size_t outcap);

#endif /* LINEBUF_H */
