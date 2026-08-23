# Legacy frontend architecture review package

This directory is the P0-S2 legacy-frontend review package. It records the actual frontend found at `/home/ccc/work/NS3_Factory/frontend` without making that code a production dependency.

Start with:

- `legacy_frontend_audit.md` for architecture and risk findings;
- `legacy_page_inventory.md` and `legacy_route_map.md` for the user-facing structure;
- `legacy_api_sse_map.md` for backend coupling;
- `frontend_migration_matrix.md` for reuse decisions;
- `new_frontend_initial_ia.md` for the proposed Environment → Scenario → Experiment → Run information architecture;
- `legacy-reference/` for the curated, reference-only source snapshot.

No new frontend implementation is present here. The package is intentionally isolated on `docs/frontend-audit` and must not be imported by production targets.
