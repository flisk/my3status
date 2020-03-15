CFLAGS ?= -Wall \
          -Werror=format-security \
          -Werror=implicit-function-declaration \
          -Wextra

CFLAGS := $(CFLAGS) \
          `pkg-config --cflags --libs gio-2.0 libpulse libvirt` \
          -lm

my3status: my3status.c pulseaudio.c upower.c

install: my3status
	install -D ./my3status $(DESTDIR)/usr/bin/my3status
