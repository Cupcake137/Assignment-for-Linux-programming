#include "thread_workers.h"
#include "logging.h"
#include "common.h"
#include "protocol.h"
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

typedef struct client {
    int fd;
    int identified;
    uint16_t sensor_id;
    char line[MAX_LINE];
    size_t used;
    struct timespec last_activity;
    struct client *next;
} client_t;

static int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int open_server(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    address.sin_port = htons((uint16_t)port);
    if (make_nonblocking(fd) < 0 ||
        bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void close_client(client_t *client)
{
    if (client->used != 0) log_event("connection_manager: incomplete line discarded");
    if (client->identified) {
        log_event("The sensor node with %u has closed the connection", client->sensor_id);
    } else {
        log_event("connection_manager: unidentified client disconnected");
    }
    close(client->fd);
    free(client);
}

static double idle_seconds(client_t *client)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - client->last_activity.tv_sec) +
        (double)(now.tv_nsec - client->last_activity.tv_nsec) / 1000000000.0;
}

/* Return -1 to disconnect, 0 to keep the client. */
static int receive_data(gateway_ctx_t *ctx, client_t *client)
{
    char bytes[256];
    ssize_t count = recv(client->fd, bytes, sizeof(bytes), 0);
    if (count == 0) return -1;
    if (count < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &client->last_activity);
    for (ssize_t i = 0; i < count; ++i) {
        if (bytes[i] != '\n') {
            if (bytes[i] == '\0' || client->used >= sizeof(client->line) - 1) {
                log_event("connection_manager: invalid or oversized line");
                return -1;
            }
            client->line[client->used++] = bytes[i];
            continue;
        }
        client->line[client->used] = '\0';
        client->used = 0;
        sensor_data_t data;
        if (parse_sensor_line(client->line, &data) < 0) {
            log_event("connection_manager: invalid measurement");
            return -1;
        }
        if (client->identified && client->sensor_id != data.sensor_id) {
            log_event("connection_manager: sensor ID changed on one connection");
            return -1;
        }
        if (!client->identified) {
            client->identified = 1;
            client->sensor_id = data.sensor_id;
            log_event("A sensor node with %u has opened a new connection", data.sensor_id);
        }
        if (sbuffer_push(ctx->buffer, &data) < 0) {
            gateway_fail(ctx, "Cannot add reading to shared buffer");
            return -1;
        }
#ifdef DEBUG
        log_event("DEBUG: received sensor=%u value=%.2f ts=%lld",
                  data.sensor_id, data.value, (long long)data.ts);
#endif
    }
    return 0;
}

void *connection_manager_thread(void *arg)
{
    gateway_ctx_t *ctx = arg;
    client_t *clients = NULL;
    int server_fd = open_server(ctx->port);
    if (server_fd < 0) {
        gateway_fail(ctx, "Cannot open TCP listening socket");
        sbuffer_close(ctx->buffer);
        return NULL;
    }
    log_event("connection_manager: listening on 127.0.0.1:%d", ctx->port);
    while (atomic_load(&g_running)) {
        size_t count = 1;
        for (client_t *c = clients; c != NULL; c = c->next) ++count;
        struct pollfd *fds = calloc(count, sizeof(*fds));
        if (fds == NULL) {
            gateway_fail(ctx, "Cannot allocate poll array");
            break;
        }
        fds[0].fd = server_fd;
        fds[0].events = POLLIN;
        size_t index = 1;
        for (client_t *c = clients; c != NULL; c = c->next) {
            fds[index].fd = c->fd;
            fds[index].events = POLLIN;
            ++index;
        }
        int ready = poll(fds, (nfds_t)count, 200);
        if (ready < 0) {
            free(fds);
            if (errno == EINTR) continue;
            gateway_fail(ctx, "poll failed");
            break;
        }
        /* Handle existing nodes before adding a client to keep indices aligned. */
        client_t *previous = NULL;
        client_t *current = clients;
        index = 1;
        while (current != NULL) {
            client_t *next = current->next;
            short events = fds[index++].revents;
            int remove = 0;
            if (events & (POLLIN | POLLHUP)) {
                remove = receive_data(ctx, current) < 0;
            }
            if (events & (POLLERR | POLLNVAL)) remove = 1;
            if (idle_seconds(current) >= CLIENT_TIMEOUT_SECONDS) {
                log_event("connection_manager: idle timeout");
                remove = 1;
            }
            if (remove) {
                if (previous == NULL) clients = next;
                else previous->next = next;
                close_client(current);
            } else {
                previous = current;
            }
            current = next;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            gateway_fail(ctx, "Listening socket failed");
        }
        if ((fds[0].revents & POLLIN) && atomic_load(&g_running)) {
            int fd = accept(server_fd, NULL, NULL);
            if (fd >= 0) {
                client_t *client = calloc(1, sizeof(*client));
                if (client == NULL || make_nonblocking(fd) < 0) {
                    free(client);
                    close(fd);
                    gateway_fail(ctx, "Cannot initialize TCP client");
                } else {
                    client->fd = fd;
                    clock_gettime(CLOCK_MONOTONIC, &client->last_activity);
                    client->next = clients;
                    clients = client;
                }
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                gateway_fail(ctx, "accept failed");
            }
        }
        free(fds);
    }
    close(server_fd);
    while (clients != NULL) {
        client_t *next = clients->next;
        close_client(clients);
        clients = next;
    }
    /* No more producer writes: consumers can drain and finish. */
    sbuffer_close(ctx->buffer);
    log_event("connection_manager: exiting");
    return NULL;
}
