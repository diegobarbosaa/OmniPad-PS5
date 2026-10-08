# Technical Guide: Operation on PS5 Firmware 13.60 (Relapse Exploit)

This document details the internal mechanics, architecture updates, and low-level solutions implemented in **OmniPad PS5** to run reliably on modern PlayStation 5 firmwares, specifically up to **FW 13.60**.

---

## 1. The Firmware 13.60 Jailbreak Landscape

With the release of the **Relapse** exploit chain, PS5 jailbreak support spans from **FW 7.00 through 13.60**, encompassing the original PS5 Fat, Slim (CFI-2000), and Pro (CFI-7000) revisions.

The exploit operates in two stages:
1. **WebKit Sandbox Escape:** Memory corruption in JavaScriptCore (JSC) via Structured Clone type confusion.
2. **Kernel Exploitation:** Race condition Use-After-Free in the `aio_multi_wait` syscall, achieving arbitrary kernel read/write in ProsperoOS (FreeBSD kernel).

Once the environment is active, the standard ELF loader / payload manager (listening on port **9021** or etaHEN) becomes available to execute native payloads.

---

## 2. Limitations in Earlier Projects & How OmniPad Solves Them

Pioneering projects were developed and tested almost exclusively on legacy firmwares (such as 10.01 on Fat consoles), presenting critical bottlenecks on modern revisions:

### A. Static Bus Addressing (`/dev/ugen0.2`)
* **The Problem:** On early Fat models, the internal Bluetooth radio chip consistently appeared on `/dev/ugen0.2`. On PS5 Slim (CFI-2000), PS5 Pro (CFI-7000), or newer firmware bus topologies up to 13.60, the controller radio relocates to nodes like `/dev/ugen0.3` or `/dev/ugen1.2`.
* **OmniPad's Solution:** A **dynamic USB descriptor scanner**. It iterates over all `/dev/ugen*.*` device nodes, inspecting class descriptors (`0xE0` - Wireless Controller, `0x01` - RF, `0x01` - Bluetooth) to detect the active interrupt and ACL endpoints automatically.

### B. Error `0x803B0006` (Assignment Screen Pending)
* **The Problem:** On modern firmwares, calling `scePadVirtualDeviceAddDevice(param, 3)` directly returns error `0x803B0006`. The operating system blocks the virtual controller pending user profile confirmation on the console's graphical interface (ShellUI).
* **OmniPad's Solution:** Non-destructive `SceShellUI` code cave interception:
  1. The payload attaches to `SceShellUI` or `SceShellCore` via elevated credentials (`authid` `0x4800000000010003` and full `0xFF` capabilities).
  2. It locates an executable *code cave* in the `.text` segment of loaded system libraries (`libScePad.sprx` or `libSceMbus.sprx`), strictly respecting W^X protections.
  3. It synthesizes a momentary **PS button** press event.
  4. This smoothly invokes the official PS5 profile selection dialog on screen, allowing immediate assignment to your primary profile without workarounds or secondary account hacks.

### C. USB Dongle & Wired Cable Support
* **The Problem:** Earlier payloads exclusively targeted Bluetooth. If a battery died or a player wanted to connect a 2.4GHz wireless dongle (e.g. Machenike G5 Pro Max SE, 8BitDo) or a wired USB controller (Xbox, Switch Pro, DS3), the payload could not recognize it.
* **OmniPad's Solution:** A dynamic 250Hz USB Hotplug engine. In **v1.0.4**, 2.4GHz wireless USB dongles (Machenike, 8BitDo) and wired USB controllers (Switch Pro, DualShock 4, DualShock 3, Xbox, PC HID) operate at 4ms latency across independent virtual slots, with direct console Bluetooth pairing scheduled for **v1.1.0**.

---

## 3. Communication & Privilege Architecture

```text
[ Controller ] ─── (USB Cable / 2.4G USB Dongle - Active v1.0.4) ────┐
                                                                      ├───► [ OmniPad PS5 Core ]
[ Controller ] ─── (Console Internal Bluetooth - Roadmap v1.1.0) ────┘           │
                                                             │ 1. ucred privilege elevation
                                                             │ 2. ScePad Virtual Device creation (Type 3)
                                                             │ 3. Kernel log (/dev/klog) DeviceId matching
                                                             │ 4. ShellUI code cave injection (PS Button)
                                                             ▼
                                                     [ PlayStation 5 OS ]
                                               (Recognized as Native DualSense)
                                                             │
                                                             ▼
                                                   [ Games & Emulators ]
```

---

## 4. Official Payload Loading Pipeline on FW 13.60

For maximum stability on **Firmware 13.60 (Relapse exploit)**, **OmniPad PS5** is designed to work in synergy with the established payload chain:

### Recommended Payload Execution Order:
1. **1st: `kstuff-1.13-fpkg-dr-test5.elf`**
   - Enables kernel FPKG support and applies ucred privilege patches.
   - Initializes the Drakmor Debug Redirector (**dr-test5**), exposing the kernel log stream on port `3232` and `/dev/klog`.
2. **2nd: `shadowmountplus v1.7 Beta 4.elf`**
   - High-speed background auto-mounter for PFS containers, exFAT, and game packages.
   - Mounts game access points under `/user/app/`.
3. **3rd: `OmniPad-PS5.elf`**
   - Initializes virtual DualSense controllers directly in the kernel (`libScePad` / MBus).
   - Handles profile assignment and error `0x803B0006` bypass.
   - Starts the Web Dashboard on port **8095**, bound to loopback unless authenticated LAN mode is explicitly configured.
   - The TCP debug input on port **9045** is disabled by default and is available only in opt-in local development builds.

### Conflict Prevention & Kernel Compatibility:
- **USB Storage Isolation (ShadowMountPlus):** Mass storage drives (external SSDs and flash drives containing FPKG games mounted by ShadowMountPlus) use USB class `0x08`. OmniPad immediately identifies and discards mass storage nodes, preventing interference with external game drives.
- **Kernel Log Ring-Buffer Drain (kstuff-1.13):** The klog server on port 3232 maintains an event buffer. OmniPad performs a non-blocking backlog drain right before instantiating a virtual pad, ensuring the 64-bit `DeviceId` captured accurately belongs to the newly added controller.
- **Privilege Resilience:** If `kstuff-1.13` has already granted root credentials (`uid 0`), OmniPad detects the existing permissions and skips redundant re-elevation, preventing kernel panics.
- **Multi-Player Support:** OmniPad provides **3 virtual DualSense slots** (Players 2, 3, and 4) which seamlessly join the **1 physical DualSense** (Player 1) for a complete 4-player local gaming experience.
