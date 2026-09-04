# Channel 2 Runtime Architecture

## Ownership decision

Assembly owns one `ITransmissionWaveformArtifacts` implementation for a run.
A waveform Tx PHY publishes one immutable
`TransmissionWaveformArtifact{TransmissionId, PacketId, original bits, X}`.
Waveform Rx PHY instances look it up by the desired signal's TransmissionId.
Every receiver obtains the same `shared_ptr<const ...>`; modulation is never
repeated in receiver fan-out.

The in-memory implementation is `TransmissionWaveformStore`. It is not global,
singleton or authoritative state. `ScenarioRuntime` accepts a narrow
`IPhysicalArtifactLifecycle` only through the Channel 2 assembly composition
and releases all cycle artifacts after every `RunOneCycle` return, including
failure. All arrival/finalize events have completed before successful cleanup.
This bounds memory while allowing every receiver window in the cycle to read
X. A shared pointer held briefly by a decoder remains valid during a lookup.

Waveforms are intentionally absent from `DigitalPacket`, `TxEmission`,
`WorldSnapshot`, Trace and the in-flight signal ledger. Those DTOs retain their
stable value semantics and do not copy sample arrays.

Rejected alternatives:

- Putting X in `TxEmission` or `ReceivedSignal` would copy a large array for
  every receiver and pollute public contracts.
- A process-global registry would leak across runs and harm deterministic
  tests and parallel execution.
- Regenerating X in Rx would violate broadcast physical identity.
- A second waveform scheduler would compete with ns-3 time authority.
- A type-erased pointer in public contracts would weaken type safety and still
  expose lifetime concerns to M1-M4.

## Composition point

`PhyExecutionComposition` is an Assembly-internal selection:

- `Abstract(...)` binds the existing Tx/channel/noise/Rx providers and has no
  physical-artifact lifecycle.
- `Waveform(...)` binds the same stable public provider contracts plus an
  Assembly-owned artifact lifecycle.

`ScenarioRuntime` has a composition constructor but retains its existing
constructor. Current Acceptance code therefore remains Channel 1 without a
mode branch in M1, M2, M3 or M4. A future Experiment field is interpreted by
Application/Assembly before Runtime construction; it selects a complete
provider set once, rather than placing `if (channel2)` checks in event code.

## Tx/channel/Rx boundaries

The next WaveformTxPhy will implement existing `ITxPhy`. During its single
`Encode` call it will serialize, modulate, publish X, and return the existing
metadata-only `TxEmission`. The next WaveformRxPhy will implement existing
`IRxPhy`, resolve X using the desired TransmissionId, turn
`ChannelFieldResponse.paths()` into taps, apply H, optionally add independent
N/I, demodulate, compare bits, and return the existing `RxDecodeResult` plus
appropriate waveform-derived quality evidence.

No public contract change is required for P0-CH2-00. A later contract change
is justified only if recovered payload or measured BER must cross a stable
module boundary not already represented by Reception/application results.

## Delay and gain rules

`first_arrival_delay` schedules the signal's absolute first arrival in the
ns-3 lifecycle. CIR convolution uses only each path's `excess_delay`; applying
first arrival again as leading waveform samples would double count propagation
time. Path gains are absolute pressure gains. Path-aware processing must not
also apply `aggregate_transmission_loss_db`; that scalar remains Channel 1's
fallback representation.

## Provider evolution

Bellhop import/normalization produces the generic path response consumed by
the waveform Rx. A measured-CIR provider can later produce the same normalized
response, or a reviewed broader channel-block contract if sample-domain CIR is
required. JANUS plugs into modem interfaces, not the channel provider. A HIL
adapter can implement the artifact repository boundary or consume its
immutable X/Y buffer while ns-3 continues to own lifecycle time.
