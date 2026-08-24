# Frontend v2 route model

## Canonical routes

| Route | Owned resource/view | Mutation boundary |
|---|---|---|
| `/` | Overview | Read-only summaries and navigation |
| `/environments` | Environment catalog | Starts explicit import/register workflows; registered items remain immutable |
| `/environments/:assetId` | Environment detail | Read-only validated package metadata, axes, coverage and provenance |
| `/scenarios` | Scenario catalog | Create a draft or choose a version |
| `/scenarios/:scenarioId/edit` | Scenario Editor | Edits one draft: nodes, capabilities, geometry and environment binding |
| `/experiments` | Experiment catalog | Create and validate experiment configurations |
| `/experiments/:experimentId` | Experiment Editor/detail | Edits an eligible revision; binds an immutable scenario version |
| `/runs` | Run catalog | Creates a Run from a validated experiment; filters lifecycle state |
| `/runs/:runId` | Run Monitor | Read-only events/snapshots/progress, plus explicit supported commands |
| `/results` | Result catalog/comparison | Selects completed or active Run projections for read-only comparison |
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

## Legacy route disposition

The legacy `/studio`, `/config`, `/monitor` and `/results` routes all rendered
one large component. They are not aliases in the v2 model. Existing bookmarks
may later receive explicit redirects, but no v2 route may route these distinct
responsibilities back into one workbench component.

Administration/settings routes are deferred until concrete deployment and
workspace requirements exist.
