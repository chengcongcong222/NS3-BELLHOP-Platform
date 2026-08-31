# P0-S4-08 acceptance visualization

## Demonstration path

The read-only demonstration path is:

```text
Experiment version
  -> POST Run by identity/version
  -> authoritative Run lifecycle + observation-only SSE monitor
  -> atomically published formal Result
  -> backend AcceptanceReport evidence
```

Acceptance4Node is the third-party acceptance baseline. Extended6Node is
always labeled as an extension example and is never rendered against the
Acceptance4Node requirements.

## Authority and ordering

- Run lifecycle and event-stream completeness come only from the Run resource.
- SSE errors are visible transport failures and never mutate Run lifecycle.
- Events retain backend `RunEventSequence` order even when simulation times are
  non-monotonic; browser arrival time is not an ordering key.
- Signal and NoArrival come directly from ChannelOutcome trace payloads.
- Acceptance verdicts come directly from `AcceptanceReport`; the browser does
  not compare actual values with requirements.
- `catalog_sequence` is a backend read-model creation ordinal used for latest
  Run/Result selection. It is not a simulation sequence.

## Visualization data boundaries

Scenario topology uses only initial x/y coordinates and initial velocity from
the Scenario DTO. Node depth remains in the table. No live trajectory is
invented. Fusion plotting uses only formal estimate coordinates and times in
FusionResult; there is no fabricated target truth or bearing geometry.

The Result projection displays only supported aggregates. In particular, the
frontend does not derive a missing reception-disposition aggregate. Per-event
Reception disposition may be displayed when present in a formal trace event.

Modeled BER is labeled as simulation-model evidence and not hardware
measurement. Measured or External wording is reserved for a formal DTO that
explicitly carries that source. Canonical decimal integers remain strings;
nanosecond-to-second display uses BigInt and retains the original ns value.

The S4-08 system-information display reads the single compiled product
baseline in `frontend/src/productMetadata.ts`. React pages and components do
not carry independent ns-3 version or scheduler-authority literals. A later
delivery baseline may replace this compiled projection with an authoritative
backend build-metadata endpoint without changing simulation causality.

## Explicitly deferred

P0-S4-08 does not add Scenario/Experiment editors, database, authentication,
cancel, retry, multi-run scheduling, Bellhop runtime execution or a second
simulation-state implementation in the browser.
