# OmniPad PS5
# Universal Controller Engine & Web Control Center for PS5 (FW 7.00 - 13.60)
# Combines Bluetooth (AnyPad-PS5) + USB Hotplug (Ghostcontrol / PoorDS4) + ShellUI Fixes (YetAnotherControllerEnabler)

PS5_HOST ?=
PS5_PORT ?= 9021

BUILD := build
HOST_CC ?= clang
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -Wno-overlength-strings -O1 -g
HOST_SANITIZERS ?=

.PHONY: all ps5 send clean clean-host test host-test host-test-sanitize

all: ps5

ps5:
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK is undefined. Please export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk)
endif
	$(MAKE) -f ps5.mk

send: ps5
	@test -n "$(PS5_HOST)" || (echo "Set PS5_HOST to the console address."; exit 1)
	@echo "[*] Sending payload to PS5 $(PS5_HOST):$(PS5_PORT)..."
	nc -w 5 $(PS5_HOST) $(PS5_PORT) < dist/OmniPad-PS5-*.elf || \
	python3 -c "import socket, glob; f=open(glob.glob('dist/OmniPad-PS5-*.elf')[0],'rb').read(); s=socket.create_connection(('$(PS5_HOST)', $(PS5_PORT)), timeout=5); s.sendall(f); s.close(); print('[+] Envio concluido!')"

test: host-test

host-test:
	@mkdir -p $(BUILD)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc src/usb_controllers.c src/profiles.c src/util.c src/log.c tests/test_parsers.c -o $(BUILD)/test_parsers
	./$(BUILD)/test_parsers
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc tests/test_vpad_abi.c -o $(BUILD)/test_vpad_abi
	./$(BUILD)/test_vpad_abi
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc src/usb_controllers.c src/profiles.c src/util.c src/log.c tests/test_flow_sim.c -o $(BUILD)/test_flow_sim
	./$(BUILD)/test_flow_sim
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc src/bt_packets.c tests/test_bt_packets.c -o $(BUILD)/test_bt_packets
	./$(BUILD)/test_bt_packets
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc src/tcp_frames.c tests/test_tcp_frames.c -o $(BUILD)/test_tcp_frames
	./$(BUILD)/test_tcp_frames
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc src/tcp_stream.c src/tcp_frames.c tests/test_tcp_disabled.c -o $(BUILD)/test_tcp_disabled
	./$(BUILD)/test_tcp_disabled
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc -DOMNIPAD_ENABLE_TCP_DEBUG -DTCP_STREAM_TESTING src/tcp_stream.c src/tcp_frames.c tests/test_tcp_stream.c -o $(BUILD)/test_tcp_stream
	./$(BUILD)/test_tcp_stream
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc -pthread src/usb_lifecycle.c tests/test_usb_lifecycle.c -o $(BUILD)/test_usb_lifecycle
	./$(BUILD)/test_usb_lifecycle
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc -pthread src/http_core.c tests/test_http_core.c -o $(BUILD)/test_http_core
	./$(BUILD)/test_http_core
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc -pthread -DWEB_TESTING -DWEB_TOKEN_PATH=\"$(BUILD)/test_web.token\" -DWEB_STOP_FLAG=\"$(BUILD)/test_web.stop\" -DTEST_WEB_TOKEN_PATH=\"$(BUILD)/test_web.token\" -DTEST_WEB_STOP_FLAG=\"$(BUILD)/test_web.stop\" src/web.c src/http_core.c tests/test_web.c -o $(BUILD)/test_web
	./$(BUILD)/test_web
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SANITIZERS) -Isrc -pthread -fsyntax-only src/usb_hotplug.c src/tcp_stream.c src/bt_host.c src/bt_hci_usb.c

host-test-sanitize:
	$(MAKE) BUILD=build/sanitize HOST_SANITIZERS='-fsanitize=address,undefined -fno-omit-frame-pointer' host-test

clean-host:
	rm -rf $(BUILD)

clean:
	rm -rf $(BUILD)
