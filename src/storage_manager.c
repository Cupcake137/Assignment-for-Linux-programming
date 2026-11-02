#include "thread_workers.h"
#include "logging.h"
#include "common.h"
#include <sqlite3.h>
#include <time.h>

static void retry_delay(void)
{
    struct timespec delay = {0, 100000000};
    for (int i = 0; i < DB_RETRY_SECONDS * 10 && atomic_load(&g_running); ++i) {
        nanosleep(&delay, NULL);
    }
}

static void close_database(sqlite3 **db, sqlite3_stmt **insert)
{
    sqlite3_finalize(*insert);
    *insert = NULL;
    sqlite3_close(*db);
    *db = NULL;
}

/* A ready connection includes the table and a prepared INSERT statement. */
static int open_database(gateway_ctx_t *ctx, sqlite3 **db, sqlite3_stmt **insert)
{
    const char *check = "SELECT 1 FROM sqlite_master WHERE type='table' AND name='sensor_data'";
    const char *schema = "CREATE TABLE IF NOT EXISTS sensor_data ("
        "id INTEGER PRIMARY KEY, sensor_id INTEGER NOT NULL,"
        "temperature REAL NOT NULL, timestamp INTEGER NOT NULL)";
    const char *sql = "INSERT INTO sensor_data(sensor_id,temperature,timestamp) VALUES(?,?,?)";
    for (int attempt = 1; attempt <= DB_ATTEMPTS; ++attempt) {
        sqlite3_stmt *query = NULL;
        int ok = sqlite3_open(ctx->db_path, db) == SQLITE_OK;
        int new_table = 0;
        if (ok) {
            sqlite3_busy_timeout(*db, 2000);
            ok = sqlite3_prepare_v2(*db, check, -1, &query, NULL) == SQLITE_OK;
        }
        if (ok) {
            int step = sqlite3_step(query);
            ok = step == SQLITE_ROW || step == SQLITE_DONE;
            new_table = step == SQLITE_DONE;
        }
        sqlite3_finalize(query);
        if (ok) ok = sqlite3_exec(*db, schema, NULL, NULL, NULL) == SQLITE_OK;
        if (ok && new_table) log_event("New table sensor_data created");
        if (ok) ok = sqlite3_prepare_v2(*db, sql, -1, insert, NULL) == SQLITE_OK;
        if (ok) {
            log_event("SQLite database connection established");
            return 0;
        }
        log_event("Unable to initialize SQLite database (attempt %d/%d): %s",
                  attempt, DB_ATTEMPTS, *db ? sqlite3_errmsg(*db) : "out of memory");
        close_database(db, insert);
        if (attempt < DB_ATTEMPTS) retry_delay();
    }
    return -1;
}

static int insert_reading(sqlite3_stmt *insert, const sensor_data_t *data)
{
    return sqlite3_bind_int(insert, 1, data->sensor_id) == SQLITE_OK &&
        sqlite3_bind_double(insert, 2, data->value) == SQLITE_OK &&
        sqlite3_bind_int64(insert, 3, data->ts) == SQLITE_OK &&
        sqlite3_step(insert) == SQLITE_DONE;
}

void *storage_manager_thread(void *arg)
{
    gateway_ctx_t *ctx = arg;
    sqlite3 *db = NULL;
    sqlite3_stmt *insert = NULL;
    if (open_database(ctx, &db, &insert) < 0) {
        gateway_fail(ctx, "SQLite initialization failed after all attempts");
        return NULL;
    }
    log_event("storage_manager: started");
    sensor_data_t data;
    int failed = 0;
    while (sbuffer_pop_for(ctx->buffer, CONSUMER_STORAGE_MANAGER, &data) == 0) {
        int stored = 0;
        for (int attempt = 1; attempt <= DB_ATTEMPTS; ++attempt) {
            if (insert_reading(insert, &data)) {
                stored = 1;
                break;
            }
            log_event("SQLite write failed (attempt %d/%d): %s",
                      attempt, DB_ATTEMPTS, sqlite3_errmsg(db));
            close_database(&db, &insert);
            if (attempt == DB_ATTEMPTS) break;
            retry_delay();
            if (open_database(ctx, &db, &insert) < 0) break;
        }
        if (!stored) { failed = 1; break; }
        /* The INSERT committed. Only now may this consumer acknowledge it. */
        sbuffer_ack_for(ctx->buffer, CONSUMER_STORAGE_MANAGER);
        if (sqlite3_reset(insert) != SQLITE_OK) { failed = 1; break; }
    }
    close_database(&db, &insert);
    if (failed) gateway_fail(ctx, "SQLite storage failed; pending readings were not acknowledged");
    log_event("storage_manager: exiting");
    return NULL;
}
