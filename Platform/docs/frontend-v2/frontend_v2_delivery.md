# P0-S4-07 frontend delivery

## Reproducible offline toolchain

The repository supplies the exact Node.js `v24.20.0` Linux x86-64 archive and
npm `11.19.0`. `Platform/frontend/scripts/npm.sh` verifies the official archive
SHA256 before extracting it. `.npmrc` sets npm to offline mode and points to the
repository-local cache. No system Node.js, package-manager install, network
fetch or floating version is part of the formal build.

`S4_07_FRONTEND_DEPENDENCY_GATE=APPROVED`

From `Platform/frontend` in a fresh clone:

```sh
./scripts/npm.sh ci --offline
./scripts/npm.sh run build
./scripts/npm.sh test
```

`package.json` contains exact direct versions, `package-lock.json` locks the
complete closure and integrity values, and
`Platform/third_party/npm_dependencies.json` records the audited package,
version and license inventory. Node provenance and its official checksum are
recorded under `Platform/third_party/nodejs/`.

## Runtime boundaries

- `src/api/client.ts` is the sole HTTP boundary.
- `src/api/queries.ts` owns stable TanStack Query definitions.
- `src/api/runEvents.ts` parses and projects SSE without inventing a frontend
  sequence. Exact decimal IDs, counts and nanoseconds remain strings.
- Page modules consume Backend HTTP DTOs and do not import worker or C++ wire
  types.
- Environment, Scenario, Experiment, Run and Result catalogs are server state.
- The only P0-S4-07 mutation is creating a Run from exact Experiment identity
  and version.

## Scope and deferred work

The application implements the frozen catalog/detail routes, real Run launch,
basic Run monitoring and formal Result views. Scenario/Experiment editing,
cancel, authentication, pagination, hot resource refresh, complex acoustic
animation and advanced result comparison are not implemented in this phase.
