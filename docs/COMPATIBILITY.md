# Controller Compatibility Guide

**OmniPad PS5** combines native wireless Bluetooth support and high-speed USB / 2.4GHz wireless dongle connections into a single unified driver.

---

## 🎮 Wireless Bluetooth Controllers (PS5 Internal Bluetooth)

Direct pairing with the console's internal Bluetooth antenna (no PC, external dongle, or proxy needed):

| Controller | Mode / Connection | Tested Versions | Status |
|---|---|---|---|
| **Sony DualShock 4** | Classic Bluetooth (Share + PS) | CUH-ZCT1, CUH-ZCT2 | ✅ Fully Supported |
| **Sony DualSense / Edge** | Classic Bluetooth (Create + PS) | CFI-ZCT1W | ✅ Supported |
| **Xbox Wireless (One S / Series X\|S)** | Classic Bluetooth / BLE | Models 1708, 1914, Elite 2 | ✅ Supported (SMP Crypto) |
| **Nintendo Switch Pro Controller** | Classic Bluetooth (Sync button) | HAC-013 & clones | ✅ Supported |
| **Nintendo Switch Online (NES, SNES, N64, MD)** | Classic Bluetooth | All official models | ✅ Supported |
| **8BitDo Bluetooth (SN30 Pro, Pro 2, Ultimate)** | XInput / Switch / DInput mode | Latest firmwares | ✅ Supported |
| **GameSir (G3s, G4s, T1s, T2a)** | Bluetooth HID | All models | ✅ Supported |
| **Generic Bluetooth HID Gamepads** | Standard Bluetooth HID | Android/PC gamepads | ✅ Supported |

---

## 🔌 USB Controllers & 2.4GHz Wireless Adapters (Direct USB Port)

Connect directly to any USB port on the PS5 (front or rear). The system detects devices dynamically via 250Hz Hotplug:

| Controller / Adapter | Mode / Identifier VID:PID | Status |
|---|---|---|
| **Machenike G5 Pro / G5 Pro Max SE** | 2.4GHz Dongle & Wired USB-C | ✅ Fully Supported |
| **Nintendo Switch Pro Controller (USB)** | USB-C Cable (`057e:2009`) with native handshake | ✅ Fully Supported |
| **Sony DualShock 4 (USB Wired)** | Micro-USB Cable (`054c:05c4` / `054c:09cc`) | ✅ Fully Supported |
| **Sony DualShock 3 (PS3)** | Mini-USB Cable (`054c:0268`) with wake magic packet | ✅ Fully Supported |
| **Xbox One / Series X\|S / 360 (USB)** | Native XInput (`045e:028e`, `045e:0b12`, etc.) | ✅ Fully Supported |
| **8BitDo Wireless USB Adapter (V1 & V2)** | USB 2.4G Dongle (`2dc8:310a`, `2dc8:310b`, etc.) | ✅ Fully Supported |
| **EasySMX (X10, etc.)** | Switch mode & 2.4G Receiver | ✅ Fully Supported |
| **Logitech Gamepads (F310, F710)** | XInput / DirectInput mode | ✅ Fully Supported |
| **Generic PC USB Gamepads** | Standard HID Gamepad | ✅ Supported |

---

## 👥 Multi-Player Support (Up to 4 Simultaneous Players)

The engine manages **3 independent virtual slots** (`Slot 1`, `Slot 2`, and `Slot 3`) representing **Players 2, 3, and 4**.
Working alongside your console's primary **physical DualSense** (Player 1), this enables **up to 4 simultaneous players** in local multiplayer games:
- Example: 1 Official DualSense (P1) + 1 Machenike G5 Pro (P2) + 1 Xbox Wireless (P3) + 1 Switch Pro (P4) all playing together on the same PS5.
