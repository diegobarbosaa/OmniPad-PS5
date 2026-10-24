# Controller Compatibility Guide

**OmniPad PS5 v1.0.4** is focused on high-speed **USB Wired & 2.4GHz Wireless Dongle** connections with zero lag (250Hz). Direct internal Bluetooth pairing is currently in active development for **v1.1.0**.

---

## 🔌 USB Controllers & 2.4GHz Wireless Adapters (Active in v1.0.4)

Connect directly to any USB port on the PS5 (front or rear). The system detects devices dynamically via 250Hz Hotplug:

| Controller / Adapter | Mode / Identifier VID:PID | Status |
|---|---|---|
| **Machenike G5 Pro / G5 Pro Max SE** | 2.4GHz Dongle & Wired USB-C | ✅ Fully Supported |
| **Nintendo Switch Pro Controller (USB)** | USB-C Cable (`057e:2009`) with native handshake | ✅ Fully Supported |
| **Sony DualShock 4 (USB Wired)** | Micro-USB Cable (`054c:05c4` / `054c:09cc`) | ✅ Fully Supported |
| **Sony DualShock 3 (PS3)** | Mini-USB Cable (`054c:0268`) with wake magic packet | ✅ Fully Supported |
| **Xbox One / Series X\|S / 360 (USB)** | Native XInput (`045e:028e`, `045e:0b12`, etc.) | ✅ Fully Supported |
| **8BitDo Wireless USB Adapter (V1 & V2)** | USB 2.4G Dongle (`2dc8:310a`, `2dc8:310b`, etc.) | ✅ Fully Supported |
| **Nintendo Wii U GameCube Controller Adapter** | USB Cable (`057e:0337`) — wired pass-through for up to 4 GC controllers | ✅ Fully Supported |
| **EasySMX (X10, etc.)** | Switch mode & 2.4G Receiver | ✅ Fully Supported |
| **Logitech Gamepads (F310, F710)** | XInput / DirectInput mode | ✅ Fully Supported |
| **Generic PC USB Gamepads** | Standard HID Gamepad | ✅ Supported |

---

## 📡 Wireless Bluetooth Controllers via Console Antenna (Roadmap / Planned for v1.1.0)

> ⚠️ **Note on v1.0.4:** Direct pairing with the console's internal Bluetooth antenna without a USB dongle is currently under active development and scheduled for the **v1.1.0** release.
> In v1.0.4, please connect via **USB cable** or use a **wireless USB adapter/dongle** (such as the 8BitDo Wireless USB Adapter or the controller's bundled 2.4G receiver).

| Controller | Mode / Connection | Planned Status |
|---|---|---|
| **Sony DualShock 4** | Classic Bluetooth (Share + PS) | 🔄 In Development (v1.1.0) |
| **Sony DualSense / Edge** | Classic Bluetooth (Create + PS) | 🔄 In Development (v1.1.0) |
| **Xbox Wireless (One S / Series X\|S)** | Classic Bluetooth / BLE (SMP Crypto) | 🔄 In Development (v1.1.0) |
| **Nintendo Switch Pro Controller** | Classic Bluetooth (Sync button) | 🔄 In Development (v1.1.0) |
| **8BitDo Bluetooth (SN30 Pro, Pro 2)** | Bluetooth HID mode | 🔄 In Development (v1.1.0) |
| **Generic Bluetooth HID Gamepads** | Standard Bluetooth HID | 🔄 In Development (v1.1.0) |

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
| **Nintendo Wii U GameCube Controller Adapter** | USB Cable (`057e:0337`) — wired pass-through for up to 4 GC controllers | ✅ Fully Supported |
| **EasySMX (X10, etc.)** | Switch mode & 2.4G Receiver | ✅ Fully Supported |
| **Logitech Gamepads (F310, F710)** | XInput / DirectInput mode | ✅ Fully Supported |
| **Generic PC USB Gamepads** | Standard HID Gamepad | ✅ Supported |

---

## 👥 Multi-Player Support (Up to 4 Simultaneous Players)

The engine manages **3 independent virtual slots** (`Slot 1`, `Slot 2`, and `Slot 3`) representing **Players 2, 3, and 4**.
Working alongside your console's primary **physical DualSense** (Player 1), this enables **up to 4 simultaneous players** in local multiplayer games:
- Example: 1 Official DualSense (P1) + 1 Machenike G5 Pro (P2) + 1 Xbox Wireless (P3) + 1 Switch Pro (P4) all playing together on the same PS5.
