#include <stdio.h>
#include <unistd.h>
#include "thread_workers.h"
#include "logging.h"

atomic_int g_running = 1;

void *connection_manager(void *arg) {
    (void)arg;
    log_event("connection_manager: started\n");
    while (atomic_load(&g_running)) {
        // giả lập làm việc
        sleep(1);
    }
    log_event("connection_manager: exiting\n");
    return NULL;
}

void *data_manager(void *arg) {
    (void)arg;
    log_event("data_manager: started\n");
    while (atomic_load(&g_running)) {
        sleep(1);
    }
    log_event("data_manager: exiting\n");
    return NULL;
}

void *storage_manager(void *arg) {
    (void)arg;
    log_event("storage_manager: started\n");
    while (atomic_load(&g_running)) {
        sleep(1);
    }
    log_event("storage_manager: exiting\n");
    return NULL;
}