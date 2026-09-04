# ADR-0003: Channel 2 Transient Waveform Boundary

- Status: Accepted for P0-CH2-00 parallel-development baseline
- Date: 2026-09-05
- Base: `f806aa7769440eea3ad264b00bb8ec8c4090f8c9`

## Decision

Channel 1 and Channel 2 use the existing `ITxPhy -> IChannelFieldProvider ->
IRxPhy` outer contracts under the same ns-3 Runtime. Assembly selects a whole
provider composition. Runtime has no product-mode conditional.

Channel 2 transmitted samples are a transient, run-owned physical artifact
keyed by TransmissionId. Tx publishes exactly one immutable artifact before
receiver fan-out; all Rx processing shares it. ScenarioRuntime sees only a
narrow cleanup lifecycle and releases artifacts after every completed or
failed cycle. Samples do not enter public DTOs, snapshots or Trace.

First arrival controls ns-3 event timing. Waveform convolution applies only
relative excess delays. Path-aware processing applies absolute path gains and
does not additionally apply scalar aggregate transmission loss.

## Consequences

The public contracts remain unchanged and Channel 1 behavior is preserved.
Waveform Tx/Rx and Bellhop/CIR work can proceed in separate directories from a
common boundary. A later HIL-backed artifact implementation can replace the
in-memory store without changing M1-M4. Full ns-3 Channel 2 execution remains
the next integration gate, not an achievement claimed by this ADR.
