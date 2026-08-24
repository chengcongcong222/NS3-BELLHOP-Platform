# Frontend v2 architecture baseline

## Status and source boundary

This document is the formal architecture baseline for the future Platform
frontend. It uses the read-only legacy audit at commit
`c2de48601ab077eafdedf0dd25324be4db6358c1` as product-discovery evidence.
Legacy source and DTOs are reference material only; they are not a starting
implementation and must not be copied into production.

The future application root is `Platform/frontend/`. This phase does not
create a React application or select/install dependencies.

## Top-level object model

| Object | Authoritative responsibility | Edit policy | Lifecycle |
|---|---|---|---|
| Environment | Immutable validated acoustic asset, coordinate frame, full axes, coverage and provenance | Import/build input may be drafted outside the runtime asset; a registered asset is read-only | imported/built → validating → valid or rejected; replacement receives a new `AssetId` |
| Scenario | Node identities/capabilities, initial committed state, geometry and bound Environment identity | Editable only through a scenario draft; a published version is immutable | draft → validated → versioned |
| Experiment | Exact scenario version, routing/MAC/component choices, deterministic seed and run configuration | Editable before use; each Run captures an immutable experiment snapshot | draft → validated → ready; later edits form a new revision |
| Run | One execution resource with captured input identities, lifecycle, events, snapshots and result reference | Read-only except an explicit lifecycle command such as future cancel | queued → running → completed, failed or cancelled |
| Result | Read-only projections, metrics, timelines, comparisons and exports derived from a Run | Never edits runtime or scenario state | available after or incrementally during a Run according to backend capability |

Filesystem paths are not identities for any object. The frontend uses stable
domain IDs and versions supplied by the server.

## Ownership and editor boundaries

- Environment pages inspect provenance, axes, coverage/no-arrival and
  validation. Bellhop execution remains an offline producer/job concern.
- Scenario Editor owns nodes, capabilities, initial geometry and environment
  binding. It does not own routing/MAC choices or run streaming.
- Experiment Editor owns scenario-version selection, routing, MAC, component
  configuration, seed and launch configuration. It does not mutate a Scenario.
- Run Monitor is a read-only projection of one Run plus explicit lifecycle
  commands supported by the API. It cannot edit the captured inputs.
- Results pages are read-only analysis/comparison views and never feed control
  decisions back into a live run.

## Core flow

```text
register validated Environment
  -> edit and version Scenario
  -> configure and validate Experiment
  -> create Run resource
  -> monitor Run events and snapshots
  -> inspect or compare Results
```

The server owns authoritative resources and run lifecycle. The browser owns
only drafts, presentation state and read-only projections. The old combined
`StudioPage`/single mixed store architecture is explicitly retired.

## Runtime and environment boundary

Environment packages are loaded and validated before ScenarioRuntime starts.
The UI may request repository metadata or initiate an offline import workflow,
but a live channel query never performs filesystem or frontend I/O. The UI
must distinguish no physical arrival from load/coverage/provider errors.

## Deferred implementation choices

React component structure, authentication, generated API clients, deployment,
accessibility implementation details and final visual design remain future
work. The state and route boundaries in this baseline must be preserved when
those choices are made.
