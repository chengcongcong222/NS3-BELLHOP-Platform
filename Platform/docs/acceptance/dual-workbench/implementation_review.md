# P0-DUAL-WORKBENCH implementation checkpoint

Date: 2026-10-20. Base: `ec311c1906789082982eb89b7e5b8bb14ff243e1`.
Branch: `feature/dual-workbench`. This is **incomplete implementation**, not
product acceptance, a development freeze, or permission to deploy.

## First checkpoint (`70afcc2`)

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

## First-checkpoint verification actually executed

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

## Second checkpoint: ordinary-communication A/B integration

Implemented after review of `70afcc2`:

- Split authoring freeze from admission, preserving the original single-run API.
  Reject revisions changed during preparation; compare the two complete frozen
  definitions after removing only the PHY selector. Neither worker starts if
  common inputs differ. Scope currently PlanningV1 / CommunicationOnly only.
- Durable `RunGroup` identity, request-key idempotency/conflict handling, common
  snapshot, system identity, two definitions/execution configurations, hashes,
  and explicit COMM packet manifest. Manifest mirrors the existing deterministic
  application source; it is **not yet a general injected workload API**.
- Save the initial archive before publishing runs. Atomic replacement/fsync and
  envelope checksum; bounded 32 MiB archive. A failure does not suppress B.
  Cancel only pending B; preserve successful A. Restart marks unfinished members
  Interrupted without resuming or manufacturing events. Archive write failure
  is visible as ArchiveFailed instead of false completion.
- Group create/list/detail/results/export/cancel-pending APIs; frontend optional
  A+B action, group library, member states, comparison/A/B views, retained results
  after restart, and explicit unavailable historical replay indication.

Executed evidence (2026-10-20; not browser acceptance):

- Backend CMake OFF regression: **353 passed / 13 skipped**. The runtime test
  is intentionally skipped in OFF, then explicitly run against the new ON worker.
- Group control-plane/API tests plus actual ns-3 group test: **8 passed**.
  Real workers built from this worktree in `/tmp/dual-workbench-build-on`;
  temporary reference environment, three nodes, two cycles, eight COMM packets.
  Both A and B completed; all eight actual PacketIds, senders and unicast targets
  matched the manifest for each member. B produced waveform statistics. A second
  app instance read identical archived results through the API with zero new runs.
- Real-test first attempt failed because its repository directory had not been
  created before calling the builder. Fixed the fixture; no scientific result
  was fabricated to overcome that failure.
- Frontend: **34 files / 284 tests passed**, TypeScript/Vite build passed.
  New tests cover incomplete archived B and queued cancellation; fixture-backed,
  not user/browser end-to-end evidence. Existing bundle-size warning remains.

Important limitations: no general common observation source for BearingFusion;
no group rerun endpoint; no durable event replay; no independent frozen-worker
binary packaging; no OSDM-specific paired numerical gate or true browser gate.
Runtime source still generates COMM packets from frozen flows rather than reading
the stored manifest, so source/manifest equivalence beyond this tested fixture
requires further verification. No claim that step 1 is fully closed.

## Remaining blocking work

1. Complete A/B common workload/observation execution contract, paired frozen
   rerun, archive error recovery, complete comparison metrics and browser tests.
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
