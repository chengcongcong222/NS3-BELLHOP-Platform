# P0-S4-06 Backend Resource Catalog

## Authority chain

The frontend resource chain is:

```text
Environment version
  -> Scenario version
  -> Experiment version
  -> Run
  -> Result
```

Environment existence and metadata are read through the C++ resource catalog
adapter, which opens and validates the existing `EnvironmentAssetRepository`.
Scenario and Experiment resources are projected by the same adapter from the
existing C++ application-domain Acceptance presets. Python validates the
adapter document and owns only immutable HTTP-facing models, deterministic
indexes and reference resolution. It does not parse Environment packages or
redefine simulation-domain semantics.

No HTTP resource contains a repository root, absolute path, package directory
or filename identity. An Environment is identified only by
`EnvironmentAssetId` plus its explicit asset format version.

## Read API

- `GET /environments`
- `GET /environments/{environment_asset_id}`
- `GET /scenarios`
- `GET /scenarios/{scenario_id}/versions/{version}`
- `GET /experiments`
- `GET /experiments/{experiment_id}/versions/{version}`
- `GET /runs`
- `GET /results`

Lists are sorted by ID and then version. They do not depend on filesystem or
dictionary insertion order. Published Scenario and Experiment versions are
immutable; P0 deliberately provides no create, update or delete API. New
content under the same logical ID requires a new version.

Startup publishes the catalog atomically: the complete adapter document is
parsed, every resource is validated, duplicate identities/versions are
rejected, every Scenario-to-Environment and Experiment-to-Scenario reference
is resolved, and only then is the deterministic index made available to HTTP
handlers. There is no partial catalog and no P0 hot reload. Newly supplied
catalog content becomes visible only after a future explicit refresh design or
a backend process restart.

The two frontend-readiness catalogs are also process-local read models.
`GET /runs` returns every known Run summary, including captured resources,
lifecycle, event completeness, formal-result availability and owned failure.
`GET /results` returns only Completed Runs with an atomically published formal
Result, summarized by acceptance status, exact simulation duration and fusion
result count. Both lists are sorted by RunId and neither relies on browser
session history as an authority.

## Run resolution

`POST /runs` accepts exactly `experiment_id` and `experiment_version`.
Resolution follows the immutable references:

```text
Experiment -> Scenario -> Environment
```

Only after every reference and version is available does the backend build the
existing schema-v1 `StartRunCommand`. Acceptance4Node is the formal acceptance
Experiment; Extended6Node is an extension Experiment. The schema-v1 worker
bridge currently requires Scenario and Experiment definition versions to be
equal, which both P0 presets satisfy.

At Run creation, Experiment, Scenario and Environment references are copied
into the Run command and control-plane record. Later catalog additions cannot
change an existing Run. Run detail and Result detail return the captured
references, giving Result provenance without exposing runtime state.

Worker completion is identity-attested against those captured references. A
WorkerCompleted message must return the exact Run, Experiment/version,
Scenario/version and Environment identity/version. Any mismatch is a
WorkerProtocolFailure, fails the Run and withholds the formal Result even when
the child exits zero and reports an acceptance Pass.

Versions, counts, NodeIds, exact nanoseconds and other potentially unsafe web
integers remain canonical decimal strings. Physical scalar summaries remain
finite JSON numbers.

## Error model and P0 scope

Resource lookup uses owned machine-readable errors:

- `EnvironmentNotFound`;
- `ScenarioNotFound`;
- `ScenarioVersionNotFound`;
- `ExperimentNotFound`;
- `ExperimentVersionNotFound`;
- `InvalidReference`.

P0 has no database, authentication, generic persistence, Bellhop subprocess,
arbitrary scenario editor or resource mutation endpoint. The catalog is
rebuilt from authoritative inputs when the backend starts. The adapter path,
repository root and selected Acceptance environment are required startup
configuration; the backend does not fabricate an empty or implicit catalog.

The five JSON files under `backend/tests/fixtures` freeze Environment,
Scenario, Experiment, Run and Result response shapes for P0-S4-07 frontend
development.
