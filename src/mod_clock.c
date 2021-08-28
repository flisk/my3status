#include <time.h>
#include "modules.h"

#define MAX_OUTPUT 32

static char output[MAX_OUTPUT] = { 0xf0, 0x9f, 0x95, 0x9b, ' ', 0 };

static struct my3status_module mod = {
	.name		= "clock",
	.output		= output,
	.output_visible	= true
};

static void *run(void *);

int mod_clock_init(struct my3status_state *my3status)
{
	mod.state = my3status;

	pthread_t t;
	if (pthread_mutex_init(&mod.output_mutex, NULL) != 0 ||
	    pthread_create(&t, NULL, run, NULL) != 0) {
		return -1;
	}

	my3status_add_module(my3status, &mod);
	return 0;
}

static void *run(__attribute__((unused)) void *arg)
{
	while (1) {
		time_t now = time(NULL);
		struct tm *tm = localtime(&now);

		if (tm == NULL) {
			error(0, errno, "localtime");
			goto update_and_sleep;
		}

		pthread_mutex_lock(&mod.output_mutex);

		output[3] =
			tm->tm_hour > 0
			? 0x90 + (tm->tm_hour - 1) % 12
			: 0x9b;
		
		if (strftime(output + 5, MAX_OUTPUT - 5, "%a %-d %b %R", tm) == 0) {
			error(0, errno, "strftime");
		}

		pthread_mutex_unlock(&mod.output_mutex);

	update_and_sleep:
		my3status_update(&mod);

		// Micro-optimization: unix timestamp mod 60 is a lot faster for
		// determining seconds in the current minute vs. letting localtime do
		// its whole song and dance.
		int seconds_in_minute = now % 60;

		// Let's talk about leap seconds.
		// 
		// The time_t we acquired above is a traditional Unix timestamp, meaning
		// an integer representing the seconds that have passed since 1970-01-01
		// 00:00 UTC. This value is specifically defined to increase by exactly
		// 86400 every day.
		// 
		// This definition leaves no room for leap seconds, so they simply do
		// not exist on Unix systems. Instead, the system clock is slowed down
		// to accommodate actual time, meaning during a leap second, tm_sec is
		// going to remain stuck at 59 for two seconds.
		// 
		// Which means: if we sleep for 60 - tm_sec seconds until the next
		// update, we'll get the correct time MOST of the time, but if there's a
		// leap second occurring, we'll miss the minute change.
		// 
		// Is this worth handling? Probably not. But there's a really easy hack
		// we can do to "fix" this: sleep for an extra second. This means our
		// displayed time will always drag by a second, but minute changes
		// during leap seconds won't be missed.
		sleep(61 - seconds_in_minute);
	}

	// should not be reachable
	return NULL;
}