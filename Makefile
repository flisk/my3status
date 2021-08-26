override CFLAGS := \
	`pkg-config --cflags --libs libpulse sqlite3` \
	-lm -lpthread \
	-Wall \
	-Wextra \
	-Werror=format-security \
	-Werror=implicit-function-declaration \
	-O2 \
	$(CFLAGS)

PREFIX ?= /usr/local

SOURCES = $(wildcard src/*.c)

all: bin/my3status

clean:
	rm bin/my3status

bin/my3status: $(SOURCES)
	$(CC) $^ -o $@ $(CFLAGS)

install: bin/my3status
	install -D ./bin/my3status $(DESTDIR)$(PREFIX)/bin/my3status

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/my3status