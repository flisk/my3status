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

struct my3status_module;

/* Main application state */
struct my3status_state {
	pthread_t			 main_thread;

	struct my3status_module_node	*first_module;
	struct my3status_module_node	*last_module;
};

struct my3status_module {
	const char			*name;
	struct my3status_state		*state;
	char				*output;
	bool				 output_visible;
	pthread_mutex_t	 		 output_mutex;
};

struct my3status_module_node {
	struct my3status_module		*module;
	struct my3status_module_node	*next;
};

/*
 * Registers a module. Intended to be called from mod_init_* functions.
 */
void my3status_add_module(struct my3status_state *, struct my3status_module *);

/*
 * Notifies the main thread that updated output is available.
 */
void my3status_update(struct my3status_module *);