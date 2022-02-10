#include <dlfcn.h>
#include <fcntl.h>
#include <sys/signalfd.h>

#include "my3status.h"

// because vscode is stupid
#ifndef MY3STATUS_MODULE_PREFIX
#define MY3STATUS_MODULE_PREFIX ""
#endif

static int parse_args(int, char **, struct my3status_state *);

static int load_external_module(struct my3status_state *, const char *);
static char *generate_module_path(const char *);

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
			if (m->output_visible) {
				printf(
					"{\"name\":\"%s\",\"full_text\":\"%s\"}%s",
					m->name, m->output, module_end
				);
			}
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
	if (argc == 1) {
		fputs("no modules specified. for a list of modules, run "
		      "`strings` on this program and guess which strings might"
		      " be valid module names.\n",
		      stderr);
		return -1;
	}

	int r = 0;
	for (int i = 1; i < argc; ++i) {
		if (strcmp("clock", argv[i]) == 0) {
			mod_clock_init(state);
		} else if (strcmp("df", argv[i]) == 0) {
			mod_df_init(state);
		} else if (strcmp("pulse", argv[i]) == 0) {
			mod_pulse_init(state);
		} else if (strcmp("sysinfo", argv[i]) == 0) {
			mod_sysinfo_init(state);
		} else if (strcmp("inoitems", argv[i]) == 0) {
			mod_inoitems_init(state);
		} else if (load_external_module(state, argv[i]) == -1) {
			fprintf(stderr, "invalid module: %s\n", argv[i]);
			r = -1;
		}
	}

	return r;
}

static int load_external_module(struct my3status_state *s, const char *name)
{
	char *module_path = generate_module_path(name);
	if (module_path == NULL) {
		return -1;
	}

	void *dl = dlopen(module_path, RTLD_LAZY);
	free(module_path);

	if (dl == NULL) {
		fprintf(stderr, "%s: dlopen: %s\n", __func__, dlerror());
		return -1;
	}

	void (*module_init)(struct my3status_state *);
	module_init = dlsym(dl, "my3status_module_init");

	if (module_init == NULL) {
		fprintf(stderr, "%s: dlsym: %s\n", __func__, dlerror());
		return -1;
	}

	module_init(s);

	return 0;
}

static char *generate_module_path(const char *name)
{
	size_t name_len = strlen(name);
	if (strcmp(name + name_len - 3, ".so") != 0) {
		fprintf(stderr, "%s: name doesn't end with .so: %s\n",
			__func__, name);
		return NULL;
	}

	const char *module_prefix = getenv("MY3STATUS_MODULE_PREFIX");
	if (module_prefix == NULL) {
		module_prefix = MY3STATUS_MODULE_PREFIX;
	}

	size_t prefix_len = strlen(module_prefix);
	char *s = calloc(1, prefix_len + name_len + 2);
	if (s == NULL) {
		error(0, errno, "%s: calloc: ", __func__);
		return NULL;
	}

	strcpy(s, module_prefix);
	s[prefix_len] = '/';
	strcpy(s + 1 + prefix_len, name);

	return s;
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