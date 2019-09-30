#include <gio/gio.h>
#include <pthread.h>

struct my3status_upower_state {
	pthread_mutex_t mutex;
	pthread_t main_thread;
	gdouble percent;
	gint64 time_to_empty;
	gint64 time_to_full;
};

int my3status_upower_init(struct my3status_upower_state*);
