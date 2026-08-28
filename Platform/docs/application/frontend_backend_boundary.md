# Frontend / Backend Application Boundary

## Direction

```text
Frontend
  → future HTTP/API adapter
  → application RunService and repositories
  → application acceptance/runtime executor
  → ScenarioRuntime
```

The frontend never calls runtime internals directly. HTTP, FastAPI, JSON, SSE, authentication and database persistence are not part of P0-S4-01.

## Stable read models

`RunRecord` exposes stable IDs and versions, lifecycle, exact integer-nanosecond simulation timestamps, final snapshot version and an optional owned failure summary. `RunResult` exposes only read-only application summaries:

- `RunProjectionSummary`;
- optional `AcceptanceReportSummary`;
- `FusionResultSummary` values;
- final `NodeSummary` values.

These DTOs do not expose `CycleWorkingState`, `ProtocolKnowledgeStore`, `ProtocolCyclePlan`, `TransmissionRecordStore`, `AcousticFieldAsset` internals, repository paths or a live runtime owner. The Environment reference is an asset ID plus explicit format version, never the package directory.

Application adapters own conversion from assembly results into this boundary. Frontend view-model adapters may later perform presentation/unit conversion, but browser floating-point seconds never become authoritative simulation time.

## Deferred event boundary

Core `TraceEvent` remains unchanged. A future S4-02 event-facing application projection may add a Run-local sequence/cursor while wrapping typed trace summaries, without modifying core trace identity or advancing simulation time. SSE transport, retention, replay and reconnect semantics are therefore intentionally deferred rather than embedded in RunService.
