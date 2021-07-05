/* vim: set noet ts=8 sw=8: -*- indent-tabs-mode: t; -*- */
#include <sys/inotify.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "my3status.h"

#define EMOJI_MAILBOX_EMPTY	"\xf0\x9f\x93\xaa"
#define EMOJI_MAILBOX_NONEMPTY	"\xf0\x9f\x93\xac"
#define INOTIFY_BUF_SIZE	(sizeof(struct inotify_event) + NAME_MAX + 1)

static void *watch_maildirs(void *arg)
{
	pthread_t main_thread = (pthread_t) arg;

	int fd = inotify_init();
	if (fd == -1) {
		error(1, errno, "%s: inotify_init: ", __func__);
	}

        int r;

	r = inotify_add_watch(
		fd,
		"/home/tobias/.local/share/mbsync/fastmail/INBOX/new",
                IN_MOVED_TO | IN_DELETE
	);

	if (r == -1) {
		error(1, errno, "%s: inotify_add_watch: ", __func__);
	}

	r = inotify_add_watch(
		fd,
		"/home/tobias/.local/share/mbsync/turing/INBOX/new",
                IN_MOVED_TO | IN_DELETE
	);

	if (r == -1) {
		error(1, errno, "%s: inotify_add_watch: ", __func__);
	}

	char buf[INOTIFY_BUF_SIZE];
	ssize_t read_size;
	const struct inotify_event *event;

	for (;;) {
		read_size = read(fd, buf, sizeof(buf));
		if (read_size == -1 && errno != EAGAIN) {
			error(1, errno, "%s: read: ", __func__);
		}

		event = (const struct inotify_event *) buf;

		fprintf(stderr, "inotify: %s\n", event->name);
		pthread_kill(main_thread, SIGUSR1);
	}

	error(1, 0, "%s: this line should be unreachable", __func__);
	return NULL;
}

static void count_new(const char *path, int *num_new, int *num_errors)
{
        DIR *d = opendir(path);
        if (d == NULL) {
                error(0, errno, "%s: opendir: ", __func__);
                *num_errors += 1;
                return;
        }

        for (;;) {
                struct dirent *entry = readdir(d);
                if (entry == NULL) {
                        break;
                }

                const char *n = entry->d_name;
                if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) {
                        continue;
                }

                *num_new += 1;
        }

        if (closedir(d) != 0) {
                error(0, errno, "%s: closedir: ", __func__);
                *num_errors += 1;
        }
}

void my3status_maildir_item(int last)
{
        int num_new = 0;
        int num_errors = 0;

        const char *error_indicator = "";
        const char *emoji = EMOJI_MAILBOX_EMPTY;

        count_new("/home/tobias/.local/share/mbsync/fastmail/INBOX/new", &num_new, &num_errors);
        count_new("/home/tobias/.local/share/mbsync/turing/INBOX/new", &num_new, &num_errors);

        if (num_new > 0) {
                emoji = EMOJI_MAILBOX_NONEMPTY;
        }

        if (num_errors > 0) {
                error_indicator = " (!)";
        }

        i3bar_item(last, "maildir", "%s %d%s", emoji, num_new, error_indicator);
}

void my3status_maildir_init()
{
        pthread_t p;
        pthread_t self = pthread_self();
        int r = pthread_create(&p, NULL, watch_maildirs, (void *) self);

        if (r != 0) {
                error(1, errno, "%s: pthread_create: ", __func__);
        }
}
