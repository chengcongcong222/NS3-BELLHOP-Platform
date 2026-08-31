# P0-S5-03 Acceptance Handoff

- Canonical archive: `ns3-bellhop-platform-p0-s5-03-linux-x86_64.tar.gz`
- Integrity: verify its adjacent `.sha256`, then run `./release.sh verify`
- Runtime prerequisite: Linux x86_64, CPython 3.12, ns-3.47 prefix
- Start: `PLATFORM_NS3_PREFIX=/path/to/ns-3.47 ./release.sh prepare`, then
  `./release.sh start`
- UI: `http://127.0.0.1:4173`
- Experiment: Acceptance4Node version 1
- Environment: `reference-shallow-water-v1`, FNV1A64 `fb64e543f9042c52`
- Evidence: download from Result or use the documented evidence HTTP endpoints
- Stop: `./release.sh stop`

Hard checks are 3–4 nodes, 60 bit/s, modeled BER ≤ `1e-4`, feature-level
fusion, at least five bearing observations and fusion period ≤ 180 s. Modeled
BER is not hardware evidence. ReferenceShallowWaterV1 is a public-data proxy,
not an actual project field site.

TBD: site-specific hydrography, hardware BER/source-level calibration,
waveform/HIL, durable multi-run storage, authentication, system installer and
non-Linux-x86_64 releases.
