# Legacy page inventory

| Page | Route and entry | User purpose and major interaction | Read APIs | Write/run APIs | Live/visual/state notes |
|---|---|---|---|---|---|
| Overview / 总览首页 | `/` → `OverviewPage` | Inspect active scenario and headline metrics; preview topology; derive or create a scenario; enter configuration | Bootstrap reads scenarios, scenario, results | `POST /scenario/derive`; wizard creation flow | `TopologyPreview` SVG and metric cards; page-local dialogs, App-owned active dataset |
| Workbench overview | `/studio` → `StudioPage` | Unified node/link/scenario editing and model-library workbench | Scenario bootstrap plus components, templates, environment DBs, data files, bathymetry, rays, events, results, archives | Scenario save/derive; node-template CRUD | Zustand selection/view/run/playback plus a large page-local scenario draft |
| Scenario configuration | `/config` → same `StudioPage` | Configure nodes, roles, links, protocol/model bindings, runtime settings, environment and topology | Same workbench reads | `PATCH /scenario/:name`, derive/create assets/templates | SVG plan/profile and 3D scene; not an independent page model |
| Run monitor | `/monitor` → same `StudioPage` | Start a simulation, watch log/events, stop client stream, inspect time and communication activity | Events, results, rays, archives after run | Streaming `POST /run`; cancellation only through `AbortController` | SSE-formatted streamed fetch; 500 ms UI elapsed timer; Zustand run/log/playback state |
| Results analysis | `/results` → same `StudioPage` | Inspect latest or archived metrics/events/rays, filter event layers/codes, replay communication and node motion | Results, events, archive list/detail | None intrinsic; workbench still exposes save/run controls | SVG/3D views, trends and playback timeline; server and view state coexist |
| Bellhop environment management | `/environment` → `EnvironmentPage` | Browse/edit environment DBs; manage WOSS sources; import/build data; open an environment draft | Environment DB/capabilities, WOSS sources, data files | Environment/WOSS CRUD, cache/real-data import, build environment(s), scenario generation | Dense page-local form and dialog state; legacy file/engine paths exposed |
| Experiment templates | `/templates` → `ExperimentTemplatesPage` | Create/edit/delete reusable templates; bind environment/runtime; apply or derive a scenario | Templates, environments, scenarios | Template CRUD, apply, derive | Page-local draft/loading/error state; no shared server cache |
| Scenario and archive assets | `/assets` → `AssetsPage` | Select/delete scenarios; inspect latest results/history; browse archives; compare runs and generate report | Scenarios, results, history, archive list/detail, report | Delete scenario; loading a scenario changes App state | Tables/cards; report comparison state held locally |
| System settings | `/settings` → `SettingsPage` | Check backend/run status and edit current scenario metadata/settings | Status | Save current scenario metadata/settings | Manual status refresh plus initial effect; page-local form state |

## Cross-page state ownership

- `App`: active scenario, loaded dataset, bootstrap/retry error, run-version invalidation.
- `useStudioRuntimeStore`: selected scene/node/edge, 2D/profile/3D mode, run phase/logs/elapsed time, playback, communication events.
- `StudioPage`: editable scenario, bindings, templates/assets, environment data, results/archives, event filters, playback derivations, dialogs and menu state.
- Other pages: local form, request, selection, error, and dialog state.

The inventory does not identify a separate “model orchestration” page: model/protocol composition is embedded in the workbench and `PropertyInspector`.
