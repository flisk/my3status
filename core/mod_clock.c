#include <sys/timerfd.h>
#include <time.h>
#include "my3status.h"

#define MAX_OUTPUT 32

static char output[MAX_OUTPUT] = { 0xf0, 0x9f, 0x95, 0x9b, ' ', 0 };

static void *run(void *);
static void start_timer(int);
static void update_time(struct my3status_module *, time_t);

int mod_clock_init(struct my3status_state *s)
{
	return my3status_init_internal_module(
		s, "clock", output, true, run
	);
}

static void *run(void *arg)
{
	struct my3status_module *m = arg;

	int timer = timerfd_create(CLOCK_REALTIME, 0);
	if (timer == -1) {
		error(1, errno, "mod_clock: timerfd_create");
	}

	time_t now = time(NULL);
	update_time(m, now);
	start_timer(timer);

	unsigned long expirations;
	ssize_t s;
	while (1) {
		s = read(timer, &expirations, sizeof(expirations));
		now = time(NULL);

		update_time(m, now);

		if (s != -1) {
			// normal expiration
			continue;
		}

		if (errno == ECANCELED) {
			// system time was changed
			start_timer(timer);
		} else {
			error(1, errno, "mod_clock: read");
		}
	}

	// should not be reachable
	return NULL;
}

static void start_timer(int tfd)
{
	static struct itimerspec t = {
		.it_interval = (struct timespec) { .tv_sec = 60, .tv_nsec = 0 },
		.it_value = (struct timespec) { 0 }
	};

	static struct timespec now = { 0 };

	if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
		error(1, errno, "mod_clock: clock_gettime");
	}

	time_t seconds_left = 60 - now.tv_sec % 60;
	t.it_value.tv_sec = now.tv_sec + seconds_left;
	t.it_value.tv_nsec = 1000000000L - now.tv_nsec;

	int flags = TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET;
	if (timerfd_settime(tfd, flags, &t, NULL) == -1) {
		error(1, errno, "mod_clock: timerfd_settime");
	}
}

static void update_time(struct my3status_module *m, time_t now)
{
	struct tm *tm = localtime(&now);

	if (tm == NULL) {
		error(1, errno, "localtime");
	}

	my3status_output_begin(m);

	output[3] =
		tm->tm_hour > 0
		? 0x90 + (tm->tm_hour - 1) % 12
		: 0x9b;

	if (strftime(output + 5, MAX_OUTPUT - 5, "%a %-d %b %R", tm) == 0) {
		error(1, errno, "strftime");
	}

	my3status_output_done(m);
}