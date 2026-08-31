# Acceptance evidence bundle

After a Run completes with a complete event stream and formal
`AcceptanceReport`, the backend exposes:

- `GET /runs/{run_id}/acceptance-evidence` — schema-v1 JSON;
- `GET /runs/{run_id}/acceptance-evidence.txt` — deterministic text export.

Run creation freezes a `RunManifest` containing exact Environment, Scenario,
Experiment and `/system/info` metadata. The evidence bundle later joins only
that manifest, the captured terminal Run, formal Result projection, fusion and
node records, and the backend-produced AcceptanceReport. It never recomputes a
threshold or repairs a contradictory verdict.

The bundle explicitly records whether BER evidence is `Modeled` or
`NotEvaluated`. `NoArrival` means a valid channel query produced no physical
arrival and therefore no Reception; `NotDecoded` means a signal arrived and
entered Rx processing but decoding failed. No unsupported aggregate is inferred
from other counters.

The in-memory P0 catalog is process-local. Evidence is immutable and
deterministic for the lifetime of the process, but durable result/evidence
persistence remains TBD.
