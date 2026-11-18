#ifndef LOG_H
#define LOG_H

#include <stdbool.h>

/* Khởi tạo hệ thống ghi log (ghi ra stderr và file logs/server.log) */
void log_init(const char *filepath, bool verbose);

/* Đóng tài nguyên ghi log */
void log_close(void);

/* Các mức ghi log an toàn luồng (thread-safe qua mutex) */
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

/* Chỉ in khi bật cờ verbose (-v) */
void log_msg(const char *fmt, ...);

#endif /* LOG_H */
