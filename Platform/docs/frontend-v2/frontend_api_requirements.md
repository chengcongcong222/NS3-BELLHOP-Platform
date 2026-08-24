# Frontend v2 API requirements

## Resource principles

- APIs expose stable domain IDs and explicit versions, never filesystem paths
  as resource identity.
- Published Environment/Scenario versions and captured Run inputs are
  immutable.
- Validation failures, not found, already exists, unsupported format,
  out-of-coverage and no-physical-arrival are distinct outcomes.
- Simulation time uses an exact integer representation with a declared unit;
  browser floating-point seconds are presentation only.
- DTOs are new Platform application DTOs. Legacy `DemoScenario`, `LinkMetric`,
  `LinkRays`, file paths, `time_step_ms`, process exit shapes and direct Bellhop
  execution payloads must pass through adapters or be replaced.

## Minimum resource surface

The future application API needs catalog/detail operations for Environments,
Scenarios and Experiments, including immutable version references and explicit
validation. Exact mutation endpoints remain backend design work; the frontend
must not infer them from the legacy FastAPI surface.

Environment responses should expose `AssetId`, package/asset format versions,
producer/source/normalization provenance, coordinate frame, axis metadata,
cell and no-arrival counts, checksum algorithm/value and validation state.
They must not expose repository root or package path.

## Run API baseline

### Create

```http
POST /runs
Content-Type: application/json

{
  "experiment_id": "...",
  "experiment_version": 3
}
```

Success creates a Run resource immediately (normally `201 Created`) and returns
at least `run_id`, lifecycle state, captured experiment/scenario/environment
identities and versions, and resource links. Creation is not a long-lived
execute-and-stream response.

### Read and stream

```text
GET /runs/:id
GET /runs/:id/events       -> text/event-stream (SSE)
GET /runs/:id/snapshots
GET /runs/:id/results
```

The Run resource is authoritative for lifecycle. SSE transports ordered Run
events and includes a stable event ID/cursor so reconnect and deduplication do
not depend on arrival timing. Malformed or unknown records surface as protocol
errors; they are not silently ignored. Snapshots and results are independently
fetchable resources so a client can reconstruct a projection after reconnect.

`GET /runs/:id/events` may stream current events and/or resume from an explicit
cursor according to the eventual backend contract. It must not create a Run or
advance simulation time.

### Future cancel

```text
POST /runs/:id/cancel
```

Cancellation is a server-side lifecycle command with an idempotency policy and
an explicit resulting Run state. `AbortController` only cancels the browser
request and is not a substitute for this endpoint. Cancel remains future until
runtime semantics are approved.

## Error and compatibility requirements

HTTP status and response error bodies must preserve a stable machine-readable
code plus owned message. Unknown schema/format versions are rejected, not
silently coerced. API versioning, authentication, pagination limits and SSE
retention are still TBD and require separate review before implementation.
