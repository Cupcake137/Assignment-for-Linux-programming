#ifndef THREAD_WORKERS_H
#define THREAD_WORKERS_H

#include <stdatomic.h>
#include "sbuffer.h"

extern atomic_int g_running;
extern atomic_int g_failed;

typedef struct {
    sbuffer_t *buffer;
    int port;
    const char *map_path;
    const char *db_path;
} gateway_ctx_t;

/* Record a fatal error and wake blocked consumers. */
void gateway_fail(gateway_ctx_t *ctx, const char *message);
void *connection_manager_thread(void *arg);
void *data_manager_thread(void *arg);
void *storage_manager_thread(void *arg);

#endif
