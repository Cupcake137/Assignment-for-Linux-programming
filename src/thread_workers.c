#include "thread_workers.h"
#include "logging.h"
#include <stdio.h>

atomic_int g_running = 1;
atomic_int g_failed = 0;

void gateway_fail(gateway_ctx_t *ctx, const char *message)
{
    fprintf(stderr, "%s\n", message);
    log_event("ERROR: %s", message);
    atomic_store(&g_failed, 1);
    atomic_store(&g_running, 0);
    sbuffer_close(ctx->buffer);
}
