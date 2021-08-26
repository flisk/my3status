#include <stdio.h>
#include <string.h>
#include "upower.h"

static int display_device_property(
	GDBusConnection *conn,
	const gchar *property_name,
	GVariant **variant
) {
	GVariant *parameters = g_variant_new(
		"(ss)",
		"org.freedesktop.UPower.Device",
		property_name
	);

	GError *g_error = NULL;
	GVariant *g_result = g_dbus_connection_call_sync(
		conn,
		"org.freedesktop.UPower",
		"/org/freedesktop/UPower/devices/DisplayDevice",
		"org.freedesktop.DBus.Properties",
		"Get",
		parameters,
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&g_error
	);

	if (g_error != NULL) {
		fprintf(
			stderr,
			"g_dbus_connection_call_sync: [%i] %s\n",
			g_error->code,
			g_error->message
		);
		return 0;
	}

	g_variant_get(g_result, "(v)", variant);
	g_variant_unref(g_result);

	return 1;
}

static int device_percentage(GDBusConnection *conn, gdouble *percent) {
	GVariant *g_percent;

	if (!display_device_property(conn, "Percentage", &g_percent)) {
		return 0;
	}

	*percent = g_variant_get_double(g_percent);
	g_variant_unref(g_percent);

	return 1;
}

static void properties_changed(
	GDBusConnection *conn,
	__attribute__((unused)) const gchar *sender_name,
	__attribute__((unused)) const gchar *object_path,
	__attribute__((unused)) const gchar *interface_name,
	__attribute__((unused)) const gchar *signal_name,
	GVariant *parameters,
	gpointer user_data
) {
	struct my3status_upower_state *state = user_data;

	gdouble percent = -1.0;
	device_percentage(conn, &percent);

	GVariant *dict = g_variant_get_child_value(parameters, 1);
	GVariantIter *iter = g_variant_iter_new(dict);

	gchar *key;
	GVariant *value;

	gint64 time_to_empty = -1;
	gint64 time_to_full = -1;

	while ((g_variant_iter_next(iter, "{sv}", &key, &value))) {
		if (strcmp("TimeToEmpty", key) == 0) {
			state->time_to_empty = g_variant_get_int64(value);
		} else if (strcmp("TimeToFull", key) == 0) {
			state->time_to_full = g_variant_get_int64(value);
		}

		g_variant_unref(value);
		g_free(key);
	}

	g_variant_iter_free(iter);
	g_variant_unref(dict);

	pthread_mutex_lock(&state->mutex);

	if (percent != -1.0)
		state->percent = percent;

	if (time_to_empty != -1)
		state->time_to_empty = time_to_empty;

	if (time_to_full != -1)
		state->time_to_full = time_to_full;

	pthread_mutex_unlock(&state->mutex);

	pthread_kill(state->main_thread, SIGUSR1);
}

static void initialize_state(
	GDBusConnection *conn,
	struct my3status_upower_state *state
) {
	GVariant *time_to_full;
	GVariant *time_to_empty;

	display_device_property(conn, "TimeToFull", &time_to_full);
	display_device_property(conn, "TimeToEmpty", &time_to_empty);

	state->time_to_full = g_variant_get_int64(time_to_full);
	state->time_to_empty = g_variant_get_int64(time_to_empty);

	g_variant_unref(time_to_full);
	g_variant_unref(time_to_empty);

	device_percentage(conn, &state->percent);
}

static void *run_upower_thread(__attribute__((unused)) void *state) {
	GMainLoop *main_loop;

	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	return NULL; /* should never be reached */
}

int my3status_upower_init(struct my3status_upower_state *state) {
	GError *error;
	GDBusConnection *conn;

	error = NULL;
	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

	if (error != NULL) {
		fprintf(
			stderr,
			"g_bus_get_sync: %d: %s\n",
			error->code,
			error->message
		);
		return 0;
	}

	initialize_state(conn, state);

	g_dbus_connection_signal_subscribe(
		conn,
		"org.freedesktop.UPower",
		"org.freedesktop.DBus.Properties",
		"PropertiesChanged",
		"/org/freedesktop/UPower/devices/DisplayDevice",
		NULL,
		G_DBUS_SIGNAL_FLAGS_NONE,
		properties_changed,
		state,
		NULL
	);

	state->main_thread = pthread_self();
	pthread_mutex_init(&state->mutex, NULL);

	pthread_t p;
	int r = pthread_create(&p, NULL, run_upower_thread, (void *) state);

	if (r != 0)
		return 0;

	return 1;
}
