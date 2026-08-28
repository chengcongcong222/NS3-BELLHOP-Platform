# Simulation Worker Boundary

## Control and execution planes

The target backend architecture separates process ownership:

```text
Frontend
  → future FastAPI control/API plane
  → worker process manager
  → one out-of-process C++ platform_sim_worker per Run
  → RunService → production executor → ScenarioRuntime → ns-3
```

FastAPI will own request validation, authorization, resource lookup and worker process management. It must not load or run ns-3 in its main process. The C++ worker is the simulation execution plane and retains all existing application/runtime causality rules.

This boundary isolates ns-3's process-global Simulator lifecycle, simulation/provider failure and future native-code faults from the API server. Each worker executes exactly one Run and exits. A later Run starts a new worker with a newly initialized ns-3 lifecycle; no clock, pending event or Simulator terminal state is shared between Runs. P0-S4-03 does not add a multi-run or parallel scheduler.

## Run and output ownership

The caller provides RunId and an explicit EnvironmentAssetId. RunId remains application metadata and is not used for communication IDs, event order, packet generation or physical calculations. The worker still executes through `RunService → AcceptanceRunExecutor → ScenarioRuntime`; it does not copy ScenarioRuntime or introduce a second scheduler.

The codec-independent bridge uses a typed variant:

- `WorkerStarted` reports process-side acceptance of one Run;
- `WorkerRunEvent` carries the unchanged RunId, RunEventSequence and typed Trace payload from RunService's journal;
- `WorkerCompleted` carries the authoritative Completed RunRecord and formal RunResult;
- `WorkerFailed` carries the specific Error and optional terminal RunRecord.

RunService remains the sole RunEventSequence authority. The worker replays existing records without renumbering them. Run lifecycle, event stream and formal result stay distinct resources; a terminal message does not replace RunRecord lifecycle authority.

The prototype bridge emits typed messages in process and then proves native process isolation with the standalone executable. Live cross-process message transport is intentionally deferred until an IPC codec is approved.

## Exit semantics

`platform_sim_worker` uses these process results:

- `0`: the Run completed successfully at the simulation/process level;
- `2`: command/protocol input failure;
- `3`: worker composition or simulation execution failure.

Acceptance metric failure is a domain verdict inside a successfully produced RunResult, not a worker failure. For example, a completed modeled-BER run whose acceptance report is `Fail` still exits 0.

The current executable accepts the bounded prototype input:

```text
platform_sim_worker <repository-root> <environment-asset-id> <run-id> <pass|verdict-fail>
```

The repository root is worker process configuration, not a domain resource ID and is never exposed through application DTOs. This CLI is an isolation smoke interface, not the future public API or a general serialization format.

## Serialization audit

The formal repository currently has no approved JSON dependency or existing worker/IPC codec. No nlohmann-json, Boost.JSON, RapidJSON, jsoncpp or simdjson integration was found. P0-S4-03 therefore does not hand-write a general JSON parser and does not introduce JSON into contracts, runtime, planning or PHY.

Future reviewed options include a worker-adapter-only dependency on `nlohmann_json` for a compact DTO codec or `Boost.JSON` if Boost becomes an approved platform dependency. The selection must freeze schema/versioning, malformed-input behavior, framing, payload limits and dependency delivery before implementation. Binary framing and OS IPC remain alternative decisions. Until then, process smoke uses validated positional fixture arguments and process exit only; typed bridge behavior is tested without a codec.

HTTP, SSE framing, database storage, authentication, cancellation, retry, crash recovery and concurrent scheduling remain future work.
