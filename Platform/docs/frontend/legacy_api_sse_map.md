# Legacy API and live-data map

## Client and base URL

`src/services/api.ts` is the only API abstraction. It uses browser `fetch` directly. `BASE` is `${VITE_API_BASE}/api`; development falls back to `http://127.0.0.1:8000/api`, while production uses same-origin `/api`. Vite proxies `/api` to the localhost FastAPI service.

There is no authentication/header abstraction, retry policy, schema decoder, query cache, or generated client.

## Endpoint groups

| Capability | Actual endpoints and methods | Key legacy shapes/coupling |
|---|---|---|
| Scenarios | `GET /scenarios`; `GET/PATCH/DELETE /scenario/:name`; `POST /scenario/derive`; `POST /generate-scenario` | `DemoScenario`, scenario filename/name identity, nested legacy protocol/environment configuration |
| Results and trace | `GET /results?scenario`; `GET /events?scenario`; `GET /rays?scenario` | `LinkMetric[]`, `TraceEventRecord[]`, `LinkRays[]`, file path returned alongside rows |
| Bathymetry/SSP/grid/files | `GET /bathymetry`; `GET /ssp-data`; `GET /data-files`; `POST /import-bathymetry`; `POST /upload-ssp`; `POST /generate-grid`; `POST /generate-bathymetry`; `POST /generate-ssp`; `POST /save-ssp` | File paths are public DTO fields; data generation and asset catalog are combined |
| Environment DB | `GET/POST /environment-databases`; `PATCH/DELETE /environment-database/:name`; `GET /environment-capabilities`; `GET /environment-preview-rays` | Legacy DB records include Bellhop/WOSS engine paths, raw artifact paths, geo/time metadata and build parameters |
| WOSS sources | `GET/POST /woss-sources`; `DELETE /woss-source/:name`; `POST /woss-cache/import`; `POST /woss-cache/import-real-data`; `POST /woss-source/:id/build-environment`; plural build endpoint | WOSS-specific source/profile/cache schema must remain adapter-side in the new platform |
| Bellhop execution | `POST /run-bellhop` | Direct engine execution, path and run-mode concepts; explicitly not the new online Platform runtime contract |
| Model/node library | `GET /components`; `GET/POST /node-templates`; `PATCH/DELETE /node-templates/:id` | UI library assets and legacy protocol identifiers |
| Experiment templates | `GET/POST /experiment-templates`; `PATCH/DELETE /experiment-template/:id`; `POST .../:id/apply`; `POST .../:id/derive` | Template binds transmission type, environment DB, engine, duration, seed, and legacy `time_step_ms` |
| Run status/history | `GET /status`; `GET /history` | Process-like `phase`, timestamps, exit code, output file paths and archive flags |
| Archives/report | `GET /experiment-archives`; `GET /experiment-archive/:id`; `POST /experiment-report` | Archive summaries/details include legacy metric/event/ray arrays and filesystem artifact names |

## Live run stream

`startRun` sends `POST /api/run` with `{ scenario }` and incrementally reads the response body. Records are separated by blank lines and only `data: <json>` lines are decoded.

| Event | Key fields | UI effect |
|---|---|---|
| `start` | `scenario` | Enter running state/log start |
| `log` | `message` | Append run log |
| `done` | `exit_code`, `results`, optional `events`, `archive_id`, `archive_error` | Update metrics/events, finish run and refresh artifacts |
| `error` | `exit_code`, optional `message` and archive fields | Enter error state |
| `cancelled` | none | End client-visible run |

This is SSE framing over a streamed `fetch`, not the browser `EventSource` API. No WebSocket use was found. No status polling loop was found; the 500 ms interval in `StudioPage` only updates elapsed UI time.

## Migration boundary

All legacy DTOs and endpoint names are reference material. In particular, scenario files, direct Bellhop execution, WOSS cache operations, file-path DTOs, fixed-step-looking `time_step_ms`, process exit codes, and legacy trace/result records must be adapted to new Environment, Scenario, Experiment, Run, Snapshot, and Trace contracts rather than copied into production APIs.
