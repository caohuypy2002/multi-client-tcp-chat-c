/*
 * Trách nhiệm: Xử lý định dạng giao thức văn bản (text protocol),
 * validate nickname, lọc ký tự điều khiển (chống ANSI injection),
 * phân tích cú pháp các lệnh từ client.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "protocol.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdio.h>

static const char *s_reserved_names[] = {
    "server", "system", "admin", "all", NULL
};

char *protocol_trim_whitespace(char *str) {
    if (str == NULL) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

bool protocol_validate_nickname(const char *name, const char **err_code, const char **err_msg) {
    if (name == NULL || *name == '\0') {
        if (err_code) *err_code = "BAD_NAME";
        if (err_msg) *err_msg = "Tên không được để trống";
        return false;
    }

    size_t len = strlen(name);
    if (len < 1 || len > MAX_NAME_LEN) {
        if (err_code) *err_code = "BAD_NAME";
        if (err_msg) *err_msg = "Tên phải có độ dài từ 1 đến 16 ký tự";
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!isalnum(c) && c != '_') {
            if (err_code) *err_code = "BAD_NAME";
            if (err_msg) *err_msg = "Tên chỉ được chứa chữ cái, số và dấu gạch dưới";
            return false;
        }
    }

    for (int i = 0; s_reserved_names[i] != NULL; i++) {
        if (strcasecmp(name, s_reserved_names[i]) == 0) {
            if (err_code) *err_code = "RESERVED";
            if (err_msg) *err_msg = "Tên này là tên dành riêng của hệ thống";
            return false;
        }
    }

    return true;
}

size_t protocol_sanitize_text(char *text) {
    if (text == NULL) return 0;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        /* Thay thế các byte điều khiển và DEL bằng dấu cách; giữ nguyên UTF-8 (>= 0x80) */
        if (c < 0x20 || c == 0x7F) {
            text[i] = ' ';
        }
    }

    return len;
}

void protocol_get_time_str(char *out, size_t out_sz) {
    if (out == NULL || out_sz == 0) return;
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(out, out_sz, "%H:%M:%S", &tm_buf);
}

bool protocol_parse_client_line(const char *line, parsed_cmd_t *out_cmd) {
    if (line == NULL || out_cmd == NULL) return false;
    memset(out_cmd, 0, sizeof(*out_cmd));
    out_cmd->type = CMD_UNKNOWN;

    /* Sao chép dòng để bóc tách */
    char buf[MAX_LINE];
    size_t line_len = strlen(line);
    if (line_len >= sizeof(buf)) return false;
    memcpy(buf, line, line_len + 1);

    char *saveptr = NULL;
    const char *cmd_token = strtok_r(buf, " \t\r\n", &saveptr);
    if (cmd_token == NULL) {
        return false;
    }

    if (strcmp(cmd_token, "JOIN") == 0) {
        out_cmd->type = CMD_JOIN;
        const char *name = strtok_r(NULL, " \t\r\n", &saveptr);
        if (name == NULL) return false;
        /* Không cho phép có thêm từ thứ 2 trong tên */
        const char *extra = strtok_r(NULL, " \t\r\n", &saveptr);
        if (extra != NULL) {
            snprintf(out_cmd->text, sizeof(out_cmd->text), "%s %s", name, extra);
        } else {
            snprintf(out_cmd->text, sizeof(out_cmd->text), "%s", name);
        }
        return true;
    } else if (strcmp(cmd_token, "MSG") == 0) {
        out_cmd->type = CMD_MSG;
        /* Phần còn lại của dòng là nội dung tin nhắn */
        if (saveptr != NULL) {
            const char *msg = protocol_trim_whitespace(saveptr);
            snprintf(out_cmd->text, sizeof(out_cmd->text), "%s", msg);
        } else {
            out_cmd->text[0] = '\0';
        }
        return true;
    } else if (strcmp(cmd_token, "PM") == 0) {
        out_cmd->type = CMD_PM;
        const char *target = strtok_r(NULL, " \t\r\n", &saveptr);
        if (target == NULL) return false;
        snprintf(out_cmd->target, sizeof(out_cmd->target), "%s", target);

        if (saveptr != NULL) {
            const char *msg = protocol_trim_whitespace(saveptr);
            snprintf(out_cmd->text, sizeof(out_cmd->text), "%s", msg);
        } else {
            out_cmd->text[0] = '\0';
        }
        return true;
    } else if (strcmp(cmd_token, "LIST") == 0) {
        out_cmd->type = CMD_LIST;
        return true;
    } else if (strcmp(cmd_token, "INFO") == 0) {
        out_cmd->type = CMD_INFO;
        return true;
    } else if (strcmp(cmd_token, "PONG") == 0) {
        out_cmd->type = CMD_PONG;
        return true;
    } else if (strcmp(cmd_token, "QUIT") == 0) {
        out_cmd->type = CMD_QUIT;
        return true;
    }

    return false;
}
