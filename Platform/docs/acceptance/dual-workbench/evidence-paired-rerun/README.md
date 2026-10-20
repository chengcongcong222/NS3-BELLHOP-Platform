# A/B rerun checkpoint evidence

Date: 2026-10-20. Base: `64da585` plus this commit's source changes.
Status: partial implementation evidence; **P0_DUAL_WORKBENCH_BLOCKED**.

- `browser-evidence.json`: Chromium CDP user-path checks and both real group
  exports. No API response substitution. The input scenario was seeded through
  the authoring API; creation of the groups and rerun were browser operations.
- `comparison-1600.png`, `comparison-800.png`: final comparison layout.
- `member-b.png`: actual B result view.
- `first-attempt.json`: initial automation failure (wrong status selector).
  A subsequent browser-discovered member-result crash was fixed in this commit;
  its cause and correction are recorded in `../implementation_review.md`.

Reproduction entry points:

1. Build the ns-3 ON worker, resource adapter and reference environment builder.
2. Set `PYTHONPATH=Platform/backend/src`, `PLATFORM_SIM_WORKER_PATH` and
   `PLATFORM_REFERENCE_ENVIRONMENT_BUILDER_PATH`; run
   `Platform/backend/tests/seed_paired_browser.py` with an empty temporary root.
3. Point a temporary backend at that root's `repository` and `workspace`, using
   the built worker and adapter. The browser script expects ports 8097/4197,
   and Chromium remote debugging on 9227.
4. Build the frontend with `VITE_API_BASE_URL=http://127.0.0.1:8097`.
   Run `Platform/frontend/scripts/paired-browser-check.mjs <temporary-root>`
   with Node 24. Provide Chinese fonts for readable screenshots.
5. Shut down these temporary processes. Do not switch the 4182/8012 main entry.

The script checks zero new JavaScript exceptions and horizontal overflow,
but does not replace manual usability review or all U01–U25 gates. Physics
coverage is ordinary COMM A/B only; not C mixed reception, BearingFusion,
official UWA replay validation, or complete product acceptance.
