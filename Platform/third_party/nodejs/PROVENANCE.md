# Node.js runtime provenance

- Component: Node.js
- Exact release: `v24.20.0` (Krypton LTS)
- Bundled npm: `11.19.0`
- Platform artifact: `node-v24.20.0-linux-x64.tar.xz`
- Official source URL: `https://nodejs.org/dist/v24.20.0/node-v24.20.0-linux-x64.tar.xz`
- Official checksum manifest: `https://nodejs.org/dist/v24.20.0/SHASUMS256.txt`
- SHA256: `2f2c0da162318f0de47665410c7c8c2ed3d36c8f3105de4bbc61176c70a7cbf2`
- License: MIT plus bundled third-party notices in upstream `LICENSE`
- Introduced: 2026-08-30
- Purpose: reproducible Linux x86-64 frontend build/test runtime

The archive is an unmodified official release artifact. Frontend tooling
verifies the exact SHA256 before extraction and does not search for or install
a system Node.js runtime.
