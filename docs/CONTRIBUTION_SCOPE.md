# G7 Pro retention with generic per-slot storage

Base/dependency: `contrib/gamesir-hid`. This is a private preparation branch; no PR or
public release is authorized. Target personal environment: PS5 Pro FW 10.20.

## Scope
A USB-selected 20000 ms bound ONLY for G7 Pro. Existing add callers and all other types, including G7 SE, keep 500 ms. Virtual code uses stored configuration, not model/name checks. Removal/reuse resets; same-device recycle preserves configuration but clears state/age.

## Evidence and risks
Twenty-second retention was physically tested in an earlier personal build; this architectural extraction still needs console regression testing. Silent radio loss may retain input until the bound. Lifecycle tests cover isolation, reuse, invalid/default configuration, deferred removal and recycle.

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
