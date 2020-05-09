/* vim: set noet ts=8 sw=8: */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pulseaudio.h"

static void on_context_state_change(pa_context*, void*);
static void on_subscribed(pa_context*, int, void*);
static void on_state_change(pa_context*, pa_subscription_event_type_t, uint32_t,
			    void*);
static void on_server_info(pa_context*, const pa_server_info*, void*);
static void on_sink_info(pa_context*, const pa_sink_info*, int, void*);


int my3status_pulse_init(struct my3status_pulse_state *state) {
	int r;
	pa_threaded_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	pa_context *context;

	r = pthread_mutex_init(&state->mutex, NULL);
	if (r != 0) {
		fprintf(
			stderr,
			"pthread_mutex_init returned %d: %s\n",
			r, strerror(r)
		);
		return 0;
	}

	state->main_thread = pthread_self();

	mainloop = pa_threaded_mainloop_new();

	r = pa_threaded_mainloop_start(mainloop);
	if (r < 0) {
		fprintf(stderr, "pa_threaded_mainloop_start returned %d\n", r);
		return 0;
	}

	mainloop_api = pa_threaded_mainloop_get_api(mainloop);

	context = pa_context_new(mainloop_api, "my3status");

	pa_context_set_state_callback(context, on_context_state_change, state);
	pa_context_set_subscribe_callback(context, on_state_change, state);

	pa_threaded_mainloop_lock(mainloop);
	r = pa_context_connect(context, NULL, PA_CONTEXT_NOFAIL, NULL);
	pa_threaded_mainloop_unlock(mainloop);

	if (r < 0) {
		fprintf(stderr, "pa_context_connect returned %d\n", r);
		return 0;
	}

	return 1;
}

static void on_context_state_change(pa_context *context, void *userdata) {
	if (pa_context_get_state(context) != PA_CONTEXT_READY) {
		return;
	}

	/* trigger initial update */
	pa_context_get_server_info(context, on_server_info, userdata);

	pa_subscription_mask_t sub_mask =
		PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SINK;

	pa_context_subscribe(context, sub_mask, on_subscribed, NULL);
}

static void on_subscribed(__attribute__((unused)) pa_context *context,
			  __attribute__((unused)) int success,
			  __attribute__((unused)) void *userdata)
{
}

static void on_state_change(
	pa_context *context,
	__attribute__((unused)) pa_subscription_event_type_t event_type,
	__attribute__((unused)) uint32_t i,
	void *userdata
) {
	pa_context_get_server_info(context, on_server_info, userdata);
}

static void on_server_info(
	pa_context *context,
	const pa_server_info *server_info,
	void *userdata
) {
	pa_operation *operation;

	operation = pa_context_get_sink_info_by_name(
		context,
		server_info->default_sink_name,
		on_sink_info,
		userdata
	);

	pa_operation_unref(operation);
}

static void on_sink_info(
	__attribute__((unused)) pa_context *context,
	const pa_sink_info *sink_info,
	int eol,
	void *userdata
) {
	struct my3status_pulse_state *state;
	pa_volume_t volume_avg;
	int volume_percent;

	if (1 == eol) {
		return;
	}

	state = userdata;

	volume_avg = pa_cvolume_avg(&sink_info->volume);
	volume_percent = (int) round((double) volume_avg * 100.0 / PA_VOLUME_NORM);

	pthread_mutex_lock(&state->mutex);

	state->muted = sink_info->mute;
	state->volume = volume_percent;

	pthread_mutex_unlock(&state->mutex);

	pthread_kill(state->main_thread, SIGUSR1);
}
