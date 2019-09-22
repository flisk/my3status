/* vim: set noet ts=8 sw=8: */
#include <pulse/pulseaudio.h>

struct my3status_pa_state {
	pa_threaded_mainloop	*mainloop;
	pa_context		*context;
	pthread_t		 main_thread;
	unsigned int		 muted;
	unsigned int		 volume;
};

int my3status_pa_init(struct my3status_pa_state*);
int my3status_pa_update(struct my3status_pa_state*);
