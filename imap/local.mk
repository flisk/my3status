ifeq ($(DEBUG), 0)
CARGO_BUILD_MODE := release
CARGO_BUILD_ARG := --release
else
CARGO_BUILD_MODE := debug
endif

CARGO_IMAP_TARGET := imap/target/$(CARGO_BUILD_MODE)/libimap.so

all:: $(BUILD_DIR)/imap.so

clean::
	rm -rf imap/target

install:: $(BUILD_DIR)/imap.so
	install -D $(BUILD_DIR)/imap.so $(DESTDIR)$(PREFIX)/lib/my3status/imap.so

uninstall::
	rm $(DESTDIR)$(PREFIX)/lib/my3status/imap.so

$(BUILD_DIR)/imap.so: $(CARGO_IMAP_TARGET)
	cp $^ $@

$(CARGO_IMAP_TARGET): imap/build.rs imap/Cargo.toml imap/Cargo.lock
$(CARGO_IMAP_TARGET): $(wildcard imap/src/*.rs)
	cd imap; cargo build $(CARGO_BUILD_ARG)