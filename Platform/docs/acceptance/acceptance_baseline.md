# Acceptance4Node formal acceptance baseline

The sole machine-readable threshold authority is
`Platform/acceptance/acceptance4_baseline_v1.json` (schema 1, baseline version
1). This document maps that authority to runtime evidence; it does not define a
second threshold set.

| Metric | Hard requirement | Formal evidence source |
|---|---:|---|
| Network size | 3–4 nodes | Backend `AcceptanceReport.network_node_count`; captured Scenario has 4 nodes |
| Communication rate | 60 bit/s | captured Experiment PHY + Backend verdict |
| BER | no greater than `1e-4` | Backend `AcceptanceReport`, source labelled `Modeled` or `NotEvaluated` |
| Feature fusion | required | Backend `AcceptanceReport.feature_level_fusion` |
| Bearing points | at least 5 | Backend report and formal FusionResult records |
| Fusion period | at most 180 s | Backend report and formal FusionResult time fields |

The formal topology is three moving sensor nodes and one fixed fusion center.
`Extended6Node` is an engineering extension, not the third-party baseline.

Demo parameters are deliberately separate: 25 kHz center frequency, 110 dB re
1 µPa @ 1 m simulated source level, 2 s TDMA guard, network update every ten
cycles, shallow-water field, nominal speed near 5 km/h and average initial
horizontal range near 1 km. Hardware source-level reference/calibration remains
TBD; 110 dB must not be presented as measured hardware evidence.

The ns-3 ON build and `/system/info` identify ns-3 3.47. `ns3::Simulator` is
the sole simulation-clock and event-scheduling authority; M1 /
`Ns3KernelGateway` is the Platform-side access gateway to ns-3 scheduling, not
a scheduler authority.
Kernel smoke, dispatcher, signal lifecycle, acceptance scenario, fusion and
real worker HTTP integration tests are the runtime-mechanism evidence. The
launcher, FastAPI metadata, SSE and evidence exporter are control/observation
planes and do not participate in simulation causality.

`AcceptanceEvidenceBundle` copies the captured RunManifest, formal Result and
Backend AcceptanceReport. It never recomputes a verdict. A `Fail` verdict may
belong to a successfully completed Run. `NoArrival` means no physical arrival
and no Reception; `NotDecoded` means an arrival entered Rx processing but did
not decode.

Not currently promised: measured/hardware BER, hardware source-level
calibration, durable Run/evidence persistence, authentication, production
service supervision, arbitrary scenario editing, or a complete waveform/HIL
pipeline.
