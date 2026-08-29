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
