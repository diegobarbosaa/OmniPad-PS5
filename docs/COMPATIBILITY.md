# Controller Compatibility Guide 🎮

> **OmniPad PS5 v1.0.4** is an open-source low-level controller subsystem for jailbroken PlayStation 5 consoles (FW 7.00 – 13.60).
> 
> **Current Version Scope (v1.0.4):**
> High-speed **USB Wired Connections & Supported 2.4GHz Wireless USB Dongles** operating at 250Hz (~4ms polling).
> **Direct internal Bluetooth pairing through the console's internal antenna is actively in development for v1.1.0 and is NOT active in v1.0.4.**

---

## 🔬 Developer Hardware Testing Disclosure

OmniPad PS5 is developed and maintained by a solo developer. 
- **Currently on the developer's physical test bench:** **Machenike G5 Pro Max SE** (tested with both its 2.4GHz USB Dongle and direct USB-C cable).
- **Not yet physically tested on bench:** Xbox Series X\|S, Xbox One, Xbox 360, DualShock 4, DualShock 3, Switch Pro.
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
| **Sony DualShock 4** | Micro-USB Cable (`054c:05c4`/`09cc`) | 🟡 **Experimental (Spec-Implemented)** | 64-byte USB HID parser implemented. *(Direct BT in v1.1.0)*. |
| **Sony DualShock 3 (PS3)** | Mini-USB Cable (`054c:0268`) | 🟡 **Experimental (Spec-Implemented)** | Magic wake packet (`0x03f4`) implemented. *(No Bluetooth)*. |
| **Nintendo Switch Pro Controller** | USB-C Cable (`057e:2009`) | 🟡 **Experimental (Spec-Implemented)** | 3-step USB activation handshake implemented. *(Direct BT in v1.1.0)*. |
| **8BitDo Wireless USB Adapter (v1 & v2)** | USB 2.4G Dongle (`2dc8:*`) | 🟡 **Experimental (Spec-Implemented)** | Bridges controllers into standard USB HID/XInput. |
| **Logitech Gamepads (F310, F710)** | USB Cable / 2.4G Receiver | 🟡 **Experimental (Spec-Implemented)** | Operates in either XInput or standard HID mode. |
| **Generic PC USB Gamepads** | Standard USB Cable | 🟡 **Experimental (Spec-Implemented)** | 10-byte standard HID descriptor normalization. |
| **Thrustmaster T598 Wheel** | USB Cable, base in PC mode (`044f:b696`) | 🟢 **Verified on PS5 Hardware** | Emulated as a DualSense so games without wheel support can be driven with it. See section below. |

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
| **Sony DualShock 4** | Classic Bluetooth L2CAP (HID PSM `0x0011` / `0x0013`) | 🔄 In Development (v1.1.0) |
| **Sony DualSense / Edge** | Classic Bluetooth L2CAP (DualSense report `0x31`) | 🔄 In Development (v1.1.0) |
| **Nintendo Switch Pro** | Classic Bluetooth HID (SPI calibration & timers) | 🔄 In Development (v1.1.0) |
| **Xbox Wireless (One S / Series X\|S)** | BLE + SMP AES-128 Cryptographic Key Exchange | 🔄 In Development (v1.1.0) |
| **8BitDo Bluetooth (SN30 Pro, Pro 2)** | Standard Bluetooth HID profile | 🔄 In Development (v1.1.0) |
| **Generic Bluetooth Gamepads** | Standard Bluetooth HID | 🔄 In Development (v1.1.0) |

> 💡 **Why was Bluetooth deferred to v1.1.0?**
> The PS5's internal Bluetooth chip is also used by the operating system for system stability and sleep modes. Rushing direct HCI hijacking risked controller desyncs and console sleep panics. The team prioritized delivering a 100% rock-solid 250Hz USB Hotplug engine and the FW 13.60 profile bypass first.

---

## 👥 4. Multi-Player Architecture (Up to 4 Players)

OmniPad creates and manages **3 independent virtual DualSense slots** in the ProsperoOS kernel (`libScePad`):

| Player | Controller Hardware | Managed By |
|---|---|---|
| **Player 1** | Official Physical DualSense | Native PS5 Operating System |
| **Player 2** | Supported USB Controller / Dongle (Slot 1) | OmniPad PS5 Virtual Device Pool |
| **Player 3** | Supported USB Controller / Dongle (Slot 2) | OmniPad PS5 Virtual Device Pool |
| **Player 4** | Supported USB Controller / Dongle (Slot 3) | OmniPad PS5 Virtual Device Pool |

**Example 4-Player Local Setup:**
- **Player 1:** Official DualSense (Primary User)
- **Player 2:** Machenike G5 Pro (via bundled 2.4G USB Dongle)
- **Player 3:** Xbox Series X controller (connected via wired USB-C cable or 8BitDo Adapter)
- **Player 4:** Nintendo Switch Pro controller (connected via wired USB-C cable)

---

## ⚙️ 5. DualSense Feature Parity & Limitations

When non-DualSense controllers are emulated as virtual DualSense devices, the following capabilities apply:

| Feature | Support in v1.0.4 | Notes |
|---|---|---|
| **Digital Buttons & D-Pad** | ✅ Fully Supported | 100% mapped (Cross, Circle, Square, Triangle, L1, R1, L3, R3, Options, Share). |
| **Analog Joysticks** | ✅ Fully Supported | Normalized to 0–255 with absolute center at 128 (zero drift). |
| **Analog Triggers (L2 / R2)** | ✅ Fully Supported | Full 0–255 analog depth with digital threshold click. |
| **PS / Home Button** | ✅ Fully Supported | Opens the native profile selector and control center. |
| **Touchpad Click** | ✅ Fully Supported | Mapped to open maps and in-game menu functions. |
| **Multi-touch Touchpad Surface** | ❌ Not Supported | Non-Sony controllers lack a capacitive touch surface. |
| **Adaptive Triggers (Motor Force)** | ❌ Not Supported | Non-DualSense controllers do not have internal resistance motors. |
| **Advanced DualSense Haptics** | ❌ Not Supported | Standard rumble is passed through where hardware permits. |
| **Gyroscope / Accelerometer (IMU)** | ❌ Neutral Only | Fixed to identity quaternion in v1.0.4. (Roadmap for motion-capable controllers). |
