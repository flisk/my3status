/* vim: set noet ts=8 sw=8: */
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <errno.h>
#include <error.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "pulseaudio.h"

#include <gio/gio.h>
#include <libvirt/libvirt.h>

#define I3BAR_ITEM(name, full_text) \
	printf("{\"name\":\"" name "\",\"full_text\":\""); \
	full_text; \
	printf("\"},")

#define TIMEBUF_SIZE 32
#define UNICODE_LINUX_BIRD "\xf0\x9f\x90\xa7"
#define UNICODE_FLOPPY "\xf0\x9f\x92\xbe"
#define UNICODE_PACKAGE "\xf0\x9f\x93\xa6"

static GVariant *get_upower_property(GDBusConnection *conn,
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

static void item_battery_format_seconds(gint64 t, char *buf) {
	gint64 minutes = t % 60;
	gint64 hours = t / 60 / 60;

	sprintf(buf, "%ld:%02ld", hours, minutes);
}

/*
 * Date and time with a time-sensitive clock icon
 */
static void item_datetime() {
	char timebuf[TIMEBUF_SIZE];
	time_t t = time(NULL);
	struct tm *tm;

	if ((tm = localtime(&t)) == NULL)
		error(1, errno, "localtime");

	char clock[5] =
		{
		 0xf0,
		 0x9f,
		 0x95,
		 tm->tm_hour == 0 ? 0x9b : 0x90 + (tm->tm_hour - 1) % 12,
		 0
		};

	if (strftime(timebuf, TIMEBUF_SIZE, "%a %-d %b %R", tm) == 0)
		error(1, errno, "strftime");

	I3BAR_ITEM("datetime", printf("%s %s", clock, timebuf));
}

/*
 * Battery charge level and remaining time
 */
static void item_battery(GDBusConnection *conn) {
	GVariant *device_type_v = get_upower_property(conn, "Type");
	guint32 device_type = g_variant_get_uint32(device_type_v);
	g_variant_unref(device_type_v);

	if (device_type != 2)
		return;

	GVariant *percent_v = get_upower_property(conn, "Percentage");
	GVariant *time_to_empty_v = get_upower_property(conn, "TimeToEmpty");

	gdouble percent = g_variant_get_double(percent_v);
	gint64 time_to_empty = g_variant_get_int64(time_to_empty_v);

	g_variant_unref(percent_v);
	g_variant_unref(time_to_empty_v);

	// On AC, fully charged. Don't display the item.
	if (percent == 100.0 && time_to_empty == 0)
		return;

	char buf[32];
	const char *status_char = "";
	const char *space = "";

	if (time_to_empty > 0) {
		// Discharging
		item_battery_format_seconds(time_to_empty, buf);
		space = " ";
	} else {
		GVariant *time_to_full_v = get_upower_property(conn, "TimeToFull");
		gint64 time_to_full = g_variant_get_int64(time_to_full_v);
		g_variant_unref(time_to_full_v);

		if (time_to_full > 0) {
			// Charging
			item_battery_format_seconds(time_to_full, buf);
			status_char = "⚡";
			space = " ";
		} else {
			buf[0] = 0;
		}
	}

	I3BAR_ITEM("battery", printf("🔋%s %d%%%s%s",
				status_char, (int) percent, space, buf));
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

	I3BAR_ITEM("fs_usage", printf(UNICODE_FLOPPY " %.0f%%", used_percent));
}

static void item_pulse_volume(struct my3status_pa_state *pa_state) {
	if (my3status_pa_update(pa_state) == -1)
		return;

	/*
	 * Integer-dividing the volume by 34 gives an offset we can add to
	 * 0x88, producing the appropriate speaker volume emojis:
	 *
	 *   0% -  33% → 0 (speaker low volume)
	 *  34% -  66% → 1 (speaker medium volume)
	 *  67% - 100% → 2 (speaker high volume)
	 */
	char icon[5] = {
		0xf0, 0x9f, 0x94,
		pa_state->muted ? 0x87 : 0x88 + MIN(pa_state->volume / 34, 2),
		0
	};

	I3BAR_ITEM("volume_pulse", printf("%s %d%%", icon, pa_state->volume));
}

static void item_libvirt_domains(virConnectPtr conn) {
	int active_domains = virConnectNumOfDomains(conn);

	I3BAR_ITEM("libvirt_domains",
		   printf("%s %d", UNICODE_PACKAGE, active_domains));
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

	/*
	 * This stops glibc from doing a superfluous stat() for every
	 * strftime(). It's an asinine micro-optimization, but, for
	 * fun and no profit, I'd like this program as efficient as I
	 * can make it.
	 */
	if (setenv("TZ", ":/etc/localtime", 0) != 0)
		error(1, errno, "setenv");

	GDBusConnection *conn = dbus_system_connect();

	struct my3status_pa_state pa_state = {0};
	my3status_pa_init(&pa_state);

	virConnectPtr libvirtConn = virConnectOpenReadOnly("qemu:///system");
	if (libvirtConn == NULL) {
		error(1, 0, "virConnectOpenReadOnly");
	}

	puts("{\"version\": 1}\n[\n[]");
	while (1) {
		fputs(",[", stdout);
		item_libvirt_domains(libvirtConn);
		item_pulse_volume(&pa_state);
		item_fs_usage();
		item_sysinfo();
		item_battery(conn);
		item_datetime();
		puts("]");

		if (fflush(stdout) == EOF) {
			error(1, errno, "fflush");
		}

		sleep(10);
	}
}
