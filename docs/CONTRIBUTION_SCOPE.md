# GameSir HID identification and decoding

Base/dependency: `contrib/test-baseline`. This is a private preparation branch; no PR or
public release is authorized. Target personal environment: PS5 Pro FW 10.20.

## Scope
Only exact 3537:1022 and 3537:1082 identities, nine-byte G7 Pro and prefixed G7 SE reports, and invalid-report fallback protection. No watchdog, PS shortcut, Switch or Ultimate changes.

## Evidence and risks
G7 Pro worked in an earlier user build on 10.20; G7 SE relies on Essy external evidence and synthetic tests. Unknown GameSir PIDs keep upstream classification; malformed reports must not become generic phantom input.

The user physically tested G7 Pro and 20-second retention only in an earlier
personal build. The combined B26, new architecture and maintained PS still need
physical testing. Ultimate normal modes remain non-functional in user tests;
no full compatibility claim is made. DS4 is not physically tested this round.

GameSir/Nintendo findings and adapted code are credited to Essyekky32 (Essy),
author of issue #16: https://github.com/diegobarbosaa/OmniPad-PS5/issues/16 .
Essy's external report concerns FW 13.42; it is distinct from the user's 10.20
evidence. Upstream authors, credits and GPL license are preserved.

Codex/AI assisted analysis, selective extraction, implementation and automated
tests. Host simulation and compilation are not physical console validation.
No independent human code review is claimed. Tests and final results for each
branch are recorded in the personal integration documentation.
