# Frontend / Backend Application Boundary

## Direction

```text
Frontend
  → future HTTP/API adapter
  → application RunService and repositories
  → application acceptance/runtime executor
  → ScenarioRuntime
```

The frontend never calls runtime internals directly. HTTP, FastAPI, JSON, an SSE socket, authentication and database persistence are not part of P0-S4-02.

## Stable read models

`RunRecord` exposes stable IDs and versions, lifecycle, exact integer-nanosecond simulation timestamps, final snapshot version and an optional owned failure summary. `RunResult` exposes only read-only application summaries:

- `RunProjectionSummary`;
- optional `AcceptanceReportSummary`;
- `FusionResultSummary` values;
- final `NodeSummary` values.

These DTOs do not expose `CycleWorkingState`, `ProtocolKnowledgeStore`, `ProtocolCyclePlan`, `TransmissionRecordStore`, `AcousticFieldAsset` internals, repository paths or a live runtime owner. The Environment reference is an asset ID plus explicit format version, never the package directory.

Application adapters own conversion from assembly results into this boundary. Frontend view-model adapters may later perform presentation/unit conversion, but browser floating-point seconds never become authoritative simulation time.

## Read-only event boundary

Core `TraceEvent` remains unchanged. `RunEventRecord` wraps one typed trace with a RunId and a Run-local sequence assigned once at append. `RunService::ReadEvents` accepts a last-seen sequence cursor and a bounded limit; it never creates a Run, executes a Run or advances simulation time. Readers cannot access journal append or a trace sink pointer.

Events, snapshots and results are independent read resources. Event replay is not required to reconstruct or validate the formal `RunResult`, and the journal never stores a `WorldSnapshot`. A future HTTP/SSE adapter may serialize these records and map a transport resume token to the application cursor, but HTTP status, headers, `text/event-stream`, retention and reconnect policy remain S4-03 work.
