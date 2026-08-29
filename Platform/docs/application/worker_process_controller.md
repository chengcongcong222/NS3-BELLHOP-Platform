# Worker process controller

`WorkerProcessController` is the backend-side, HTTP-independent owner of one
worker child process. Future FastAPI code may call this adapter without loading
ns-3 into the control-plane process.

Its OS lifecycle is `NotStarted -> Starting -> Running -> Completed|Failed`
and is deliberately distinct from application `RunLifecycle`. A controller is
one-use. It creates stdin/stdout pipes, forks and execs the configured worker,
writes one bounded StartRunCommand, decodes bounded stdout frames, forwards
typed RunEvents to `IWorkerEventConsumer`, waits for the child, and validates
terminal/exit consistency.

Valid terminal combinations are:

- `WorkerCompleted` plus exit 0: Completed;
- `WorkerFailed` plus nonzero exit: Failed with a typed worker failure.

Completed plus nonzero, Failed plus zero, EOF before terminal, malformed or
oversized stdout, sequence/RunId mismatch, an event after terminal, pipe
failure, and signal termination are process/protocol failures. Parent-side
errors never crash the backend process. A fresh controller starts a fresh
worker with ns-3 time zero and RunEventSequence one; no controller implements
multi-run scheduling, retry, cancellation, HTTP, SSE or complex backpressure.
