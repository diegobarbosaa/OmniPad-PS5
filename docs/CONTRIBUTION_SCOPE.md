# Integration scope: GameSir G7 HID/retention/PS shortcut (#19) + Nintendo full-report parser/init (#20)

## Hardware context and scope

I use a **PS5 Pro on firmware 10.20** with a GameSir G7 Pro and an **8BitDo Ultimate 2 Wireless (not the Ultimate 2C)**. These are focused contributions extracted from my personal integration build, rather than a proposal to maintain or redesign OmniPad. No Bluetooth stack, UI redesign, new Ultimate XInput/DInput implementation, or diagnostic payload is included.

Physical observations below come from my personal consolidated build; the exact extracted contribution branches have host-test and PS5 compilation coverage but have **not been separately flashed and physically retested**. Please treat these as draft contributions for review and community testing, not universal compatibility claims.

## Result and implementation


## PR #19 — GameSir G7 HID, per-slot retention, PS shortcut

* Decode the explicit GameSir G7 Pro HID identity `3537:1022` and G7 SE `3537:1082` rather than interpreting these reports as generic XInput. G7 SE parsing is covered on the host but is not physically validated by me.
* Configure stale-input retention from the USB driver through a simple per-slot parameter. Only G7 Pro opts into **20,000 ms** in this contribution; existing callers and other devices retain the **500 ms** default. The virtual layer does not inspect vendor names or VID/PID. Removal/reuse clears slot policy, and cached PS transitions do not refresh physical input freshness.
* **G7 Pro PS shortcut:** hold **View + Menu** together for **600 ms**. PS is then held while the combination remains down, and released when either button is released. Both short and long PS actions and release worked in my physical tests. Timeout/disconnection handling prevents indefinite stale holds; there are no repeated synthetic pulses. The combination is consumed while both buttons are down.
* The shortcut is deliberately enabled only for G7 Pro. The helper could be reused for other devices, but enabling it globally would change combination behavior and requires a deliberate upstream policy and hardware testing. Switch mode already has native Home-to-PS mapping; no new chord is enabled for all Switch Pro identities.

## Known issues — please read before using

**G7 Pro through its dongle still has intermittent D-pad failures:** rapid/repeated taps can be missed, and sometimes a held direction fails to start or reacts after another input. Earlier personal builds also exhibited stuck/repeating directions after release. The diagnostic B28 felt better but was not a functional fix. A short cable test appeared fluid without the issue; this is not long-duration validation.

The cause remains unconfirmed. In the sampled diagnostic trace, all recorded D-pad transitions reached the virtual state and successful InsertData calls; this does not prove every physical action reached USB or that every injected state was consumed by the console. The observed approximately-32-ms timing pattern is not proof of the dongle's complete polling rate. This PR does **not** claim to fix that defect. The controller can also appear as a keyboard plus gamepad; whether this is related remains under investigation.

Existing **#17** reports USB_FS packet loss and stale input with closely matching symptoms on other hardware. That work may be relevant; its transport changes have not been incorporated or validated on my G7. This contribution does not compete with or modify that transport proposal. The per-slot retention change overlaps the policy area discussed there and is intentionally presented for maintainer review.

For completeness, Ultimate 2 Wireless worked in my personal build through its dongle in Switch Pro emulation, but that protocol work is a separate contribution. Its normal XInput/DInput modes did not produce usable input in my tests.

## Validation and review boundaries

The extracted branch passed **8 available host suites** and PS5 SDK compilation in Docker. Tests cover GameSir reports, slot policy/lifecycle, maintained PS timing/release, watchdog behavior, the real USB-worker/virtual-update path with console calls stubbed, and the available parser/ABI/flow regressions. Six legacy test targets reference source files absent from the public upstream checkout; those are explicitly reported as SKIP, not passed.

Physical personal-build checks: G7 basic gameplay/sticks, retained input, and View+Menu short/long PS and release; dongle D-pad defect remains as above. DS4 was not physically tested during this work. No new compatibility claim is made for other controllers.

Base: upstream `b7682ef`. Commits separate the test prerequisite, GameSir HID, per-slot G7 retention and PS hold. A small common test-baseline commit is also present in the Nintendo draft; it is a shared prerequisite, not unrelated runtime behavior. No personal history, B28/B29 capture instrumentation, raw dumps, or ELFs are included.

## PR #20 — Nintendo full-report parser and USB initialization

Decode Nintendo full `0x30` input reports with the corrected layout, reject protocol responses instead of generic-HID fallback, and perform bounded reader-owned USB initialization/ACK handling. These changes affect **the Nintendo protocol path generally**, not an identifiable Ultimate-only driver. The parser keeps the **original upstream Y conversion**; no personal-build Y regression is presented as a new upstream fix.

The `CTRL_8BITDO_ADAPTER` route is not widened to forward new `0x30` reports. No Ultimate-specific VID/PID, experimental XInput/DInput parser, USB interface redesign, retained-input policy or PS chord is added here. No changes are made to DS4 behavior.

## Ultimate 2 Wireless: what I actually tested

On my PS5 Pro FW10.20, the Ultimate 2 Wireless **worked without observed problems during the tests performed**, through its **2.4-GHz dongle in Nintendo Switch Pro emulation**. Controls, left-stick orientation and the subsequently tested D-pad worked. This is limited personal-build evidence, not complete validation of every feature/game or this extracted branch.

To reproduce my connection mode: with the controller **off and out of the charging dock**, hold **Y**, then press **Home to turn it on while continuing to hold Y**, using the dongle. Mode availability depends on the controller/receiver firmware; this instruction does not require or suggest changing PS5 firmware. Reference: https://gist.github.com/superjamie/271f186774494a80a4df95afbf1bb77d .

**Normal XInput/DInput modes were not functional in my tests.** USB identification by name is not proof of usable input. Further investigation is pending; no support claim or speculative fix for those modes is included. Existing #18 covers a broader Bluetooth/Ultimate profile effort, which is separate from this dongle/Nintendo protocol work.

## Why no additional PS shortcut on the Ultimate

Switch emulation uses shared identity `057E:2009`; it does not reliably distinguish this Ultimate from other Switch Pro-compatible controllers. Nintendo Home is already mapped to PS in the parser. Adding View/Menu (Minus/Plus) as a general chord would affect every controller on that route and consume that combination. I left that policy to the maintainer rather than extending a hardware-specific feature globally. The G7-only maintained PS shortcut is in the separate GameSir draft.

## Known issues and validation boundaries

This draft does not fix the G7 dongle D-pad issue reported in the separate GameSir contribution. Ultimate working in Switch mode does not imply the same defect exists on its path.

The extracted branch passed **4 available host suites** and PS5 SDK compilation in Docker, including Nintendo report/response checks and exhaustive upstream-Y equivalence. Host tests do **not** exercise real Prospero USB requests, ACK timing or retries. The exact extracted branch and other genuine Switch Pro hardware still need physical testing. Six upstream targets with missing source files are explicitly SKIP, not passed. DS4 was not physically tested.

Base: upstream `b7682ef`. Parser and handshake remain separate commits for review/reversion; handshake explicitly depends on the corrected parser/response filtering. A small common test-baseline commit is also included in the GameSir draft. No personal branch history, experimental Ultimate modes, diagnostic B28/B29 logging or compiled ELFs are included. Broader protocol/architecture work is left to upstream, including the planned 1.1 work.

## Credits and transparency

Credit to **Essyekky32 (Essy), author of #16**, for the GameSir/Nintendo findings and code that informed these changes. Original project authors, notices and GPL-3.0 license are preserved.

Codex/AI assisted analysis, selective implementation, extraction, and automated tests. Physical testing was performed by me on the console; compilation and simulated host tests are not substitutes for hardware validation. No independent human code review is claimed. Further testing time is limited, and unresolved behavior is disclosed below.
