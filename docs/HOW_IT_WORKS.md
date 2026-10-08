# How OmniPad PS5 Works 📘
### End-to-End Architectural and Technical Guide

This document provides an in-depth breakdown of the internal mechanics of **OmniPad PS5**, tracing the data lifecycle from the moment a button is pressed on a physical controller (via USB cable, 2.4GHz wireless dongle, or Bluetooth) until the event reaches games and the ProsperoOS operating system as an official **DualSense**.

---

## 🗺️ Architectural Overview

The engine is organized into **6 core layers**:

```
 ┌─────────────────────────────────────────────────────────────┐
 │                      1. INPUT SOURCES                       │
 │   • 2.4GHz Dongles & USB Cables (Active in v1.0.4)          │
 │   • Console Internal Bluetooth (Roadmap for v1.1.0)         │
 │   • Opt-in Loopback TCP Debug Input (Port 9045)              │
 └──────────────────────────────┬──────────────────────────────┘
                                │
 ┌──────────────────────────────▼──────────────────────────────┐
 │              2. HARDWARE & DRIVER LAYER                     │
 │   • usb_hotplug.c (Dynamic Hotplug Monitor + Wake Packets)  │
 │   • bt_hci_usb.c (Dynamic /dev/ugen*.* Scanner - Roadmap)   │
 └──────────────────────────────┬──────────────────────────────┘
                                │
 ┌──────────────────────────────▼──────────────────────────────┐
 │             3. NORMALIZATION & PARSING                      │
 │   • profiles.c & usb_controllers.c                          │
 │   • Converts proprietary HID/XInput packets to pad_state_t  │
 └──────────────────────────────┬──────────────────────────────┘
                                │
 ┌──────────────────────────────▼──────────────────────────────┐
 │            4. VIRTUAL DUALSENSE SLOTS                       │
 │   • ps5_vpad.c (3 Decoupled Virtual Slots: Players 2, 3, 4) │
 │   • Formats exact 120-byte ScePadData kernel ABI structure  │
 └──────────────────────────────┬──────────────────────────────┘
                                │
 ┌──────────────────────────────▼──────────────────────────────┐
 │              5. PS5 INJECTION SUBSYSTEM                     │
 │   • shellui_inject.c (Elevated ptrace into SceShellUI)      │
 │   • libScePad (scePadVirtualDeviceInsertData @ 250Hz)       │
 │   • libSceMbus (sceMbusBindDeviceWithUserId)                │
 └──────────────────────────────┬──────────────────────────────┘
                                │
 ┌──────────────────────────────▼──────────────────────────────┐
 │                   6. GAMES & EMULATORS                      │
 │   The game reads the input as an official DualSense!        │
 └─────────────────────────────────────────────────────────────┘
```

---

## 🔬 Data Lifecycle Walkthrough

### Step 1: Capturing Raw Hardware Reports

#### A. Wired USB Cables & 2.4GHz Wireless Dongles
1. The `usb_hotplug.c` module continuously polls for device connections across the USB bus.
2. Upon detecting an insertion, it reads the Vendor ID (VID) and Product ID (PID) from the USB device descriptor.
3. If the controller requires a proprietary initialization handshake (e.g., **Nintendo Switch Pro** via USB-C or **DualShock 3** requiring magic wake packet `0xF4 0x42 0x03`), the engine transmits the exact initialization sequence.
4. A dedicated asynchronous thread polls the IN interrupt endpoint at **250 Hz** (~4ms latency).

#### B. Wireless Bluetooth (Console's Internal Radio — Roadmap / v1.1.0)
> ⚠️ **Roadmap Notice:** In **v1.0.4**, direct Bluetooth communication via the console's internal radio is intentionally dormant in `main.c` while the BLE SMP cryptographic handshake is being completed. Direct pairing through the console antenna is scheduled for **v1.1.0**. In v1.0.4, all wireless gaming is handled through physical USB adapters (e.g. 8BitDo or manufacturer 2.4GHz dongles).
1. The PS5 motherboard hosts an internal Bluetooth radio chip connected via an internal USB interface.
2. The `bt_hci_usb.c` module dynamically scans `/dev/ugen*.*` nodes, matching class `0xE0:0x01:0x01` (Wireless Bluetooth HCI).
3. The upcoming v1.1.0 engine will communicate over HCI/L2CAP and BLE SMP, decoding radio frames over the air.

---

### Step 2: Canonical Normalization (`pad_state_t`)

Different manufacturers arrange button bits, analog sticks, and triggers in incompatible binary formats:
- **DualShock 4:** 64-byte USB report with digital buttons packed into 4-bit nibbles.
- **Xbox (XInput & GIP):** 20-byte report for Xbox 360 / clones, and 18+ byte GIP reports (`0x20` / Guide `0x07`) for wired Xbox One / Series X|S.
- **Nintendo Switch Pro:** Compact 12-bit packed analog coordinates over 64-byte USB report after handshake.

The `profiles.c` and `usb_controllers.c` modules decode and normalize these reports into a uniform canonical structure:
- **Analog Sticks:** 0 to 255 (with 128 as the exact mechanical center).
- **Triggers (L2/R2):** 0 to 255 (0 = released, 255 = fully depressed).
- **Bitmask:** Uniform mapping across Cross, Circle, Square, Triangle, D-Pad, L1, R1, L3, R3, Options, Create/Share, Touchpad click, and PS button.

---

### Step 3: Exact 120-Byte `ScePadData` Kernel ABI

To be accepted by the ProsperoOS kernel, input data must strictly conform to the 120-byte structure defined by `libScePad.sprx`:

```c
typedef struct {
    uint32_t       buttons;             /* Button bitmask */
    uint8_t        lx, ly, rx, ry;      /* Analog stick positions */
    uint8_t        l2, r2;              /* Analog trigger depths */
    uint8_t        padding[2];
    float          orientation[4];      /* Quaternion orientation (x,y,z,w) */
    float          acceleration[3];     /* Accelerometer (g) */
    float          angularVelocity[3];  /* Gyroscope (rad/s) */
    ScePadTouchData touchData;          /* Touchpad finger tracking */
    uint8_t        connected;           /* 1 = connected, 0 = disconnected */
    uint64_t       timestamp;           /* Microsecond process clock */
    uint8_t        extensionUnitData[16];
    uint8_t        connectedCount;
    uint8_t        reserved[2];
    uint8_t        deviceUniqueDataLen;
    uint8_t        deviceUniqueData[12];
} ScePadData;
```

A static assertion (`_Static_assert(sizeof(PadData) == 120, "ABI mismatch")`) validates memory layout at build time.

---

### Step 4: Virtual Device Creation & MBus Binding

1. The engine registers a new virtual device with the kernel:
   ```c
   scePadVirtualDeviceAddDevice(param, VIRTUAL_DEVICE_DUALSENSE);
   ```
2. The ProsperoOS kernel assigns a unique 64-bit **DeviceId** and announces it via the kernel log:
   ```text
   SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x0000000100000002][type:1][subType:22]
   ```
3. OmniPad captures this event via `/dev/klog` or the debug redirector (port `3232`).
4. Once the `DeviceId` is identified, it is bound to the target user via `libSceMbus.sprx`:
   ```c
   sceMbusBindDeviceWithUserId(deviceId, userId);
   ```

---

### Step 5: Bypassing FW 13.60 Error `0x803B0006` (`shellui_inject.c`)

On modern firmwares like **13.60**, the operating system requires interactive user confirmation when attaching new virtual devices, otherwise failing with `0x803B0006`.

OmniPad bypasses this block safely:
1. Locates the running `SceShellUI` or `SceShellCore` process.
2. Attaches via `sys_ptrace` with elevated service credentials (`0x4800000000010003`).
3. Identifies an existing executable code cave in the `.text` segment of `libScePad.sprx` (respecting W^X memory protections).
4. Injects a synthesized momentary **PS button** press event.
5. The official Sony account selector opens on TV, allowing immediate assignment to your profile without workarounds or secondary account hacks.

---

### Step 6: 250Hz Real-Time Injection Loop

Once bound to a player slot, the main loop injects state packets at **250 Hz** (every 4ms):
```c
scePadVirtualDeviceInsertData(handle, &pad_data);
```
Games and emulators receive the inputs directly from the kernel, recognizing your Machenike dongle, 8BitDo adapter, or wired USB controller (Xbox, Switch Pro, DualShock) with the responsiveness of a native DualSense.

---

## 🌐 Web Dashboard (Port 8095)

An embedded zero-dependency HTTP server serves the dashboard at `127.0.0.1:8095` by default. A valid, permission-restricted `/data/anypad/web.token` explicitly enables LAN binding. In LAN mode, `GET /` and the read-only `GET /api/status` are public; log access and every `POST /api/*` endpoint require a bearer token. The server sends no CORS permission headers. See [NETWORK_SECURITY.md](NETWORK_SECURITY.md) for configuration.

The TCP test input is disabled in production builds. `make ps5 TCP_DEBUG=1` opts into a loopback-only development listener on port 9045; it claims only a free virtual slot and releases that slot when the client disconnects or sends an invalid/incomplete frame.
