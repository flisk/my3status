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
#include <pthread.h>

#include <libvirt/libvirt.h>

#include "pulseaudio.h"
#include "upower.h"

#define I3BAR_ITEM(name, full_text) \
	printf("{\"name\":\"" name "\",\"full_text\":\""); \
	full_text; \
	printf("\"},")

#define TIMEBUF_SIZE 32
#define UNICODE_LINUX_BIRD "\xf0\x9f\x90\xa7"
#define UNICODE_FLOPPY "\xf0\x9f\x92\xbe"
#define UNICODE_PACKAGE "\xf0\x9f\x93\xa6"

/*
 * Date and time with a time-sensitive clock icon
 */
static void item_datetime() {
	char timebuf[TIMEBUF_SIZE];
	time_t t = time(NULL);
	struct tm *tm;

	if ((tm = localtime(&t)) == NULL)
		error(1, errno, "localtime");

	char fourth_byte =
		tm->tm_hour == 0
		? 0x9b
		: 0x90 + (tm->tm_hour - 1) % 12;

	char clock[5] = { 0xf0, 0x9f, 0x95, fourth_byte, 0 };

	if (strftime(timebuf, TIMEBUF_SIZE, "%a %-d %b %R", tm) == 0) {
		error(1, errno, "strftime");
	}

	I3BAR_ITEM("datetime", printf("%s %s", clock, timebuf));
}

static void item_upower_format_seconds(gint64 t, char *buf) {
	gint64 minutes = t % 60;
	gint64 hours = t / 60 / 60;

	sprintf(buf, " %ld:%02ld", hours, minutes);
}

/*
 * Battery charge level and remaining time
 */
static void item_upower(struct my3status_upower_state *state) {
	char time_buf[32] = { 0 };
	const char *indicator = "";

	pthread_mutex_lock(&state->mutex);

	gint64 time_to_empty = state->time_to_empty;
	gint64 time_to_full = state->time_to_full;
	gdouble percent = state->percent;

	pthread_mutex_unlock(&state->mutex);

	if (time_to_empty > 0) {
		// Discharging
		item_upower_format_seconds(time_to_empty, time_buf);
	} else if (time_to_full > 0) {
		// Charging
		item_upower_format_seconds(time_to_full, time_buf);
		indicator = "⚡";
	}

	I3BAR_ITEM("upower", printf("🔋%s %d%%%s",
				    indicator, (int) percent, time_buf));
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

static void item_pulse(struct my3status_pulse_state *state) {
	pthread_mutex_lock(&state->mutex);

	unsigned int muted = state->muted;
	unsigned int volume = state->volume;

	pthread_mutex_unlock(&state->mutex);

	/*
	 * Integer-dividing the volume by 34 gives an offset we can add to
	 * 0x88, producing the appropriate speaker volume emojis:
	 *
	 *   0% -  33% → 0 (speaker low volume)
	 *  34% -  66% → 1 (speaker medium volume)
	 *  67% - 100% → 2 (speaker high volume)
	 */
	char fourth_byte =
		muted
		? 0x87
		: 0x88 + MIN(volume / 34, 2);

	char icon[5] = { 0xf0, 0x9f, 0x94, fourth_byte, 0 };

	I3BAR_ITEM("volume_pulse", printf("%s %d%%", icon, state->volume));
}

static void item_libvirt_domains(virConnectPtr conn) {
	int active_domains = virConnectNumOfDomains(conn);

	I3BAR_ITEM("libvirt_domains",
		   printf("%s %d", UNICODE_PACKAGE, active_domains));
}

static void sleep_until_next_minute() {
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);

	/* The extra second means our displayed time will always drag
	   by one second, but it (hopefully) won't miss minute changes
	   during leap seconds. */
	int remaining_seconds = 61 - tm->tm_sec;

	sleep(remaining_seconds);
}

static void on_signal(__attribute__((unused)) int signum) {
}

int main() {
	/* No-op signal handler because SIGUSR1 won't interrupt
	   sleep() with signal(SIGUSR1, SIG_IGN) */
	signal(SIGUSR1, on_signal);

	/* Stop glibc from running a superfluous stat() on each
	   strftime() */
	if (setenv("TZ", ":/etc/localtime", 0) != 0)
		error(1, errno, "setenv");

	struct my3status_pulse_state pulse_state = { 0 };
	struct my3status_upower_state upower_state = { 0 };

	my3status_pulse_init(&pulse_state);
	my3status_upower_init(&upower_state);

	virConnectPtr libvirtConn = virConnectOpenReadOnly("qemu:///system");
	if (libvirtConn == NULL) {
		error(1, 0, "virConnectOpenReadOnly");
	}

	puts("{\"version\": 1}\n"
	     "[[]");

	while (1) {
		fputs(",[", stdout);
		item_libvirt_domains(libvirtConn);
		item_pulse(&pulse_state);
		item_fs_usage();
		item_sysinfo();
		item_upower(&upower_state);
		item_datetime();
		puts("]");

		if (fflush(stdout) == EOF) {
			error(1, errno, "fflush");
		}

		sleep_until_next_minute();
	}
}
