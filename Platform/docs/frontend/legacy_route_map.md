# Legacy route map

All routes are declared in `src/App.tsx` beneath the `Shell` layout. There are no lazy route modules or nested feature routers.

| URL | Page component | Layout/navigation | Major feature |
|---|---|---|---|
| `/` | `OverviewPage` | `Shell`, primary “总览首页” | Current scenario summary, metric cards, topology preview, derive/create shortcuts |
| `/studio` | `StudioPage` | `Shell`, workbench “工作台总览” | Unified scenario/model canvas and workbench |
| `/config` | `StudioPage(initialTab="config")` | `Shell`, workbench “场景配置” | Scenario, node, link, model, and environment editing |
| `/monitor` | `StudioPage(initialTab="run")` | `Shell`, workbench “运行监视” | Run launch, streamed logs/events, elapsed state, playback |
| `/results` | `StudioPage(initialTab="results")` | `Shell`, workbench “结果分析” | Latest/archive results, metrics, event filters and timeline |
| `/environment` | `EnvironmentPage` | `Shell`, primary “Bellhop 环境管理” | Environment DB and WOSS source management/build workflows |
| `/templates` | `ExperimentTemplatesPage` | `Shell`, primary “实验模板” | Template CRUD, apply, derive scenario |
| `/assets` | `AssetsPage` | `Shell`, primary “场景管理与归档” | Scenario list, run history, archives, result comparison/report |
| `/settings` | `SettingsPage` | `Shell`, primary “系统设置” | Backend status and current scenario metadata/settings |

The `/studio` family is route-level presentation over one component, not four separate page architectures.
