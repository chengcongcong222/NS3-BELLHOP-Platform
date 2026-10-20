# ADR-0013: Paired runs and recorded-sample proxy networks

- Status: Accepted within the verified independent-branch scope
- Authorization: user approval on 2026-10-20 after frozen-scope conflict review
- Baseline: ec311c1906789082982eb89b7e5b8bb14ff243e1
- Verification: `docs/acceptance/dual-workbench/`; deployment is not authorized by this status.

## Explicit supersession

For new paired experiments only, supersede ADR-0001 §2.50/§2.54's
process-local-only product restriction: allow durable group manifests, immutable
member definitions and terminal result archives, a backend-owned sequential
work queue, and cancellation of members which have not started. This is an
OS-process work queue, never a second simulation scheduler. Ordinary runs and
group members must share one admission authority and at most one active Worker.
Restart marks unfinished archived work Interrupted; it never claims that an
in-memory Run or its missing events survived. Cancellation does not kill an
active simulation or invent a new core Run lifecycle.

Extend ADR-0011 with explicitly versioned authored v5 proxy definitions. Keep
v4's two-endpoint measured-link semantics and all v1–v3 paths unchanged.
Do not rewrite historical ADRs, evidence, reference traces or research verdicts.

## Scientific and ownership invariants

- The two families are geometric Bellhop networks and recorded-channel logical
  networks. Logical layout is display metadata, not distance or a PHY input.
- A/B members freeze a common input and identical explicit workload, not merely
  the same seed. Encoded airtime and resulting schedules may differ. Group/Run
  IDs must not seed scientific randomness; derive streams from frozen scientific
  inputs and explicit member tags, preserving exact-config reruns (§2.46).
- Proxy link relationships are synthetic/unknown. D3 remains NO-GO for measured
  synchronous multiple-transmitter evidence. No fabricated reverse links,
  absolute loss, coordinates, propagation delay, or Bellhop fallback.
- Preserve physical fan-out (§2.29), not just intended recipients. Preflight
  must cover every eligible directed physical pair, including overhearing;
  a sparse protocol graph does not prove absent acoustic propagation.
- One transmission creates one waveform. Each receiver mixes signal-only
  channel outputs on an explicit common time/carrier/sample reference, then
  adds its single continuous noise realization. No per-link noise summation.
- ReceiverWindow contains only causally arrived overlap candidates. Decoding
  consumes mixed samples, never other senders' truth bits or future planner
  information. Bit comparison happens after decisions. No decisions means no BER.
- Preserve ns-3 time, phase order, closed cycles and run-owned sample lifecycle.
  Incompatible sample clocks/carriers are rejected unless an independently
  validated transformation exists; integer rounding is not such a transformation.
- Legacy v4 numerical outputs and traces remain the compatibility authority.
  New common-clock processing must not silently change old replay semantics.

## Implementation and release gates

Follow the supplied P0-DUAL-WORKBENCH V1 U01–U25, P01–P18 and R01–R12 gates.
Numerical mixture tests alone do not establish production receive integration.
Real Worker/browser runs and old A/B/C regressions are mandatory. Resource
limits require measured evidence. New UI capabilities stay unavailable until
the corresponding execution path is verified. No main merge, live-service
switch, research modification or permanent secondary deployment is authorized.

## API compatibility

New group APIs and authored v5 are additive. Old routes and DTOs keep their
meaning. Scientific sample matrices remain PHY-internal, never public contracts
or trace payloads. Receiver mixture provenance is bounded metadata. Durable
archives identify completeness explicitly; no replay is offered without events.

### V5 reception-overlap admission

The authorized mixed-reception path needs an additive planning admission flag:
`allow_receiver_overlap`, default false. Only the explicitly assembled v5
mixture-capable provider enables it. It removes the conservative *global*
airtime-plus-propagation serialization check, not closed-cycle completion,
sender capability, route/provenance validation, or same-sender airtime exclusion.
Every candidate still finishes its full conservative receive envelope before
cycle close. Runtime remains responsible for half duplex and causal overlap
windows. Existing callers, v1–v4, and registered algorithm contexts retain the
original serialized admission. This flag does not generate opportunities or
change TDMA planning; it permits testing collisions caused by assigned delays.

V5 also declares an exact emission-time quantum (LCM of receiver passband and
recording sample grids, including rational UWA rates). `PlanningInput` gains
this additive read-only duration, default 1 ns. Adaptive TDMA rounds **planned
slot width upward** to that declared grid; it never rounds a propagated waveform
or changes a scheduled event after admission. Configured TDMA and cycle durations
must already align or fail preflight. Default 1 ns leaves old plans unchanged.
Registered algorithms are not enabled for v5 by this extension.

Reception traces may append bounded `mixture` metadata only after samples
actually pass through v5 processing: contributing signal count, sample count,
combined signal-only power and once-added noise power. This is also emitted
when synchronization fails (no bit decisions; waveform/BER remains absent).
Combined power is not desired-signal SNR. Old traces omit this field entirely,
including Python serialization, preserving their canonical byte representation.

The same durable bundle owner also archives a single v5 C member (no second
queue). Frozen full directed mappings, receiver-noise identities, command and
formal result are exported and checked on repeat. This implements the task's
C persistence gate; it does not retrofit persistence to legacy single runs.
Event history remains explicitly unavailable in restored bundles.

### Explicit mixed-modem proxy preset

V5 may append `transmitter_modulations` to its frozen proxy mapping. This is
known transmitter configuration, not blind modem detection. The initial mixed
preset uses the global OSDM baseband clock and carrier. Individual transmitters
may select BPSK/BFSK at that same clock with 100 samples/symbol (nominal rate
fs/100), or inherit OSDM. No runtime resampling policy, arbitrary independent
clock or online adaptation is implied. Nonintegral rates fail preflight.
One run-owned modem dispatcher chooses the encoder and desired-sender decoder
from this frozen identity, sharing the existing artifact owner. It never uses
payload truth to select a modem or aid decoding. Interferers still enter the
real common-passband mixture regardless of their modulation. Per-node exact
encoded airtimes enter planning; the conservative propagation bound covers the
longest encoded packet. Legacy v1–v4 and v5 without overrides use their existing
single-modem composition unchanged.

### Paired bearing input freeze

The existing cycle-start bearing generator is deterministic from immutable
trajectory, target and sample time. An offline Worker mode may compute its
exact serialized report ledger without starting ns-3 or a Run. A/B preparation
compares and freezes that ledger into both authored definitions before queue
admission. Runtime still generates inputs at existing INPUT_READY events and
must match each report/packet/source/time against the ledger before enqueue.
A mismatch fails; it cannot replace geometry with made-up measurements.
This additive paired-only field does not change legacy standalone bearing runs,
application delivery, fusion triggers or physics. Missing records, damaged
bytes, wrong order and unsuitable workloads are rejected.
