# ns-3 runtime prerequisite

ns-3 is not bundled in the P0-S5-03 archive. The operator must provide an
ns-3.47 Linux x86_64 installation prefix through `PLATFORM_NS3_PREFIX`. The
release preflight requires `lib/libns3.47-core-default.so`, injects only that
prefix's `lib` directory into the release process environment, and audits the
Worker dependency resolution before startup.

ns-3 is free software distributed under GNU GPLv2; obtain and review it from
the official ns-3 project. The release does not copy, modify, install or manage
the prerequisite.
