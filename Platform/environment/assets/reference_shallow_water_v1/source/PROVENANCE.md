# ReferenceShallowWaterV1 source provenance

This is a **reference/proxy acoustic environment** for deterministic digital
simulation. It is not a project field site, a field measurement, or a measured
acoustic field.

- Proxy origin: 18.0° N, 110.0° E, northwestern South China Sea.
- Season: April climatology (spring).
- Temperature and salinity source: NOAA/NCEI World Ocean Atlas 2023 monthly
  one-degree products, nearest grid profile at 17.5° N, 109.5° E. The cached
  source NetCDF files are `woa23_decav_t04_01.nc` and
  `woa23_decav_s04_01.nc`. Dataset landing page:
  <https://www.ncei.noaa.gov/access/world-ocean-atlas-2023/>.
- Sound speed: derived from the WOA23 temperature/salinity profile by the
  legacy source preparation pipeline and retained without undocumented tuning.
- Bathymetry source: GEBCO 2020, 15 arc-second global grid, sampled along a
  90° bearing from the proxy origin at 250 m spacing. Product page:
  <https://www.gebco.net/data-products/gridded-bathymetry-data/gebco-2020>.
- GEBCO data are used under the GEBCO terms of use, with attribution; this
  reference asset is not for navigation and does not imply GEBCO endorsement:
  <https://www.gebco.net/data-products/gridded-bathymetry/terms-of-use>.
- Source artifacts were retrieved/prepared in the legacy asset cache on
  2026-04-15 and introduced into Platform on 2026-08-31.

The authoritative machine-readable record, including SHA-256 checksums, is
`source_manifest.json`. The old mutable cache record is deliberately not
used as provenance because its notes contained two conflicting nearest-grid
longitudes, 113.5° E and 109.5° E. The final asset was re-verified against the
requested 18.0° N, 110.0° E coordinate and the WOA23 one-degree grid: its only
authoritative selection is 17.5° N, 109.5° E. The source artifact identities,
normalization recipes and checksums recorded in the manifest are the formal
provenance; the contradictory historical annotation is not part of asset
identity.
