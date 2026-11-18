#ifndef HANDLER_H
#define HANDLER_H

#include <pthread.h>

/* Hàm xử lý luồng riêng biệt cho từng client */
void *client_handler_thread(void *arg);

/* Quản lý bộ đếm số lượng luồng client đang hoạt động */
void handler_inc_active_threads(void);
void handler_dec_active_threads(void);

extern int g_active_threads;
extern pthread_mutex_t g_threads_lock;
extern pthread_cond_t g_threads_cond;

#endif /* HANDLER_H */
