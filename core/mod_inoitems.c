#define _GNU_SOURCE

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include "my3status.h"

#define OUTPUT_MAX 512
#define ITEMS_DIR "inostatus"

static char output[OUTPUT_MAX] = "";

static char *items_dir;
static int items_dir_fd;
static int inotify_fd;

static void *run(void *);
static void init_dir();
static void init_inotify();
static void print_items(struct my3status_module *);
static void print_item(const char *, int, char **);

int mod_inoitems_init(struct my3status_state *s)
{
	return my3status_init_internal_module(
		s, "inoitems", output, true, run
	);
}

static void *run(void *arg)
{
	struct my3status_module *m = arg;

	init_dir();
	init_inotify();

	size_t event_size = sizeof(struct inotify_event) + NAME_MAX + 1;
	struct inotify_event *event = malloc(event_size);

	if (event == NULL) {
		PANIC(errno, "malloc failed");
	}

	print_items(m);

	while (1) {
		if (read(inotify_fd, event, event_size) == -1) {
			PANIC(errno, "couldn't read from inotify fd");
		}

		print_items(m);
	}

	return NULL;
}

static void init_dir()
{
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == NULL) {
		PANIC(0, "XDG_RUNTIME_DIR not set");
	}

	size_t items_dir_size = strlen(runtime_dir) + 1 + strlen(ITEMS_DIR) + 1;
	items_dir = malloc(items_dir_size);
	if (items_dir == NULL) {
		PANIC(errno, "malloc");
	}

	sprintf(items_dir, "%s/" ITEMS_DIR, runtime_dir);

	if (mkdir(items_dir, 0700) == -1 && errno != EEXIST) {
		PANIC(errno, "mkdir failed: %s", ITEMS_DIR);
	}

	items_dir_fd = open(items_dir, O_PATH | O_DIRECTORY);
	if (items_dir_fd == -1) {
		PANIC(errno, "open failed: %s", items_dir);
	}
}

static void init_inotify()
{
	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		PANIC(errno, "inotify_init failed");
	}

	int ret = inotify_add_watch(
		inotify_fd,
		items_dir,
		IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_TO
	);

	if (ret == -1) {
		PANIC(errno, "couldn't add watch to items dir");
	}
}

static void print_items(struct my3status_module *m)
{
	struct dirent **names;

	int n = scandir(items_dir, &names, NULL, alphasort);
	if (n == -1) {
		PANIC(errno, "scandir");
	}

	my3status_output_begin(m);

	char *output_ptr = output;
	for (int i = 0; i < n; i += 1) {
		if (names[i]->d_name[0] == '.') {
			continue;
		}

		print_item(names[i]->d_name, i == n - 1, &output_ptr);

		free(names[i]);
	}

	free(names);

	*(output_ptr - 1) = '\0';

	my3status_output_done(m);
}

static void print_item(const char *file, int last, char **output_ptr)
{
	int fd = openat(items_dir_fd, file, O_RDONLY);
	if (fd == -1) {
		goto error;
	}

	char *buf = *output_ptr;

	ssize_t read_max = OUTPUT_MAX - (output - buf) - 1;
	ssize_t n = read(fd, buf, read_max);
	if (n == -1) {
		goto error;
	}

	if (!last) {
		if (n > 0 && buf[n - 1] == '\n') {
			buf[n - 1] = '/';
		} else {
			buf[n] = '/';
			n += 1;
		}
	}

	*output_ptr += n;
	
	goto finally;

error:
	error(0, errno, "print_item: %s", file);

finally:
	close(fd);
}
