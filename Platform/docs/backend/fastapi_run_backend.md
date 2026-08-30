# P0-S4-05 FastAPI Run backend

## Process boundary

FastAPI is the control/API plane. `platform_sim_worker` is the simulation
execution plane and remains the only owner of `ScenarioRuntime ->
ns3::Simulator`. The Python process never imports or embeds ns-3. It starts the
worker with `asyncio.create_subprocess_exec(executable, environment_root)`;
there is no shell command or C++ controller binding.

Worker stdin/stdout uses the existing schema v1 NDJSON model. stderr is
captured as bounded human diagnostics and is never parsed as protocol or
returned verbatim as an HTTP error body.

## P0 API

- `GET /health` checks backend liveness and worker executable availability.
- `POST /runs` resolves an immutable Experiment version, creates its Run and
  immediately returns 201.
- `GET /runs/{run_id}` returns authoritative control-plane lifecycle.
- `GET /runs/{run_id}/results` is available only for Completed Runs.
- `GET /runs/{run_id}/events` streams replayable SSE records.

The backend generates `run-<opaque>` RunIds. The request exposes only
`experiment_id` and `experiment_version`. The resource catalog resolves the
captured Experiment -> Scenario -> Environment chain and then constructs the
existing schema-v1 worker command. Generic Platform internals are not
serialized into a second public configuration schema.

Environment, Scenario and Experiment read endpoints and their immutable
version semantics are documented in `backend_resource_catalog.md`.

All identities, versions, counts, event sequences and integer nanoseconds are
JSON decimal strings in both HTTP and worker wire representations. JavaScript
Number is never authoritative for these values.

## Lifecycle, concurrency and retention

P0 supports one active Run. A concurrent POST returns `BackendBusy` rather
than entering a partially implemented scheduler. Created transitions to
Running when execution ownership begins and then exactly one Completed or
Failed terminal state. A business acceptance verdict of Fail remains a
Completed Run with a formal result.

The catalog, events and results are in memory. Restarting FastAPI intentionally
loses P0 backend state. There is no database, authentication, cancel, retry,
WebSocket, pybind11 or multi-run scheduler in this phase.

HTTP request cancellation, SSE disconnect and later browser reconnect are
observation-plane events only. They do not terminate the worker, change Run
lifecycle or affect the simulation result. There is no user Cancel API in P0.
FastAPI application shutdown is different: the application lifespan closes the
catalog, terminates every active child owned by the Python gateway and waits for
each child to be reaped, so backend shutdown cannot leave an orphan worker.

A WorkerCompleted frame is provisional until stdout framing has ended and the
child has been reaped with exit status zero. Only after that consistency check
passes does the catalog atomically publish both Completed lifecycle and its
formal Result. A business acceptance verdict of Fail still follows this normal
Completed path.

## SSE replay

SSE `id` is the worker-provided `RunEventSequence`; the backend never
renumbers it. Missing `Last-Event-ID` means cursor 0. A supplied cursor must be
canonical nonnegative decimal and no greater than the latest sequence.
Backlog selection and live waiting share one catalog condition, preventing a
gap at their boundary. Terminal streams replay the remaining prefix and close.

## Error body

Errors use `{ "error": { "code": ..., "message": ... } }`. Codes distinguish
`NotFound`, `AlreadyExists`, `InvalidRequest`, `RunNotReady`, `RunFailed`,
`BackendBusy`, `WorkerProtocolFailure` and `WorkerProcessFailure`.
