/* -*- indent-tabs-mode: t; -*- */
#include <sqlite3.h>
#include <stdio.h>
#include <time.h>

#include "my3status.h"

#define PILL_EMOJI "\xf0\x9f\x92\x8a"

const char *INIT_QUERY =
	"CREATE TABLE IF NOT EXISTS \"pills_taken\" ("
	"  \"when\" INTEGER PRIMARY KEY,"
	"  \"which\" TEXT NOT NULL"
	");";

const char *LATEST_RECORD_QUERY =
	"SELECT \"when\" FROM \"pills_taken\" WHERE \"which\" = ? "
	"ORDER BY \"when\" DESC "
	"LIMIT 1";

void my3status_meds_item(const char *db_file, const char *which) {
	sqlite3 *db;
	int r;

	r = sqlite3_open(db_file, &db);
	if (r != SQLITE_OK) {
		fprintf(stderr, "can't open database: %s\n", sqlite3_errmsg(db));
		goto fail;
	}

	char *init_query_err;
	r = sqlite3_exec(db, INIT_QUERY, NULL, NULL, &init_query_err);
	if (r != SQLITE_OK) {
		fprintf(stderr, "can't create table: %s\n", init_query_err);
		goto fail;
	}

	sqlite3_stmt *stmt;
	r = sqlite3_prepare(db, LATEST_RECORD_QUERY, -1, &stmt, NULL);
	if (r != SQLITE_OK) {
		fprintf(stderr, "can't prepare statement: %s\n", sqlite3_errmsg(db));
		goto fail;
	}

	r = sqlite3_bind_text(stmt, 1 /* parameter index */, which, -1, SQLITE_STATIC);
	if (r != SQLITE_OK) {
		fprintf(stderr, "can't bind parameter: %s\n", sqlite3_errmsg(db));
		goto fail;
	}

	r = sqlite3_step(stmt);
	if (r != SQLITE_ROW) {
		fprintf(stderr, "statement execution failed: %s\n", sqlite3_errmsg(db));
		goto fail;
	}


	time_t now = time(NULL);
	time_t when = (time_t) sqlite3_column_int64(stmt, 0);

	r = sqlite3_finalize(stmt);
	if (r != SQLITE_OK) {
		fprintf(stderr, "couldn't finalize statement: %s\n", sqlite3_errmsg(db));
		goto fail;
	}

        unsigned long
		seconds = now - when,
		minutes = (seconds % 3600) / 60,
		hours   = (seconds % 86400) / 3600,
		days    = seconds / 86400;

	if (days > 0) {
		i3bar_item("meds", PILL_EMOJI " %ldd", days);
	} else if (hours > 0 || minutes > 0) {
		i3bar_item("meds", PILL_EMOJI " %01ld:%02ld", hours, minutes);
	} else {
		i3bar_item("meds", PILL_EMOJI " :3");
	}

	sqlite3_close(db);
	return;

fail:
	sqlite3_close(db);
	i3bar_item("meds", PILL_EMOJI " !");
}
