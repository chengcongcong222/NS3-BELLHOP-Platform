# Offline Python dependency supply

The approved P0-S4-05 baseline is CPython 3.12.3 on Linux x86_64:

- FastAPI 0.141.1
- Pydantic 2.13.5
- Uvicorn 0.52.4
- HTTPX 0.28.1
- pytest 9.1.1

`Platform/backend/requirements.lock` contains the exact direct and transitive
versions plus SHA-256 for all 21 wheels. Original PyPI wheels and embedded
license files are stored under `Platform/third_party/python_wheels/wheels`.
The only platform-specific wheel is pydantic-core for CPython 3.12 manylinux
x86_64.

Configure finds exactly Python 3.12.3. Build creates a virtual environment in
the build tree and installs with `--no-index --require-hashes`; it never uses a
system FastAPI installation or network fallback. For a standalone local venv:

```sh
sh Platform/backend/scripts/bootstrap_offline_venv.sh /tmp/platform-backend-venv
```

The bootstrap refuses any Python patch version other than 3.12.3. Provenance
and license notices are under `Platform/third_party`.
