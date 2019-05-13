/* vim: set noet ts=8 sw=8: */
#include <stdio.h>
#include <math.h>
#include "pulseaudio.h"

#define CAST_STATE(userdata) (struct my3status_pa_state*) (userdata)

static void pa_server_info_cb(pa_context*, const pa_server_info*, void*);
static void pa_sink_info_cb(pa_context*, const pa_sink_info *, int, void*);

int my3status_pa_init(struct my3status_pa_state *s) {
	s->mainloop = pa_threaded_mainloop_new();
	pa_threaded_mainloop_start(s->mainloop);

	pa_mainloop_api *api = pa_threaded_mainloop_get_api(s->mainloop);
	s->context = pa_context_new(api, "my3status");

	pa_threaded_mainloop_lock(s->mainloop);
	int r = pa_context_connect(s->context, NULL, PA_CONTEXT_NOFAIL, NULL);
	pa_threaded_mainloop_unlock(s->mainloop);

	if (-1 == r) {
		fputs("pa_context_connect fucked up", stderr);
		return -1;
	}

	return 0;
}

int my3status_pa_update(struct my3status_pa_state *s) {
	pa_operation *o;

	pa_threaded_mainloop_lock(s->mainloop);
	o = pa_context_get_server_info(s->context, &pa_server_info_cb, s);

	if (o == NULL) {
		pa_threaded_mainloop_unlock(s->mainloop);
		return -1;
	}

	pa_threaded_mainloop_wait(s->mainloop);
	pa_operation_unref(o);

	pa_threaded_mainloop_accept(s->mainloop);
	pa_threaded_mainloop_unlock(s->mainloop);

	return 0;
}

static void pa_server_info_cb(pa_context *c, const pa_server_info *i,
		__attribute__((unused)) void *userdata) {
	struct my3status_pa_state *s = CAST_STATE(userdata);

	pa_operation *o = pa_context_get_sink_info_by_name(
			c,
			i->default_sink_name,
			&pa_sink_info_cb,
			s);
	pa_operation_unref(o);
}

static void pa_sink_info_cb(
		__attribute__((unused)) pa_context *c,
		const pa_sink_info *i,
		int eol,
		void *userdata
		) {
	if (1 == eol)
		return;

	struct my3status_pa_state *s = CAST_STATE(userdata);

	pa_volume_t avg = pa_cvolume_avg(&i->volume);
	int percent = (int) round((double) avg * 100.0 / PA_VOLUME_NORM);

	s->muted = i->mute;
	s->volume = percent;

	pa_threaded_mainloop_signal(s->mainloop, 1);
}
