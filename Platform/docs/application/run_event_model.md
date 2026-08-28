# Run Event Model

## Trace and application identity

`TraceEvent` is the frozen causal-core observation value. It owns simulation time, kind and typed payload, but deliberately has no RunId, application sequence, wall-clock timestamp or transport cursor.

`RunEventRecord` is the application read model:

```text
RunId + RunEventSequence + TraceEvent
```

The current Run's `RunEventSink` assigns the sequence when the event is successfully appended. Sequence starts at 1, is strictly increasing within that Run and follows Trace Emit call order. Each Run has an independent sequence space. Events sharing a simulation timestamp are not re-sorted. Reading the same record again preserves its sequence and does not create a new identity.

Append is a private journal mutation capability available to `RunEventSink`; ordinary readers receive only `ReadAfter` and `GetLatestSequence`. This prevents a future API caller from injecting a fabricated event into authoritative Run history.

## Cursor and bounded reads

The cursor is the last sequence already seen by the reader. Sequence 0 is the before-first-event cursor and is never assigned to a record.

- `ReadAfter(0, limit)` begins with sequence 1.
- `ReadAfter(k, limit)` returns only sequence values greater than `k`, ascending.
- cursor equal to latest returns an empty page.
- cursor greater than latest returns `OutOfRange`; it never silently resets.
- limit must be in `[1, 256]`.

Concatenating successive pages is exactly equivalent to one ordered read of the same completed journal, subject to the same maximum page size.

## Failure and lifecycle independence

Journal append is non-causal. `RunEventSink` observes append failure, marks the stream incomplete and still returns control without changing simulation state. A Run can therefore be Completed with a formal RunResult and `event_stream_complete=false`. RunResult remains based on the authoritative simulation outcome, never on event replay.

A Failed Run retains every record successfully appended before the simulation failure. Completed and Failed journals remain readable for as long as the corresponding in-memory Run resources are queryable. P0 does not delete history during terminal transitions.

Only successful appends consume sequence values. If append attempts are success, failure, success, the two formal records receive sequences 1 and 2. Once any append fails, `event_stream_complete` remains false even if later appends succeed. Sequence allocation is checked: a journal whose latest successful record is `UINT64_MAX` rejects the next append with `Overflow` and never wraps to zero; this observation failure remains non-causal.

The event journal contains simulation Trace observations, not lifecycle transition events. `RunRecord` is the sole authority for Created, Running, Completed and Failed. Event presence, absence or the final Trace kind must never be used to infer lifecycle. `ReadEvents` is a read-only query for every existing Run lifecycle: Created is normally empty, Running returns its successfully appended prefix, and Completed/Failed support terminal replay.

Events are not snapshots and do not store `WorldSnapshot`. Results, future snapshots and events are independent resources. Persistence, retention, HTTP, JSON, SSE framing, Last-Event-ID mapping, authentication and concurrent writers remain future work.
