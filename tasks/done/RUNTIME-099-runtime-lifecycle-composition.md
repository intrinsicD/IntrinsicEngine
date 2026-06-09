# RUNTIME-099 — Runtime lifecycle composition pipeline

## Status
- Status: done.
- Completed: 2026-06-09.
- Owner/agent: Codex.
- Branch: `main`.
- Final implementation commit: this retirement commit.
- Maturity: `CPUContracted` for CPU/null lifecycle order and shutdown determinism.
- Summary: `Engine::RunFrame()` now carries an internal data-only `RuntimeFrameContext` while delegating platform/render/maintenance/operational/shutdown phase sequencing to the promoted `Extrinsic.Core.FrameLoop` contracts. Existing and updated runtime contract tests pin platform/minimized skip, render begin/extract/prepare/execute/end order, begin-frame failure skip, maintenance ordering, operational-transition fallback, shutdown ordering, and runtime source-layer boundaries.

## Goal
- Replace the remaining legacy runtime render-orchestration lifecycle with an explicit promoted composition pipeline covering `begin_frame`, input/UI, fixed-step simulation, extraction, prepare, execute, present/end, maintenance, and shutdown.

## Non-goals
- No renderer pass-body implementation; graphics-owned pass bodies stay under `tasks/backlog/rendering/`.
- No platform backend implementation; runtime consumes `Platform.IWindow` and backend-neutral RHI descriptors.
- No feature catalog recreation; feature-toggle decisions route through config or focused owner modules.

## Context
- Owner/layer: `runtime` composition root; runtime may depend on lower layers and owns cross-layer wiring.
- [`RORG-031-runtime-composition.md`](../backlog/runtime/RORG-031-runtime-composition.md) is the seed task for this area.
- Reused promoted pieces: `Extrinsic.Core.FrameLoop`, `Runtime.Engine`, `Runtime.EcsSystemBundle`, `Runtime.RenderWorldPool`, `Runtime.RenderExtraction`, `Runtime.PhysicsBridge`, `Runtime.ImGuiAdapter`, `Graphics.RenderPrepPipeline`, and `IRenderer::IsOperational()`.
- The runtime gates operational promotion on `RHI::IDevice::IsOperational()`, not Vulkan diagnostics.
- Legacy `Runtime.FrameLoop`, `Runtime.RenderOrchestrator`, and `Runtime.ResourceMaintenance` are not imported by the promoted runtime path; the equivalent stage contracts are `Extrinsic.Core.FrameLoop` plus runtime-owned hooks and `RuntimeFrameContext` data.

## Value gate
- Current state: promoted runtime composes frame loop, ECS systems, physics bridge, extraction, ImGui, renderer prep, and asset handoffs.
- Improvement: explicit lifecycle records/helpers make frame order, skip paths, and shutdown deterministic without reviving legacy orchestration modules.
- Scope decision: retain lifecycle composition because it improves testability and layer separation; do not add feature catalogs, renderer pass bodies, or backend-specific runtime gates.

## Required changes
- [x] Define a data-only `RuntimeFrameContext` or equivalent internal record for stage order, frame index, fixed-step alpha, extraction inputs, and maintenance outputs.
- [x] Refactor `Engine::RunFrame` helper boundaries so lifecycle order is explicit and testable without importing legacy `Runtime.RenderOrchestrator` or `Runtime.ResourceMaintenance`.
- [x] Ensure minimized/resize/headless paths still produce deterministic skip or fail-closed diagnostics.
- [x] Integrate shutdown ordering for ImGui adapter, render extraction caches, physics bridge, asset handoffs, renderer, platform window, and streaming executor.
- [x] Update `RORG-031-runtime-composition.md` to point at this implementation child and any follow-up slices.

## Tests
- [x] Add `contract;runtime` coverage for lifecycle stage order, minimized/resize skips, render-operational fallback, and shutdown determinism.
- [x] Add `integration;runtime` headless engine startup/shutdown coverage if existing tests do not cover the new order.
- [x] Preserve default CPU/null correctness gate behavior.

## Docs
- [x] Update `src/runtime/README.md` with current lifecycle order.
- [x] Update `docs/architecture/runtime.md` with current lifecycle composition and shutdown ordering.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` runtime and legacy-render-orchestration rows.
- [x] Regenerate module inventory if public module surfaces change. No public module surface changed, so no regeneration was required.

## Acceptance criteria
- [x] Runtime lifecycle stage order is represented by named helpers or records with focused tests.
- [x] Shutdown order is deterministic and test-covered for caches, adapters, streaming work, renderer, and platform.
- [x] Legacy `Runtime.FrameLoop`, `Runtime.RenderOrchestrator`, and `Runtime.ResourceMaintenance` have promoted equivalents or explicit retirement notes.

## Verification results
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'runtime|integration|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
git diff --check
```

Result: configure succeeded; `IntrinsicTests` built; CTest passed 878/878;
layering, test layout, doc links, task policy, task-state links, docs-sync,
module-inventory check, and `git diff --check` passed.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'runtime|integration|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing legacy runtime, graphics, RHI, or interface modules.
- Adding Vulkan-specific diagnostics as runtime operational gates.

## Maturity
- Completed: `CPUContracted` for CPU/null lifecycle order and shutdown determinism.
- `Operational` proof remains existing opt-in `gpu;vulkan` sandbox/runtime smoke coverage such as `RUNTIME-095`; no additional `Operational` follow-up is owed by this task.
