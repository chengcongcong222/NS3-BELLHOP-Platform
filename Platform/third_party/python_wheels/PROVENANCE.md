# Python backend wheel provenance

- Imported: 2026-08-29
- Runtime baseline: CPython 3.12.3 on Linux x86_64
- Package source: exact releases from `https://pypi.org/project/<name>/<version>/`
- Artifact policy: binary wheels only; no source build and no network fallback
- Integrity authority: `Platform/backend/requirements.lock`
- Installation policy: `pip --no-index --require-hashes`

The wheelhouse contains the exact direct and transitive dependency closure for
FastAPI 0.141.1, Pydantic 2.13.5, Uvicorn 0.52.4, HTTPX 0.28.1 and pytest
9.1.1. Twenty wheels are platform-independent `py3-none-any` artifacts. The
only platform-specific artifact is pydantic-core 2.46.5 for CPython 3.12 on
manylinux x86_64. Every wheel is retained byte-for-byte as published and
contains its upstream license metadata and notice files.

The repository build creates its virtual environment under the build tree and
installs exclusively from this wheelhouse. A clean clone therefore does not
contact PyPI during configure, build or test.
