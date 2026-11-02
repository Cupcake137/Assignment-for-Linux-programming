#ifndef SBUFFER_H
#define SBUFFER_H

#include <stdint.h>

typedef struct {
    uint16_t sensor_id;
    double value;
    int64_t ts;
} sensor_data_t;

typedef enum {
    CONSUMER_DATA_MANAGER = 0,
    CONSUMER_STORAGE_MANAGER = 1
} consumer_t;

typedef struct sbuffer sbuffer_t;
sbuffer_t *sbuffer_create(void);
/* Call destroy only after all threads have joined. */
void sbuffer_destroy(sbuffer_t *b);
int sbuffer_push(sbuffer_t *b, const sensor_data_t *data);
/* Copy, but do not mark complete. 0=success, 1=closed/drained, -1=bad argument.
 * Exactly one thread per consumer role; always pop -> process -> ack in order.
 * Calling pop again without ack returns the same item. */
int sbuffer_pop_for(sbuffer_t *b, consumer_t who, sensor_data_t *out);
int sbuffer_ack_for(sbuffer_t *b, consumer_t who);
void sbuffer_close(sbuffer_t *b);

#endif
