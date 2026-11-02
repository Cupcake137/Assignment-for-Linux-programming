#include "sbuffer.h"
#include <stdlib.h>
#include <pthread.h>

/* One node is shared by both consumers. Done means processing succeeded. */
typedef struct sbuffer_node {
    sensor_data_t data;
    int read_by_data_mgr;
    int read_by_storage_mgr;
    struct sbuffer_node *next;
} sbuffer_node_t;

struct sbuffer {
    sbuffer_node_t *head;
    sbuffer_node_t *tail;
    int closed;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
};

static int valid_consumer(consumer_t who)
{
    return who == CONSUMER_DATA_MANAGER || who == CONSUMER_STORAGE_MANAGER;
}

/* Called with the mutex held. Each role advances independently. */
static sbuffer_node_t *find_unread(sbuffer_t *b, consumer_t who)
{
    sbuffer_node_t *node = b->head;
    while (node != NULL) {
        int done = who == CONSUMER_DATA_MANAGER ?
            node->read_by_data_mgr : node->read_by_storage_mgr;
        if (!done) return node;
        node = node->next;
    }
    return NULL;
}

static void remove_fully_consumed_head(sbuffer_t *b)
{
    while (b->head != NULL && b->head->read_by_data_mgr &&
           b->head->read_by_storage_mgr) {
        sbuffer_node_t *old = b->head;
        b->head = old->next;
        free(old);
    }
    if (b->head == NULL) b->tail = NULL;
}

sbuffer_t *sbuffer_create(void)
{
    sbuffer_t *b = calloc(1, sizeof(*b));
    if (b == NULL) return NULL;
    if (pthread_mutex_init(&b->mtx, NULL) != 0) {
        free(b);
        return NULL;
    }
    if (pthread_cond_init(&b->cond, NULL) != 0) {
        pthread_mutex_destroy(&b->mtx);
        free(b);
        return NULL;
    }
    return b;
}

void sbuffer_destroy(sbuffer_t *b)
{
    if (b == NULL) return;
    sbuffer_node_t *cur = b->head;
    while (cur != NULL) {
        sbuffer_node_t *next = cur->next;
        free(cur);
        cur = next;
    }
    pthread_mutex_destroy(&b->mtx);
    pthread_cond_destroy(&b->cond);
    free(b);
}

int sbuffer_push(sbuffer_t *b, const sensor_data_t *data)
{
    if (b == NULL || data == NULL) return -1;
    sbuffer_node_t *node = calloc(1, sizeof(*node));
    if (node == NULL) return -1;
    node->data = *data;
    pthread_mutex_lock(&b->mtx);
    if (b->closed) {
        pthread_mutex_unlock(&b->mtx);
        free(node);
        return -1;
    }
    if (b->tail != NULL) b->tail->next = node;
    else b->head = node;
    b->tail = node;
    pthread_cond_broadcast(&b->cond);
    pthread_mutex_unlock(&b->mtx);
    return 0;
}

int sbuffer_pop_for(sbuffer_t *b, consumer_t who, sensor_data_t *out)
{
    if (b == NULL || out == NULL || !valid_consumer(who)) return -1;
    pthread_mutex_lock(&b->mtx);
    sbuffer_node_t *node = find_unread(b, who);
    while (node == NULL && !b->closed) {
        pthread_cond_wait(&b->cond, &b->mtx);
        node = find_unread(b, who);
    }
    int result = 1;
    if (node != NULL) {
        *out = node->data;
        result = 0;
    }
    pthread_mutex_unlock(&b->mtx);
    return result;
}

int sbuffer_ack_for(sbuffer_t *b, consumer_t who)
{
    if (b == NULL || !valid_consumer(who)) return -1;
    pthread_mutex_lock(&b->mtx);
    sbuffer_node_t *node = find_unread(b, who);
    int result = -1;
    if (node != NULL) {
        if (who == CONSUMER_DATA_MANAGER) node->read_by_data_mgr = 1;
        else node->read_by_storage_mgr = 1;
        remove_fully_consumed_head(b);
        result = 0;
    }
    pthread_mutex_unlock(&b->mtx);
    return result;
}

void sbuffer_close(sbuffer_t *b)
{
    if (b == NULL) return;
    pthread_mutex_lock(&b->mtx);
    b->closed = 1;
    pthread_cond_broadcast(&b->cond);
    pthread_mutex_unlock(&b->mtx);
}
