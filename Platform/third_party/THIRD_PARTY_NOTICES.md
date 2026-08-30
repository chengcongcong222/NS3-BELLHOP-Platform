# Third-party notices

## nlohmann/json

- Version: 3.12.0 (`v3.12.0`)
- Copyright: Copyright (c) 2013-2025 Niels Lohmann
- License: MIT
- Purpose: Platform worker wire codec and future backend adapter only
- Provenance: `nlohmann_json/PROVENANCE.md`
- Upstream license: `nlohmann_json/LICENSE.MIT`

The complete upstream MIT license notice is preserved in the referenced
license file and applies to the vendored header tree.

## Python backend dependency wheelhouse

The P0-S4-05 FastAPI backend uses an exact, hash-locked Python dependency
closure distributed as original PyPI wheels under `python_wheels/wheels`.
Each wheel retains its upstream license files. The direct dependencies are:

- FastAPI 0.141.1 (MIT)
- Pydantic 2.13.5 (MIT)
- Uvicorn 0.52.4 (BSD-3-Clause)
- HTTPX 0.28.1 (BSD-3-Clause)
- pytest 9.1.1 (MIT)

Transitive packages and licenses are recorded in wheel metadata and locked in
`Platform/backend/requirements.lock`. The set includes MIT, BSD-2-Clause,
BSD-3-Clause, Apache-2.0, MPL-2.0 and PSF-2.0 licensed components.

## Frontend Node.js runtime and npm dependency closure

The P0-S4-07 frontend uses the unmodified official Node.js `v24.20.0` Krypton
LTS Linux x86-64 archive, including npm `11.19.0`. Its MIT license and bundled
third-party notices are preserved under `nodejs/LICENSE`; release provenance
and SHA256 are recorded in `nodejs/PROVENANCE.md`.

Frontend packages use exact versions in `Platform/frontend/package.json` and
the complete transitive closure is integrity-locked in `package-lock.json`.
Original registry artifacts are supplied through the repository-local
`npm_cache`, allowing `npm ci --offline`. Direct packages are React 19.2.8,
React DOM 19.2.8, React Router DOM 7.18.3, TanStack Query 5.102.8, Vite 8.2.2,
TypeScript 7.0.2, Vitest 4.1.11, Testing Library and jsdom. The audited full
package/version/license list is stored in `npm_dependencies.json`.
