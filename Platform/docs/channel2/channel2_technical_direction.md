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

## OSDM training-receiver contract

OSDM is the default Channel 2 modem. The input is a binary `BitFrame`; the
default maps each pair of bits to a unit-energy QPSK constellation symbol.
The modem also retains BPSK as a configuration option. The default framing is
`K=1024`, `M=16`, `N=K/M=64`, and cyclic prefix `CP=640`.
Each K-symbol data block applies the MATLAB-compatible transform
`s=(F_N^H tensor I_M)d`, prefixes the final CP complex samples, and emits a
`WaveformBuffer` containing `complex<double>` samples at 6000 Hz.
An incomplete final block is padded with zero-bit constellation symbols; the
immutable transmission artifact separately stores the valid framed bit count,
so padding can never become recovered payload.

The transmitted frame begins with one cyclically-prefixed deterministic CAZAC
training block. The receiver correlates this known sequence over the configured
search window, selects the earliest significant arrival, and estimates
`H_hat[k]=Y_training[k]/X_training[k]`. The 640-sample default prefix exceeds
the roughly 523-sample
maximum delay spread of the checked-in reference Bellhop scenario at 6000 Hz;
other environments must select a prefix from their own maximum delay spread.
The receiver uses the estimate for per-bin MMSE equalization, removes each
data CP, applies
`x=(F_N tensor I_M)r`, performs QPSK hard decisions, trims padding, checks
CRC32, and reconstructs payload bytes. The equalizer receives no Bellhop or
external-CIR tap list; those taps affect it only through the propagated samples.

This is an executable first complete OSDM send/receive chain, not a claim of a
field-ready modem. The channel is block-static, Doppler compensation, FEC,
diversity combining and concurrent waveform interference remain future work,
and MMSE regularization currently uses the simulation's generated noise power.

Modem selection is independent of channel selection. OSDM/QPSK is the default;
legacy single-carrier BPSK and BFSK remain selectable. JANUS, imported waveforms
and target-source signals are future modem/source providers. A HIL adapter may
provide or consume the same immutable transmission waveform artifact without
changing public network contracts. ADC/DAC, Doppler, array processing and full
waveform interference are intentionally outside the current baseline.
