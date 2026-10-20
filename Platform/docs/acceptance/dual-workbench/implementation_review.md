# P0-DUAL-WORKBENCH implementation checkpoint

Date: 2026-10-20. Base: `ec311c1906789082982eb89b7e5b8bb14ff243e1`.
Branch: `feature/dual-workbench`. This is **incomplete implementation**, not
product acceptance, a development freeze, or permission to deploy.

## Implemented

- ADR-0013 records the user's explicit authorization to supersede historical
  volatile-only control-plane restrictions, without changing simulation time.
- `catalog.py`: atomic admission of one or two commands; validate all before
  publishing entries; sequential execution under the existing single Worker
  owner; cancel only queued members; stop queued work on shutdown. An immediate
  shutdown also prevents a task allocated but not yet entered from spawning.
- `receiver_sample_mixer.hpp`: bounded, deterministic complex-sample summation
  for already aligned inputs and one receiver noise slice; phase cancellation,
  partial overlap, tails, duplicate identity, clock and overflow validation.
  It is NOT yet connected to the production receiver. Packet-local baseband
  outputs cannot be summed without explicit oscillator/time conversion.
- Frontend: explicit geometric/recorded family buttons; resource selector
  restricted to the current family; retain existing independent draft buffers,
  save/cancel switch behavior and historical draft identity precedence.

## Verification actually executed

- Frontend: 33 files / 282 tests passed; TypeScript/Vite production build passed.
  Existing >500 kB bundle warning retained. These are fixture-backed component
  tests, **not real-browser/Worker U01–U25 acceptance**.
- Initial batch/API tests: 17 passed; additional immediate-shutdown test added
  afterward and included in the subsequent CMake backend regression.
- New mixer compiled with C++23, `-Wall -Wextra -Werror`, and executed; Debug
  CMake target also passed (assertions enabled).
- Backend tests use this worktree's newly compiled authority adapters in
  `/tmp/dual-workbench-build-off`, with the repository offline dependency lock.
- The initial ad-hoc backend run had 338 passed / 5 failed / 6 skipped: five
  failures were missing CMake-supplied executable environment variables. After
  compiling this worktree's adapters, the CMake backend test passed. The early
  sandbox API run was interrupted after stalling; normal-permission rerun passed.
- No ON build, official UWA numerical comparison, multi-Tx production run,
  paired production run, or real-browser screenshot has been claimed.

## Remaining blocking work

1. Common immutable workload generation and A/B freeze; durable idempotent
   RunGroup service/API, archives, restart/export and frontend result views.
2. v5 resource resolution, per-receiver noise, all physical fan-out links,
   directed-link binding validation and frozen assumption manifests.
3. Common oscillator/sample-time mapping, actual replay signal/noise separation,
   causal receiver integration, real mixed-waveform decoding and provenance.
4. Editable logical network, global pool/overrides, per-node configuration and
   C proxy result presentation wired to the production backend.
5. All supplied U01–U25/P01–P18/R01–R12 gates; real datasets and official replay;
   old trace equivalence; ON/OFF regressions; performance limits; viewport proof.

## Protection

Main and research branches are untouched. No service activation, restart,
asset edits or permanent additional service. D3 remains NO-GO. Earlier observed
live release: `OSDM-UWA-INTEGRATION-20261016-UI090d689`, source
`23b27dcbf42cc6748fe6170810db72e7c9c36b44`, frontend `index-eZZApcsW.js`.
Logical node positions must never influence CIR/delay/gain. Missing physical
fan-out links are configuration errors, not invented NoArrival outcomes.

Overall acceptance: **P0_DUAL_WORKBENCH_BLOCKED** (integration and required
evidence incomplete; not a negative verdict on physical feasibility).
