/* vim: set noet ts=8 sw=8: */
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include "pulseaudio.h"

static void pa_context_state_cb(pa_context*, void*);
static void pa_context_success_cb(pa_context*, int, void*);
static void pa_subscribe_cb(pa_context*, pa_subscription_event_type_t,
			    uint32_t, void*);
static void pa_server_info_cb(pa_context*, const pa_server_info*, void*);
static void pa_sink_info_cb(pa_context*, const pa_sink_info*, int, void*);


int my3status_pa_init(struct my3status_pa_state *s) {
	s->main_thread = pthread_self();

	s->mainloop = pa_threaded_mainloop_new();
	pa_threaded_mainloop_start(s->mainloop);

	pa_mainloop_api *api = pa_threaded_mainloop_get_api(s->mainloop);
	s->context = pa_context_new(api, "my3status");

	pa_context_set_state_callback(s->context, pa_context_state_cb, NULL);
	pa_context_set_subscribe_callback(s->context, pa_subscribe_cb, s);

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
	o = pa_context_get_server_info(s->context, pa_server_info_cb, s);

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

static void pa_context_state_cb(pa_context *c,
				__attribute__((unused)) void *userdata) {
	pa_context_state_t s = pa_context_get_state(c);

	if (s != PA_CONTEXT_READY)
		return;

	pa_subscription_mask_t sub_mask =
		PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SINK;

	pa_context_subscribe(c, sub_mask, pa_context_success_cb, NULL);
}

static void pa_context_success_cb(__attribute__((unused)) pa_context *c,
				  __attribute__((unused)) int success,
				  __attribute__((unused)) void *userdata) {}

static void pa_subscribe_cb(__attribute__((unused)) pa_context *c,
			    __attribute__((unused)) pa_subscription_event_type_t t,
			    uint32_t i,
			    void *userdata) {
	if (i != 0)
		return;

	struct my3status_pa_state *s = userdata;
	pthread_kill(s->main_thread, SIGUSR1);
}

static void pa_server_info_cb(pa_context *c,
			      const pa_server_info *i,
			      void *userdata) {
	struct my3status_pa_state *s = userdata;

	pa_operation *o = pa_context_get_sink_info_by_name(
							   c,
							   i->default_sink_name,
							   pa_sink_info_cb,
							   s);
	pa_operation_unref(o);
}

static void pa_sink_info_cb(__attribute__((unused)) pa_context *c,
			    const pa_sink_info *i,
			    int eol,
			    void *userdata) {
	if (1 == eol)
		return;

	struct my3status_pa_state *s = userdata;

	pa_volume_t avg = pa_cvolume_avg(&i->volume);
	int percent = (int) round((double) avg * 100.0 / PA_VOLUME_NORM);

	s->muted = i->mute;
	s->volume = percent;

	pa_threaded_mainloop_signal(s->mainloop, 1);
}
