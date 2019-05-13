CFLAGS ?= -Wall -Werror=format-security -Werror=implicit-function-declaration \
		  -Wextra -O2 -march=native \
		  `pkg-config --cflags --libs gio-2.0 libpulse` -lm

my3status: my3status.c pulseaudio.c
