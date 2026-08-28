# Application Run Lifecycle

## State machine

P0 has one synchronous command path:

```text
Created → Running → Completed
                  ↘ Failed
```

`CreateRun` resolves and captures the exact Experiment, Scenario and Environment identities/versions and immediately creates a `RunRecord`. It does not execute simulation as part of creation.

`ExecuteRun` is synchronous in P0. Before invoking the executor it publishes `Running` to the repository. Successful execution atomically publishes a terminal `Completed` record and its `RunResult`; execution failure publishes terminal `Failed` with the original machine-readable `ErrorCode` and owned message. Completed and Failed records cannot be executed again, and duplicate RunId creation is rejected.

Errors remain distinguishable:

- missing Experiment or Scenario: `NotFound` with the named resource class;
- missing Environment asset: `NotFound` with an Environment-specific summary;
- duplicate RunId: `AlreadyExists`;
- re-execution or unavailable result: `FailedPrecondition`;
- simulation/provider failures: their original error code and message are retained in `RunFailureSummary`.

`GetRun` is the lifecycle authority. `GetResult` succeeds only after a completed result has been published. `ScenarioRuntime` is never returned to callers.

Cancellation, queued workers, retry/recovery, checkpoints, persistent repositories and concurrent execution policy remain future design work. Terminal records are deliberately not reset in place.
