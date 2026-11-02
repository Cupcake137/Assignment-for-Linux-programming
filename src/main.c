#include "common.h"
#include "logging.h"
#include "logger_proc.h"
#include "thread_workers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signo)
{
    (void)signo;
    stop_requested = 1;
}

int main(int argc, char *argv[])
{
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "Usage: %s PORT [MAP_PATH DB_PATH]\n", argv[0]);
        return 1;
    }
    char *end;
    errno = 0;
    long port = strtol(argv[1], &end, 10);
    if (errno || *argv[1] == '\0' || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }
    sbuffer_t *buffer = sbuffer_create();
    if (buffer == NULL) return 1;
    gateway_ctx_t ctx = {buffer, (int)port,
                        argc == 4 ? argv[2] : MAP_FILE,
                        argc == 4 ? argv[3] : DB_FILE};

    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        sbuffer_destroy(buffer);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    if (mkfifo(FIFO_NAME, 0600) < 0 && errno != EEXIST) {
        perror("mkfifo");
        sbuffer_destroy(buffer);
        return 1;
    }
    struct stat st;
    if (lstat(FIFO_NAME, &st) < 0 || !S_ISFIFO(st.st_mode)) {
        fprintf(stderr, "logFifo exists but is not a FIFO\n");
        sbuffer_destroy(buffer);
        return 1;
    }

    /* Fork before creating any threads. */
    pid_t logger_pid = fork();
    if (logger_pid < 0) {
        perror("fork");
        sbuffer_destroy(buffer);
        unlink(FIFO_NAME);
        return 1;
    }
    if (logger_pid == 0) {
        signal(SIGINT, SIG_IGN);
        _exit(logger_process_run());
    }
    if (log_init() < 0) {
        fprintf(stderr, "Cannot start FIFO logger\n");
        kill(logger_pid, SIGKILL);
        waitpid(logger_pid, NULL, 0);
        sbuffer_destroy(buffer);
        unlink(FIFO_NAME);
        return 1;
    }
    log_event("Gateway started on port %ld", port);
    pthread_t t_data, t_store, t_conn;
    int data_started = 0, store_started = 0, conn_started = 0;
    if (pthread_create(&t_data, NULL, data_manager_thread, &ctx) == 0) data_started = 1;
    if (data_started && pthread_create(&t_store, NULL, storage_manager_thread, &ctx) == 0) {
        store_started = 1;
    }
    if (store_started && pthread_create(&t_conn, NULL, connection_manager_thread, &ctx) == 0) {
        conn_started = 1;
    }
    if (!conn_started) gateway_fail(&ctx, "Cannot create all worker threads");
    printf("Gateway starting on 127.0.0.1:%ld; Ctrl+C to stop.\n", port);
    fflush(stdout);

    int child_status = 0;
    int child_reaped = 0;
    struct timespec delay = {0, 100000000};
    while (atomic_load(&g_running) && !stop_requested && !log_failed()) {
        pid_t result = waitpid(logger_pid, &child_status, WNOHANG);
        if (result == logger_pid) {
            child_reaped = 1;
            gateway_fail(&ctx, "Logger exited unexpectedly");
            break;
        }
        if (result < 0 && errno != EINTR) {
            gateway_fail(&ctx, "Cannot monitor logger process");
            break;
        }
        nanosleep(&delay, NULL);
    }
    if (log_failed()) atomic_store(&g_failed, 1);
    atomic_store(&g_running, 0);
    /* Producer closes the buffer after it stops writing. */
    if (conn_started) pthread_join(t_conn, NULL);
    else sbuffer_close(buffer);
    if (data_started) pthread_join(t_data, NULL);
    if (store_started) pthread_join(t_store, NULL);
    sbuffer_destroy(buffer);

    log_event("Gateway shutting down");
    log_event("SHUTDOWN");
    log_close();
    if (!child_reaped) {
        pid_t result;
        do { result = waitpid(logger_pid, &child_status, 0); }
        while (result < 0 && errno == EINTR);
        if (result < 0) atomic_store(&g_failed, 1);
    }
    unlink(FIFO_NAME);
    if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0 || log_failed()) {
        atomic_store(&g_failed, 1);
    }
    return atomic_load(&g_failed) ? 1 : 0;
}
