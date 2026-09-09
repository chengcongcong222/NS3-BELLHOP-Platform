# Channel 2 Technical Direction

## Purpose

The platform keeps two physical execution paths under the same ns-3 event
runtime.

- **Channel 1 — abstract PHY:** packet-level airtime, scalar transmission
  loss, noise observation and modeled BER. It remains the default for large
  network studies, protocol work and fast regression.
- **Channel 2 — waveform PHY:** payload framing, modem, one transmitted
  waveform `X`, receiver-specific channel response `H`, received waveform
  `Y`, demodulation and BER measured by comparing source and recovered bits.
  It is intended for small high-fidelity and future hardware-in-the-loop
  experiments.

Neither path owns time. `ns3::Simulator`, reached only through M1, remains the
sole clock and event scheduler. Channel 2 is a PHY execution path inside the
existing transmission/reception lifecycle, not a parallel simulator.

## Frozen physical model

第一阶段现已实现并验收 `Y = H(X) + N`：正式 Bellhop 多径作用之后再添加
可重复的 AWGN 或近似 Wenz 噪声，也可显式关闭噪声。它不是 `H(X+N)`。
并发源的波形干扰 I 仍未实现，后续才扩展到 `H(X) + N + I`。
H、N、I 是不同物理输入，不混为传播损失或接收判决概率。

当前波形噪声由独立的显式配置决定，旧实验的标量噪声不能唯一决定 N。
在向正式前端开放通道2之前，必须统一实验噪声来源、单位、带宽和 seed 的投影，
并拒绝矛盾配置；这不是已经完成的产品能力。

`H` is represented by propagation paths containing absolute pressure gain,
phase and delay. Bellhop arrivals are the first physics-based source, not the
definition of Channel 2. Measured CIR replay and hybrid providers must be able
to produce the same path/CIR boundary later.

The initial time-variation policy is block-wise quasi-static. Geometry selects
a channel snapshot for a block; the design does not assert that one ns-3
packet is always one coherence block. Splitting a long transmission into
multiple blocks is deferred.

## Extension direction

Modem selection is independent of channel selection. Reference BPSK and BFSK
are current implementations; JANUS, imported waveforms and target-source
signals are future modem/source providers. A HIL adapter may provide or consume
the same immutable transmission waveform artifact without changing public
network contracts. Real-time synchronization, ADC/DAC, Doppler, array
processing and full waveform interference are intentionally outside
P0-CH2-00.
