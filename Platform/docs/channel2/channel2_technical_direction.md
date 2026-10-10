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

The implemented receive order is `Y = X * H + N`: channel convolution occurs
first and receiver noise is added afterwards. AWGN and Wenz profiles are
selectable. Concurrent-source interference `I` remains future work. `H`,
`N`, and `I` remain separate physical concepts and provider inputs.

`H` is represented by propagation paths containing absolute pressure gain,
phase and delay. Bellhop arrivals are the first physics-based source, not the
definition of Channel 2. Measured CIR replay and hybrid providers must be able
to produce the same path/CIR boundary later.

The initial time-variation policy is block-wise quasi-static. Geometry selects
a channel snapshot for a block; the design does not assert that one ns-3
packet is always one coherence block. Splitting a long transmission into
multiple blocks is deferred.

## Extension direction
## OSDM v1 contract

OSDM is the default Channel 2 modem. The input is a binary `BitFrame`; v1 maps
each bit to a real BPSK constellation value before OSDM processing. The default
configuration is `K=256`, `M=16`, `N=K/M=16`, and cyclic prefix
`CP=64`. Each K-symbol block applies the MATLAB-compatible transform
`s=(F_N^H tensor I_M)d`, prefixes the final CP complex samples, and emits a
`WaveformBuffer` containing `complex<double>` samples at 6000 Hz. An
incomplete final block is padded with zero-bit constellation symbols; the
immutable transmission artifact separately stores the valid framed bit count
so padding can never become recovered payload.

The receiver uses the already known simulated CIR in the ideal
frequency-domain MMSE equalizer, removes each CP, applies
`x=(F_N tensor I_M)r`, performs the BPSK real-axis decision, trims padding,
checks CRC32, and then reconstructs payload bytes. This is the first executable
OSDM baseline. It does not yet implement synchronization, channel estimation,
Doppler compensation, diversity combining, or the supplier's later
equalization alternatives.


Modem selection is independent of channel selection. OSDM/BPSK is the default;
legacy single-carrier BPSK and BFSK remain selectable. JANUS, QPSK constellation
mapping, imported waveforms and target-source
signals are future modem/source providers. A HIL adapter may provide or consume
the same immutable transmission waveform artifact without changing public
network contracts. Real-time synchronization, ADC/DAC, Doppler, array
processing and full waveform interference are intentionally outside
P0-CH2-00.
