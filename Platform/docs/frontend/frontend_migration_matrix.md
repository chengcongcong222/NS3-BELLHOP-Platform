# Frontend migration matrix

| Legacy module | User purpose | Legacy API dependency | Visual/interaction value | Code reuse value | New Platform concept | Recommendation |
|---|---|---|---|---|---|---|
| `Shell` + `App` routing | Global navigation and active scenario | Bootstrap scenario/results | Clear primary/workbench grouping | Low: route/domain boundaries are legacy | Environment, Scenario, Experiment, Run | Design reference |
| `OverviewPage` | Fast status and entry points | Scenario/results/derive | Useful overview density and topology preview | Medium for small cards only | Workspace overview | Selective component reuse |
| `StudioPage` | All-in-one experiment workbench | Nearly every API family | Rich workflow knowledge | Very low: 4,008 lines and mixed responsibilities | Scenario editor, Experiment definition, Run monitor, Results | Rewrite |
| `PropertyInspector` | Edit scene/node/link/model properties | Legacy component/template/environment schemas | Useful inspector pattern | Low: 2,932 lines, deeply DTO-coupled | Typed Scenario configuration panels | Rewrite |
| `ScenarioWizard` | Guided scenario creation | Generate/derive scenario | Good staged interaction | Medium after new commands/schema | Scenario creation | Selective component reuse |
| `EnvironmentPage` | Manage acoustic assets and source data | Environment/WOSS/Bellhop/file endpoints | Valuable library/build workflow | Low: legacy engine/file coupling | Environment asset catalog/import jobs | Rewrite |
| `ExperimentTemplatesPage` | Reusable experiment recipes | Legacy template CRUD/apply/derive | Useful compare/apply workflow | Low to medium after DTO replacement | Experiment template | Design reference |
| `AssetsPage` | Scenario/history/archive management | Scenario/results/history/archive/report | Useful inventory and comparison concepts | Low: unrelated object types are combined | Scenario catalog, Run history, Result comparison | Rewrite |
| `SettingsPage` | Service status and scenario metadata | Status/scenario save | Limited | Low | Deployment health and workspace settings | Remove / defer |
| `StudioCanvas` | Editable plan topology and event overlay | `DemoNode`, `LinkMetric`, trace-derived events | High interaction value | Medium if rewritten behind new view models | Scenario topology and Run event projection | Selective component reuse |
| `ProfileView` | Range/depth, seabed, path and metrics | Bathymetry/rays/legacy metrics | High domain visualization value | Medium after independent view model | Environment slice and channel-result view | Selective component reuse |
| `Scene3DView` | 3D scene/ray exploration | Legacy nodes/rays | Strong exploratory value | Medium; rendering ideas reusable | Scenario geometry and channel visualization | Selective component reuse |
| `TopologyPreview`/cards/trends | Lightweight summaries | Nodes/metrics | High for dashboards | High after prop normalization | Overview projections | Selective component reuse |
| `services/api.ts` | Backend access | Entire old FastAPI schema | None | None | Versioned Platform API client | Rewrite |
| `studioRuntimeStore` | Selection, run logs and playback | Legacy communication-event type | Useful state categories | Medium after splitting stores | UI state and Run projection state | Design reference |
| Global `styles.css` | Visual language | DOM/class names | Colors, density and layouts are useful | Low as a monolith | New design tokens/components | Design reference |
| Direct Bellhop runner UI | Launch Bellhop from frontend | `/run-bellhop` and engine paths | Conflicts with offline asset pipeline | None | Offline environment import/build job | Remove / defer |

“Selective component reuse” means extracting presentation behavior behind new typed view models; it does not authorize importing from `legacy-reference`.
