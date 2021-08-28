#include <assert.h>
#include <fcntl.h>
#include <sys/signalfd.h>

#include "my3status.h"
#include "modules.h"

static int parse_args(int, char **, struct my3status_state *);

static int listen_sigusr1();
static void wait_for_signals(int);

int main(int argc, char **argv)
{
	// Stop glibc from running a superfluous stat() on each strftime()
	if (setenv("TZ", ":/etc/localtime", 0) != 0) {
		error(1, errno, "setenv");
	}

	struct my3status_state state = {
		.main_thread = pthread_self()
	};

	int sfd = listen_sigusr1();

	if (parse_args(argc, argv, &state) == -1) {
		exit(EXIT_FAILURE);
	}
	
	printf("{\"version\":1}\n"
	       "[\n");

	struct my3status_module_node	*n;
	struct my3status_module		*m;
	const char			*module_end;

	while (1) {
		wait_for_signals(sfd);

		fputs("[", stdout);

		n = state.first_module;
		while (n != NULL) {
			m = n->module;
			module_end = (n->next == NULL ? "" : ",");

			pthread_mutex_lock(&m->output_mutex);
			printf(
				"{\"name\":\"%s\",\"full_text\":\"%s\"}%s",
				m->name, m->output, module_end
			);
			pthread_mutex_unlock(&m->output_mutex);

			n = n->next;
		}

		fputs("],\n", stdout);

		if (fflush(stdout) == EOF) {
			error(1, errno, "fflush");
		}
	}
}

static int listen_sigusr1()
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		error(1, errno, "sigprocmask");
	}

	int sfd = signalfd(-1, &mask, 0);
	if (sfd == -1) {
		error(1, errno, "signalfd");
	}

	return sfd;
}

static int parse_args(int argc, char **argv, struct my3status_state *state)
{
	int r = 0;

	if (argc == 1) {
		fputs("no modules specified. for a list of modules, run "
		      "`strings` on this program and guess which strings might"
		      " be valid module names.\n",
		      stderr);
		return -1;
	}

	for (int i = 1; i < argc; ++i) {
		if (strcmp("clock", argv[i]) == 0) {
			mod_clock_init(state);
		} else if (strcmp("df", argv[i]) == 0) {
			mod_df_init(state);
		} else if (strcmp("meds", argv[i]) == 0) {
			mod_meds_init(state);
		} else if (strcmp("pulse", argv[i]) == 0) {
			mod_pulse_init(state);
		} else if (strcmp("sysinfo", argv[i]) == 0) {
			mod_sysinfo_init(state);
		} else {
			r = -1;
			fprintf(stderr, "invalid module: '%s'\n", argv[i]);
		}
	}

	return r;
}

static void wait_for_signals(int sfd)
{
	static struct signalfd_siginfo siginfo;

	ssize_t s = read(sfd, &siginfo, sizeof(siginfo));
	if (s != sizeof(siginfo)) {
		error(1, errno, "read");
	}

	// wait 30 ms for any other signals to arrive before we send off our output
	usleep(30 * 1000);

	// put the signalfd into non-blocking mode and try to read any other pending
	// signals so we can treat multiple successive updates as one
	int flags = fcntl(sfd, F_GETFL, 0);
	fcntl(sfd, F_SETFL, flags | O_NONBLOCK);

	while (s != -1) {
		s = read(sfd, &siginfo, sizeof(siginfo));
	}

	if (errno != EAGAIN) {
		error(1, errno, "read: expected EAGAIN");
	}

	// revert signalfd to blocking mode for next iteration
	fcntl(sfd, F_SETFL, flags);
}

void my3status_add_module(
	struct my3status_state	*state,
	struct my3status_module	*module
) {
	assert(state->first_module == NULL || state->last_module != NULL);

	struct my3status_module_node *item;
	item = malloc(sizeof(struct my3status_module_node));
	if (item == NULL) {
		error(1, errno, "malloc");
	}

	item->module = module;
	item->next = NULL;

	struct my3status_module_node **dest_ptr;
	if (state->first_module == NULL) {
		dest_ptr = &state->first_module;
	} else {
		dest_ptr = &state->last_module->next;
	}

	*dest_ptr = item;
	state->last_module = item;
}

void my3status_update(struct my3status_module *m)
{
	//fprintf(stderr, "\t%s triggered update\n", m->name);
	pthread_kill(m->state->main_thread, SIGUSR1);
}