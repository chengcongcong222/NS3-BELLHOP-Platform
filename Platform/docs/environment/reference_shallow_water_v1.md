# ReferenceShallowWaterV1

`reference-shallow-water-v1` is the Platform P0-S5-02 reference shallow-water
environment. It is a **reference/proxy modeled environment** built from public
ocean data for deterministic digital simulation. It is not the project's
actual field site, a field measurement, or a measured acoustic field.

## Public source and proxy choice

The proxy origin is 18.0° N, 110.0° E in the northwestern South China Sea. It
provides an approximately 100 m shallow-water setting appropriate to the
current nominal 1 km demonstration without claiming equivalence to a future
reservoir, Yellow River, or sea-trial site.

- SSP inputs use the April one-degree temperature and salinity climatology
  from [NOAA/NCEI World Ocean Atlas 2023](https://www.ncei.noaa.gov/access/world-ocean-atlas-2023/),
  nearest grid 17.5° N, 109.5° E. The normalized source profile has 24 strictly
  increasing samples from 0 to 175 m and sound speeds from 1536.136 to
  1510.123 m/s.
- The eastward reference transect uses the
  [GEBCO 2020 15 arc-second grid](https://www.gebco.net/data-products/gridded-bathymetry-data/gebco-2020).
  It contains 41 samples at 250 m spacing over 0–10 km, with positive-down
  depths from 102 to 110 m. GEBCO attribution and
  [terms of use](https://www.gebco.net/data-products/gridded-bathymetry/terms-of-use)
  apply; the data are not for navigation.

Checksums, retrieval dates, dataset versions and usage terms are frozen in
`environment/assets/reference_shallow_water_v1/source/source_manifest.json`.
The SSP was not manually tuned to obtain an acceptance verdict.

Historical mutable-cache notes contained conflicting 113.5° E and 109.5° E
annotations. The final provenance was re-verified from the requested 18.0° N,
110.0° E coordinate and the WOA23 one-degree grid; 17.5° N, 109.5° E is the
only authoritative grid selection. The historical annotation is excluded from
asset identity.

## Offline Bellhop configuration

The committed ASCII arrival field was generated offline by BellhopCXX 2D CPU
from source commit `b396d40ba49c2f349258b9687cfae8ff8323828f` with executable
SHA-256 `09ae115c19e352f15b0e4b932201960e7fa64a6d8858b59aaaad06df2c4ff0cd`.
The deterministic command was:

```text
bellhopcxx2d -1 reference_shallow_water_v1
```

The frozen model uses 25 kHz, source/receiver depth axes
`[8, 55, 60, 65, 75] m`, a 0–2500 m range axis at 100 m spacing, a vacuum flat
surface, GEBCO-derived linear bathymetry, and an acousto-elastic bottom
(1600 m/s, 1.8 g/cm³, 0.8 dB/wavelength). Bellhop uses cubic-spline SSP
interpolation and automatically selected 12,500 geometric hat beams over
−80° to +80°. `bellhop/config.json` records every setting and input/output
SHA-256. Two consecutive offline executions produced the same `.arr` checksum
`b13232e6cd47c5f4e6e70e05685d85bdd138adfe3888870c30f8dcf0b35826a3`.

Bellhop is never launched by the online Runtime. `platform_reference_environment_builder`
reads the committed `.arr` through the existing Bellhop ASCII parser and
frozen raw-arrival normalizer, then immutably publishes the package to the
existing `EnvironmentAssetRepository`.

## Canonical asset and validation

- AssetId: `reference-shallow-water-v1` (never a filesystem path)
- Package/asset format versions: 1 / 1
- Coordinate frame: Platform positive-up Z, surface Z = 0 m
- Frequency: 25,000 Hz
- Coverage: source and receiver depth 8–75 m; horizontal range 0–2500 m
- Cells: 650 total, 625 Signal, 25 NoArrival
- Payload: 231,056 bytes
- Canonical payload checksum: FNV1A64 `fb64e543f9042c52`
- Normalization: `bellhop-raw-arrival-normalizer-v1`

The golden metadata and full validation record are
`golden_metadata.json` and `validation_report.json`. Tests cover source
metadata, SSP and bathymetry validity, Bellhop file checksums, parser/import,
deterministic packaging, immutable publication, repository round-trip,
frequency selection, representative Signal/NoArrival queries, the real
Acceptance4Node worker path, evidence capture, and frontend display.

Representative 60 m source to 8 m receiver results are sanity checks, not
calibration targets: at 100/800/1000/2500 m, TL is approximately
38.313/51.976/52.574/60.741 dB and first-arrival delay is approximately
0.0736/0.5232/0.6534/1.6364 s. Path counts are 10/8/9/18.

## Acceptance relationship and limitations

The formal launcher binds Acceptance4Node to this exact AssetId and checksum.
The verified two-cycle reference run completed with 4 nodes, 60 bit/s, 18
Signal channel outcomes, modeled maximum BER 0, 6 bearing observations, a 24 s
fusion period, and overall `Pass`. This outcome is reported, not fitted: a
future reference asset that produces `Fail` must remain a valid modeled result.
The previous manual demo fixture also passed with the same topology, channel
counts, bearing count and fusion period; its modeled maximum BER was
approximately `4.20e-11`. The reference run's modeled maximum BER was `0.0`
at the published floating-point precision. It is a modeled numerical result,
not proof of absolute zero errors or a hardware measurement; at high SNR the
computed double can reach the floating-point representation floor. This
comparison is evidence, not a calibration objective.

The immutable evidence bundle captures the exact environment identity,
checksum and provenance at Run creation. Its terminology is:

- Environment evidence: `Reference / modeled`
- Propagation: `Bellhop-derived`
- BER: `Modeled` or `NotEvaluated`

Remaining limitations include reference rather than site-specific hydrography,
a single 25 kHz profile, a two-dimensional eastward bathymetry proxy, frozen
P0 spatial sampling, modeled rather than hardware BER, and no waveform/HIL
calibration. These remain explicit boundaries rather than hidden defaults.
