# Available upstream test baseline

Base/dependency: `b7682ef`. This is a private preparation branch; no PR or
public release is authorized. Target personal environment: PS5 Pro FW 10.20.

## Scope
Keep existing available parser/ABI/flow tests. Missing source targets remain conditional and emit explicit SKIP lines; no test is represented as passing when absent. Keep C test sources trackable. Guard a Prospero-only log field for host compilation.

## Evidence and risks
Six upstream tests are unavailable in the published tree; this is a documented coverage gap. No USB runtime behavior changes on PS5.

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
