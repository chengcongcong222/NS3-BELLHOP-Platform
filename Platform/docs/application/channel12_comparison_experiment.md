# Channel 1/2 paired comparison experiment

This is a research-side experiment, **not** the P0 formal acceptance BER
report and not a frontend result. It extends
[channel12_comparison_baseline.md](channel12_comparison_baseline.md) without
changing the default worker, frontend, runtime scheduler, or PHY contracts.

## Frozen comparison setup

The executable runs the same Acceptance4Node feature workload against the
same Bellhop-derived reference environment and BPSK PHY configuration twice
for each seed: optional sampled-scalar Channel 1, then waveform Channel 2.
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
uses Bellhop's path delays/complex gains, waveform convolution and its
current ideal-phase BPSK detector. These are intentionally different
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
| Wrong payload bits | 0 | 49,182 |
| BER (observed) | 0 | 0.3415416667 |
| Failed target packets | 0 | 800 |
| PER (observed) | 0 | 0.6666666667 |

The paired difference `Channel2 - Channel1` is +0.3415416667 BER
(bootstrap interval [0.3413888889, 0.3416807292]) and +0.6666666667 PER
(interval [0.6666666667, 0.6666666667]). Zero observed scalar errors do
not imply that its unknown population BER is mathematically zero. The
degenerate PER interval reflects nearly deterministic failures in this
fixed scene; more seed repetitions do not create environmental diversity.

An independent no-waveform-noise control using
`platform_application_channel2_feature_demo --modulation bpsk --noise none
--seed 42` still showed 80/120 wrong payload bits for the first target
reception. Thus AWGN alone does not explain this gap. The current receiver
only uses ideal first-arrival phase correction and symbol-sum decisions;
it does not estimate/equalize the full multipath channel. Coherent
multipath and inter-symbol interference are plausible causes. This
experiment does **not** establish that Channel 2 is better. The next
technical task is to add and validate a multipath-aware receiver
(equalization or another explicitly reviewed design), then rerun the
same frozen comparison without hiding unfavorable conditions.
