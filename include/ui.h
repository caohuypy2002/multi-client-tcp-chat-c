#ifndef UI_H
#define UI_H

#include <stdbool.h>

/* Khởi tạo giao diện terminal và kiểm tra khả năng hỗ trợ màu ANSI */
void ui_init(bool force_no_color);

/* In lại dấu nhắc nhập liệu "> " */
void ui_print_prompt(void);

/* In tin nhắn thông thường từ phòng chat */
void ui_print_msg(const char *time_str, const char *from, const char *text, bool is_me);

/* In tin nhắn riêng tư (PM) */
void ui_print_pm(const char *time_str, const char *from, const char *to, const char *text, bool is_outgoing);

/* In thông báo hệ thống (vào/ra phòng, trạng thái) */
void ui_print_system(const char *text);

/* In thông báo lỗi */
void ui_print_error(const char *text);

/* In danh sách người dùng online */
void ui_print_user_list(int count, int max, const char *users_csv);

/* In thông tin chẩn đoán luồng nhân Linux (PID/TGID, Worker TID/LWP) */
void ui_print_thread_info(int srv_pid, int my_tid, int count, int max, const char *details);

/* In bảng hướng dẫn các lệnh / */
void ui_print_help(void);

#endif /* UI_H */
