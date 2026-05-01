# GRAPHICS-006 — Material, shader, and pipeline registry
## Goal
- Promote non-legacy material, shader, and pipeline registry APIs so passes can request pipelines and material state through explicit graphics contracts.
## Non-goals
- No material editor UI.
- No importer/exporter work.
- No copy of legacy shader-registry or pipeline-library implementation.
- No ECS component ownership of graphics material GPU slots, leases, or backend resources.
## Context
- Owner: `src/graphics/renderer`, `src/graphics/rhi`, and backend-specific implementations where required.
- Legacy material registry, shader registry, shader compiler, hot reload, and pipeline library behavior are references for feature coverage only.
## Required changes
- Define shader module identities, pipeline cache keys, material parameter layout contracts, and reload invalidation behavior.
- Define the canonical material SSBO layout, material-slot lifetime, fallback material slot, texture/bindless references, and material dirty-update path.
- Route backend-specific compilation/pipeline creation behind RHI/backend seams.
- Add structured diagnostics for missing shaders, incompatible material layouts, and failed pipeline creation.
## Tests
- Add unit/contract tests for shader registration, cache-key stability, reload invalidation, material defaults, and failure diagnostics.
- Keep Vulkan shader compilation as opt-in when it requires GPU or external tooling.
## Docs
- Document shader asset ownership, hot-reload lifecycle, material parameter layout, and pipeline-cache policy.
## Acceptance criteria
- Renderer passes can request pipelines/material layouts without legacy registry modules.
- Runtime/extraction can resolve CPU material descriptions or asset IDs into graphics-owned material slots without storing those slots in canonical ECS components.
- Registry failures are deterministic and testable in CPU-only CI.
- Backend-specific work remains behind declared RHI/backend integration points.
## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making Vulkan shader compilation mandatory for the default CPU gate.
