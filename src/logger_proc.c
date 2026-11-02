#include "logger_proc.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

int logger_process_run(void)
{
    /* Open the file first so startup errors do not leave a reader waiting. */
    FILE *log_fp = fopen(LOG_FILE, "w");
    if (log_fp == NULL) return 1;
    int fd = open(FIFO_NAME, O_RDONLY);
    if (fd < 0) { fclose(log_fp); return 1; }
    FILE *fifo_fp = fdopen(fd, "r");
    if (fifo_fp == NULL) { close(fd); fclose(log_fp); return 1; }

    char line[512];
    unsigned long seq = 0;
    int result = 0;
    while (fgets(line, sizeof(line), fifo_fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "SHUTDOWN") == 0) break;
        time_t now = time(NULL);
        struct tm tm_now;
        char tbuf[32];
        if (localtime_r(&now, &tm_now) == NULL ||
            strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm_now) == 0 ||
            fprintf(log_fp, "%lu %s %s\n", ++seq, tbuf, line) < 0 ||
            fflush(log_fp) != 0) {
            result = 1;
            break;
        }
    }
    if (ferror(fifo_fp)) result = 1;
    if (fclose(log_fp) != 0) result = 1;
    fclose(fifo_fp);
    return result;
}
