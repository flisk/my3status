#include <math.h>
#include <sys/vfs.h>
#include "my3status.h"

#define MAX_OUTPUT 16

static char output[MAX_OUTPUT] = "💾 ";

static void *run(void *);

int mod_df_init(struct my3status_state *s)
{
	struct my3status_module *m =
		my3status_register_module(s, "df", output, true);

	pthread_t p;
	if (pthread_create(&p, NULL, run, m) != 0) {
		return -1;
	}

	return 0;
}

static void *run(void *arg)
{
	struct my3status_module *m = arg;

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

		my3status_output_begin(m);
		snprintf(output + 5, MAX_OUTPUT - 5, "%d%%", used_percent);
		my3status_output_done(m);

		previous_used_percent = used_percent;

	sleep:
		sleep(10);
	}

	// should not be reachable
	return NULL;
}