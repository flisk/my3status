/* vim: set noet ts=8 sw=8: */
#include <sys/sysinfo.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define I3BAR_ITEM(name, full_text) \
	printf("{\"name\":\"" name "\",\"full_text\":\""); \
	full_text; \
	printf("\"},")

#define TIMEBUF_SIZE 32

void print_datetime(struct tm *tm, char *timebuf) {
	if (strftime(timebuf, TIMEBUF_SIZE, "%a %-d %b %R", tm) == 0)
		error(1, errno, "strftime");

	printf("%s", timebuf);
}

void print_sysinfo(struct sysinfo *si) {
	float load_5min = si->loads[0] / (float) (1 << SI_LOAD_SHIFT);
	long up_hours = si->uptime / 3600;
	long up_days = up_hours / 24;

	up_hours -= up_days * 24;

	printf("%.2f up %ldd %ldh", load_5min, up_days, up_hours);
}

int main() {
	struct sysinfo si;
	struct tm *tm;
	time_t tt;
	char timebuf[TIMEBUF_SIZE];

	// This stops glibc from doing a superfluous stat() for every
	// strftime(). It's an asinine micro-optimization, but, for fun and no
	// profit, I'd like this program as efficient as I can make it.
	if (setenv("TZ", ":/etc/localtime", 0) == -1)
		error(1, errno, "setenv");

	puts("{\"version\": 1}\n[\n[]");
	while (1) {
		if (sysinfo(&si) != 0)
			error(1, errno, "sysinfo");

		tt = time(NULL);
		if ((tm = localtime(&tt)) == NULL)
			error(1, errno, "localtime");


		printf(",[");

		I3BAR_ITEM("uptime", print_sysinfo(&si));
		I3BAR_ITEM("datetime", print_datetime(tm, timebuf));

		printf("]");
		fflush(stdout);
		sleep(5);
	}
}
