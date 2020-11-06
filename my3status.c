/* -*- indent-tabs-mode: t; -*- */
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <errno.h>
#include <error.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "pulseaudio.h"
#include "maildir.h"
#include "meds.h"

#ifdef UPOWER
#include "upower.h"
#endif

#ifdef LIBVIRT
#include <libvirt/libvirt.h>
#endif

#define UNICODE_LINUX_BIRD	"\xf0\x9f\x90\xa7"
#define UNICODE_FLOPPY		"\xf0\x9f\x92\xbe"
#define UNICODE_PACKAGE		"\xf0\x9f\x93\xa6"
#define UNICODE_BATTERY		"\xf0\x9f\x94\x8b"

void i3bar_item(const char *name, const char *format, ...)
{
	va_list args;
	va_start(args, format);

	printf("{'name':'%s','full_text':'", name);
	vprintf(format, args);
	printf("'},");

	va_end(args);
}

static void item_datetime()
{
	struct tm *tm;
	char timebuf[32];

	time_t t = time(NULL);

	if ((tm = localtime(&t)) == NULL) {
		error(1, errno, "localtime");
	}

	char clock[5] = { 0xf0, 0x9f, 0x95, 0, 0 };

	clock[3] = tm->tm_hour > 0
		? 0x90 + (tm->tm_hour - 1) % 12
		: 0x9b;

	if (strftime(timebuf, 32, "%a %-d %b %R", tm) == 0) {
		error(1, errno, "strftime");
	}

	i3bar_item("datetime", "%s %s", clock, timebuf);
}

static void item_sysinfo() {
	struct sysinfo	si;
	float		load_5min;
	long		up_hours;
	long		up_days;

	if (sysinfo(&si) != 0) {
		error(1, errno, "sysinfo");
	}

	load_5min = si.loads[0] / (float) (1 << SI_LOAD_SHIFT);

	up_hours  = si.uptime / 3600;
	up_days	  = up_hours / 24;
	up_hours -= up_days  * 24;

	i3bar_item(
		"sysinfo",
		UNICODE_LINUX_BIRD " %.2f %ldd %ldh",
		load_5min, up_days, up_hours
	);
}

static void item_fs_usage() {
	struct statfs s;

	if (statfs("/", &s) != 0) {
		error(1, errno, "statfs");
	}

	unsigned long total = s.f_blocks;
	unsigned long used  = total - s.f_bavail;
	float used_percent  = (100.0f / total) * used;

	i3bar_item("fs_usage", UNICODE_FLOPPY " %.0f%%", used_percent);
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

	i3bar_item("volume_pulse", "%s %d%%", icon, state->volume);
}

#ifdef UPOWER
static void item_upower(struct my3status_upower_state *state)
{
	const char *indicator = "";
	char timebuf[32] = { 0 };
	int percent = -1;
	gint64 time = -1;

	pthread_mutex_lock(&state->mutex);

	percent = (int) state->percent;

	if (state->time_to_empty > 0) {
		// Discharging
		time = state->time_to_empty;
	} else if (state->time_to_full > 0) {
		// Charging
		time = state->time_to_full;
		indicator = "⚡";
	}

	pthread_mutex_unlock(&state->mutex);

	if (time != -1) {
		gint64 minutes = time % 60;
		gint64 hours = time / 60 / 60;
		sprintf(timebuf, " %ld:%02ld", hours, minutes);
	}

	i3bar_item(
		"upower",
		UNICODE_BATTERY "%s %d%%%s", indicator, percent, timebuf
	);
}
#endif

#ifdef LIBVIRT
static void item_libvirt_domains(virConnectPtr *virtConn, int retry) {
	if (*virtConn == NULL) {
		*virtConn = virConnectOpenReadOnly("qemu:///system");

		if (*virtConn != NULL) {
			fprintf(stderr, "Connected to qemu:///system\n");
		} else {
			fprintf(stderr, "Failed to connect to qemu:///system\n");
			return;
		}
	}

	int active_domains = virConnectNumOfDomains(*virtConn);

	if (active_domains == -1) {
		fprintf(stderr, "Lost connection to qemu:///system\n");

		virConnectClose(*virtConn);
		*virtConn = NULL;

		if (retry == 0) {
			item_libvirt_domains(virtConn, 1);
		}

		return;
	}

	i3bar_item("libvirt_domains", UNICODE_PACKAGE " %d", active_domains);
}
#endif

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
	if (setenv("TZ", ":/etc/localtime", 0) != 0) {
		error(1, errno, "setenv");
	}

	struct my3status_pulse_state pulse_state = { 0 };

	my3status_pulse_init(&pulse_state);
	my3status_maildir_init();

#ifdef UPOWER
	struct my3status_upower_state upower_state = { 0 };
        my3status_upower_init(&upower_state);
#endif

#ifdef LIBVIRT
	virConnectPtr virtConn = NULL;
#endif

	puts("{\"version\": 1}\n"
	     "[[]");

	while (1) {
		fputs(",[", stdout);

		my3status_meds_item("/home/tobias/Nextcloud/meds.sqlite", "ritalin");

#ifdef LIBVIRT
		item_libvirt_domains(&virtConn, 0);
#endif

		my3status_maildir_item();

		item_pulse(&pulse_state);
		item_sysinfo();
		item_fs_usage();

#ifdef UPOWER
                item_upower(&upower_state);
#endif

                item_datetime();

		puts("]");

		if (fflush(stdout) == EOF) {
			error(1, errno, "fflush");
		}

		sleep_until_next_minute();
	}
}
