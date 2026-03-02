#ifndef LOGGING_H
#define LOGGING_H

void log_init();
void log_event(const char *fmt, ...);
void log_cleanup();

#endif // LOGGING_H