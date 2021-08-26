#include <sys/sysinfo.h>
#include "mod_sysinfo.h"

// this is kinda big because the possible range for long values is, well, long
#define MAX_OUTPUT 512

static char output[MAX_OUTPUT] = "🐧 ";

static struct my3status_module mod = {
	.name   = "sysinfo",
	.output = output
};

static void *run(void *);

int mod_sysinfo_init(struct my3status_state *state)
{
	mod.state = state;

	pthread_t t;
	if (pthread_mutex_init(&mod.output_mutex, NULL) != 0 ||
	    pthread_create(&t, NULL, run, NULL) != 0) {
		return -1;
	}

	my3status_add_module(state, &mod);
	return 0;
}

void *run(__attribute__((unused)) void *arg)
{
	struct sysinfo s;

	float	load_5min, previous_load_5min = -1.0f;
	long	up_hours, previous_up_hours = -1L;
	long	up_days, previous_up_days = -1L;

	while (1) {
		if (sysinfo(&s) != 0) {
			error(1, errno, "sysinfo");
		}

		load_5min = s.loads[0] / (float) (1 << SI_LOAD_SHIFT);

		up_hours  = s.uptime / 3600;
		up_days	  = up_hours / 24;
		up_hours -= up_days  * 24;

		if (
			load_5min == previous_load_5min &&
			up_days == previous_up_days &&
			up_hours == previous_up_hours
		) {
			goto sleep;
		}

		pthread_mutex_lock(&mod.output_mutex);
		snprintf(
			output + 5, MAX_OUTPUT - 5, "%.2f %ldd %ldh",
			load_5min, up_days, up_hours
		);
		pthread_mutex_unlock(&mod.output_mutex);

		my3status_update(&mod);

		previous_load_5min = load_5min;
		previous_up_days = up_days;
		previous_up_hours = up_hours;
	
	sleep:
		sleep(10);
	}

	// should be unreachable
	return NULL;
}