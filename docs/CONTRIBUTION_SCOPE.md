# GameSir G7 HID, per-slot retention and maintained PS shortcut (known dongle D-pad issue)

## Hardware context and scope

I use a **PS5 Pro on firmware 10.20** with a GameSir G7 Pro and an **8BitDo Ultimate 2 Wireless (not the Ultimate 2C)**. These are focused contributions extracted from my personal integration build, rather than a proposal to maintain or redesign OmniPad. No Bluetooth stack, UI redesign, new Ultimate XInput/DInput implementation, or diagnostic payload is included.

Physical observations below come from my personal consolidated build; the exact extracted contribution branches have host-test and PS5 compilation coverage but have **not been separately flashed and physically retested**. Please treat these as draft contributions for review and community testing, not universal compatibility claims.

## Result and implementation

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

## Credits and transparency

Credit to **Essyekky32 (Essy), author of #16**, for the GameSir/Nintendo findings and code that informed these changes. Original project authors, notices and GPL-3.0 license are preserved.

Codex/AI assisted analysis, selective implementation, extraction, and automated tests. Physical testing was performed by me on the console; compilation and simulated host tests are not substitutes for hardware validation. No independent human code review is claimed. Further testing time is limited, and unresolved behavior is disclosed below.
