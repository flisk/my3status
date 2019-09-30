#include <stdio.h>
#include "upower.h"

static int ensure_dbus_connected(GDBusConnection **conn) {
	if (*conn != NULL && !g_dbus_connection_is_closed(*conn)) {
		return 1;
	}

	GError *error = NULL;

	*conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

	if (error) {
		fprintf(stderr,
			"g_bus_get_sync: %i: %s\n",
			error->code, error->message);
		return 0;
	}

	return 1;
}


static int get_property(GDBusConnection *conn,
			const char *prop_name,
			GVariant **value)
{
	GError *error;
	GVariant *params;
	GVariant *result;

	error = NULL;

	params = g_variant_new("(ss)",
			       "org.freedesktop.UPower.Device",
			       prop_name);

	result = g_dbus_connection_call_sync(conn,
					     "org.freedesktop.UPower",
					     "/org/freedesktop/UPower/devices/DisplayDevice",
					     "org.freedesktop.DBus.Properties",
					     "Get",
					     params,
					     NULL,
					     G_DBUS_CALL_FLAGS_NONE,
					     -1,
					     NULL,
					     &error);

	if (error) {
		fprintf(stderr,
			"g_dbus_connection_call_sync: %i: %s\n",
			error->code, error->message);
		return 0;
	}

	g_variant_get(result, "(v)", value);
	g_variant_unref(result);

	return 1;
}

int display_device_is_battery(GDBusConnection *conn) {
	GVariant *g_device_type;
	guint32 device_type;

	if (!get_property(conn, "Type", &g_device_type)) {
		return 0;
	}

	device_type = g_variant_get_uint32(g_device_type);
	g_variant_unref(g_device_type);

	return device_type == 2;
}

int update_battery_data(struct my3status_upower_state *state) {
	GVariant *g_percent;
	GVariant *g_time_to_empty;
	GVariant *g_time_to_full;

	if (!get_property(state->dbus_conn, "Percentage", &g_percent) ||
	    !get_property(state->dbus_conn, "TimeToEmpty", &g_time_to_empty) ||
	    !get_property(state->dbus_conn, "TimeToFull", &g_time_to_full)) {
		return 0;
	}

	state->percent = g_variant_get_double(g_percent);
	state->time_to_empty = g_variant_get_int64(g_time_to_empty);
	state->time_to_full = g_variant_get_int64(g_time_to_full);

	g_variant_unref(g_percent);
	g_variant_unref(g_time_to_empty);
	g_variant_unref(g_time_to_full);

	return 1;
}

int my3status_upower_update(struct my3status_upower_state *state) {
	return ensure_dbus_connected(&state->dbus_conn)
		&& display_device_is_battery(state->dbus_conn)
		&& update_battery_data(state);
}
