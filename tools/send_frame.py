#!/usr/bin/env python3
"""
send_frame.py - Send test controller inputs over LAN to OmniPad PS5
Listens on TCP port 9045
"""

import sys
import socket
import struct
import time

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
        print(f"Usage: python {sys.argv[0]} <PS5_IP> <button|lx|ly|rx|ry> [duration_seconds]")
        print("Available buttons:", ", ".join(BUTTONS.keys()))
        sys.exit(1)

    ps5_ip = sys.argv[1]
    cmd = sys.argv[2].lower()
    duration = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0

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

    print(f"[*] Connecting to {ps5_ip}:9045...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3.0)
        s.connect((ps5_ip, 9045))
        
        frame = make_frame(mask, lx, ly, rx, ry)
        s.sendall(frame)
        print(f"[+] Sent command '{cmd}' for {duration}s!")
        time.sleep(duration)

        # Release frame
        s_rel = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s_rel.connect((ps5_ip, 9045))
        s_rel.sendall(make_frame(0, 128, 128, 128, 128))
        s_rel.close()
        s.close()
        print("[+] Button released successfully.")
    except Exception as e:
        print(f"[-] Error sending frame: {e}")

if __name__ == "__main__":
    main()
