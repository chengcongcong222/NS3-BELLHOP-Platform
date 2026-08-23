# Initial information architecture for the new frontend

## Primary navigation

1. **Overview** — platform health, recent environments/scenarios/experiments/runs, review-required items, and shortcuts.
2. **Environments** — immutable/imported acoustic datasets, provenance, coverage, validation, and offline build/import jobs.
3. **Scenarios** — node capabilities, initial committed state, geometry, environment selection, and editable scenario drafts.
4. **Experiments** — planner/policy/component configuration, scenario binding, deterministic seed/configuration, validation, and launch intent.
5. **Runs** — queued/running/completed executions, lifecycle, logs/traces, snapshots, and cancellation where the backend supports it.
6. **Results** — run metrics, event timeline, topology/path projections, comparisons, and export.
7. **Administration** — deployment status, component compatibility and user/workspace settings; defer until real requirements exist.

## Object responsibilities

| Object | Owns | Must not own |
|---|---|---|
| Environment | Dataset provenance, axes/coverage, coordinate frame, normalized asset validation | Scenario node identity, packet identity, live run state |
| Scenario | Node identities/capabilities, starting snapshot, geometry, selected environment | A running process, result archives, frontend-only playback state |
| Experiment | Scenario reference/version, component/planner choices, seed and run configuration | Mutable scenario editing session, environment generation internals |
| Run | Experiment snapshot, lifecycle, trace/log stream, checkpoints and result references | Reusable template configuration |
| Result view | Read-only projections and comparisons over completed/active run data | Mutation of authoritative runtime state |

## Core flow

```text
Import/validate Environment
  -> create or edit Scenario draft
  -> validate and version Scenario
  -> configure Experiment
  -> preflight compatibility
  -> launch Run
  -> monitor Run events/snapshots
  -> analyze/compare Results
```

## Page boundaries

- Scenario editor: topology/geometry, node inspector, environment binding, validation; no run streaming.
- Experiment editor: component selection and planning/runtime configuration against a versioned scenario; no direct asset generation.
- Run monitor: lifecycle, logs, traces, cycle/snapshot progress and read-only topology projection; no scenario mutation.
- Result analysis: time/event filters, path/topology/environment views and comparison; no control decisions.
- Environment detail: provenance, coordinate frame, coverage/no-arrival map, axes and import validation; Bellhop execution remains an offline service/job concern.

The IA intentionally defines navigation and responsibility only. It does not select a React framework, implement APIs, or prescribe database persistence.
