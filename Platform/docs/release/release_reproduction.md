# P0-S5-05 Release Reproduction

Canonical packaging accepts only a clean committed source tree. From the
candidate SHA, with repository-approved offline Python/Node supplies present:

```bash
python3 Platform/release/build_release.py \
  --ns3-prefix /path/to/ns-3.47 \
  --work-dir /temporary/unique-work \
  --output-dir /temporary/unique-output
```

The builder configures a C++23 Release build, runs ON/ns-3.47 CTest, performs
offline `npm ci` and the production frontend build, publishes the reference
asset, audits ELF `NEEDED` entries, generates ReleaseManifest/inventory/file
checksums, and creates a canonical `tar.gz`. File order, uid/gid, mtime and gzip
metadata are normalized from the source commit, so two builds of the same SHA
and target must have identical archive SHA256.

The runtime uses the prebuilt binaries and requires an external ns-3.47 prefix.
It does not use the source repository, build tree, Node/npm or Bellhop.
