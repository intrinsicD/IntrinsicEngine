# GRAPHICS-014 — Visualization attributes and overlays
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
- Define scalar/color/vector attribute packet contracts and GPU upload/update seams.
- Define auxiliary GPU resource descriptors for per-domain attribute buffers, centroid/label data, vector-field/isoline output packets, and Htex patch-preview atlas textures.
- Complete colormap system, isoline packet generation, vector-field overlay packets, and overlay snapshot interfaces.
- Add diagnostics for missing attributes, mismatched domains, invalid ranges, and unsupported colormap IDs.
## Tests
- Add unit/integration tests for scalar/color BDA config, colormap selection, isoline defaults, vector-field packets, and overlay split from geometry ownership.
## Docs
- Update visualization sections in rendering architecture docs and the non-legacy parity matrix.
## Acceptance criteria
- Visualization works through promoted graphics snapshot/API boundaries.
- Attribute-domain mismatches fail deterministically.
- Overlay data does not require live ECS ownership in graphics.
## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Depending on legacy `Graphics.VectorFieldManager` or related legacy modules.
