#include "my3status.h"

#define MAX_OUTPUT 32

static char output[MAX_OUTPUT] = "⬆️ ";

static void *run(void *);
static void check_for_updates(struct my3status_module *);

int mod_apt_init(struct my3status_state *s)
{
	struct my3status_module *m =
		my3status_register_module(s, "apt", output, false);

	pthread_t p;
	if (pthread_create(&p, NULL, run, m) != 0) {
		return -1;
	}

	return 0;
}

static void *run(void *arg)
{
	struct my3status_module *m = arg;

	while (1) {
		check_for_updates(m);
		sleep(3);
	}

	// should be unreachable
	return NULL;
}

#define MAX_LINEBUF 128

static void check_for_updates(struct my3status_module *m)
{
	static char linebuf[MAX_LINEBUF];

	FILE *p = popen("apt-get upgrade -s", "r");
	if (p == NULL) {
		error(1, errno, "popen");
	}

	char c;
	char *lineptr = linebuf;

	while ((c = fgetc(p)) != EOF) {
		if (c != '\n') {
			*lineptr = c;
			lineptr++;
			if (lineptr - linebuf > MAX_LINEBUF) {
				error(1, 0, "apt: line buffer exhausted");
			}
			continue;
		}

		*lineptr = '\0';
		lineptr = linebuf;
	}

	pclose(p);

	char *invalid;
	long upgraded = strtol(linebuf, &invalid, 10);

	if (invalid == linebuf) {
		error(1, 0, "apt: line format error: %s", linebuf);
	}

	my3status_output_begin(m);
	if (upgraded > 0) {
		m->output_visible = true;
		snprintf(output + 5, MAX_OUTPUT, "%ld", upgraded);
	} else {
		m->output_visible = false;
	}
	my3status_output_done(m);
}