/*
 * Unit test cho module linebuf (Message Framing).
 * Kiểm tra: 1 tin trọn vẹn, nhiều tin dính nhau, nạp từng byte,
 * xử lý \r\n, dòng quá dài, tiếng Việt UTF-8 bị cắt giữa ký tự nhiều byte.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "linebuf.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_single_line(void) {
    linebuf_t lb;
    linebuf_init(&lb);

    const char *msg = "HELLO 1 10\n";
    assert(linebuf_feed(&lb, msg, strlen(msg)) == 0);

    char out[MAX_LINE];
    assert(linebuf_next_line(&lb, out, sizeof(out)) == 1);
    assert(strcmp(out, "HELLO 1 10") == 0);
    assert(linebuf_next_line(&lb, out, sizeof(out)) == 0);
    printf("  [PASS] test_single_line\n");
}

static void test_crlf_and_multiple_lines(void) {
    linebuf_t lb;
    linebuf_init(&lb);

    const char *stream = "JOIN Alice\r\nMSG Xin chao\r\nQUIT\n";
    assert(linebuf_feed(&lb, stream, strlen(stream)) == 0);

    char out[MAX_LINE];
    assert(linebuf_next_line(&lb, out, sizeof(out)) == 1);
    assert(strcmp(out, "JOIN Alice") == 0);

    assert(linebuf_next_line(&lb, out, sizeof(out)) == 1);
    assert(strcmp(out, "MSG Xin chao") == 0);

    assert(linebuf_next_line(&lb, out, sizeof(out)) == 1);
    assert(strcmp(out, "QUIT") == 0);

    assert(linebuf_next_line(&lb, out, sizeof(out)) == 0);
    printf("  [PASS] test_crlf_and_multiple_lines\n");
}

static void test_byte_by_byte_and_utf8(void) {
    linebuf_t lb;
    linebuf_init(&lb);

    /* "Xin chào Việt Nam" chứa các ký tự UTF-8 nhiều byte */
    const char *full = "MSG Xin chào Việt Nam 🇻🇳\n";
    size_t len = strlen(full);

    char out[MAX_LINE];
    for (size_t i = 0; i < len - 1; i++) {
        assert(linebuf_feed(&lb, &full[i], 1) == 0);
        assert(linebuf_next_line(&lb, out, sizeof(out)) == 0);
    }

    /* Đưa byte cuối cùng (\n) vào */
    assert(linebuf_feed(&lb, &full[len - 1], 1) == 0);
    assert(linebuf_next_line(&lb, out, sizeof(out)) == 1);
    assert(strcmp(out, "MSG Xin chào Việt Nam 🇻🇳") == 0);
    printf("  [PASS] test_byte_by_byte_and_utf8\n");
}

static void test_line_too_long(void) {
    linebuf_t lb;
    linebuf_init(&lb);

    char huge[MAX_LINE + 100];
    memset(huge, 'A', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';

    assert(linebuf_feed(&lb, huge, strlen(huge)) == 0);

    char out[MAX_LINE];
    /* Dòng vượt quá MAX_LINE phải trả về -1 */
    assert(linebuf_next_line(&lb, out, sizeof(out)) == -1);

    /* Dòng sau đó kết thúc bằng \n hợp lệ phải đọc lại được */
    const char *valid = "\nJOIN Bob\n";
    assert(linebuf_feed(&lb, valid, strlen(valid)) == 0);
    assert(linebuf_next_line(&lb, out, sizeof(out)) == 1);
    assert(strcmp(out, "JOIN Bob") == 0);

    printf("  [PASS] test_line_too_long\n");
}

int main(void) {
    printf("=== CHẠY TEST LINEBUF (FRAMING) ===\n");
    test_single_line();
    test_crlf_and_multiple_lines();
    test_byte_by_byte_and_utf8();
    test_line_too_long();
    printf(">>> TẤT CẢ TEST LINEBUF ĐÃ PASS! <<<\n\n");
    return 0;
}
