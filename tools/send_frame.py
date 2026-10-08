#!/usr/bin/env python3
"""Send development controller input to OmniPad's loopback-only TCP test port.

The payload must be built with TCP_DEBUG=1. The debug listener is disabled in
production builds and binds to 127.0.0.1 when enabled.
"""

import sys
import socket
import struct
import time
import math

BUTTONS = {
    "cross":    1 << 14,
    "circle":   1 << 13,
    "square":   1 << 15,
    "triangle": 1 << 12,
    "l1":       1 << 10,
    "r1":       1 << 11,
    "l2":       1 << 8,
    "r2":       1 << 9,
    "options":  1 << 3,
    "share":    1 << 0,
    "ps":       1 << 16,
    "touchpad": 1 << 20,
    "up":       1 << 4,
    "right":    1 << 5,
    "down":     1 << 6,
    "left":     1 << 7,
}

def make_frame(btn_mask=0, lx=128, ly=128, rx=128, ry=128, l2=0, r2=0):
    # 16 bytes: uint32 buttons, uint8 lx, ly, rx, ry, l2, r2, 6 bytes reserved
    return struct.pack("<IBBBBBB6x", btn_mask, lx, ly, rx, ry, l2, r2)

def main():
    if len(sys.argv) < 3:
        print(f"Usage: python {sys.argv[0]} <HOST> <button|left_stick_left|left_stick_right|left_stick_up|left_stick_down> [duration_seconds]")
        print("HOST must be 127.0.0.1 on the machine running the development payload.")
        print("Available buttons:", ", ".join(BUTTONS.keys()))
        sys.exit(1)

    ps5_ip = sys.argv[1]
    cmd = sys.argv[2].lower()
    try:
        duration = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    except ValueError:
        print("Duration must be a number between 0 and 3600 seconds.")
        return 1
    if not math.isfinite(duration) or duration < 0 or duration > 3600:
        print("Duration must be between 0 and 3600 seconds.")
        return 1

    stick_commands = {
        "left_stick_left", "left_stick_right", "left_stick_up", "left_stick_down"
    }
    if cmd not in BUTTONS and cmd not in stick_commands:
        print(f"Unknown button or stick command: {cmd}")
        return 1

    mask = BUTTONS.get(cmd, 0)
    lx, ly, rx, ry = 128, 128, 128, 128

    if cmd == "left_stick_left":
        lx = 0
    elif cmd == "left_stick_right":
        lx = 255
    elif cmd == "left_stick_up":
        ly = 0
    elif cmd == "left_stick_down":
        ly = 255

    print(f"[*] Connecting to local debug port at {ps5_ip}:9045...")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(3.0)
            sock.connect((ps5_ip, 9045))

            frame = make_frame(mask, lx, ly, rx, ry)
            deadline = time.monotonic() + duration
            while True:
                sock.sendall(frame)
                if time.monotonic() >= deadline:
                    break
                time.sleep(0.02)
        print(f"[+] Sent command '{cmd}' for {duration}s!")
        print("[+] Debug slot released when the connection closed.")
    except Exception as e:
        print(f"[-] Error sending frame: {e}")
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
