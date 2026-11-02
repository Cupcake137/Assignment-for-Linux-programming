#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static long number(const char *text, long minimum, long maximum)
{
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || *text == '\0' || *end != '\0' || value < minimum || value > maximum) {
        fprintf(stderr, "Invalid argument: %s\n", text);
        exit(1);
    }
    return value;
}

int main(int argc, char *argv[])
{
    if (argc != 7) {
        fprintf(stderr, "Usage: %s IP PORT SENSOR_ID TEMPERATURE COUNT INTERVAL_MS\n", argv[0]);
        return 1;
    }
    int port = (int)number(argv[2], 1, 65535);
    unsigned id = (unsigned)number(argv[3], 1, UINT16_MAX);
    char *end;
    errno = 0;
    double temperature = strtod(argv[4], &end);
    if (errno || *argv[4] == '\0' || *end != '\0' || !isfinite(temperature)) return 1;
    long count = number(argv[5], 1, 1000000);
    long interval = number(argv[6], 0, 60000);
    signal(SIGPIPE, SIG_IGN);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1 ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }
    struct timespec delay = {interval / 1000, (interval % 1000) * 1000000};
    for (long i = 0; i < count; ++i) {
        char line[128];
        int length = snprintf(line, sizeof(line), "%u %.17g %lld\n",
                              id, temperature, (long long)time(NULL));
        if (length < 0 || (size_t)length >= sizeof(line)) { close(fd); return 1; }
        size_t sent = 0;
        while (sent < (size_t)length) {
            ssize_t n = send(fd, line + sent, (size_t)length - sent, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { perror("send"); close(fd); return 1; }
            sent += (size_t)n;
        }
        if (i + 1 < count) nanosleep(&delay, NULL);
    }
    close(fd);
    return 0;
}
