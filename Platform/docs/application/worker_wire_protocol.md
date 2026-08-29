# Worker wire protocol v1

## Transport and framing

Formal worker mode is `platform_sim_worker <environment-repository-root>`.
The parent writes one `StartRunCommand` JSON object followed by newline to
stdin. The worker writes only `WorkerMessage` JSON objects followed by newline
to stdout; diagnostics go only to stderr. A line is one complete frame.

Schema v1 limits command input to 1 MiB and each output message to 4 MiB.
Empty, partial, malformed and oversized frames fail explicitly. Every object
has numeric `schema_version: 1` and a string `type`; unknown versions, types,
missing fields and unknown fields are rejected. The nlohmann DOM parser does
not expose duplicate object member occurrence after parsing; schema v1
therefore does not claim duplicate-member detection and senders must not emit
duplicates.

## StartRunCommand

`StartRunCommand` carries RunId plus explicit preset identity
(ScenarioId, ExperimentId, definition version, acceptance profile), an
EnvironmentAssetId/reference, and execution parameters (cycle count, Rx
quality mode, equivalent noise power and deterministic seed). The environment
repository root remains process configuration and is not serialized as a
domain resource.

## Worker messages

The only message types are `WorkerStarted`, `WorkerRunEvent`,
`WorkerCompleted`, and `WorkerFailed`:

- success is `Started -> RunEvent* -> exactly one Completed`;
- simulation/composition failure is `Started -> RunEvent* -> exactly one Failed`;
- protocol failure may emit `WorkerFailed` with null RunId when no valid RunId
  could be recovered;
- no message may follow a terminal message.

`WorkerRunEvent` preserves the original RunId, RunEventSequence, Trace kind,
typed Trace payload and nanosecond timestamp. It is never renumbered.
`WorkerCompleted` contains the authoritative terminal RunRecord and complete
formal RunResult, including Acceptance verdict and `event_stream_complete`.
`WorkerFailed` contains an owned message, ErrorCode and category `Protocol`,
`Composition`, or `Simulation`.

## Numeric policy

All strong numeric IDs, versions, counts, RunEventSequence values and integer
nanoseconds are canonical decimal strings. This includes values below the
JavaScript safe-integer limit so consumers never need two policies. Floating
point is used only for physical/metric quantities whose typed model is already
floating point. Simulation time never uses floating seconds.

The protocol codec depends on the repository-vendored nlohmann/json only in
the worker adapter boundary. JSON does not enter the simulation core.
