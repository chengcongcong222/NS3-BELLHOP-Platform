# P0-UX-R4-05 Authoring exact rebase review

The `*-old.png` images are the actual legacy application captures preserved by UX-R1 at 1440×900. The paired `*-new.png` images are captured from the real Platform backend and current frontend at the same viewport, without request interception.

## Workbench

- Restores the legacy `page-grid--overview`, full-width scenario hero, three compact metrics, split status/topology region, and high-frequency action panel.
- Replaces legacy scenario/store values with formal Environment, Scenario, Experiment, Run, and Result catalog values.
- Keeps new resource types behind four workflow entries instead of returning to a dashboard tile wall.

## Environment Builder

- Restores the legacy Bellhop workbench hierarchy: identity header, narrow build rail, compact source panels, dominant profile preview, and grouped Bellhop parameters.
- Server draft revision, WOA/GEBCO acquisition, import, Build Job, and publish remain the current authoring APIs.
- The screenshots and preview contain only values from the real server draft created by `capture.mjs`.

## Scenario Studio

- Restores the legacy Studio toolbar, dark central canvas, compact node rail, inspector, depth strip, selection, drag, and measurement hierarchy.
- The adapter surface remains `ScenarioDraft`; environment bounds and trajectory-binding status come from current DTO semantics.
- No motion model, mock topology, or frontend simulation loop was reintroduced.

## Experiment / Channel2

- Restores the legacy compact step rail and two-column parameter grouping.
- Physical layer is now a normal parameter field rather than two oversized product cards; Channel2 expands modulation/noise/PSD directly beside PHY parameters.
- Save and run continue to send the same real authoring payload into the worker; read-only preset fields remain visibly read-only.
