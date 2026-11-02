#include "logging.h"
#include "common.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>

static int g_fifo_fd = -1;
static atomic_int g_log_failed = 0;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

int log_init(void)
{
    /* Do not hang forever if the logger fails before opening its FIFO. */
    struct timespec delay = {0, 100000000};
    for (int attempt = 0; attempt < 30; ++attempt) {
        g_fifo_fd = open(FIFO_NAME, O_WRONLY | O_NONBLOCK);
        if (g_fifo_fd >= 0) break;
        if (errno != ENXIO && errno != EINTR) return -1;
        nanosleep(&delay, NULL);
    }
    if (g_fifo_fd < 0) return -1;
    int flags = fcntl(g_fifo_fd, F_GETFL);
    if (flags < 0 || fcntl(g_fifo_fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        log_close();
        return -1;
    }
    return 0;
}

int log_event(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    /* Reserve space for newline and terminator, including truncated messages. */
    vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    size_t length = strlen(buf);
    /* Embedded newlines must not split one event into several records. */
    for (size_t i = 0; i < length; ++i) {
        if (buf[i] == '\r' || buf[i] == '\n') buf[i] = ' ';
    }
    buf[length++] = '\n';
    pthread_mutex_lock(&g_log_mutex);
    size_t written = 0;
    int result = 0;
    while (written < length) {
        ssize_t n = write(g_fifo_fd, buf + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            atomic_store(&g_log_failed, 1);
            result = -1;
            break;
        }
        written += (size_t)n;
    }
    pthread_mutex_unlock(&g_log_mutex);
    return result;
}

void log_close(void)
{
    if (g_fifo_fd >= 0) close(g_fifo_fd);
    g_fifo_fd = -1;
}

int log_failed(void)
{
    return atomic_load(&g_log_failed);
}
