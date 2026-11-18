/*
 * Trách nhiệm: Cung cấp tiện ích ghi log có timestamp, mức độ nghiêm trọng,
 * an toàn trong môi trường đa luồng (bảo vệ bằng pthread_mutex).
 */

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

static FILE *s_log_file = NULL;
static bool s_verbose = false;
static pthread_mutex_t s_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void log_write_internal(const char *level, const char *fmt, va_list args) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    pthread_mutex_lock(&s_log_mutex);

    /* In ra stderr */
    fprintf(stderr, "[%s] [%s] ", time_str, level);
    va_list args_stderr;
    va_copy(args_stderr, args);
    vfprintf(stderr, fmt, args_stderr);
    va_end(args_stderr);
    fprintf(stderr, "\n");
    fflush(stderr);

    /* In vào file nếu có */
    if (s_log_file != NULL) {
        fprintf(s_log_file, "[%s] [%s] ", time_str, level);
        va_list args_file;
        va_copy(args_file, args);
        vfprintf(s_log_file, fmt, args_file);
        va_end(args_file);
        fprintf(s_log_file, "\n");
        fflush(s_log_file);
    }

    pthread_mutex_unlock(&s_log_mutex);
}

void log_init(const char *filepath, bool verbose) {
    pthread_mutex_lock(&s_log_mutex);
    s_verbose = verbose;
    if (filepath != NULL) {
        /* Tạo thư mục cha logs nếu chưa có */
        mkdir("logs", 0755);
        s_log_file = fopen(filepath, "a");
        if (s_log_file == NULL) {
            fprintf(stderr, "Cảnh báo: Không thể mở file log: %s\n", filepath);
        }
    }
    pthread_mutex_unlock(&s_log_mutex);
}

void log_close(void) {
    pthread_mutex_lock(&s_log_mutex);
    if (s_log_file != NULL) {
        fclose(s_log_file);
        s_log_file = NULL;
    }
    pthread_mutex_unlock(&s_log_mutex);
}

void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_internal("INFO", fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_internal("WARN", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_internal("ERROR", fmt, args);
    va_end(args);
}

void log_msg(const char *fmt, ...) {
    if (!s_verbose) return;
    va_list args;
    va_start(args, fmt);
    log_write_internal("MSG", fmt, args);
    va_end(args);
}
