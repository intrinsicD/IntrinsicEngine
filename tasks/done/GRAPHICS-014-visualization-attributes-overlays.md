# GRAPHICS-014 — Visualization attributes and overlays

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-013C` completion.
- Current slice: data-only visualization packet contracts and diagnostics; texture/atlas residency remains deferred to `GRAPHICS-015`.
- Clarification: existing mesh UVs/texcoords must be usable for per-fragment visualization bakes (for example KMeans labels), and Htex must remain an always-recreatable alternate mapping for any mesh, including meshes that already have UVs.
- Completed slices in active work: data-only packet contracts/diagnostics, UV-vs-Htex bake mapping policy, renderer-owned `RenderWorld::Visualization` snapshot spans, and CPU/null tests. GPU texture residency remains deferred to `GRAPHICS-015`.
- Completed: 2026-05-03.
- PR/commit: 1a95b78, 9a265ec, c134901.
- Follow-up questions: `tasks/backlog/rendering/GRAPHICS-014Q-visualization-runtime-backend-clarifications.md`.

## Goal
- Complete visualization attribute upload, colormap, isoline, vector-field, Htex patch-preview atlas, and overlay rendering contracts through graphics snapshots.
## Non-goals
- No editor widget implementation.
- No importer/exporter work.
- No direct dependency on legacy visualization managers.
- No geometry ownership of isoline, vector-field, or Htex patch generation algorithms in graphics; graphics consumes geometry/runtime-produced snapshots or auxiliary GPU resources.
## Context
- Owner: `src/graphics/renderer` and `src/graphics/assets` where GPU-resident visualization data is needed.
- Legacy color mapper, colormap, property enumerator, isoline extractor, vector field, and overlay factory modules define feature coverage to re-own cleanly.
## Required changes
- [x] Define scalar/color/vector attribute packet contracts and GPU upload/update seams.
- [x] Define auxiliary GPU resource descriptors for per-domain attribute buffers, centroid/label data, vector-field/isoline output packets, and Htex patch-preview atlas textures.
- [x] Define per-fragment bake descriptors that can target either existing mesh texcoords or an Htex patch atlas. UV-backed bakes require texcoord data; Htex-backed bakes must be possible even when UVs are present and may request Htex regeneration.
- [x] Complete colormap system, isoline packet generation, vector-field overlay packets, and overlay snapshot interfaces.
- [x] Add diagnostics for missing attributes, mismatched domains, invalid ranges, and unsupported colormap IDs.
## Tests
- [x] Add unit/integration tests for scalar/color BDA config, colormap selection, isoline defaults, vector-field packets, and overlay split from geometry ownership.
- [x] Label graphics-only unit tests `unit;graphics` and runtime/overlay handoff tests `integration;runtime;graphics` so both run in the default CPU gate.
## Docs
- [x] Update visualization sections in rendering architecture docs and the non-legacy parity matrix.
## Acceptance criteria
- [x] Visualization works through promoted graphics snapshot/API boundaries.
- [x] Attribute-domain mismatches fail deterministically.
- [x] Overlay data does not require live ECS ownership in graphics.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
- Verified locally with targeted visualization packet/render-world tests and the default CPU-supported correctness gate on 2026-05-03.
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Depending on legacy `Graphics.VectorFieldManager` or related legacy modules.

## Follow-up cross-link
`GRAPHICS-024` (overlays/presentation/editor handoff planning) confirms that
`Graphics.VisualizationPackets` is the canonical promoted home for overlay GPU
upload, descriptor binding, and dirty-state translation. Runtime/editor/app
retains overlay mutation, dirty-domain stamping, and selection/pick-ID
ownership; graphics consumes immutable visualization snapshots. Producer-side
filtering and backend upload questions stay tracked under `GRAPHICS-014Q`. See
the overlay / presentation / editor handoff inventory in
`../../docs/migration/nonlegacy-parity-matrix.md` for the per-row owner matrix.
This appendix does not modify acceptance criteria or completion metadata.
