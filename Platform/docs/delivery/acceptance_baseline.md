# Acceptance4Node baseline

The sole machine-readable authority is
`Platform/acceptance/acceptance4_baseline_v1.json` (schema 1, baseline version
1). Tests and evidence exports consume that document; this page explains it
and does not define a second set of thresholds.

## Hard third-party acceptance requirements

- network size: 3–4 nodes;
- communication rate: 60 bit/s;
- BER: at most `1e-4` (dimensionless probability, model evidence must identify
  its source);
- feature-level fusion is required;
- at least five bearing points;
- fusion period at most 180 seconds.

The formal fixture is four nodes: three moving sensor nodes plus one fixed
fusion center. `Extended6Node` is explicitly an engineering extension and must
not be reported as the third-party acceptance baseline.

## Demonstration parameters, not extra hard requirements

The demo uses 25 kHz center frequency, 110 dB re 1 µPa @ 1 m simulated source
level, 2 s TDMA guard, structure/schedule refresh every ten cycles, a
shallow-water field, nominal sensor speed near 5 km/h, and average initial
horizontal range near 1 km. Hardware source-level reference and calibration
remain TBD; the UI and evidence must not label the simulated value as measured.

The delivered Acceptance4Node preset binds immutable environment
`reference-shallow-water-v1` (FNV1A64 `fb64e543f9042c52`). This WOA23/GEBCO
2020 reference proxy is `Reference / modeled`; its propagation field is
`Bellhop-derived`. It is a simulation basis rather than a hard acceptance
threshold or a claim of field measurement. Full provenance and limits are in
[`reference_shallow_water_v1.md`](../environment/reference_shallow_water_v1.md).

If modeled BER is numerically `0.0`, the original backend value is retained.
It is not a hardware zero-error claim; at high SNR the computed double may have
reached the floating-point representation floor.
