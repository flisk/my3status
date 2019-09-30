#include <gio/gio.h>

struct my3status_upower_state {
	GDBusConnection *dbus_conn;
	gdouble percent;
	gint64 time_to_empty;
	gint64 time_to_full;
};

int my3status_upower_update(struct my3status_upower_state*);
