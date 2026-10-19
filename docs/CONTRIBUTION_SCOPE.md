# Nintendo full-report parser and USB initialization; Ultimate 2 Switch-mode test report

## Hardware context and scope

I use a **PS5 Pro on firmware 10.20** with a GameSir G7 Pro and an **8BitDo Ultimate 2 Wireless (not the Ultimate 2C)**. These are focused contributions extracted from my personal integration build, rather than a proposal to maintain or redesign OmniPad. No Bluetooth stack, UI redesign, new Ultimate XInput/DInput implementation, or diagnostic payload is included.

Physical observations below come from my personal consolidated build; the exact extracted contribution branches have host-test and PS5 compilation coverage but have **not been separately flashed and physically retested**. Please treat these as draft contributions for review and community testing, not universal compatibility claims.

## Result and implementation

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
