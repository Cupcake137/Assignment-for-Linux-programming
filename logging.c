#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

#define FIFO_NAME "logFifo"

static int fifo_fd = -1;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// initializes the logging system by opening the FIFO for writing
void log_init() {
    fifo_fd = open(FIFO_NAME, O_WRONLY);
    if (fifo_fd < 0) {
        perror("Failed to open FIFO");
        exit(EXIT_FAILURE);
    }
}

// logs an event by writing a formatted string to the FIFO
void log_event(const char *fmt, ...) {
    pthread_mutex_lock(&log_mutex);

    char buffer[512];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    write(fifo_fd, buffer, strlen(buffer));

    pthread_mutex_unlock(&log_mutex);
}

// cleans up the logging system by closing the FIFO
void log_cleanup() {
    if (fifo_fd != -1) {
        close(fifo_fd);
    }
}
