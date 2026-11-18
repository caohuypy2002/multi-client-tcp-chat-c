/*
 * Unit test cho module protocol.
 * Kiểm tra: Validate nickname (E5, E6), làm sạch văn bản (E13),
 * phân tích cú pháp các lệnh client (E8).
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_validate_nickname(void) {
    const char *code = NULL;
    const char *msg = NULL;

    /* Tên hợp lệ */
    assert(protocol_validate_nickname("An", &code, &msg) == true);
    assert(protocol_validate_nickname("Binh_123", &code, &msg) == true);
    assert(protocol_validate_nickname("user_name_16char", &code, &msg) == true);

    /* Tên rỗng */
    assert(protocol_validate_nickname("", &code, &msg) == false);
    assert(strcmp(code, "BAD_NAME") == 0);

    /* Tên quá 16 ký tự */
    assert(protocol_validate_nickname("ten_nay_dai_hon_16_ky_tu", &code, &msg) == false);
    assert(strcmp(code, "BAD_NAME") == 0);

    /* Chứa ký tự đặc biệt hoặc dấu cách */
    assert(protocol_validate_nickname("An Nguyen", &code, &msg) == false);
    assert(protocol_validate_nickname("An@123", &code, &msg) == false);

    /* Tên dành riêng của hệ thống (so trùng không phân biệt hoa thường) */
    assert(protocol_validate_nickname("Server", &code, &msg) == false);
    assert(strcmp(code, "RESERVED") == 0);
    assert(protocol_validate_nickname("admin", &code, &msg) == false);
    assert(strcmp(code, "RESERVED") == 0);
    assert(protocol_validate_nickname("SYSTEM", &code, &msg) == false);
    assert(strcmp(code, "RESERVED") == 0);
    assert(protocol_validate_nickname("all", &code, &msg) == false);
    assert(strcmp(code, "RESERVED") == 0);

    printf("  [PASS] test_validate_nickname\n");
}

static void test_sanitize_text(void) {
    /* Chứa mã ANSI xóa màn hình \033[2J và byte điều khiển */
    char dirty[] = "Hello\033[2JWorld\a\b\t!";
    protocol_sanitize_text(dirty);

    /* Kiểm tra ký tự ESC (0x1B), \a (0x07), \b (0x08), \t (0x09) đã bị đổi thành dấu cách */
    assert(dirty[5] == ' ');  /* ESC */
    assert(dirty[14] == ' '); /* \a */
    assert(dirty[15] == ' '); /* \b */
    assert(dirty[16] == ' '); /* \t */
    assert(strcmp(dirty, "Hello [2JWorld   !") == 0);

    /* Nội dung tiếng Việt UTF-8 (>= 0x80) KHÔNG được bị biến đổi */
    char vn_text[] = "Chào bạn! Hôm nay thế nào? 🚀";
    char original[sizeof(vn_text)];
    memcpy(original, vn_text, sizeof(vn_text));

    protocol_sanitize_text(vn_text);
    assert(strcmp(vn_text, original) == 0);

    printf("  [PASS] test_sanitize_text\n");
}

static void test_parse_commands(void) {
    parsed_cmd_t cmd;

    assert(protocol_parse_client_line("JOIN Alice", &cmd) == true);
    assert(cmd.type == CMD_JOIN);
    assert(strcmp(cmd.text, "Alice") == 0);

    assert(protocol_parse_client_line("MSG Xin chao ca phong!", &cmd) == true);
    assert(cmd.type == CMD_MSG);
    assert(strcmp(cmd.text, "Xin chao ca phong!") == 0);

    assert(protocol_parse_client_line("PM Bob Day la tin rieng", &cmd) == true);
    assert(cmd.type == CMD_PM);
    assert(strcmp(cmd.target, "Bob") == 0);
    assert(strcmp(cmd.text, "Day la tin rieng") == 0);

    assert(protocol_parse_client_line("LIST", &cmd) == true);
    assert(cmd.type == CMD_LIST);

    assert(protocol_parse_client_line("INFO", &cmd) == true);
    assert(cmd.type == CMD_INFO);

    assert(protocol_parse_client_line("PONG", &cmd) == true);
    assert(cmd.type == CMD_PONG);

    assert(protocol_parse_client_line("QUIT", &cmd) == true);
    assert(cmd.type == CMD_QUIT);

    /* Lệnh sai */
    assert(protocol_parse_client_line("UNKNOWN_CMD", &cmd) == false);
    assert(protocol_parse_client_line("JOIN", &cmd) == false); /* Thiếu tên */
    assert(protocol_parse_client_line("PM Bob", &cmd) == true); /* PM không nội dung */
    assert(cmd.text[0] == '\0');

    printf("  [PASS] test_parse_commands\n");
}

int main(void) {
    printf("=== CHẠY TEST PROTOCOL ===\n");
    test_validate_nickname();
    test_sanitize_text();
    test_parse_commands();
    printf(">>> TẤT CẢ TEST PROTOCOL ĐÃ PASS! <<<\n\n");
    return 0;
}
