#pragma once

// headers for declarations in this file
#include <pthread.h>

// headers commonly used by modules
#include <errno.h>
#include <error.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PANIC(errno, format, ...) \
        error( \
                1, (errno), ("[%s:%d] " format), \
                __func__, __LINE__ __VA_OPT__(,) __VA_ARGS__ \
        )

struct my3status_module;

/* Main application state */
struct my3status_state {
	pthread_t			 main_thread;

	struct my3status_module_node	*first_module;
	struct my3status_module_node	*last_module;
};

struct my3status_module {
	struct my3status_state	*state;
	const char		*name;
	const char		*output;
	bool			 output_visible;
	pthread_mutex_t		 output_mutex;
};

struct my3status_module_node {
	struct my3status_module		*module;
	struct my3status_module_node	*next;
};

/*
 * Registers a module. Intended to be called from mod_init_* functions.
 */
struct my3status_module *my3status_register_module(
	struct my3status_state *s, const char *name, const char *output,
	bool visible
);

int my3status_init_internal_module(
	struct my3status_state	*state,
	const char		*name,
	const char		*output,
	bool			 initially_visible,
	void			*(*run)(void *)
);
int mod_apt_init(struct my3status_state *);
int mod_clock_init(struct my3status_state *);
int mod_df_init(struct my3status_state *);
int mod_inoitems_init(struct my3status_state *);
int mod_meds_init(struct my3status_state *);
int mod_pulse_init(struct my3status_state *);
int mod_sysinfo_init(struct my3status_state *);

void my3status_output_begin(struct my3status_module *);
void my3status_output_done(struct my3status_module *);
