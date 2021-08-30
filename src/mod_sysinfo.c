#include <sys/sysinfo.h>
#include "my3status.h"

// this is kinda big because the possible range for long values is, well, long
#define MAX_OUTPUT 512

static char output[MAX_OUTPUT] = "🐧 ";

static void *run(void *);

int mod_sysinfo_init(struct my3status_state *s)
{
	struct my3status_module *m =
		my3status_register_module(s, "sysinfo", output, true);

	pthread_t p;
	if (pthread_create(&p, NULL, run, m) != 0) {
		return -1;
	}

	return 0;
}

void *run(void *arg)
{
	struct my3status_module *m = arg;
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

		my3status_output_begin(m);
		snprintf(
			output + 5, MAX_OUTPUT - 5, "%.2f %ldd %ldh",
			load_5min, up_days, up_hours
		);
		my3status_output_done(m);

		previous_load_5min = load_5min;
		previous_up_days = up_days;
		previous_up_hours = up_hours;
	
	sleep:
		sleep(10);
	}

	// should be unreachable
	return NULL;
}