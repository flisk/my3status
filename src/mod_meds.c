#include <dirent.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <time.h>

#include <sqlite3.h>

#include "mod_meds.h"

#define MAX_OUTPUT 32
#define STUPID_HARDCODED_DB_PATH "/home/tobias/Nextcloud/meds.sqlite"
#define STUPID_HARDCODED_PILL_TYPE "ritalin"

#define INOTIFY_BUF_SIZE (sizeof(struct inotify_event) + NAME_MAX + 1)

static char output[MAX_OUTPUT] = "💊 ";

static struct my3status_module mod = {
	.name	= "meds",
	.output	= output
};

const char *SQL_LATEST_RECORD =
	"SELECT \"when\" FROM \"pills_taken\" WHERE \"which\" = ? "
	"ORDER BY \"when\" DESC "
	"LIMIT 1";

static void *run(void *);
static time_t update_output();

static void db_connect(sqlite3 **, sqlite3_stmt **);
static int db_init_watch();

int mod_meds_init(struct my3status_state *state)
{
	mod.state = state;

	pthread_t p;
	if (pthread_mutex_init(&mod.output_mutex, NULL) != 0 ||
	    pthread_create(&p, NULL, run, NULL) != 0) {
		return -1;
	}

	my3status_add_module(state, &mod);
	return 0;
}

static void *run(__attribute__((unused)) void *arg)
{
	sqlite3 *db = NULL;
	sqlite3_stmt *latest_record_stmt = NULL;

	db_connect(&db, &latest_record_stmt);
	int ino_fd = db_init_watch();

	struct pollfd pollfds[] = {
		(struct pollfd) { .fd = ino_fd, .events = POLLIN }
	};

	ssize_t s;
	char buf[INOTIFY_BUF_SIZE];
	while (1) {
		time_t sleep_for = update_output(db, latest_record_stmt);

		int r = poll(pollfds, 1, sleep_for * 1000);

		if (r == -1) {
			error(1, errno, "poll()");
		}

		if (r == 0) {
			continue;
		}

		s = read(ino_fd, buf, INOTIFY_BUF_SIZE);
		if (s == -1) {
			error(1, errno, "%s: read: ", __func__);
		}
	}

	// should be unreachable
	return NULL;
}

static time_t update_output(sqlite3 *db, sqlite3_stmt *latest_record_stmt)
{
	int r;
	r = sqlite3_bind_text(latest_record_stmt, 1, STUPID_HARDCODED_PILL_TYPE, -1, SQLITE_STATIC);
	if (r != SQLITE_OK) {
		error(1, 0, "can't bind parameter: %d, %s", r, sqlite3_errmsg(db));
	}

	r = sqlite3_step(latest_record_stmt);
	if (r != SQLITE_ROW) {
		error(1, 0, "can't execute statement: %s", sqlite3_errmsg(db));
	}

	sqlite_int64 time_value = sqlite3_column_int64(latest_record_stmt, 0);

	r = sqlite3_reset(latest_record_stmt);
	if (r != SQLITE_OK) {
		error(1, 0, "can't reset statement: %s", sqlite3_errmsg(db));
	}

	uint64_t now = (uint64_t) time(NULL);

	uint64_t seconds	= now - time_value;
	uint64_t minutes	= (seconds % 3600) / 60;
	uint64_t hours		= (seconds % 86400) / 3600;
	uint64_t days		= seconds / 86400;

	pthread_mutex_lock(&mod.output_mutex);

	if (days > 0) {
		snprintf(output + 5, MAX_OUTPUT - 5, "%ldd", days);
	} else {
		snprintf(output + 5, MAX_OUTPUT - 5, "%01ld:%02ld", hours, minutes);
	}

	pthread_mutex_unlock(&mod.output_mutex);
	my3status_update(&mod);

	uint32_t sleep_for =
		days > 0
		? 86400 - (seconds % 86400)
		: 60 - (seconds % 60);

	return sleep_for;
}

static void db_connect(sqlite3 **db, sqlite3_stmt **latest_record_stmt)
{
	int r;

	r = sqlite3_open_v2(STUPID_HARDCODED_DB_PATH, db, SQLITE_OPEN_READONLY, NULL);
	if (r != SQLITE_OK) {
		error(1, 0, "can't open meds.sqlite: %s", sqlite3_errmsg(*db));
	}

	r = sqlite3_prepare(*db, SQL_LATEST_RECORD, -1, latest_record_stmt, NULL);
	if (r != SQLITE_OK) {
		error(1, 0, "can't prepare statement: %s", sqlite3_errmsg(*db));
	}
}

static int db_init_watch()
{
	int fd = inotify_init();
	if (fd == -1) {
		error(1, errno, "%s: inotify_init: ", __func__);
	}

	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	uint32_t watch_mask = IN_CLOSE_WRITE | IN_MODIFY;
	int r = inotify_add_watch(fd, STUPID_HARDCODED_DB_PATH, watch_mask);

	if (r == -1) {
		error(1, errno, "%s: inotify_add_watch: ", __func__);
	}

	return fd;
}