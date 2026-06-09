# RUNTIME-099 — Runtime lifecycle composition pipeline

## Goal
- Replace the remaining legacy runtime render-orchestration lifecycle with an explicit promoted composition pipeline covering `begin_frame`, input/UI, fixed-step simulation, extraction, prepare, execute, present/end, maintenance, and shutdown.

## Non-goals
- No renderer pass-body implementation; graphics-owned pass bodies stay under `tasks/backlog/rendering/`.
- No platform backend implementation; runtime consumes `Platform.IWindow` and backend-neutral RHI descriptors.
- No feature catalog recreation; feature-toggle decisions route through config or focused owner modules.

## Context
- Owner/layer: `runtime` composition root; runtime may depend on lower layers and owns cross-layer wiring.
- `RORG-031-runtime-composition.md` is the seed task for this area. This child task is the semantic implementation plan and should become the active task when lifecycle composition work begins.
- Existing reusable pieces: `Core.FrameLoop`, `Runtime.Engine`, `Runtime.EcsSystemBundle`, `Runtime.RenderWorldPool`, `Runtime.RenderExtraction`, `Runtime.PhysicsBridge`, `Runtime.ImGuiAdapter`, `Graphics.RenderPrepPipeline`, and `IRenderer::IsOperational()`.
- The runtime must gate on `RHI::IDevice::IsOperational()`, not Vulkan diagnostics.

## Value gate
- Current state: promoted runtime already composes frame loop, ECS systems, physics bridge, extraction, ImGui, renderer prep, and asset handoffs, but stage order is embedded in `Engine::RunFrame`.
- Improvement: explicit lifecycle records/helpers make frame order, skip paths, and shutdown deterministic without reviving legacy orchestration modules.
- Scope decision: retain lifecycle composition because it improves testability and layer separation; do not add feature catalogs, renderer pass bodies, or backend-specific runtime gates.

## Required changes
- [ ] Define a data-only `RuntimeFrameContext` or equivalent internal record for stage order, frame index, fixed-step alpha, extraction inputs, and maintenance outputs.
- [ ] Refactor `Engine::RunFrame` helper boundaries so lifecycle order is explicit and testable without importing legacy `Runtime.RenderOrchestrator` or `Runtime.ResourceMaintenance`.
- [ ] Ensure minimized/resize/headless paths still produce deterministic skip or fail-closed diagnostics.
- [ ] Integrate shutdown ordering for ImGui adapter, render extraction caches, physics bridge, asset handoffs, renderer, platform window, and streaming executor.
- [ ] Update `RORG-031-runtime-composition.md` to point at this implementation child and any follow-up slices.

## Tests
- [ ] Add `contract;runtime` coverage for lifecycle stage order, minimized/resize skips, render-operational fallback, and shutdown determinism.
- [ ] Add `integration;runtime` headless engine startup/shutdown coverage if existing tests do not cover the new order.
- [ ] Preserve default CPU/null correctness gate behavior.

## Docs
- [ ] Update `src/runtime/README.md` with current lifecycle order.
- [ ] Update `docs/architecture/runtime.md` or create/update the relevant runtime architecture doc if this changes public architecture.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` runtime and legacy-render-orchestration rows.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Runtime lifecycle stage order is represented by named helpers or records with focused tests.
- [ ] Shutdown order is deterministic and test-covered for caches, adapters, streaming work, renderer, and platform.
- [ ] Legacy `Runtime.FrameLoop`, `Runtime.RenderOrchestrator`, and `Runtime.ResourceMaintenance` have promoted equivalents or explicit retirement notes.

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
- Target: `CPUContracted` for CPU/null lifecycle order and shutdown determinism; `Operational` proof remains existing or follow-up `gpu;vulkan` sandbox/runtime smokes.
