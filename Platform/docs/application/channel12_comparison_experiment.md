# Channel 1/2 paired comparison experiment

This is a research-side experiment, **not** the P0 formal acceptance BER
report and not a frontend result. It extends
[channel12_comparison_baseline.md](channel12_comparison_baseline.md) without
changing the default worker, frontend, runtime scheduler, or PHY contracts.

## Frozen comparison setup

The executable runs the same Acceptance4Node feature workload against the same
Bellhop-derived reference environment twice for each seed: optional
sampled-scalar Channel 1, then OSDM waveform Channel 2. Channel 1 retains its
coherent-BPSK/AWGN probability model; Channel 2 uses QPSK constellation mapping
inside OSDM with `K=1024`, `M=16`, `N=64`, `CP=640`, and one
cyclically-prefixed deterministic CAZAC training block before the data blocks.
The experiment rejects any pair whose current-hop target
TransmissionId/PacketId/receiver/payload-bit set differs. Both paths use
15-byte feature payloads plus the same CRC32 test trailer and 6-second TDMA
slots. The scenario and environment asset stay fixed across seed repetitions.

Only AWGN is used in this comparison. The user-supplied acoustic pressure
PSD in dB re 1 µPa²/Hz is identical in both paths. The scalar path receives
equivalent in-band noise power
`PSD_dB + 10 log10(occupied_bandwidth_hz)`; the waveform path synthesizes
complex samples from that same PSD with an explicit seed. The two generators
do **not** produce the same sample-wise noise realization. Channel 1 uses
Bellhop's incoherent aggregate TL and independent bit-error draws; Channel 2
uses Bellhop's path delays/complex gains, waveform convolution, an
automatic training correlation, training-derived frequency-response estimate,
frequency-domain MMSE equalization, CP removal, and OSDM inverse
transformation. These are intentionally different
physical approximations; matching PSD is not matching a multipath receiver.

A repetition is one full paired simulation run. For each channel, pooled
BER is total observed wrong payload bits divided by total observed target
payload bits; pooled PER is total failed target packets divided by total
target attempts. No-arrival contributes to PER and to a separate count but
never invents received bits for BER. The 95% interval is a deterministic
2,000-resample *paired run-cluster percentile bootstrap* of
`Channel2 - Channel1`, preserving packet correlation within a run.
It describes variability over the chosen noise seeds **conditional on this
single fixed environment and workload**. It is not an interval over sea
states, locations, hardware or alternative receiver designs.

## Reproduce

Set `BUILD_DIR` to an ns-3-enabled CMake build directory, then run:

```sh
cmake --build "$BUILD_DIR" --target platform_application_channel12_comparison_experiment
"$BUILD_DIR/application/platform_application_channel12_comparison_experiment" \
  --repetitions 100 --cycles 4 --bootstrap 2000 --seed 42 --noise-psd-db 0
```

`--asset-root` may point to another directory containing
`bellhop/reference_shallow_water_v1.arr`. The CLI rejects malformed
or unbounded sample-count/seed options. The default PSD of 0 dB is an
**explicit experiment fixture**, not an ocean-noise recommendation or
hardware calibration.

## Observed result on the reference asset

| Metric | Channel 1 sampled scalar | Channel 2 waveform |
|---|---:|---:|
| Target packets attempted | 1,200 | 1,200 |
| No arrival | 0 | 0 |
| Observed payload bits | 144,000 | 144,000 |
| Wrong payload bits | 0 | 0 |
| BER (observed) | 0 | 0 |
| Failed target packets | 0 | 0 |
| PER (observed) | 0 | 0 |

The paired differences and both paired-bootstrap intervals are `[0,0]` for
this explicit zero-dB PSD fixture. That result verifies the OSDM integration
and removal of the earlier unmitigated multipath errors; it does **not**
establish that either channel has zero population BER or that Channel 2 is
generally better. The Channel 2 receiver no longer consumes the exact
simulated CIR for synchronization or equalization; it estimates the block-static
channel from the received training preamble. It still uses the simulation's
generated noise power as the MMSE regularization input and does not compensate
Doppler. Noise stress points and additional sea states are required for a
meaningful performance boundary, and future equalizers must be compared using
this same paired packet basis.
