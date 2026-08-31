# NS3-BELLHOP Platform

This directory is the standalone C++23/ns-3.47 Platform product. The formal
P0-S5 demo is `Acceptance4Node`: three moving sensor nodes and one fixed fusion
center. `Extended6Node` is an extension and is not part of the third-party
acceptance baseline.

## Offline preparation and one-command launch

All Python wheels, Node.js and npm artifacts are repository supplied. ns-3.47
is an external prerequisite and must be identified explicitly; no script
downloads or installs it.

```sh
cd Platform
PLATFORM_NS3_PREFIX="$HOME/.local/ns3/3.47" ./scripts/platform_demo.sh prepare
./scripts/platform_demo.sh start
```

Open the URL printed by `start`. The same entry point owns lifecycle actions:

```sh
./scripts/platform_demo.sh status
./scripts/platform_demo.sh restart
./scripts/platform_demo.sh stop
```

`prepare` configures an ns-3 ON build, runs the full CTest suite, builds the
frontend offline, creates and validates the demo environment repository, and
runs preflight. `start` refuses occupied ports, stale/incomplete dependencies,
an invalid environment catalog, a missing worker, or an incomplete frontend
build. Backend and frontend logs are separated under `.runtime/demo/logs/`;
PIDs and logs are runtime state and are ignored by Git.

The default ports are backend `8000` and frontend `4173`. Override them with
`PLATFORM_BACKEND_PORT` and `PLATFORM_FRONTEND_PORT`. Override the out-of-source
build or runtime state locations with `PLATFORM_DEMO_BUILD_DIR` and
`PLATFORM_DEMO_STATE_DIR`; repository files contain no developer-specific
absolute path.

## Acceptance flow

1. Select `Acceptance 4-Node Experiment`.
2. Create the Run from its exact identity and version.
3. Observe lifecycle and SSE projection; SSE is read-only and non-causal.
4. Open the formal Result.
5. Download the immutable acceptance evidence text bundle. The JSON bundle is
   available at `/runs/{run_id}/acceptance-evidence`.

The evidence verdict is copied from the backend `AcceptanceReport`. A failed
acceptance metric is a completed experiment outcome, not a backend/system
failure. `NoArrival` and `NotDecoded` remain distinct.

See [acceptance_baseline.md](docs/acceptance/acceptance_baseline.md),
[acceptance_evidence.md](docs/delivery/acceptance_evidence.md), and
[BUILDING.md](BUILDING.md).
