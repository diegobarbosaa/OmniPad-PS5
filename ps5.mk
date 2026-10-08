# OmniPad PS5 — PS5 Payload Makefile
# Compatible with PS5 Firmware 7.00 through 13.60 (Relapse Exploit)

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

VERSION   := $(shell sed -E -n 's/.*ANYPAD_VERSION[[:space:]]+"([^"]+)".*/\1/p' src/version.h | tr -d '\r')
BUILD_NUM := $(shell sed -E -n 's/.*ANYPAD_BUILD[[:space:]]+([0-9]+).*/\1/p' src/version.h | tr -d '\r')
ELF       := dist/OmniPad-PS5-$(VERSION)-b$(BUILD_NUM).elf
BUILD     := build/ps5

CFLAGS    := -std=c99 -Wall -Wextra -O2 -Isrc
ifeq ($(TCP_DEBUG),1)
CFLAGS    += -DOMNIPAD_ENABLE_TCP_DEBUG
endif
LDLIBS    += -lScePad -lSceUserService -lSceSystemService -lSceAppInstUtil -ldl

SRCS := src/util.c src/log.c src/profiles.c src/http_core.c \
        src/tcp_frames.c src/usb_lifecycle.c \
        src/usb_controllers.c src/usb_hotplug.c \
        src/shellui_inject.c src/ps5_vpad.c \
        src/web.c src/tcp_stream.c src/main.c

OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

$(ELF): $(OBJS)
	@mkdir -p dist
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@cp $@ dist/OmniPad-PS5-$(VERSION).elf
	@cp $@ dist/OmniPad-PS5.elf
	@echo "[+] Built successfully: $@"
	@echo "[+] Canonical artifact: dist/OmniPad-PS5.elf"

HEADERS := $(wildcard src/*.h)

$(BUILD)/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)
