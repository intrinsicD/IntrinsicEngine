# GRAPHICS-015 — GPU assets, textures, and residency

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-014` completion.
- Current slice: promoted from backlog; implementation not started in this handoff. Must preserve `GRAPHICS-014` UV/Htex visualization bake packet contracts while adding texture/residency seams.

## Goal
- Promote graphics-side GPU asset cache, texture loading/upload, sampler policy, and residency tracking needed by materials and passes.
## Non-goals
- No model importer/exporter migration.
- No material UI work.
- No direct filesystem policy beyond declared asset APIs.
## Context
- Owner: `src/graphics/assets`, `src/graphics/rhi`, and renderer material integration.
- Legacy texture loader and global resource behavior are references for feature coverage, while final ownership must respect asset IDs and graphics layer boundaries.
## Required changes
- Define texture asset handles, upload requests, residency states, sampler descriptors, fallback textures, and failure diagnostics.
- Integrate GPU asset cache with material/shader registry contracts without importing higher layers.
- Add bounded cache/eviction or explicit non-eviction diagnostics as appropriate.
## Tests
- Add unit tests for cache hits/misses, fallback texture behavior, sampler keys, failed uploads, residency transitions, and null backend behavior.
- Label GPU asset cache unit tests `unit;graphics` so they run in the default CPU gate.
## Docs
- Document graphics asset residency, fallback resources, and texture upload lifecycle.
## Acceptance criteria
- Materials and passes can reference GPU textures through promoted graphics asset APIs.
- Missing or failed texture assets have deterministic fallbacks and diagnostics.
- Cache behavior is CPU-testable without requiring Vulkan.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Moving model import/export ownership into graphics.
