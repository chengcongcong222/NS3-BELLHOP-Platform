# Frontend v2 state model

## Four separate state domains

| Domain | Examples | Owner and lifetime | Prohibited coupling |
|---|---|---|---|
| Server state | Environment metadata, scenario versions, experiments, Run resources, snapshots and results | Query/cache layer; server remains authoritative | Do not copy into a mixed global UI store or edit in place |
| Scenario draft state | Node edits, capability edits, geometry and environment binding plus validation issues | One Scenario Editor session; initialized from an immutable server version | Must not contain live Run events, result playback or server cache metadata |
| UI state | Selection, panels, viewport, filters and dialog state | Feature-local state or small domain-specific UI stores | Must not masquerade as persisted domain state |
| Run projection/playback state | SSE cursor, buffered event projection, selected simulation time and playback controls | One Run/Result view; reconstructible from server events/snapshots | Must not become authoritative runtime state or mutate scenario/experiment inputs |

## Server-state direction

TanStack Query is the P0-S4-07 server-state layer because the legacy frontend
had page-specific `fetch` calls with no cache/invalidation model. Query keys
include resource identity/version, and the Run catalog is invalidated after a
successful Run creation. The typed API boundary preserves loading, protocol,
transport and not-found outcomes separately.

## Draft policy

Scenario and Experiment editors use explicit draft objects with:

- the base resource identity and version;
- locally edited values;
- dirty fields;
- synchronous validation issues;
- server validation/save state.

Saving creates or updates only a server-authorized draft/revision. Publishing
or launching never silently mutates the base version. Navigating away with
unsaved changes requires an explicit user decision.

## UI and playback stores

Zustand may be retained for small UI/draft/playback domains, but each domain
uses a separate store and API. The legacy `studioRuntimeStore` mix of selected
entities, run lifecycle, logs, timer, events and playback must not be migrated.

SSE records are appended to a Run projection keyed by `RunId` and the server
`RunEventSequence`. Duplicate sequence values are ignored, a gap enters an
explicit protocol-error state, and native `EventSource` reconnect behavior is
used. The projection is disposable and recoverable from
`GET /runs/:id/events`; it is never a second source of truth.

## Determinism and units

Frontend projections preserve exact IDs, simulation timestamps and explicit
units received from APIs. Wall-clock UI timers may indicate user-facing wait
time but cannot fabricate or advance simulation time. Array order with domain
meaning must follow server canonical order rather than object/map iteration.
