/* vim: set noet ts=8 sw=8: */
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <gio/gio.h>

#define I3BAR_ITEM(name, full_text) \
	printf("{\"name\":\"" name "\",\"full_text\":\""); \
	full_text; \
	printf("\"},")

#define TIMEBUF_SIZE 32
#define UNICODE_LINUX_BIRD "\xf0\x9f\x90\xa7"
#define UNICODE_FLOPPY "\xf0\x9f\x92\xbe"

static GVariant *get_upower_property(
		GDBusConnection *conn,
		const char *prop_name) {
	GError *err = NULL;
	GVariant *res = g_dbus_connection_call_sync(
			conn,
			"org.freedesktop.UPower",
			"/org/freedesktop/UPower/devices/DisplayDevice",
			"org.freedesktop.DBus.Properties",
			"Get",
			g_variant_new("(ss)",
				"org.freedesktop.UPower.Device",
				prop_name),
			NULL,
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			NULL,
			&err);

	if (err) {
		fprintf(stderr, "g_dbus_connection_call_sync: %i: %s",
				err->code, err->message);
		exit(1);
	}

	GVariant *v;
	g_variant_get(res, "(v)", &v);
	g_variant_unref(res);

	return v;
}

static void format_seconds(gint64 t, char *buf) {
	gint64 minutes = t % 60;
	gint64 hours = t / 60 / 60;

	sprintf(buf, "%ld:%02ld", hours, minutes);
}

/*
 * Date and time with a time-sensitive clock icon
 */
static void print_datetime(struct tm *tm, char *timebuf) {
	char clock[5] = "\xf0\x9f\x95\x0\x0";
	clock[3] = tm->tm_hour == 0 ? 0x9b : 0x90 + (tm->tm_hour - 1) % 12;

	if (strftime(timebuf, TIMEBUF_SIZE, "%a %-d %b %R", tm) == 0)
		error(1, errno, "strftime");

	printf("%s %s", clock, timebuf);
}

/*
 * Battery charge level and remaining time
 */
static void print_battery(GDBusConnection *conn) {
	GVariant *p = get_upower_property(conn, "Percentage");
	GVariant *te = get_upower_property(conn, "TimeToEmpty");
	GVariant *tf = get_upower_property(conn, "TimeToFull");

	gdouble percent = g_variant_get_double(p);
	gint64 time_to_empty = g_variant_get_int64(te);
	gint64 time_to_full = g_variant_get_int64(tf);

	g_variant_unref(p);
	g_variant_unref(te);
	g_variant_unref(tf);

	if (time_to_empty > 0) { // Discharging
		char buf[32];
		format_seconds(time_to_empty, buf);

		I3BAR_ITEM("battery", printf("🔋 %d%% (%s)",
					(int) percent, buf));
	} else if (time_to_full > 0) { // Charging
		char buf[32];
		format_seconds(time_to_full, buf);

		I3BAR_ITEM("battery", printf("🔋⚡ %d%% (%s)",
					(int) percent, buf));
	}
}

/*
 * System load (5 minute average) and concise uptime
 */
static void print_sysinfo(struct sysinfo *si) {
	float load_5min = si->loads[0] / (float) (1 << SI_LOAD_SHIFT);
	long up_hours   = si->uptime / 3600;
	long up_days    = up_hours / 24;

	up_hours -= up_days * 24;

	printf(UNICODE_LINUX_BIRD " %.2f %ldd %ldh",
			load_5min, up_days, up_hours);
}

/*
 * Percentage of used space on the filesystem
 */
static void print_fs_usage(struct statfs *s) {
	unsigned long total = s->f_blocks;
	unsigned long used  = total - s->f_bfree;
	float used_percent  = (100.0f / total) * used;

	printf(UNICODE_FLOPPY " %.1f%%", used_percent);
}

static GDBusConnection *dbus_system_connect() {
	GDBusConnection *conn;
	GError *err = NULL;

	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (err) {
		fprintf(stderr, "g_bus_get_sync: %i: %s\n",
				err->code, err->message);
		exit(1);
	}

	return conn;
}

int main() {
	struct tm	*tm;
	time_t		 tt;
	char		 timebuf[TIMEBUF_SIZE];
	struct sysinfo	 si;
	struct statfs	 sfs;
	GDBusConnection	*conn;

	// This stops glibc from doing a superfluous stat() for every
	// strftime(). It's an asinine micro-optimization, but, for fun and no
	// profit, I'd like this program as efficient as I can make it.
	if (setenv("TZ", ":/etc/localtime", 0) == -1)
		error(1, errno, "setenv");

	conn = dbus_system_connect();

	puts("{\"version\": 1}\n[\n[]");
	while (1) {
		tt = time(NULL);
		if ((tm = localtime(&tt)) == NULL)
			error(1, errno, "localtime");

		if (sysinfo(&si) != 0)
			error(1, errno, "sysinfo");

		if (statfs("/", &sfs) != 0)
			error(1, errno, "statfs");

		printf(",[");
		I3BAR_ITEM("fsusage", print_fs_usage(&sfs));
		I3BAR_ITEM("sysinfo", print_sysinfo(&si));
		print_battery(conn);
		I3BAR_ITEM("datetime", print_datetime(tm, timebuf));
		printf("]\n");

		fflush(stdout);
		sleep(10);
	}
}
