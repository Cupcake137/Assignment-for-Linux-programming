#include "thread_workers.h"
#include "logging.h"
#include "common.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct room {
    unsigned room_id;
    unsigned sensor_id;
    double samples[RUN_AVG_LENGTH];
    double sum;
    int count;
    int next_sample;
    struct room *next;
} room_t;

static void free_rooms(room_t *rooms)
{
    while (rooms != NULL) {
        room_t *next = rooms->next;
        free(rooms);
        rooms = next;
    }
}

static int load_rooms(const char *path, room_t **rooms)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return -1;
    char line[MAX_LINE];
    int result = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned room_id, sensor_id;
        if ((strchr(line, '\n') == NULL && !feof(fp)) ||
            parse_map_line(line, &room_id, &sensor_id) < 0) {
            result = -1;
            break;
        }
        for (room_t *r = *rooms; r != NULL; r = r->next) {
            if (r->room_id == room_id || r->sensor_id == sensor_id) result = -1;
        }
        if (result < 0) break;
        room_t *room = calloc(1, sizeof(*room));
        if (room == NULL) { result = -1; break; }
        room->room_id = room_id;
        room->sensor_id = sensor_id;
        room->next = *rooms;
        *rooms = room;
    }
    if (ferror(fp) || *rooms == NULL) result = -1;
    fclose(fp);
    return result;
}

void *data_manager_thread(void *arg)
{
    gateway_ctx_t *ctx = arg;
    room_t *rooms = NULL;
    if (load_rooms(ctx->map_path, &rooms) < 0) {
        gateway_fail(ctx, "Invalid room map: missing, empty, malformed or duplicate IDs");
        free_rooms(rooms);
        return NULL;
    }
    log_event("data_manager: started");
    sensor_data_t data;
    while (sbuffer_pop_for(ctx->buffer, CONSUMER_DATA_MANAGER, &data) == 0) {
        room_t *room = rooms;
        while (room != NULL && room->sensor_id != data.sensor_id) room = room->next;
        if (room == NULL) {
            log_event("Received sensor data with invalid sensor node ID %u", data.sensor_id);
        } else {
            room->sum -= room->samples[room->next_sample];
            room->samples[room->next_sample] = data.value;
            room->sum += data.value;
            if (!isfinite(room->sum)) {
                gateway_fail(ctx, "Temperature running sum overflowed");
                break;
            }
            room->next_sample = (room->next_sample + 1) % RUN_AVG_LENGTH;
            if (room->count < RUN_AVG_LENGTH) ++room->count;
            if (room->count == RUN_AVG_LENGTH) {
                double average = room->sum / RUN_AVG_LENGTH;
                if (average < TEMP_MIN || average > TEMP_MAX) {
                    log_event("The sensor node with %u reports it is too %s "
                              "(running avg temperature = %.2f, room = %u)",
                              data.sensor_id, average < TEMP_MIN ? "cold" : "hot",
                              average, room->room_id);
                }
            }
        }
        sbuffer_ack_for(ctx->buffer, CONSUMER_DATA_MANAGER);
    }
    free_rooms(rooms);
    log_event("data_manager: exiting");
    return NULL;
}
