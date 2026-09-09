# Channel 2 Current State Audit

## 2026-09-09 第一阶段独立验收更新

本次审查基于 `d76e2072379fa016fe1b79f9515dd5ca133bad40`，
不是下文历史基线。发送端生成波形、接收端施加正式 Bellhop 多径并加噪解调，
已经通过显式通道2装配进入正式 ns-3 发送、到达、接收结束和周期清理流程。
接收恢复的字节参与网络交付与方位融合；不是旁路计算 BER。

本轮补上装配边界约束：通道2成功解调却未提供恢复字节时直接报错，
不允许公共交付逻辑回退到发送端数据。通道1仍保留兼容行为。
实测、反例、噪声双来源及后续限制见 [独立验收报告](channel2_acceptance_review.md)。
默认前端/worker 仍选择通道1；以下表格只描述旧版本，不代表当前缺口。

## P0-CH2-00 历史审计（保留追溯）

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

P0-CH2-00 supplied that ownership boundary and cleanup hook. At that historical
baseline the standalone pipeline was not yet a runtime WaveformTxPhy or
WaveformRxPhy. Both runtime providers now exist; see the update above.

## Environment precision currently available

The normalized asset preserves canonical path vectors. Runtime interpolation
uses interpolated scalar TL/first arrival where the stencil is valid, while
the multipath vector comes from the nearest spatial cell. This is enough for a
first block-wise Channel 2 path, but it is not coherent path interpolation or
broadband/time-varying CIR. Bellhop execution and filesystem parsing remain
outside the hot path; the provider reads a prevalidated in-memory asset.
