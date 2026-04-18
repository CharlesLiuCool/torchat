/*
 * storage.c — SQLite-backed message persistence for TorChat
 *
 * Build:
 *   Include this file in the Makefile SRCS list and add -lsqlite3 to LDFLAGS.
 *
 *   Makefile diff:
 *       SRCS += storage.c
 *       LDFLAGS += -lsqlite3
 */

#include "storage.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Internal state                                                       */
/* ------------------------------------------------------------------ */

#define DEFAULT_DB_PATH "torchat_history.db"

static sqlite3* g_db = NULL;

/* Prepared statements — compiled once, reused on every call */
static sqlite3_stmt* g_stmt_insert  = NULL;
static sqlite3_stmt* g_stmt_history = NULL;
static sqlite3_stmt* g_stmt_count   = NULL;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static int db_exec(const char* sql)
{
    char* errmsg = NULL;
    int rc = sqlite3_exec(g_db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[storage] SQL error: %s\n", errmsg ? errmsg : "(unknown)");
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

static int prepare_statements(void)
{
    const char* sql_insert =
        "INSERT INTO messages (timestamp, sender, peer_addr, body) "
        "VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(g_db, sql_insert, -1, &g_stmt_insert, NULL) != SQLITE_OK) {
        fprintf(stderr, "[storage] Failed to prepare insert: %s\n",
                sqlite3_errmsg(g_db));
        return -1;
    }

    const char* sql_count = "SELECT COUNT(*) FROM messages;";
    if (sqlite3_prepare_v2(g_db, sql_count, -1, &g_stmt_count, NULL) != SQLITE_OK) {
        fprintf(stderr, "[storage] Failed to prepare count: %s\n",
                sqlite3_errmsg(g_db));
        return -1;
    }

    return 0;
}

/*
 * Populates a chat_message_t from the current row of a prepared statement.
 * Column order must be: id, timestamp, sender, peer_addr, body
 */
static void row_to_msg(sqlite3_stmt* stmt, chat_message_t* out)
{
    out->id        = sqlite3_column_int64(stmt, 0);
    out->timestamp = (time_t)sqlite3_column_int64(stmt, 1);

    const char* sender = (const char*)sqlite3_column_text(stmt, 2);
    const char* addr   = (const char*)sqlite3_column_text(stmt, 3);
    const char* body   = (const char*)sqlite3_column_text(stmt, 4);

    strncpy(out->sender,    sender ? sender : "",  sizeof(out->sender)    - 1);
    strncpy(out->peer_addr, addr   ? addr   : "",  sizeof(out->peer_addr) - 1);
    strncpy(out->body,      body   ? body   : "",  sizeof(out->body)      - 1);

    out->sender   [sizeof(out->sender)    - 1] = '\0';
    out->peer_addr[sizeof(out->peer_addr) - 1] = '\0';
    out->body     [sizeof(out->body)      - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

int storage_open(const char* db_path)
{
    if (g_db) return 0; /* already open */

    const char* path = db_path ? db_path : DEFAULT_DB_PATH;

    if (sqlite3_open(path, &g_db) != SQLITE_OK) {
        chmod(path, 0600); /* ensure new DB is user-only readable/writable */
        fprintf(stderr, "[storage] Cannot open database '%s': %s\n",
                path, sqlite3_errmsg(g_db));
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    /* Performance pragmas — safe for chat workloads */
    db_exec("PRAGMA journal_mode = WAL;");
    db_exec("PRAGMA synchronous  = NORMAL;");

    /* Create table if it doesn't exist */
    const char* ddl =
        "CREATE TABLE IF NOT EXISTS messages ("
        "    id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    timestamp INTEGER NOT NULL,"
        "    sender    TEXT    NOT NULL,"
        "    peer_addr TEXT,"
        "    body      TEXT    NOT NULL"
        ");";

    if (db_exec(ddl) != 0) {
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    /* Index for fast peer-filtered queries */
    db_exec("CREATE INDEX IF NOT EXISTS idx_peer ON messages(peer_addr);");
    /* Index for time-range deletes */
    db_exec("CREATE INDEX IF NOT EXISTS idx_ts   ON messages(timestamp);");

    if (prepare_statements() != 0) {
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    printf("[storage] Opened database '%s'\n", path);
    return 0;
}

void storage_close(void)
{
    if (!g_db) return;

    if (g_stmt_insert)  { sqlite3_finalize(g_stmt_insert);  g_stmt_insert  = NULL; }
    if (g_stmt_history) { sqlite3_finalize(g_stmt_history); g_stmt_history = NULL; }
    if (g_stmt_count)   { sqlite3_finalize(g_stmt_count);   g_stmt_count   = NULL; }

    sqlite3_close(g_db);
    g_db = NULL;
    printf("[storage] Database closed.\n");
}

/* ------------------------------------------------------------------ */
/* Write                                                                */
/* ------------------------------------------------------------------ */

long long storage_save_message(const char* sender,
                               const char* peer_addr,
                               const char* body)
{
    if (!g_db || !g_stmt_insert) return -1;

    if (body && strlen(body) > 512) {
    fprintf(stderr, "[storage] Rejecting oversized message (%zu bytes)\n", strlen(body));
    return -1;
}

    time_t now = time(NULL);

    sqlite3_reset(g_stmt_insert);
    sqlite3_bind_int64(g_stmt_insert, 1, (sqlite3_int64)now);
    sqlite3_bind_text (g_stmt_insert, 2, sender    ? sender    : "unknown", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (g_stmt_insert, 3, (peer_addr && peer_addr[0]) ? peer_addr : NULL, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (g_stmt_insert, 4, body      ? body      : "",        -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(g_stmt_insert);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[storage] Insert failed: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    return (long long)sqlite3_last_insert_rowid(g_db);
}

/* ------------------------------------------------------------------ */
/* Read — shared iteration helper                                       */
/* ------------------------------------------------------------------ */

/*
 * Runs `stmt` (already bound) and calls `cb` for each row.
 * Returns row count delivered, or -1 on error.
 */
static int run_select(sqlite3_stmt* stmt, storage_row_cb cb, void* userdata)
{
    int count = 0;
    int rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        chat_message_t msg = {0};
        row_to_msg(stmt, &msg);
        if (cb(&msg, userdata) != 0) break; /* caller asked to stop */
        count++;
    }

    sqlite3_reset(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        fprintf(stderr, "[storage] Query error: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    return count;
}

/* ------------------------------------------------------------------ */
/* Read — public API                                                    */
/* ------------------------------------------------------------------ */

int storage_load_history(int limit, storage_row_cb cb, void* userdata)
{
    if (!g_db) return -1;

    /*
     * We want the N most-recent messages but delivered oldest-first, so
     * we use a subquery:
     *   SELECT ... FROM (SELECT ... ORDER BY id DESC LIMIT N) ORDER BY id ASC
     */
    char sql[256];
    if (limit > 0) {
        snprintf(sql, sizeof(sql),
            "SELECT id, timestamp, sender, peer_addr, body "
            "FROM (SELECT id, timestamp, sender, peer_addr, body "
            "      FROM messages ORDER BY id DESC LIMIT %d) "
            "ORDER BY id ASC;",
            limit);
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT id, timestamp, sender, peer_addr, body "
            "FROM messages ORDER BY id ASC;");
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[storage] load_history prepare: %s\n",
                sqlite3_errmsg(g_db));
        return -1;
    }

    int result = run_select(stmt, cb, userdata);
    sqlite3_finalize(stmt);
    return result;
}

int storage_search(const char* keyword, storage_row_cb cb, void* userdata)
{
    if (!g_db || !keyword) return -1;

    const char* sql =
        "SELECT id, timestamp, sender, peer_addr, body "
        "FROM messages WHERE body LIKE ? ORDER BY id ASC;";

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[storage] search prepare: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    /* Wrap keyword in % wildcards for LIKE */
    char pattern[516];
    snprintf(pattern, sizeof(pattern), "%%%s%%", keyword);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

    int result = run_select(stmt, cb, userdata);
    sqlite3_finalize(stmt);
    return result;
}

int storage_load_peer_history(const char*    peer_addr,
                              int            limit,
                              storage_row_cb cb,
                              void*          userdata)
{
    if (!g_db || !peer_addr) return -1;

    char sql[320];
    if (limit > 0) {
        snprintf(sql, sizeof(sql),
            "SELECT id, timestamp, sender, peer_addr, body "
            "FROM (SELECT id, timestamp, sender, peer_addr, body "
            "      FROM messages WHERE peer_addr = ? ORDER BY id DESC LIMIT %d) "
            "ORDER BY id ASC;",
            limit);
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT id, timestamp, sender, peer_addr, body "
            "FROM messages WHERE peer_addr = ? ORDER BY id ASC;");
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[storage] peer_history prepare: %s\n",
                sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, peer_addr, -1, SQLITE_STATIC);

    int result = run_select(stmt, cb, userdata);
    sqlite3_finalize(stmt);
    return result;
}

/* ------------------------------------------------------------------ */
/* Maintenance                                                          */
/* ------------------------------------------------------------------ */

long long storage_message_count(void)
{
    if (!g_db || !g_stmt_count) return -1;

    sqlite3_reset(g_stmt_count);
    if (sqlite3_step(g_stmt_count) != SQLITE_ROW) return -1;

    long long count = (long long)sqlite3_column_int64(g_stmt_count, 0);
    sqlite3_reset(g_stmt_count);
    return count;
}

long long storage_delete_before(time_t cutoff)
{
    if (!g_db) return -1;

    const char* sql = "DELETE FROM messages WHERE timestamp < ?;";
    sqlite3_stmt* stmt = NULL;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[storage] delete_before prepare: %s\n",
                sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[storage] delete_before exec: %s\n",
                sqlite3_errmsg(g_db));
        return -1;
    }

    return (long long)sqlite3_changes(g_db);
}
