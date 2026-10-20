# ADR-0013: Paired runs and recorded-sample proxy networks

- Status: Authorized implementation scope; verification pending
- Authorization: user approval on 2026-10-20 after frozen-scope conflict review
- Baseline: ec311c1906789082982eb89b7e5b8bb14ff243e1

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
