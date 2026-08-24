# Frontend v2 visualization reuse assessment

## Decision

Reuse interaction and visual-language ideas from the audited frontend, not its
production source or legacy DTOs. Each future visualization receives a small,
read-only v2 view model assembled at the feature boundary.

| Legacy reference | Ideas worth retaining | DTO/behavior that must be replaced |
|---|---|---|
| `StudioCanvas` | SVG plan view, node selection/dragging in editor mode, edge focus, communication-event arcs and explicit world-to-view transform | `DemoNode`, `LinkMetric`, `CommunicationEvent`, `StudioEnvironmentBounds`, range heuristics and the component's combined edit/playback responsibilities |
| `ProfileView` | Shared horizontal coordinate domain, depth/bathymetry profile, selected-link emphasis, first-arrival/multipath overlays and metric trends | Direct `fetchEnvironmentPreviewRays`, `LinkRays`, `RayComponent`, legacy seconds/dB fields, preview file path and mixed editing/server-fetch logic |
| `Scene3DView` | Water/seabed spatial context, orbit controls, node/link/ray selection and coordinated highlighting | `DemoNode` roles/strings, `LinkRays`, legacy event colors, implicit coordinate remapping and default communication-range values |
| `TopologyPreview` | Compact overview SVG, deterministic coordinate normalization and simple node/link status encoding | `LinkMetric`, `tx_id`/`rx_id`, NLOS integer flag and delay-seconds formatting embedded in the component |
| `MiniTrend` | Small dependency-light SVG sparkline with min/average/max summary | Bare `number[]`, label-derived SVG IDs, implicit precision/unit choices and lack of timestamp/sample identity |

## Required v2 view-model boundaries

Future components should consume purpose-specific values such as:

- `NodeGeometryView` with strong string ID, label, position and display role;
- `TopologyEdgeView` with directed endpoints and explicit projection status;
- `RunEventProjection` with Run/Transmission/Reception identity and exact
  simulation time;
- `AcousticPathView` with explicit delay/gain/phase units and provenance;
- `MetricSeriesView` with timestamped samples, unit and formatting policy.

These are frontend view models, not C++ contracts and not backend storage
schemas. Adapters from application DTOs own unit conversion and missing-value
handling before render.

## Component boundaries

- Scenario Editor may enable node dragging; Run Monitor and Results use the
  same visual language in read-only mode.
- A visualization never fetches server data directly. Its route/feature owner
  supplies resolved view models.
- 2D SVG ideas can be reimplemented without a chart dependency. A future 3D
  implementation may evaluate React Three Fiber separately; no dependency is
  selected in this baseline.
- Coordinated selection belongs to UI state, while event buffers and playback
  cursors belong to Run projection state.

No file from `Platform/docs/frontend/legacy-reference` is copied into the
future `Platform/frontend/` implementation.
