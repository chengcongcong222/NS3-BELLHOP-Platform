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
constructor. Default published Acceptance experiments remain Channel 1 without
a mode branch in M1, M2, M3 or M4. The Authoring Run boundary now interprets
the explicit worker execution configuration and selects a complete provider
set once, rather than placing `if (channel2)` checks in event code.

## Tx/channel/Rx boundaries

发送端波形生成模块（`WaveformTxPhy`）已经实现：一次物理发送只调用一次
编码，给 payload 增加 CRC32，调制并发布不可变 X，同时返回不含样本的发送元数据。
所有接收节点通过发送身份查找同一个 X，不重复调制。

接收端波形解调模块（`WaveformRxPhy`）已经实现：将正式环境路径映射为
复基带多径，执行 H(X)，之后叠加独立 N，再理想相位校正、解调与 CRC 检查。
恢复字节通过 `RxDecodeResult.recovered_payload` 进入公共交付链。
原始比特仅在判决之后用于 BER 观察，不参与 CRC 判决。

显式波形装配在 `ScenarioRuntime` 内自动绑定恢复数据检查器
（`RecoveredPayloadRequiredRxPhy`）。即使注入的接收模块错误地返回成功且无数据，
也会在接收会话及交付之前报错。检查器由场景运行对象持有，不引用临时装配对象；
被包装的接收模块必须覆盖整个运行生命周期。通道1不使用该检查器。

当前没有其他发送源的波形干扰 I；重叠信号明确不支持，而不是假装处理成功。
实测和产品化门禁见 [独立验收报告](channel2_acceptance_review.md)。

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
