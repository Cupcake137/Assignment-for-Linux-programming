#ifndef LOGGING_H
#define LOGGING_H

/* Thread-safe FIFO writer. -1 means failure, 0 means success. */
int log_init(void);
int log_event(const char *fmt, ...);
void log_close(void);
int log_failed(void);

#endif
