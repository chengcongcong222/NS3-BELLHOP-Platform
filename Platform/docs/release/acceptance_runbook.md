# P0-S5-03 Acceptance Runbook

This procedure is for the canonical Linux x86_64 release. WSL2 may run the
Linux package, but this is not a Windows-native release. Prerequisites are
CPython 3.12 and an ns-3.47 installation prefix; Bellhop and network access are
not required.

## STEP 1 — Verify the archive

Run `sha256sum -c ns3-bellhop-platform-p0-s5-03-linux-x86_64.tar.gz.sha256`.

## STEP 2 — Extract

Extract the single canonical `.tar.gz` and enter its top-level directory.

## STEP 3 — Prepare and preflight

Set `PLATFORM_NS3_PREFIX` to the ns-3.47 prefix, then run
`./release.sh prepare`. This creates only a release-local Python environment
from the bundled hash-locked wheelhouse and verifies `SHA256SUMS`.

## STEP 4 — Start

Run `./release.sh start`.

## STEP 5 — Open the frontend

Open `http://127.0.0.1:4173`.

## STEP 6 — Select the experiment

Open Experiments and select `Acceptance 4-Node Experiment` version 1.

## STEP 7 — Start Run

Choose **Run this experiment**. Each execution receives a new RunId.

## STEP 8 — Observe Monitor

Confirm that lifecycle reaches `Completed` and the SSE projection completes.

## STEP 9 — Open Result

Open the formal Result for the completed Run.

## STEP 10 — Download evidence

Download Acceptance Evidence from the Result page. The JSON evidence endpoint
is `/runs/{run_id}/acceptance-evidence` and deterministic text is available at
`/runs/{run_id}/acceptance-evidence.txt`.

## STEP 11 — Check the acceptance metrics

- 3–4 nodes (the delivered preset has 4);
- 60 bit/s;
- modeled BER no greater than `1e-4`;
- feature-level fusion;
- at least 5 bearing observations;
- fusion period no greater than 180 seconds.

The current reference run may produce modeled BER `0.0`. This is a numerical
model result that may reach the floating-point representation floor at high
SNR; it is not hardware measurement or proof of absolute zero errors. The
environment is a WOA23/GEBCO 2020 reference proxy, not a field measurement.

## STEP 12 — Stop

Run `./release.sh stop`. Ports 8000 and 4173 must no longer be listening.
