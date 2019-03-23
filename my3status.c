/* vim: set noet ts=8 sw=8: */
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <gio/gio.h>
#include <alsa/asoundlib.h>

/*
 * When compiling with ALSA's header directories, `#include <error.h>` will
 * resolve to `/usr/include/alsa/error.h` instead of `/usr/include/error.h` --
 * the latter can only be included by its absolute path.
 */
#include "/usr/include/error.h"

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
static void item_datetime() {
	struct tm *tm;
	time_t t;
	char timebuf[TIMEBUF_SIZE];

	t = time(NULL);
	if ((tm = localtime(&t)) == NULL)
		error(1, errno, "localtime");

	char clock[5] = "\xf0\x9f\x95\x0\x0";
	clock[3] = tm->tm_hour == 0 ? 0x9b : 0x90 + (tm->tm_hour - 1) % 12;

	if (strftime(timebuf, TIMEBUF_SIZE, "%a %-d %b %R", tm) == 0)
		error(1, errno, "strftime");

	I3BAR_ITEM("datetime", printf("%s %s", clock, timebuf));
}

/*
 * Battery charge level and remaining time
 */
static void item_battery(GDBusConnection *conn) {
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
static void item_sysinfo() {
	struct sysinfo s;

	if (sysinfo(&s) != 0)
		error(1, errno, "sysinfo");

	float load_5min = s.loads[0] / (float) (1 << SI_LOAD_SHIFT);

	long up_hours = s.uptime / 3600;
	long up_days  = up_hours  / 24;
	up_hours     -= up_days * 24;

	I3BAR_ITEM("sysinfo", printf(UNICODE_LINUX_BIRD " %.2f %ldd %ldh",
				load_5min, up_days, up_hours));
}

/*
 * Percentage of used space on the filesystem
 */
static void item_fs_usage() {
	struct statfs s;

	if (statfs("/", &s) != 0)
		error(1, errno, "statfs");

	unsigned long total = s.f_blocks;
	unsigned long used  = total - s.f_bfree;
	float used_percent  = (100.0f / total) * used;

	I3BAR_ITEM("fs_usage", printf(UNICODE_FLOPPY " %.1f%%", used_percent));
}

static void item_alsa_volume() {
	snd_mixer_selem_id_t *sid;
	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, "Master");

	snd_mixer_t *mixer;
	if ((snd_mixer_open(&mixer, 0)) < 0)
		error(1, 0, "snd_mixer_open");

	if ((snd_mixer_attach(mixer, "default")) < 0)
		error(1, 0, "snd_mixer_attach");

	if ((snd_mixer_selem_register(mixer, NULL, NULL)) < 0)
		error(1, 0, "snd_mixer_selem_register");

	int ret = snd_mixer_load(mixer);
	if (ret < 0)
		error(1, 0, "snd_mixer_load");

	snd_mixer_elem_t *elem = snd_mixer_find_selem(mixer, sid);
	if (!elem)
		error(1, 0, "snd_mixer_find_selem");

	long minv, maxv;
	snd_mixer_selem_get_playback_volume_range(elem, &minv, &maxv);

	long volume;
	if (snd_mixer_selem_get_playback_volume(elem, 0, &volume) < 0)
		error(1, 0, "snd_mixer_selem_get_playback_volume");

	int switch_value;
	if (snd_mixer_selem_get_playback_switch(elem, 0, &switch_value) < 0)
		error(1, 0, "snd_mixer_selem_get_playback_switch");

	snd_mixer_close(mixer);

	int volume_percent = (int) ((100.0 / maxv) * volume);

	char speaker[5] = {0xf0, 0x9f, 0x94, 0, 0};
	if (switch_value) {
		// Integer divison of the volume by 34 produces an offset for
		// adding to the low volume speaker emoji:
		//
		//   0% -  33% → 0
		//  34% -  66% → 1 (speaker medium volume)
		//  67% - 100% → 2 (speaker high volume)
		//
		speaker[3] = 0x88 + volume_percent / 34;
	} else {
		// muted speaker
		speaker[3] = 0x87;
	}

	I3BAR_ITEM("volume_alsa", printf("%s %d%%", speaker, volume_percent));
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

static void on_signal(int sig) {
	sig = sig;
}

int main() {
	signal(SIGUSR1, on_signal);

	// This stops glibc from doing a superfluous stat() for every
	// strftime(). It's an asinine micro-optimization, but, for fun and no
	// profit, I'd like this program as efficient as I can make it.
	if (setenv("TZ", ":/etc/localtime", 0) != 0)
		error(1, errno, "setenv");

	GDBusConnection	*conn = dbus_system_connect();

	puts("{\"version\": 1}\n[\n[]");
	while (1) {
		printf(",[");

		item_alsa_volume();
		item_fs_usage();
		item_sysinfo();
		item_battery(conn);
		item_datetime();

		printf("]\n");

		fflush(stdout);
		sleep(10);
	}
}
