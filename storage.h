#pragma once

/*
 * storage.h — SQLite-backed message persistence for TorChat
 *
 * Usage pattern:
 *   1. Call storage_open()  once at startup (after backend_init).
 *   2. Call storage_save_message() inside the on_message callback.
 *   3. Call storage_load_history() on startup to replay past messages into the UI.
 *   4. Call storage_close() inside backend_shutdown() or when the window closes.
 *
 * The database is stored as a single file: "torchat_history.db"
 * in the current working directory.
 *
 * Schema (created automatically if it doesn't exist):
 *
 *   CREATE TABLE messages (
 *       id         INTEGER PRIMARY KEY AUTOINCREMENT,
 *       timestamp  INTEGER NOT NULL,   -- Unix epoch seconds (UTC)
 *       sender     TEXT    NOT NULL,   -- peer nickname, or "me" for local echo
 *       peer_addr  TEXT,               -- "ip:port" string, NULL for local messages
 *       body       TEXT    NOT NULL    -- raw message text
 *   );
 *
 * Dependencies:
 *   Linux/macOS:  link with -lsqlite3
 *                 sudo apt install libsqlite3-dev   (Debian/Ubuntu)
 *                 brew install sqlite3              (macOS)
 *   Windows:      link with sqlite3.lib / -lsqlite3 (MinGW)
 */

#include <stddef.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Message record                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    long long  id;          /* row id (0 when not yet stored)          */
    time_t     timestamp;   /* Unix epoch seconds                      */
    char       sender[64];  /* nickname of the sender                  */
    char       peer_addr[64]; /* "ip:port", or "" for local messages   */
    char       body[512];   /* message text                            */
} chat_message_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

/*
 * storage_open()
 *   Opens (or creates) the SQLite database at `db_path`.
 *   Pass NULL to use the default path "torchat_history.db".
 *   Returns 0 on success, -1 on failure (error printed to stderr).
 */
int  storage_open(const char* db_path);

/*
 * storage_close()
 *   Flushes and closes the database. Safe to call even if storage_open()
 *   was never called or failed.
 */
void storage_close(void);

/* ------------------------------------------------------------------ */
/* Write                                                                */
/* ------------------------------------------------------------------ */

/*
 * storage_save_message()
 *   Inserts one message into the database.
 *   - sender    : display name of the peer (or local nickname for self-messages)
 *   - peer_addr : "ip:port" string identifying the remote peer; pass NULL or ""
 *                 for messages sent by the local user (fd == -1)
 *   - body      : the message text
 *   Returns the new row id (>0) on success, -1 on failure.
 */
long long storage_save_message(const char* sender,
                               const char* peer_addr,
                               const char* body);

/* ------------------------------------------------------------------ */
/* Read                                                                 */
/* ------------------------------------------------------------------ */

/*
 * Callback invoked by storage_load_history() for each row, oldest first.
 *   msg      : pointer to a stack-allocated chat_message_t (valid only during
 *              the callback; copy if you need to keep it)
 *   userdata : the pointer you passed to storage_load_history()
 * Return 0 to continue iteration, non-zero to stop early.
 */
typedef int (*storage_row_cb)(const chat_message_t* msg, void* userdata);

/*
 * storage_load_history()
 *   Replays up to `limit` most-recent messages (oldest first) into `cb`.
 *   Pass 0 for `limit` to load all messages.
 *   Returns the number of rows delivered, or -1 on error.
 *
 *   Typical use: call once after storage_open() to populate the UI chat log.
 */
int storage_load_history(int limit, storage_row_cb cb, void* userdata);

/*
 * storage_search()
 *   Delivers rows whose `body` contains `keyword` (case-insensitive LIKE).
 *   Results are ordered oldest-first.
 *   Returns the number of rows delivered, or -1 on error.
 */
int storage_search(const char* keyword, storage_row_cb cb, void* userdata);

/*
 * storage_load_peer_history()
 *   Like storage_load_history() but filtered to messages from/to a specific
 *   peer identified by their `peer_addr` string ("ip:port").
 *   Returns the number of rows delivered, or -1 on error.
 */
int storage_load_peer_history(const char* peer_addr,
                              int         limit,
                              storage_row_cb cb,
                              void*          userdata);

/* ------------------------------------------------------------------ */
/* Maintenance                                                          */
/* ------------------------------------------------------------------ */

/*
 * storage_message_count()
 *   Returns the total number of stored messages, or -1 on error.
 */
long long storage_message_count(void);

/*
 * storage_delete_before()
 *   Deletes all messages older than `cutoff` (Unix epoch seconds).
 *   Returns the number of rows deleted, or -1 on error.
 *   Useful for pruning old history to keep the database small.
 */
long long storage_delete_before(time_t cutoff);
