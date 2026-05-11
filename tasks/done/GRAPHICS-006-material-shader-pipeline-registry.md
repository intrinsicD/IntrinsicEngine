# GRAPHICS-006 — Material, shader, and pipeline registry

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-005` completion.
- Completed: 2026-05-03.
- Follow-up task: `GRAPHICS-007 — Culling and draw-bucket contracts` promoted to `tasks/active/`.

## Completion metadata
- Implementation commit: `a959114` (`GRAPHICS-006 material registry contracts`).
- Task-state commit: pending local agent workflow handoff.
- Verification: focused CPU material tests passed; broader structural/default CPU gates recorded below.

## Implementation note (2026-05-03)
- Added `Extrinsic.RHI.PipelineRegistry` as a promoted CPU-testable cache layer over `RHI::PipelineManager`.
- Added deterministic `PipelineKey` construction from shader paths/generations and `PipelineDesc` render state.
- Added explicit shader-path invalidation for reload handoff, cache hit/miss diagnostics, missing shader diagnostics, invalid key diagnostics, and pipeline creation failure diagnostics.
- Added `Test.RHI.PipelineRegistry.cpp` in a CPU-only `unit;graphics` target.

## Implementation note (2026-05-03 material registry completion)
- Kept promoted material-slot allocation in `Extrinsic.Graphics.MaterialSystem`, not RHI, so renderer-owned GPU material slots remain outside canonical ECS components.
- Added canonical material layout metadata (`kMaterialLayoutVersion == 1`, default slot `0`, 128-byte `RHI::GpuMaterialSlot`, four custom `vec4` slots, and four bindless texture references).
- Added `MaterialSystemDiagnostics` for duplicate type names, incompatible layouts, invalid create types, capacity failures, fallback-slot resolves, dirty-slot counts, and coalesced upload ranges.
- Added CPU-only `Test.Graphics.MaterialSystem.cpp` coverage and a `IntrinsicGraphicsRendererCpuUnitTests` target labeled `unit;graphics`.

## Upcoming questions to clarify
- Resolved for this task: `Graphics.MaterialSystem` owns renderer material slots; RHI owns low-level buffers/pipelines only.
- Resolved for this task: layout version `1` uses four bindless texture references in `MaterialParams`; asset-ID-to-residency resolution remains a `GRAPHICS-015` follow-up.
- Resolved for this task: shader identity remains path/generation based in `RHI.PipelineRegistry`; asset-ID migration, if needed, belongs to a later hot-reload/assets task.

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
- [x] Define shader module identities, pipeline cache keys, material parameter layout contracts, and reload invalidation behavior.
- [x] Define the canonical material SSBO layout, material-slot lifetime, fallback material slot, texture/bindless references, and material dirty-update path.
- [x] Route backend-specific compilation/pipeline creation behind RHI/backend seams.
- [x] Add structured diagnostics for missing shaders, incompatible material layouts, and failed pipeline creation.
## Tests
- [x] Add unit/contract tests for shader registration, cache-key stability, reload invalidation, material defaults, and failure diagnostics.
- [x] Keep Vulkan shader compilation as opt-in when it requires GPU or external tooling.
- [x] Label registry behavior tests `unit;graphics` and `contract;graphics` for the default CPU gate; label any backend shader-compilation smoke tests `gpu;vulkan` so they stay opt-in.
## Docs
- [x] Document shader asset ownership, hot-reload lifecycle, material parameter layout, and pipeline-cache policy.
## Acceptance criteria
- [x] Renderer passes can request pipelines/material layouts without legacy registry modules.
- [x] Runtime/extraction can resolve CPU material descriptions or asset IDs into graphics-owned material slots without storing those slots in canonical ECS components.
- [x] Registry failures are deterministic and testable in CPU-only CI.
- [x] Backend-specific work remains behind declared RHI/backend integration points.
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

Focused material completion verified on 2026-05-03:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsRendererCpuUnitTests -j1
ctest --test-dir build/ci --output-on-failure -R 'GraphicsMaterialSystem' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making Vulkan shader compilation mandatory for the default CPU gate.
