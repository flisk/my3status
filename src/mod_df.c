#include <math.h>
#include <sys/vfs.h>
#include "modules.h"

#define MAX_OUTPUT 16

static char output[MAX_OUTPUT] = "💾 ";

static struct my3status_module mod = {
	.name	= "df",
	.output	= output
};

static void *run(void *);

int mod_df_init(struct my3status_state *my3status)
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
	struct statfs s;
	int previous_used_percent = -1;

	while (1) {
		if (statfs("/", &s) != 0) {
			error(1, errno, "statfs");
		}

		unsigned long total = s.f_blocks;
		unsigned long used  = total - s.f_bavail;

		int used_percent = round((100.0f / total) * used);

		if (used_percent == previous_used_percent) {
			goto sleep;
		}

		pthread_mutex_lock(&mod.output_mutex);
		snprintf(output + 5, MAX_OUTPUT - 5, "%d%%", used_percent);
		pthread_mutex_unlock(&mod.output_mutex);

		my3status_update(&mod);

		previous_used_percent = used_percent;

	sleep:
		sleep(10);
	}

	// should not be reachable
	return NULL;
}