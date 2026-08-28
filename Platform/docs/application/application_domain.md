# P0 Application Domain

## Object model

The application layer owns the stable server-side resources that sit above `ScenarioRuntime`:

```text
EnvironmentAssetRepository
        ↓ immutable asset identity/version
ScenarioDefinition
        ↓ immutable scenario identity/version
ExperimentDefinition
        ↓ captured run configuration
RunRecord ──→ RunResult
```

This model matches the frozen Frontend V2 `Environment → Scenario → Experiment → Run → Result` information architecture. P0-S4-01 implements Scenario, Experiment, Run and Result; Environment remains the existing validated `EnvironmentAssetRepository` resource.

`ScenarioId`, `ExperimentId` and `RunId` are different strong types backed by caller-provided validated strings. They accept only a bounded stable identifier grammar and never use a filesystem path as identity. Scenario and Experiment references always include an explicit non-zero version.

## Immutable definitions

`ScenarioDefinition` captures:

- ID, version and owned name;
- canonical node identities, capabilities, initial positions and velocities;
- an Environment asset identity and format version;
- an explicit mobility model;
- the fusion-center NodeId.

It does not contain `WorldSnapshot`, Trace, mutable runtime state or a `ScenarioRuntime` object.

`ExperimentDefinition` captures how the scenario is run:

- exact Scenario ID/version;
- routing and MAC modes/configuration;
- PHY rate, carrier, occupied bandwidth, source level, noise and quality mode;
- network-update interval and simulation cycle count;
- deterministic seed/config;
- feature-fusion and acceptance requirements.

Definitions are constructed through validation factories and stored by value in deterministic in-memory repositories. Registering the same ID/version twice is `AlreadyExists`; a missing exact version is `NotFound`. Editing or publishing revisions and persistent storage remain future work.

## Acceptance presets

The existing Acceptance4Node and Extended6Node configurations remain authoritative. Application preset adapters convert them into Scenario/Experiment definitions without deleting or replacing their assembly configuration. The production-style `AcceptanceRunExecutor` resolves the captured Environment identity through `EnvironmentAssetRepository`, validates its format version, composes the existing acoustic provider, PHY, M3/M4, feature workload and `ScenarioRuntime`, then returns application DTOs.

The current deterministic implementation captures a seed but has no stochastic component. RunId is never used in simulation IDs, scheduling, provider calculations or packet generation, so equal captured definitions/assets/config produce equal simulation results.
