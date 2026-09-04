# Channel 2 Current State Audit

Baseline audited: `f806aa7769440eea3ad264b00bb8ec8c4090f8c9`.

## Capability matrix

| Capability | Existing implementation | Runtime status at baseline |
|---|---|---|
| Packet payload to bits | `ExtractPayloadBitFrame`, MSB first | Used by configured Tx only |
| Waveform buffer | Owned finite complex samples plus sample rate | PHY internal only |
| Modulation/demodulation | Reference BPSK and BFSK; QPSK explicitly unsupported | Standalone/Tx prototype |
| Bellhop arrival parsing | Strict offline Bellhop 2D ASCII parser and raw bundle | Offline environment import |
| Arrival normalization | Gain/phase/delay to canonical `PropagationPath` and `AcousticFieldAsset` | Runtime asset source |
| Runtime channel query | `AcousticFieldChannelProvider` returns response/no-arrival | Fully in Runtime via contract |
| Paths to CIR taps | `BuildMultipathTaps` | Standalone waveform pipeline |
| Multipath waveform operation | Deterministic fractional-delay `ApplyMultipath` | Standalone waveform pipeline |
| Noise | Deterministic AWGN and Wenz generators | Standalone; runtime uses scalar `NoiseObservation` |
| Payload recovery and BER | Demodulation, byte recovery, actual bit comparison | Standalone waveform pipeline |
| End-to-end waveform chain | `RunWaveformPipeline` | Explicitly offline, not an ITxPhy/IRxPhy composition |

## What already reaches Runtime

`TransmissionExecutor` allocates one TransmissionId and calls
`ITxPhy::Encode` exactly once before canonical receiver fan-out. Each candidate
receiver then receives an independent `ChannelQuery`. `ReceivedSignal`, the
in-flight ledger, receiver windows, noise query and `IRxPhy::Decode` already
participate in the ns-3 lifecycle. Broadcast therefore already has the right
one-Transmission/zero-to-many-Reception cardinality.

The production Acceptance path composes `RateBasedTxPhy`,
`AcousticFieldChannelProvider`, scalar noise and scalar/deterministic Rx. It is
Channel 1 and remains unchanged.

## The actual missing chain

`ConfiguredTxPhy` serializes and modulates a packet, but uses the waveform only
to derive duration and then discards it. `TxEmission`, `ReceivedSignal`,
`TransmissionSession` and `RxDecodeRequest` carry metadata/channel DTOs, not
the transmitted samples. Consequently a production Rx cannot resolve the one
`X(TransmissionId)` that was generated before fan-out. The algorithms exist;
their transient ownership and Runtime composition did not.

P0-CH2-00 supplies that ownership boundary and cleanup hook. It deliberately
does not label the standalone pipeline as a production WaveformTxPhy or
WaveformRxPhy. Those are the next implementation tasks.

## Environment precision currently available

The normalized asset preserves canonical path vectors. Runtime interpolation
uses interpolated scalar TL/first arrival where the stencil is valid, while
the multipath vector comes from the nearest spatial cell. This is enough for a
first block-wise Channel 2 path, but it is not coherent path interpolation or
broadband/time-varying CIR. Bellhop execution and filesystem parsing remain
outside the hot path; the provider reads a prevalidated in-memory asset.
