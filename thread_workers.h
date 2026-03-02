#ifndef THREAD_WORKERS_H
#define THREAD_WORKERS_H

#include <stdatomic.h>

extern atomic_int g_running;

void *connection_manager(void *arg);
void *data_manager(void *arg);
void *storage_manager(void *arg);

#endif