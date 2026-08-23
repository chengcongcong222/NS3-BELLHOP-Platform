# REFERENCE-ONLY SNAPSHOT

These files are not part of the new frontend implementation.

Production frontend code must not import from `Platform/docs/frontend/legacy-reference`.

The snapshot exists only so architecture reviewers can inspect representative legacy organization, routing, API coupling, state handling, pages, and visualization code without access to the old working repository. It was copied from `/home/ccc/work/NS3_Factory/frontend` during the P0-S2 audit.

Future frontend work should refactor or rewrite around new Platform Environment, Scenario, Experiment, and Run concepts. Incremental compatibility with the legacy backend and its DTOs is not the implementation baseline.

No `node_modules`, lockfile, build output, cache, generated configuration, result dataset, media, backup, or credential file is included. The selected source required no redaction.
