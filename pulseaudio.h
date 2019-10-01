/* vim: set noet ts=8 sw=8: */
#include <pthread.h>
#include <pulse/pulseaudio.h>

struct my3status_pulse_state {
	pthread_mutex_t		 mutex;
	pthread_t		 main_thread;
	unsigned int		 muted;
	unsigned int		 volume;
};

int my3status_pulse_init(struct my3status_pulse_state*);
