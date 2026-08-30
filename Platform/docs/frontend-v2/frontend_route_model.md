# Frontend v2 route model

## Canonical routes

| Route | Owned resource/view | Mutation boundary |
|---|---|---|
| `/` | Overview | Read-only summaries and navigation |
| `/environments` | Environment catalog | Read-only in P0-S4-07 |
| `/environments/:assetId` | Environment detail | Read-only validated package metadata, axes, coverage and provenance |
| `/scenarios` | Scenario catalog | Read-only version catalog |
| `/scenarios/:scenarioId/versions/:version` | Scenario detail | Read-only immutable version |
| `/experiments` | Experiment catalog | Read-only version catalog |
| `/experiments/:experimentId/versions/:version` | Experiment detail | Read-only detail and explicit Run creation |
| `/runs` | Authoritative Run catalog | Read-only lifecycle/result availability |
| `/runs/:runId` | Run Monitor | Read-only Run, SSE events and progress |
| `/results` | Formal Result catalog | Completed Runs with atomically published results only |
| `/results/:runId` | Result analysis | Read-only analysis for the named Run |

`assetId`, `scenarioId`, `experimentId` and `runId` are domain identities, not
filenames. Route loaders must reject malformed identifiers and display a
not-found state separately from transport failure.

## Route ownership rules

- A route module fetches server resources through the server-state boundary;
  it does not duplicate them into a global UI store.
- Unsaved Scenario and Experiment drafts are scoped to their editor routes.
- Run playback controls belong to the Run/Result projection state and cannot
  mutate the Scenario draft.
- Results may link to captured Environment, Scenario and Experiment versions,
  but do not edit those resources.

## Deferred editor routes and legacy disposition

Scenario and Experiment editor routes are not registered in P0-S4-07. Future
editor work must use explicit draft/publish semantics and cannot simulate a
save in browser-only state.

The legacy `/studio`, `/config`, `/monitor` and `/results` routes all rendered
one large component. They are not aliases in the v2 model. Existing bookmarks
may later receive explicit redirects, but no v2 route may route these distinct
responsibilities back into one workbench component.

Administration/settings routes are deferred until concrete deployment and
workspace requirements exist.
