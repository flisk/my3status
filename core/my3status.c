#include <assert.h>
#include "my3status.h"

static void append_module(
	struct my3status_state	*state,
	struct my3status_module	*module
) {
	assert(state->first_module == NULL || state->last_module != NULL);

	struct my3status_module_node *item
		= malloc(sizeof(struct my3status_module_node));
	if (item == NULL) {
		error(1, errno, "malloc");
	}

	item->module = module;
	item->next = NULL;

	struct my3status_module_node **dest_ptr;
	if (state->first_module == NULL) {
		dest_ptr = &state->first_module;
	} else {
		dest_ptr = &state->last_module->next;
	}

	*dest_ptr = item;
	state->last_module = item;
}

struct my3status_module *my3status_register_module(
	struct my3status_state	*state,
	const char		*name,
	const char		*output,
	bool			 visible
) {
	struct my3status_module *m = calloc(1, sizeof(struct my3status_module));
	if (m == NULL) {
		error(1, errno, "calloc");
	}

	if (pthread_mutex_init(&m->output_mutex, NULL) != 0) {
		error(1, errno, "pthread_mutex_init");
	}
	
	m->state = state;
	m->name = name;
	m->output = output;
	m->output_visible = visible;

	append_module(state, m);

	return m;
}

void my3status_output_begin(struct my3status_module *m)
{
	if (pthread_mutex_lock(&m->output_mutex) != 0) {
		error(1, errno, "pthread_mutex_lock");
	}
}

void my3status_output_done(struct my3status_module *m)
{
	if (pthread_mutex_unlock(&m->output_mutex) != 0) {
		error(1, errno, "pthread_mutex_unlock");
	}

	//fprintf(stderr, "\t%s triggered update\n", m->name);
	pthread_kill(m->state->main_thread, SIGUSR1);
}