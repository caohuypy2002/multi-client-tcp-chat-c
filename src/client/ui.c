/*
 * Trách nhiệm: Định dạng hiển thị phía client: hỗ trợ màu sắc ANSI,
 * tự động tắt màu khi không phải tty hoặc có cờ --no-color,
 * xoá dòng đang gõ khi có tin nhắn mới đến (\r\033[K) và vẽ lại prompt.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "ui.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static bool s_use_color = true;

#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_GRAY    "\033[0;90m"

void ui_init(bool force_no_color) {
    if (force_no_color || !isatty(STDOUT_FILENO)) {
        s_use_color = false;
    } else {
        s_use_color = true;
    }
}

void ui_print_prompt(void) {
    if (s_use_color) {
        printf("%s> %s", COLOR_GREEN, COLOR_RESET);
    } else {
        printf("> ");
    }
    fflush(stdout);
}

static void clear_current_line(void) {
    if (s_use_color) {
        /* \r về đầu dòng, \033[K xoá sạch từ con trỏ tới cuối dòng */
        printf("\r\033[K");
    } else {
        printf("\r");
    }
}

void ui_print_msg(const char *time_str, const char *from, const char *text, bool is_me) {
    clear_current_line();
    if (s_use_color) {
        if (is_me) {
            printf("%s[%s]%s %sBạn (%s):%s %s\n", COLOR_GRAY, time_str, COLOR_RESET,
                   COLOR_GREEN, from, COLOR_RESET, text);
        } else {
            printf("%s[%s]%s %s%s:%s %s\n", COLOR_GRAY, time_str, COLOR_RESET,
                   COLOR_CYAN, from, COLOR_RESET, text);
        }
    } else {
        if (is_me) {
            printf("[%s] Bạn (%s): %s\n", time_str, from, text);
        } else {
            printf("[%s] %s: %s\n", time_str, from, text);
        }
    }
    ui_print_prompt();
}

void ui_print_pm(const char *time_str, const char *from, const char *to, const char *text, bool is_outgoing) {
    clear_current_line();
    if (s_use_color) {
        if (is_outgoing) {
            printf("%s[%s] [Riêng tới %s]:%s %s\n", COLOR_MAGENTA, time_str, to, COLOR_RESET, text);
        } else {
            printf("%s[%s] [Riêng từ %s]:%s %s\n", COLOR_MAGENTA, time_str, from, COLOR_RESET, text);
        }
    } else {
        if (is_outgoing) {
            printf("[%s] [Riêng -> %s]: %s\n", time_str, to, text);
        } else {
            printf("[%s] [Riêng <- %s]: %s\n", time_str, from, text);
        }
    }
    ui_print_prompt();
}

void ui_print_system(const char *text) {
    clear_current_line();
    if (s_use_color) {
        printf("%s*** %s ***%s\n", COLOR_YELLOW, text, COLOR_RESET);
    } else {
        printf("*** %s ***\n", text);
    }
    ui_print_prompt();
}

void ui_print_error(const char *text) {
    clear_current_line();
    if (s_use_color) {
        printf("%sLỗi: %s%s\n", COLOR_RED, text, COLOR_RESET);
    } else {
        printf("Lỗi: %s\n", text);
    }
    ui_print_prompt();
}

void ui_print_user_list(int count, int max, const char *users_csv) {
    clear_current_line();
    if (s_use_color) {
        printf("%s--- Danh sách trực tuyến (%d/%d) ---%s\n", COLOR_CYAN, count, max, COLOR_RESET);
        printf("%s%s%s\n", COLOR_GREEN, (users_csv && *users_csv) ? users_csv : "(trống)", COLOR_RESET);
        printf("%s-----------------------------------%s\n", COLOR_CYAN, COLOR_RESET);
    } else {
        printf("--- Danh sách trực tuyến (%d/%d) ---\n", count, max);
        printf("%s\n", (users_csv && *users_csv) ? users_csv : "(trống)");
        printf("-----------------------------------\n");
    }
    ui_print_prompt();
}

void ui_print_thread_info(int srv_pid, int my_tid, int count, int max, const char *details) {
    clear_current_line();
    if (s_use_color) {
        printf("%s=== CHẨN ĐOÁN LUỒNG NHÂN LINUX (KERNEL LWP/THREADS) ===%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  - Server Process PID (TGID) : %s%d%s\n", COLOR_GREEN, srv_pid, COLOR_RESET);
        printf("  - Worker Thread của bạn (TID): %s%d (LWP)%s\n", COLOR_CYAN, my_tid, COLOR_RESET);
        printf("  - Số Client đang hoạt động  : %s%d/%d%s\n", COLOR_GREEN, count, max, COLOR_RESET);
        printf("  - Chi tiết các Worker Thread: %s\n", (details && *details) ? details : "(trống)");
        printf("  - Lệnh kiểm tra trên Linux  : %sps -eLf -q %d%s\n", COLOR_MAGENTA, srv_pid, COLOR_RESET);
        printf("%s=======================================================%s\n", COLOR_YELLOW, COLOR_RESET);
    } else {
        printf("=== CHẨN ĐOÁN LUỒNG NHÂN LINUX (KERNEL LWP/THREADS) ===\n");
        printf("  - Server Process PID (TGID) : %d\n", srv_pid);
        printf("  - Worker Thread của bạn (TID): %d (LWP)\n", my_tid);
        printf("  - Số Client đang hoạt động  : %d/%d\n", count, max);
        printf("  - Chi tiết các Worker Thread: %s\n", (details && *details) ? details : "(trống)");
        printf("  - Lệnh kiểm tra trên Linux  : ps -eLf -q %d\n", srv_pid);
        printf("=======================================================\n");
    }
    ui_print_prompt();
}

void ui_print_help(void) {
    clear_current_line();
    printf("================ HƯỚNG DẪN LỆNH ================\n");
    printf(" /help                  : Xem trợ giúp này\n");
    printf(" /users (hoặc /list)    : Xem danh sách người đang online\n");
    printf(" /threads (hoặc /info)  : Xem PID/TID luồng nhân Linux của Server\n");
    printf(" /w <tên> <nội dung>    : Gửi tin nhắn riêng tư (hoặc /msg <tên> <nd>)\n");
    printf(" /quit (hoặc /exit)     : Rời khỏi phòng chat\n");
    printf(" Nhập văn bản bình thường rồi Enter để gửi tin nhóm.\n");
    printf("=================================================\n");
    ui_print_prompt();
}
