# G7 Pro maintained View+Menu PS shortcut

Base/dependency: `contrib/g7-retention`. This is a private preparation branch; no PR or
public release is authorized. Target personal environment: PS5 Pro FW 10.20.

## Scope
G7 Pro only: 600 ms delay, held PS until a button release, no repeating synthetic keepalive, timeout block-until-release. Level-update API avoids an independent remote UP; original update callers and profile activation remain. No Ultimate pulse machinery or Switch changes are imported.

## Evidence and risks
Held PS remains physically untested on 10.20. Fresh input after READY/recycle is required. Host tests simulate USB changes, silence, concurrent input and release through the real reader/vpad code, but do not exercise SceShellUI or real USB.

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
