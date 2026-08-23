# Curated legacy frontend manifest

All original paths are relative to `/home/ccc/work/NS3_Factory/frontend`. There are 25 copied source/configuration files; this manifest and the reference notice are new audit documents and are not part of that count.

| Reference path | Original path | Selection reason / represented capability |
|---|---|---|
| `package.json` | `package.json` | Exact framework, router, Zustand, three.js and Vite dependency baseline |
| `vite.config.ts` | `vite.config.ts` | Development server and `/api` proxy topology |
| `src/main.tsx` | `src/main.tsx` | React root entry |
| `src/App.tsx` | `src/App.tsx` | Bootstrap ownership and complete route declaration |
| `src/types.ts` | `src/types.ts` | Legacy frontend DTO/domain coupling |
| `src/components/Shell.tsx` | `src/components/Shell.tsx` | Main layout and primary/workbench navigation |
| `src/components/MetricCard.tsx` | `src/components/MetricCard.tsx` | Typical dashboard summary component |
| `src/components/MiniTrend.tsx` | `src/components/MiniTrend.tsx` | Typical lightweight SVG chart |
| `src/components/ProfileView.tsx` | `src/components/ProfileView.tsx` | Depth/range, bathymetry, Bellhop ray and metric visualization |
| `src/components/PropertyInspector.tsx` | `src/components/PropertyInspector.tsx` | Legacy scene/node/link/model configuration breadth and coupling |
| `src/components/ScenarioWizard.tsx` | `src/components/ScenarioWizard.tsx` | Guided scenario creation interaction |
| `src/components/Scene3DView.tsx` | `src/components/Scene3DView.tsx` | React Three Fiber scene, terrain, nodes and acoustic paths |
| `src/components/SectionCard.tsx` | `src/components/SectionCard.tsx` | Typical shared layout primitive |
| `src/components/StudioCanvas.tsx` | `src/components/StudioCanvas.tsx` | Editable SVG topology and event overlay |
| `src/components/TopologyPreview.tsx` | `src/components/TopologyPreview.tsx` | Compact overview topology |
| `src/pages/AssetsPage.tsx` | `src/pages/AssetsPage.tsx` | Scenario, history, archive and comparison workflows |
| `src/pages/EnvironmentPage.tsx` | `src/pages/EnvironmentPage.tsx` | Environment library, WOSS and build/import workflows |
| `src/pages/ExperimentTemplatesPage.tsx` | `src/pages/ExperimentTemplatesPage.tsx` | Template CRUD/apply/derive workflow |
| `src/pages/OverviewPage.tsx` | `src/pages/OverviewPage.tsx` | Dashboard and high-frequency entry points |
| `src/pages/SettingsPage.tsx` | `src/pages/SettingsPage.tsx` | Status and scenario metadata/settings behavior |
| `src/pages/StudioPage.tsx` | `src/pages/StudioPage.tsx` | Central legacy configuration, monitor and results workbench |
| `src/services/api.ts` | `src/services/api.ts` | REST surface, legacy DTO shapes and streamed SSE run client |
| `src/services/demoData.ts` | `src/services/demoData.ts` | App bootstrap and scenario/result loading behavior |
| `src/stores/studioRuntimeStore.ts` | `src/stores/studioRuntimeStore.ts` | Zustand selection/run/playback state root |
| `src/styles.css` | `src/styles.css` | Global visual language, layout and component styling |
