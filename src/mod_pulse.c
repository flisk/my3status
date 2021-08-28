#include <math.h>
#include <pulse/pulseaudio.h>
#include "modules.h"

#define MAX_OUTPUT 16

static char output[MAX_OUTPUT] = "🔈 ";

static struct my3status_module mod = {
	.name		= "pulse",
	.output		= output,
	.output_visible	= true
};

static void on_context_state_change(pa_context *, void *);
static void on_subscribed(pa_context *, int, void *);
static void on_state_change(pa_context *, pa_subscription_event_type_t,
			    uint32_t, void *);
static void on_server_info(pa_context *, const pa_server_info *, void *);
static void on_sink_info(pa_context *, const pa_sink_info *, int, void *);

int mod_pulse_init(struct my3status_state *state)
{
	mod.state = state;

	if (pthread_mutex_init(&mod.output_mutex, NULL) != 0) {
		error(1, errno, "pthread_mutex_init");
	}

	pa_threaded_mainloop *mainloop = pa_threaded_mainloop_new();

	if (pa_threaded_mainloop_start(mainloop) < 0) {
		error(1, 0, "pa_threaded_mainloop_start failed");
	}

	pa_mainloop_api *mainloop_api = pa_threaded_mainloop_get_api(mainloop);
	pa_context *context = pa_context_new(mainloop_api, "my3status");

	pa_context_set_state_callback(context, on_context_state_change, state);
	pa_context_set_subscribe_callback(context, on_state_change, state);

	pa_threaded_mainloop_lock(mainloop);
	int r = pa_context_connect(context, NULL, PA_CONTEXT_NOFAIL, NULL);
	pa_threaded_mainloop_unlock(mainloop);

	if (r < 0) {
		error(1, 0, "pa_context_connect failed");
	}

	my3status_add_module(state, &mod);
	return 0;
}

static void on_context_state_change(pa_context *context, void *userdata) {
	if (pa_context_get_state(context) != PA_CONTEXT_READY) {
		return;
	}

	// trigger initial update
	pa_context_get_server_info(context, on_server_info, userdata);

	pa_subscription_mask_t sub_mask =
		PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SINK;

	pa_context_subscribe(context, sub_mask, on_subscribed, NULL);
}

static void on_subscribed(
	__attribute__((unused)) pa_context	*context,
	__attribute__((unused)) int		 success,
	__attribute__((unused)) void		*userdata
) {}

static void on_state_change(
	pa_context						*context,
	__attribute__((unused)) pa_subscription_event_type_t	 event_type,
	__attribute__((unused)) uint32_t			 i,
	void							*userdata
) {
	pa_context_get_server_info(context, on_server_info, userdata);
}

static void on_server_info(
	pa_context		*context,
	const pa_server_info	*server_info,
	void			*userdata
) {
	pa_operation *o = pa_context_get_sink_info_by_name(
		context,
		server_info->default_sink_name,
		on_sink_info,
		userdata
	);
	pa_operation_unref(o);
}

static void on_sink_info(
	__attribute__((unused)) pa_context	*context,
	const pa_sink_info			*sink_info,
	int					 eol,
	__attribute__((unused)) void		*userdata
) {
	if (1 == eol) {
		return;
	}

	pa_volume_t volume_avg = pa_cvolume_avg(&sink_info->volume);
	int volume_percent = (int) round((double) volume_avg * 100.0 / PA_VOLUME_NORM);

	pthread_mutex_lock(&mod.output_mutex);

	// Integer-dividing the volume by 34 gives an offset we can add to 0x88,
	// producing the appropriate speaker volume emojis:
	//
	//   0% -  33% → 0 (speaker low volume)
	//  34% -  66% → 1 (speaker medium volume)
	//  67% - 100% → 2 (speaker high volume)
	output[3] =
		sink_info->mute
		? 0x87
		: 0x88 + MIN(volume_percent / 34, 2);
	
	snprintf(output + 5, MAX_OUTPUT - 5, "%d%%", volume_percent);

	pthread_mutex_unlock(&mod.output_mutex);

	my3status_update(&mod);
}
