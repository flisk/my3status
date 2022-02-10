PREFIX ?= /usr/local
BUILD_DIR ?= build

LIBS := libpulse

ifeq ($(DEBUG), 0)
CFLAGS := -O2
endif

ifndef DEBUG
DEBUG := 1
CFLAGS := -g
endif

override CFLAGS := \
	-ldl -lm -lpthread \
	-Wall -Wextra \
	-Werror=format-security -Werror=implicit-function-declaration \
	`pkg-config --cflags --libs $(LIBS)` \
	-DMY3STATUS_MODULE_PREFIX=\"$(PREFIX)/lib/my3status\" \
	$(CFLAGS)

.PHONY: all clean install uninstall

all:: $(BUILD_DIR)/my3status $(BUILD_DIR)/libmy3status.a

clean::
	rm --force $(BUILD_DIR)/*

install:: $(BUILD_DIR)/my3status $(BUILD_DIR)/libmy3status.a
	install -D $(BUILD_DIR)/my3status $(DESTDIR)$(PREFIX)/bin/my3status
	install -D $(BUILD_DIR)/libmy3status.a $(DESTDIR)$(PREFIX)/lib/libmy3status.a

uninstall::
	rm $(DESTDIR)$(PREFIX)/bin/my3status
	rm $(DESTDIR)$(PREFIX)/lib/libmy3status.a

$(BUILD_DIR)/my3status: $(wildcard core/*.c)
	$(CC) $^ -o $@ $(CFLAGS)

$(BUILD_DIR)/libmy3status.a: $(BUILD_DIR)/my3status.o
	ar rcs $@ $(BUILD_DIR)/my3status.o

$(BUILD_DIR)/my3status.o: core/my3status.c
	$(CC) -c -o $@ $^ $(CFLAGS)

include imap/local.mk