# Controller Compatibility Guide

**OmniPad PS5 v1.0.4** is focused on high-speed **USB Wired & 2.4GHz Wireless Dongle** connections with zero lag (250Hz). Direct internal Bluetooth pairing is currently in active development for **v1.1.0**.

---

## 🔌 USB Controllers & 2.4GHz Wireless Adapters (Active in v1.0.4)

OmniPad PS5 is developed and maintained by a solo developer. 
- **Currently on the developer's physical test bench:** **Machenike G5 Pro Max SE** (tested with both its 2.4GHz USB Dongle and direct USB-C cable).
- **Verified by community testers on FW 13.60:** DualShock 4 (USB cable, `054c:05c4`), 8BitDo Ultimate 2C Wireless (2.4GHz dongle and USB-C cable, `2dc8:3106`).
- **Not yet physically tested on bench:** Xbox Series X\|S, Xbox One, Xbox 360, DualShock 3, Switch Pro.
- **How support was built:** Decoders and handshakes were engineered from official USB HID/XInput/GIP documentation, open-source references, and verified via automated test suites (`tests/test_parsers.c`).
- **Community Call for Help:** Because the developer is actively working to acquire real Xbox and PS4 controllers for physical verification, these profiles are currently classified as **EXPERIMENTAL**. If you test with physical hardware, your feedback, bug reports, and logs are deeply valued!

---

## 🔌 1. Active Compatibility Matrix (v1.0.4)

All peripherals in this table connect directly to any USB-A or USB-C port on the front or rear of the PS5:

| Controller / Device | Connection Type | Hardware Testing Status | Real-World Status & Technical Notes |
|---|---|:---:|---|
| **Machenike G5 Pro / G5 Pro Max SE** | 2.4GHz USB Dongle or USB-C Cable | 🟢 **Verified on PS5 Hardware** | Fully tested & verified in real 3-player gameplay on FW 13.60. Instant plug & play. |
| **ShanWan 2.4G Gamepad** | 2.4GHz USB Dongle (`1a34:f517`) | 🟢 **Verified on PS5 Hardware** | Dongle shared with Machenike; dispatches via native XInput report parser. |
| **Microsoft Xbox Series X\|S & Xbox One** | USB-C / Micro-USB Cable | 🟡 **Experimental (Spec-Implemented)** | Decodes GIP input reports (`0x20`) and Guide button (`0x07`). Requires data cable. *(Awaiting community validation)*. |
| **Microsoft Xbox 360 Controller** | Wired USB Cable (`045e:028e`) | 🟡 **Experimental (Spec-Implemented)** | Standard XInput report parser and periodic LED keepalive implemented. |
| **Sony DualShock 4** | Micro-USB Cable (`054c:05c4`/`09cc`) | 🟢 **Verified on PS5 Hardware (community)** | Verified with a CUH-ZCT1 (`054c:05c4`): 64-byte HID reports at 250Hz, buttons and sticks mapped correctly. `09cc` (CUH-ZCT2) not yet tested. *(Direct BT in v1.1.0)*. |
| **Sony DualShock 3 (PS3)** | Mini-USB Cable (`054c:0268`) | 🟡 **Experimental (Spec-Implemented)** | Magic wake packet (`0x03f4`) implemented. *(No Bluetooth)*. |
| **Nintendo Switch Pro Controller** | USB-C Cable (`057e:2009`) | 🟡 **Experimental (Spec-Implemented)** | 3-step USB activation handshake implemented. *(Direct BT in v1.1.0)*. |
| **8BitDo Wireless USB Adapter (v1 & v2)** | USB 2.4G Dongle (`2dc8:*`) | 🟡 **Experimental (Spec-Implemented)** | Bridges controllers into standard USB HID/XInput. |
| **8BitDo Ultimate 2C Wireless** | 2.4GHz USB Dongle or USB-C Cable (`2dc8:3106`) | 🟢 **Verified on PS5 Hardware (community)** | Briefly enumerates as a vendor HID device (`2dc8:3016`) and then as XInput (`2dc8:3106`). Sends no input until it gets the XInput player-LED command (`01 03 02`) on interrupt OUT endpoint `0x05`, and re-enumerates after ~10s without it. |
| **GameSir G7 Pro** | 2.4GHz Dongle or USB-C (`3537:1022`) | 🟡 **Author-tested (PR #19), not bench-verified here** | Dedicated HID parser, 20 s stale-input retention, View+Menu (600 ms) → PS shortcut. **Known issue:** intermittent D-pad misses through the dongle (cause unconfirmed). |
| **GameSir G7 SE** | USB (`3537:1082`) | 🟡 **Experimental (host-tested only)** | Dedicated HID parser (report ID `0x05`); not physically validated. |
| **Mad Catz Street Fighter IV Wireless FightPad (PS3 RF pad)** | Original USB receiver (`0738:8828`) | 🟡 **Experimental (issue #10 capture, host-tested)** | Dedicated 27-byte HID parser built from the reporter's capture (buttons, system, 4 cardinal D-pad values). Diagonals and pressure bytes were not captured; diagonals assume a standard hat. |
| **Nintendo Switch Pro / Switch-emulation dongles** | USB (`057e:2009`) | 🟡 **Experimental** | Full `0x30` report parser + bounded USB init/ACK handling (PR #20). An 8BitDo Ultimate 2 Wireless worked in this mode on the PR author's console (limited testing). |
| **Logitech Gamepads (F310, F710)** | USB Cable / 2.4G Receiver | 🟡 **Experimental (Spec-Implemented)** | Operates in either XInput or standard HID mode. |
| **Generic PC USB Gamepads** | Standard USB Cable | 🟡 **Experimental (Spec-Implemented)** | 10-byte standard HID descriptor normalization. |
| **Thrustmaster T598 Wheel** | USB Cable, base in PC mode (`044f:b696`) | 🟢 **Verified on PS5 Hardware** | Emulated as a DualSense so games without wheel support can be driven with it. See section below. |
| **Nintendo Wii U GameCube Adapter** | USB Cable (`057e:0337`) | 🟡 **Experimental (PR #22, not hardware-verified)** | 8-byte GC report parser (port 1) with GET_REPORT handshake; Z → L2 digital, C-stick → right stick. |

### 🏎️ Thrustmaster T598 (wheel as a controller)

Lets the T598 drive games that have no native wheel support. The layout was taken from the wheel's own HID descriptor and a live capture (input report `0x07`, 64 bytes).

**Tested games (real T598 on PS5):**

| Game | Platform | Result | Setup |
|---|---|---|---|
| *Star Wars: Galactic Racer* | PS5 native | ✅ Works | None |
| *Need for Speed: Underground 2* | PS2 via PS5SX2 emulator | ✅ Works | Remap buttons in PS5SX2 (see below) |

**NFS Underground 2 in PS5SX2:** the game accelerates with Cross and brakes with Square, so remap the controls in PS5SX2's controller settings to match the wheel:

| PS2 game button | Set to | On the T598 |
|---|---|---|
| Cross (accelerate) | R2 | Gas pedal |
| Square (brake) | L2 | Brake pedal |
| L2 (gear shift) | L1 | Left paddle |
| R2 (gear shift) | R1 | Right paddle |

| T598 control | DualSense input |
|---|---|
| Steering (16-bit) | Left stick X |
| Gas pedal (10-bit) | R2 (analog) |
| Brake pedal (10-bit) | L2 (analog) |
| Left / right paddle | L1 / R1 |
| L2 / R2 wheel buttons | Full L2 / full R2 |
| Triangle, Square, Cross, Circle, D-pad, Share, Options, PS, L3, R3 | Same buttons |
| P button | Touchpad click |
| Clutch, +, −, S | Not mapped |

- **Use PC mode.** In PS mode the console drives the wheel natively with a different protocol.
- **Steering deadzone compensation:** games ignore small stick movements near center, so as soon as the wheel leaves center the stick jumps to 40% (`T598_STEER_ANTIDEADZONE_PCT`). A small center zone (`T598_STEER_CENTER_DZ`, 96/65536) avoids jitter. Optional `T598_STEER_GAIN_PCT` makes steering more sensitive. All three are compile-time settings in `src/usb_controllers.h`.
- **No force feedback** is sent; the game believes it is talking to a gamepad.
- Vendor reports (`0x02`, `0x03`, `0x14`) are ignored instead of falling back to the generic HID parser.

---

## 🔍 2. Deep Dive: Microsoft Xbox Controllers (Honest Reality Check)

Because Xbox controllers are among the most popular third-party controllers in the community, here is an unambiguous, honest explanation of what works, what does not work, and why:

### ❌ What Does NOT Work in v1.0.4:
1. **Direct Wireless Bluetooth to the PS5:**
   - Modern Xbox One (Model 1708) and Xbox Series X\|S (Model 1914) controllers transmit over **Bluetooth Low Energy (BLE)** using Microsoft's implementation of the **Security Manager Protocol (SMP)** with AES-128 cryptographic pairing.
   - OmniPad v1.0.4 does **not** enable direct console Bluetooth communication yet. **Putting your Xbox controller into Bluetooth pairing mode and expecting the PS5 to find it without an adapter will NOT work.** This subsystem is in active development for **v1.1.0**.
2. **Official "Xbox Wireless Adapter for Windows" (Microsoft USB Dongle):**
   - The official Microsoft wireless dongle uses a proprietary 2.4GHz Wi-Fi Direct radio protocol requiring complex kernel drivers (such as `xone` / `mt7612u` on Linux).
   - This dongle is **NOT supported** by OmniPad or FreeBSD/ProsperoOS.

### ✅ What DOES Work in v1.0.4:
1. **Direct Wired USB-C / Micro-USB Cable:**
   - Plug the controller directly into any PS5 USB port using a **data-capable** USB cable.
   - The engine claims the GIP endpoints (`0x82` IN, `0x02` OUT), issues the initialization sequence (`0x05 0x20 0x00 0x01 0x00`), and begins decoding standard `0x20` input packets.
   - *Requirement:* The cable **must** have physical data lines. Charging-only cables (frequently shipped with cheap power banks or battery packs) will only power the LED and will not transmit input packets.
2. **Wireless via 8BitDo Wireless USB Adapter 2:**
   - Plug the 8BitDo Wireless USB Adapter 2 into the PS5.
   - Pair your Xbox controller directly to the 8BitDo adapter by holding the pairing button on both devices.
   - The 8BitDo adapter acts as an active hardware bridge, translating Xbox inputs into USB signals that OmniPad recognizes instantly.

---

## 📡 3. Direct Internal Bluetooth (Roadmap for v1.1.0)

Direct pairing to the PS5 motherboard's internal Bluetooth radio (`bt_hci_usb.c` / `bt_host.c`) without requiring any USB dongle or cable is under active development for **v1.1.0**:

| Controller | Protocol / Stack Requirement | Roadmap Target |
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
