# GRAPHICS-006 — Material, shader, and pipeline registry

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-005` completion.
- Current slice: CPU-only `Extrinsic.RHI.PipelineRegistry` key/cache/invalidation diagnostics implemented and under verification.
- Remaining next step: define graphics material-slot/layout contracts and renderer-facing material registry seams without ECS ownership.

## Implementation note (2026-05-03)
- Added `Extrinsic.RHI.PipelineRegistry` as a promoted CPU-testable cache layer over `RHI::PipelineManager`.
- Added deterministic `PipelineKey` construction from shader paths/generations and `PipelineDesc` render state.
- Added explicit shader-path invalidation for reload handoff, cache hit/miss diagnostics, missing shader diagnostics, invalid key diagnostics, and pipeline creation failure diagnostics.
- Added `Test.RHI.PipelineRegistry.cpp` in a CPU-only `unit;graphics` target.

## Upcoming questions to clarify
- Which promoted graphics module should own material-slot allocation beyond the current `Graphics.MaterialSystem` slice: a renderer-level registry or an RHI-adjacent layout helper?
- What is the canonical material parameter layout versioning policy for texture/bindless references before `GRAPHICS-015` GPU texture residency lands?
- Should shader asset identity remain path-based for the promoted registry, or should a later task migrate it to `Asset.Registry` IDs after asset hot-reload ownership is finalized?

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
- Label registry behavior tests `unit;graphics` and `contract;graphics` for the default CPU gate; label any backend shader-compilation smoke tests `gpu;vulkan` so they stay opt-in.
## Docs
- Document shader asset ownership, hot-reload lifecycle, material parameter layout, and pipeline-cache policy.
## Acceptance criteria
- Renderer passes can request pipelines/material layouts without legacy registry modules.
- Runtime/extraction can resolve CPU material descriptions or asset IDs into graphics-owned material slots without storing those slots in canonical ECS components.
- Registry failures are deterministic and testable in CPU-only CI.
- Backend-specific work remains behind declared RHI/backend integration points.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Optional when hardware/driver support is available:
ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
```

Current CPU registry slice verified on 2026-05-03:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsRhiCpuUnitTests -j1
ctest --test-dir build/ci --output-on-failure -R 'RHIPipelineRegistry' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --build --preset ci --target IntrinsicTests -j1
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --quiet
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making Vulkan shader compilation mandatory for the default CPU gate.
