CFLAGS := \
	`pkg-config --cflags --libs gio-2.0 libpulse` \
	-lm -lpthread \
	-Wall \
	-Wextra \
	-Werror=format-security \
	-Werror=implicit-function-declaration \
	$(CFLAGS) \

my3status: my3status.c pulseaudio.c upower.c

install: my3status
	install -D ./my3status $(DESTDIR)/usr/bin/my3status

