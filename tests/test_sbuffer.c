#include "sbuffer.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#define COUNT 2000

typedef struct {
    sbuffer_t *buffer;
    consumer_t role;
} reader_args_t;

static void *consume(void *arg)
{
    reader_args_t *ctx = arg;
    sensor_data_t data;
    int received = 0;
    while (sbuffer_pop_for(ctx->buffer, ctx->role, &data) == 0) {
        assert(data.ts == received && data.value == received);
        sensor_data_t again;
        assert(sbuffer_pop_for(ctx->buffer, ctx->role, &again) == 0);
        assert(again.ts == data.ts); /* A read is not an acknowledgment. */
        if (ctx->role == CONSUMER_STORAGE_MANAGER && received % 100 == 0) {
            struct timespec delay = {0, 1000000};
            nanosleep(&delay, NULL);
        }
        assert(sbuffer_ack_for(ctx->buffer, ctx->role) == 0);
        ++received;
    }
    assert(received == COUNT);
    return NULL;
}

int main(void)
{
    sbuffer_t *buffer = sbuffer_create();
    assert(buffer != NULL);
    reader_args_t args[] = {
        {buffer, CONSUMER_DATA_MANAGER},
        {buffer, CONSUMER_STORAGE_MANAGER}
    };
    pthread_t readers[2];
    for (int i = 0; i < 2; ++i) {
        assert(pthread_create(&readers[i], NULL, consume, &args[i]) == 0);
    }
    for (int i = 0; i < COUNT; ++i) {
        sensor_data_t data = {20, (double)i, i};
        assert(sbuffer_push(buffer, &data) == 0);
    }
    sbuffer_close(buffer);
    for (int i = 0; i < 2; ++i) pthread_join(readers[i], NULL);
    sensor_data_t data = {20, 21.0, 1};
    assert(sbuffer_push(buffer, &data) == -1);
    assert(sbuffer_pop_for(buffer, (consumer_t)99, &data) == -1);
    sbuffer_destroy(buffer);
    puts("PASS buffer: ordered independent readers, delayed storage, ack, close/drain");
    return 0;
}
