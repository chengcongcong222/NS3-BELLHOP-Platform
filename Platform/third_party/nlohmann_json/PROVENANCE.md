# nlohmann/json provenance

- Project: JSON for Modern C++ (`nlohmann/json`)
- Exact release: `v3.12.0`
- Release commit: `55f9368`
- Release date: 2025-04-11
- Source URL: `https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz`
- Upstream release archive SHA-256: `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa`
- License: MIT; the unmodified upstream `LICENSE.MIT` is retained beside this file.
- Introduced into Platform: 2026-08-29
- Purpose: newline-delimited JSON codec in the worker/backend adapter boundary only.

The archive was downloaded once from the fixed official release URL and its
SHA-256 was verified before extraction. This repository vendors only the
upstream `include/` tree required to compile the header-only library and the
upstream `LICENSE.MIT`. No configure, build, or test step downloads this
dependency or searches for a system-installed copy.

The vendored headers are unmodified. Updating the dependency requires a new
explicit release selection, archive hash verification, license audit, and
offline OFF/ON build gate.
