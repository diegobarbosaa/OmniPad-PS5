# OmniPad PS5 — PS5 Payload Makefile
# Compatible with PS5 Firmware 7.00 through 13.60 (Relapse Exploit)

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

VERSION   := $(shell sed -E -n 's/.*ANYPAD_VERSION[[:space:]]+"([^"]+)".*/\1/p' src/version.h | tr -d '\r')
BUILD_NUM := $(shell sed -E -n 's/.*ANYPAD_BUILD[[:space:]]+([0-9]+).*/\1/p' src/version.h | tr -d '\r')
ELF       := dist/OmniPad-PS5-$(VERSION)-b$(BUILD_NUM).elf
BUILD     := build/ps5

CFLAGS    := -std=c11 -Wall -Wextra -O2 -Isrc
LDLIBS    += -lScePad -lSceUserService -lSceSystemService -lSceAppInstUtil -ldl

SRCS := src/util.c src/log.c src/profiles.c src/generic.c \
        src/usb_controllers.c src/usb_hotplug.c \
        src/shellui_inject.c src/ps5_vpad.c \
        src/bt_host.c \
        src/host.c src/hci_usb.c src/le.c src/evstream.c src/usb_desc.c \
        src/smp_crypto.c src/crc32.c src/haptic_level.c src/ps5_haptic.c \
        src/web.c src/web_page.c src/tcp_stream.c src/main.c src/remap.c

OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

$(ELF): $(OBJS)
	@mkdir -p dist
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@cp $@ dist/OmniPad-PS5-$(VERSION).elf
	@cp $@ dist/OmniPad-PS5.elf
	@echo "[+] Built successfully: $@"
	@echo "[+] Canonical artifact: dist/OmniPad-PS5.elf"

HEADERS := $(wildcard src/*.h)

# Dashboard page is embedded as a byte array; regenerated whenever web/index.html changes.
WEB_ASSETS := $(wildcard web/img/*)
src/web_page.c: web/index.html $(WEB_ASSETS) tools/embed_html.py
	python3 tools/embed_html.py web/index.html $@ $(WEB_ASSETS)

$(BUILD)/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD) src/web_page.c
