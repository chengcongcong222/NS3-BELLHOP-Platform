# Offline delivery rehearsal record

This record is completed by the P0-S5 final quality gate. The rehearsal uses a
fresh exported source tree and fresh build/runtime directories, with no source
`node_modules`, frontend `dist`, Python virtual environment, PID, log, or prior
developer build cache.

Required recorded evidence:

- source revision/export identity;
- exact preparation and launch commands;
- configure/build/test duration;
- OFF and ON CTest totals;
- frontend test/build totals;
- three deterministic Acceptance4Node Run verdicts;
- start/stop/restart and no-orphan result;
- generated runtime artifact locations (outside tracked source state).

## P0-S5 implementation rehearsal

- source identity: `working-tree@d80eb15f1c0930cf3d47e844adcf6a2f30ba584c`;
- final clean source export: `/tmp/ns3-bellhop-s5-final-rehearsal.fBy9Da`
  (625 files,
  NUL-safe Git tracked/untracked export);
- final fresh builds: `Platform/build/platform-demo-off` and
  `Platform/build/platform-demo` inside that export;
- runtime state: `/tmp/ns3-bellhop-s5-demo-state`;
- environment repository: `/tmp/ns3-bellhop-s5-demo-environment`;
- OFF: configure/build and 58/58 CTest passed;
- ON/ns-3.47: configure/build and 77/77 CTest passed;
- frontend offline install, 23/23 tests and production build passed;
- preflight returned `PREFLIGHT_OK`;
- liveness/readiness and frontend HTTP returned success;
- three Acceptance4Node runs returned Pass and identical normalized Result
  SHA256 `e70e7ee3a1403ee27caa9512763f817b4cac367f819ba06ece02061774522e3d`;
- JSON evidence size was 6903 bytes and text evidence size was 548 bytes for
  each run;
- start, stop and restart passed after process-group ownership validation;
- final stop left neither listener nor owned child process.
- the documented `platform_demo.sh prepare` entry point independently completed
  ON configure/build, 77/77 CTest, offline frontend install/build, environment
  asset creation and `PREFLIGHT_OK` in the clean export.

Elapsed timing is intentionally reported by the configure/build/test command
logs rather than presented as a performance guarantee. The final fresh-export
pre-review gate completed successfully with OFF 58/58 and ON 77/77 CTest.
