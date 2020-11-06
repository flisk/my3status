CFLAGS := \
	`pkg-config --cflags --libs gio-2.0 libpulse sqlite3` \
	-lm -lpthread \
	-Wall \
	-Wextra \
	-Werror=format-security \
	-Werror=implicit-function-declaration \
	$(CFLAGS)

PREFIX ?= /usr/local

my3status: my3status.c pulseaudio.c upower.c maildir.c meds.c

install: my3status
	install -D ./my3status $(DESTDIR)$(PREFIX)/bin/my3status

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/my3status

