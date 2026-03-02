#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#include "logging.h"
#include "thread_workers.h"

#define FIFO_NAME "logFifo"
#define LOG_FILE  "gateway.log"

static void run_log_process() {
    int fifo_fd = open(FIFO_NAME, O_RDONLY);
    if (fifo_fd < 0) { perror("log process open FIFO"); exit(EXIT_FAILURE); }

    FILE *log_fp = fopen(LOG_FILE, "w");
    if (!log_fp) { perror("open log file"); exit(EXIT_FAILURE); }

    char buffer[512];
    int seq = 0;

    while (1) {
        int n = read(fifo_fd, buffer, sizeof(buffer)-1);

        if (n > 0) {
            buffer[n] = '\0';
            time_t now = time(NULL);
            fprintf(log_fp, "%d %ld %s\n", ++seq, now, buffer);
            fflush(log_fp);
        }
        else if (n == 0) {
            break;
        }
        else {
            perror("log process read FIFO");
            break;
        }
    }
}

static void on_sigint(int signo) {
    (void)signo;
    atomic_store(&g_running, 0);
    printf("Signal received\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    printf("Starting gateway on port %d\n", port);

    if (access(FIFO_NAME, F_OK) == -1) {
        if (mkfifo(FIFO_NAME, 0666) != 0) {
            perror("mkfifo");
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork failed"); exit(EXIT_FAILURE); }

    if (pid == 0) {
        run_log_process();

        /*
        if (strcmp(buffer, "SHUTDOWN") == 0) {
            printf("Log process exiting\n");
            //break;
        } */

        exit(0);
    }

    // parent
    signal(SIGINT, on_sigint);

    sleep(1);       // đảm bảo log process đã mở FIFO đọc
    log_init();

    log_event("Gateway started on port %d", port);

    pthread_t t_conn, t_data, t_store;
    pthread_create(&t_conn, NULL, connection_manager, NULL);
    pthread_create(&t_data, NULL, data_manager, NULL);
    pthread_create(&t_store, NULL, storage_manager, NULL);

    // Chờ threads thoát (Ctrl+C để stop)
    pthread_join(t_conn, NULL);
    pthread_join(t_data, NULL);
    pthread_join(t_store, NULL);

    log_event("Gateway shutting down");
    log_event("SHUTDOWN"); 
    log_cleanup();

    printf("Main exiting\n");
    return 0;
}