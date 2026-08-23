# Legacy frontend architecture audit

## Scope and provenance

- Legacy frontend root: `/home/ccc/work/NS3_Factory/frontend`
- Audit mode: read-only; no dependency installation, formatting, build, or lockfile change
- Snapshot policy: 25 selected source/configuration files; no generated outputs, cache, result data, media, backup, or lockfile
- Secrets result: no credential-bearing `.env` file, token, password, API key, cookie, or authorization literal was found in the selected files. No redaction was required.

The only endpoint configuration in the snapshot is `VITE_API_BASE`, with a localhost development fallback and Vite proxy to `http://127.0.0.1:8000`. This is configuration, not a credential.

## Technology stack

| Concern | Actual implementation |
|---|---|
| UI | React 19.1 + TypeScript 5.9 |
| Build/dev | Vite 7.1 |
| Routing | `react-router-dom` 7.8, declarative `BrowserRouter`/`Routes` |
| Shared client state | Zustand 5.0 for studio selection, run, log, and playback state |
| Server access | A single hand-written `fetch` service module |
| Live run stream | Streaming `fetch` response parsed as SSE records; not `EventSource` |
| 2D visuals | Hand-authored SVG in topology, profile, trend, and studio-canvas components |
| 3D visuals | three.js through React Three Fiber and Drei |
| Styling | One large global CSS file plus extensive inline styles |

There is no Redux, TanStack Query, Recharts, ECharts, React Flow, WebSocket client, or dedicated server-state cache.

## Composition

`main.tsx` mounts `App.tsx`. `App.tsx` bootstraps a scenario and latest results, owns the active scenario, and declares all routes beneath `Shell`. `Shell` owns primary and workbench navigation. Four routes (`/studio`, `/config`, `/monitor`, `/results`) render the same 4,008-line `StudioPage` with a different initial presentation.

The frontend is therefore a useful interaction prototype, but its main workbench combines scenario editing, model binding, environment binding, run control, streaming logs, playback, visualization, and result analysis in one component. This is the principal maintainability and migration risk.

## State management

- Server state: loaded manually into React component state with `useEffect`; refetching and error handling are page-specific. There is no cache invalidation model.
- Global UI/runtime state: Zustand holds selection, view mode, run phase/logs/timer, playback controls, and communication events.
- Scenario editing state: cloned and edited inside `StudioPage`, mixed with server data, derived visualization state, and run lifecycle state.
- App state: `App` owns the active scenario, dataset, bootstrap errors, and a run-version invalidation counter.

The boundaries between server state, UI state, and scenario draft state are not explicit. A rewrite should separate an immutable server snapshot, a scenario draft/edit session, and ephemeral view/playback state.

## Visualization findings

- `TopologyPreview`: compact SVG topology on the overview page.
- `StudioCanvas`: editable SVG plan view, links, node dragging, communication-event paths, range status, and bathymetry hints.
- `ProfileView`: SVG depth/range profile with bathymetry, Bellhop ray paths, link metrics, and trends.
- `Scene3DView`: three.js seabed/water/nodes/link/ray visualization with orbit controls.
- `MiniTrend`: small SVG sparkline.

These interaction and visual encodings have design-reference value. Their data models are coupled to legacy `DemoScenario`, `LinkMetric`, `LinkRays`, and trace schemas, so direct reuse should be selective.

## Backend coupling and risks

`services/api.ts` exposes a broad legacy FastAPI surface spanning scenario files, model libraries, environment databases, WOSS imports, Bellhop execution, result files, archives, templates, and run control. Several payloads contain file paths, legacy protocol strings, `time_step_ms`, Bellhop engine paths, and old result/event shapes. Those DTOs must not become new Platform contracts.

The live-run API is a long-lived `POST /api/run` response whose `data:` records are parsed manually into `start`, `log`, `done`, `error`, and `cancelled`. Malformed records are ignored. Cancellation is client-side `AbortController`; no explicit remote cancellation endpoint is represented.

## Migration conclusion

Use the old frontend as a product-discovery and visualization reference. Reuse small presentation components only after replacing their legacy DTO inputs. Rewrite navigation, page boundaries, server-state access, scenario editing, run lifecycle, and Platform API integration around the new domain model. Do not incrementally adapt the 4,008-line workbench or import code from `legacy-reference`.
