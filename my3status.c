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

#define UNICODE_LINUX_BIRD	"\xf0\x9f\x90\xa7"
#define UNICODE_FLOPPY		"\xf0\x9f\x92\xbe"
#define UNICODE_PACKAGE		"\xf0\x9f\x93\xa6"
#define UNICODE_BATTERY		"\xf0\x9f\x94\x8b"

#define ITEM_TEXT_BUFSIZE 64

void i3bar_item(int last, const char *name, const char *format, ...)
{
	static char item_text[ITEM_TEXT_BUFSIZE];

	const char *end = (last == 1 ? "" : ",");

	va_list args;
	va_start(args, format);
	vsnprintf(item_text, ITEM_TEXT_BUFSIZE, format, args);
	va_end(args);

	printf("{\"name\":\"%s\",\"full_text\":\"%s\"}%s", name, item_text, end);
}

static void item_datetime(int last)
{
	static char timebuf[32];
	static char clock_glyph[5] = { 0xf0, 0x9f, 0x95, 0, 0 };

	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	if (tm == NULL) {
		error(1, errno, "localtime failed");
	}

	clock_glyph[3] =
		tm->tm_hour > 0
		? 0x90 + (tm->tm_hour - 1) % 12
		: 0x9b;

	if (strftime(timebuf, 32, "%a %-d %b %R", tm) == 0) {
		error(1, errno, "strftime");
	}

	i3bar_item(last, "datetime", "%s %s", clock_glyph, timebuf);
}

static void item_sysinfo(int last)
{
	static struct sysinfo s;

	float	load_5min;
	long	up_hours;
	long	up_days;

	if (sysinfo(&s) != 0) {
		error(1, errno, "sysinfo");
	}

	load_5min = s.loads[0] / (float) (1 << SI_LOAD_SHIFT);

	up_hours  = s.uptime / 3600;
	up_days	  = up_hours / 24;
	up_hours -= up_days  * 24;

	i3bar_item(
		last,
		"sysinfo",
		UNICODE_LINUX_BIRD " %.2f %ldd %ldh",
		load_5min, up_days, up_hours
	);
}

static void item_fs_usage(int last)
{
	static struct statfs s;

	if (statfs("/", &s) != 0) {
		error(1, errno, "statfs");
	}

	unsigned long total = s.f_blocks;
	unsigned long used  = total - s.f_bavail;
	float used_percent  = (100.0f / total) * used;

	i3bar_item(last, "fs_usage", UNICODE_FLOPPY " %.0f%%", used_percent);
}

static void item_pulse(int last, struct my3status_pulse_state *state)
{
	static char volume_glyph[5] = { 0xf0, 0x9f, 0x94, 0, 0 };

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
	volume_glyph[3] =
		muted
		? 0x87
		: 0x88 + MIN(volume / 34, 2);

	i3bar_item(last, "volume_pulse", "%s %d%%", volume_glyph, state->volume);
}

#ifdef UPOWER
static void item_upower(int last, struct my3status_upower_state *state)
{
	static char timebuf[32] = { 0 };

	const char *indicator = "";
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
		last,
		"upower",
		UNICODE_BATTERY "%s %d%%%s", indicator, percent, timebuf
	);
}
#endif

static void sleep_until_next_minute()
{
	/*
	 * Micro-optimization: unix timestamp mod 60 is a lot faster for determining
	 * seconds in the current minute vs. letting localtime do its whole song and
	 * dance.
	 */
	time_t now = time(NULL);
	int seconds_in_minute = now % 60;

	/*
	 * Let's talk about leap seconds.
	 *
	 * The time_t we acquired above is a traditional Unix timestamp, meaning an
	 * integer representing the seconds that have passed since 1970-01-01 00:00
	 * UTC. This value is specifically defined to increase by exactly 86400
	 * every day.
	 *
	 * This definition leaves no room for leap seconds, so they simply do not
	 * exist on Unix systems. Instead, the system clock is slowed down to
	 * accommodate actual time, meaning during a leap second, tm_sec is going to
	 * remain stuck at 59 for two seconds.
	 *
	 * Which means: if we sleep for 60 - tm_sec seconds until the next update,
	 * we'll get the correct time MOST of the time, but if there's a leap second
	 * occurring, we'll miss the minute change.
	 *
	 * Is this worth handling? Probably not. But there's a really easy hack we
	 * can do to "fix" this: sleep for an extra second. This means our displayed
	 * time will always drag by a second, but minute changes during leap seconds
	 * won't be missed.
	 */
	sleep(61 - seconds_in_minute);
}

static void on_signal(__attribute__((unused)) int signum)
{
}

int main()
{
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
	//my3status_maildir_init();

#ifdef UPOWER
	struct my3status_upower_state upower_state = { 0 };
        my3status_upower_init(&upower_state);
#endif

	printf(
		"{\"version\":1}\n"
		"["
		"[{\"name\":\"placeholder\",\"full_text\":\"…\"}],"
	);

	sleep(3);

	struct tm *tm;
	time_t time_now;

	while (1) {
		time_now = time(NULL);

		if ((tm = localtime(&time_now)) == NULL) {
			error(1, errno, "localtime");
		}

		fputs("[", stdout);

		my3status_meds_item(0, "/home/tobias/Nextcloud/meds.sqlite", "ritalin");

		//my3status_maildir_item(0);

		item_pulse(0, &pulse_state);
		item_sysinfo(0);
		item_fs_usage(0);

#ifdef UPOWER
		item_upower(0, &upower_state);
#endif

		item_datetime(1);

		fputs("],", stdout);

		if (fflush(stdout) == EOF) {
			error(1, errno, "fflush");
		}

		sleep_until_next_minute();
	}
}