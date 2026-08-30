# Frontend / Backend Application Boundary

## Direction

```text
Frontend
  → FastAPI HTTP/SSE adapter
  → Python WorkerGateway (control plane)
  → out-of-process C++ simulation worker (execution plane)
  → application RunService and production executor
  → ScenarioRuntime
```

The frontend never calls runtime internals directly, and the FastAPI process
does not host ns-3. The backend exposes immutable Environment, Scenario and
Experiment resources, then resolves an Experiment version into an
out-of-process Run. Authentication and database persistence remain outside P0.

The stable top-level relationship is:

```text
Environment -> Scenario -> Experiment -> Run -> Result
```

Published Scenario and Experiment versions are read-only. Frontend-facing IDs
are logical IDs, never repository paths or filenames. Catalog lists are sorted
by ID and version.

## Stable read models

`RunRecord` exposes stable IDs and versions, lifecycle, exact integer-nanosecond simulation timestamps, final snapshot version and an optional owned failure summary. `RunResult` exposes only read-only application summaries:

- `RunProjectionSummary`;
- optional `AcceptanceReportSummary`;
- `FusionResultSummary` values;
- final `NodeSummary` values.

These DTOs do not expose `CycleWorkingState`, `ProtocolKnowledgeStore`, `ProtocolCyclePlan`, `TransmissionRecordStore`, `AcousticFieldAsset` internals, repository paths or a live runtime owner. The Environment reference is an asset ID plus explicit format version, never the package directory.

Application and worker adapters own conversion from authoritative C++ domain
and Environment repository values into this boundary. Frontend view-model
adapters may perform presentation/unit conversion, but browser floating-point
seconds never become authoritative simulation time. IDs, versions, counts and
integer nanoseconds that may exceed JavaScript safe integer range remain
canonical decimal strings.

## Read-only event boundary

Core `TraceEvent` remains unchanged. `RunEventRecord` wraps one typed trace with a RunId and a Run-local sequence assigned once at append. `RunService::ReadEvents` accepts a last-seen sequence cursor and a bounded limit; it never creates a Run, executes a Run or advances simulation time. Readers cannot access journal append or a trace sink pointer.

Events, snapshots and results are independent read resources. Event replay is
not required to reconstruct or validate the formal `RunResult`, and the journal
never stores a `WorldSnapshot`. SSE uses RunEventSequence directly as its event
ID and supports Last-Event-ID replay without creating a second sequence.
