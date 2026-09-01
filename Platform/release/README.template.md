# NS3-BELLHOP Platform P0-S5-05

Formal Linux x86_64 offline delivery for Acceptance4Node using the
ReferenceShallowWaterV1 Bellhop-derived acoustic field.

- Release: `P0-S5-05`
- Source revision: `@SOURCE_REVISION@`
- Runtime prerequisite: CPython 3.12 and an ns-3.47 prefix
- Reference asset: `reference-shallow-water-v1`
- Asset checksum: FNV1A64 `fb64e543f9042c52`

Set `PLATFORM_NS3_PREFIX` and run:

```bash
./release.sh prepare
./release.sh start
```

Open <http://127.0.0.1:4173>. See `docs/acceptance_runbook.md` for the formal
acceptance procedure. Bellhop and network access are not runtime prerequisites.
