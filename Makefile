# OmniPad PS5
# Universal Controller Engine & Web Control Center for PS5 (FW 7.00 - 13.60)
# Combines Bluetooth (AnyPad-PS5) + USB Hotplug (Ghostcontrol / PoorDS4) + ShellUI Fixes (YetAnotherControllerEnabler)

PS5_HOST ?= 192.168.1.50
PS5_PORT ?= 9021

BUILD := build

.PHONY: all ps5 send clean test host-test

all: ps5

ps5:
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK is undefined. Please export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk)
endif
	$(MAKE) -f ps5.mk

send: ps5
	@echo "[*] Sending payload to PS5 $(PS5_HOST):$(PS5_PORT)..."
	nc -w 5 $(PS5_HOST) $(PS5_PORT) < dist/OmniPad-PS5-*.elf || \
	python3 -c "import socket, glob; f=open(glob.glob('dist/OmniPad-PS5-*.elf')[0],'rb').read(); s=socket.create_connection(('$(PS5_HOST)', $(PS5_PORT)), timeout=5); s.sendall(f); s.close(); print('[+] Envio concluido!')"

test: host-test

host-test:
	@mkdir -p $(BUILD)
	clang -O2 -Isrc src/usb_controllers.c src/profiles.c src/util.c src/log.c tests/test_parsers.c -o $(BUILD)/test_parsers
	./$(BUILD)/test_parsers
	clang -O2 -Isrc tests/test_vpad_abi.c -o $(BUILD)/test_vpad_abi
	./$(BUILD)/test_vpad_abi
	clang -O2 -Isrc src/usb_controllers.c src/profiles.c src/util.c src/log.c tests/test_flow_sim.c -o $(BUILD)/test_flow_sim
	./$(BUILD)/test_flow_sim

clean:
	rm -rf $(BUILD) dist
