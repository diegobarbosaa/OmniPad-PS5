# Nintendo full-report layout and response filtering

Base/dependency: `contrib/test-baseline`. This is a private preparation branch; no PR or
public release is authorized. Target personal environment: PS5 Pro FW 10.20.

## Scope
Nintendo 0x30 button/shoulder/navigation layout, report-ID rejection and no generic fallback for Nintendo. Preserve upstream Y conversion exactly. No handshake, GameSir or Ultimate-specific parser; no new 0x30 forwarding for CTRL_8BITDO_ADAPTER.

## Evidence and risks
Affects every control classified as Nintendo, not only Ultimate emulation. Essy reports external physical Nintendo testing on 13.42; user only observed limited Ultimate Switch input on 10.20 in a combined build. Extracted branch needs physical validation. Exhaustive axis equivalence is a regression check, not a new upstream Y fix.

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
