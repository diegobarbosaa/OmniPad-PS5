# Changelog

## v1.1.0 (Build 24) — community pre-release

Built on top of upstream v1.0.4 (Build 19). It combines the pull requests that were open
upstream, plus fixes from their review threads. Every item lists who did it and how far it
has been tested.

"Console-tested" means someone ran it on a PS5. "Host-tested" means only the unit tests in
`tests/` cover it.

### USB transport
- **Xbox One / Series GIP protocol and reliable USB_FS IN transfers** — PR #17 (eIBart0 / gabrielskaminski), fixes #12.
  One IN transfer stays armed with no timeout and is re-armed only after it completes. USB
  slots are event-driven, with no 500 ms auto-neutral. *Console-tested* with a wired Xbox
  Series controller. The same approach also fixed dropped inputs on an 8BitDo Ultimate 2C
  dongle (jpemendonca) and on a Turtle Beach Rematch Advanced (BluntEXE); both tested their
  own equivalent implementations.
- **IN reports no longer lost while an OUT packet is sent** — from BluntEXE's review of PR #17.
  An IN completion that arrived while an ACK, LED or rumble packet was going out was taken as
  the OUT completion. The input was dropped and the IN endpoint was never re-armed. *Host-reviewed only.*
- **Licensed GIP pads detected by interface descriptor** — from BluntEXE's review of PR #17.
  Xbox mode used to be enabled only for Microsoft's VID. Pads such as the Turtle Beach Rematch
  (`0e6f:034b`) went through the XInput parser and produced phantom input from GIP frames (for
  example the Guide release). *Not console-tested.*
- **USB worker tick**: 250 µs for GIP, 4 ms (~250 Hz) for everything else.

### Controllers
- **8BitDo Ultimate 2C (`2dc8:3106`)**: XInput player LED sent on the OUT endpoint — PR #21 (jpemendonca). *Console-tested* on FW 13.60.
- **GameSir G7 Pro / G7 SE**: HID parser, per-slot retention, View+Menu → PS shortcut — PR #19 (SacaSoh). *Author-tested* on FW 10.20. Known issue: intermittent D-pad over the G7 Pro dongle.
- **Nintendo Switch Pro / Switch-mode dongles**: full `0x30` report parser and USB init — PR #20 (SacaSoh). An 8BitDo Ultimate 2 in Switch mode worked on the author's console.
- **Thrustmaster T598 wheel (`044f:b696`)** as a DualSense — PR #23 (igorgump). *Console-tested*.
- **Mad Catz SF IV FightPad (`0738:8828`)**: parser built from the capture in issue #10. *Host-tested.*
- **Nintendo Wii U GameCube adapter (`057e:0337`)**: PR #22 (Jinx9110). The parser was rewritten to the adapter's real protocol (start command `0x13`, 37-byte `0x21` reports, port 1 only). *Host-tested, experimental.*

### Bluetooth, haptics, dashboard
- **AnyPad Bluetooth stack, DualSense haptics as vibration, 8BitDo Ultimate 2 BT profile, button remapping, per-controller settings, single-page dashboard with "Pair controller"** — PR #18 (BearTS, ported from AnyPad). **Bluetooth is not validated on a console yet: experimental.**

### Build
- GitHub Actions workflow: host tests plus a PS5 payload build with the repo's Dockerfile (PS5 Payload SDK v0.43).

### Not done yet
- Controllers that still need captures or descriptors: #2, #3, #4, #5, #6, #7, #8, #11, #13, #14, #24.
- FW 4.00 (#25) is outside the supported range (7.00 – 13.60).
