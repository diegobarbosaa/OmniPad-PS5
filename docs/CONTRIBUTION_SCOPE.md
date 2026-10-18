# Reader-owned Nintendo USB bring-up

Base/dependency: `contrib/switch-parser`. This is a private preparation branch; no PR or
public release is authorized. Target personal environment: PS5 Pro FW 10.20.

## Scope
Move Nintendo command sequence to the reader, use SET_REPORT 0x0280, observe 0x81 ACKs and bound retries. Parser/response filtering is an explicit prerequisite. No 8BitDo-specific forwarding or initialization is included.

## Evidence and risks
Transport is Prospero-only. Host parser suites cover response rejection but do not exercise real USB requests, ACK timing or retries; PS5 compilation and physical captures/testing are required. No claim of hardware validation of this extracted sequence is made. Broader interface/endpoint redesign is deferred to upstream.

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
