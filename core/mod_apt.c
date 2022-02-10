#include "my3status.h"

#define MAX_OUTPUT 32

static char output[MAX_OUTPUT] = "⬆ ";

static void *run(void *);
static void check_for_updates(struct my3status_module *);

int mod_apt_init(struct my3status_state *s)
{
	return my3status_init_internal_module(
		s, "apt", output, true, run
	);
}

static void *run(void *arg)
{
	struct my3status_module *m = arg;

	printf("%d\n", strlen(output));

	while (1) {
		check_for_updates(m);
		sleep(3600);
	}

	// should be unreachable
	return NULL;
}

#define MAX_LINEBUF 128

static void check_for_updates(struct my3status_module *m)
{
	static char linebuf[MAX_LINEBUF];

	FILE *p = popen("apt-get upgrade --dry-run", "r");
	if (p == NULL) {
		error(1, errno, "popen");
	}

	char c;
	char *lineptr = linebuf;
	char *invalid;
	long upgraded = -1;

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

		upgraded = strtol(linebuf, &invalid, 10);

		if (invalid != linebuf) {
			break;
		}
	}

	pclose(p);

	if (upgraded == -1) {
		fprintf(stderr, "apt: %s: no matching lines found\n", __func__);
		return;
	}

	my3status_output_begin(m);
	if (upgraded > 0) {
		m->output_visible = true;
		snprintf(output + 4, MAX_OUTPUT, "%ld", upgraded);
	} else {
		m->output_visible = false;
	}
	my3status_output_done(m);
}