# GRAPHICS-029 — Reference scene bootstrap and minimal renderable extraction (planning)

## Goal
Lock down the runtime-owned, opt-in reference scene bootstrap contract that produces at least one deterministic renderable candidate observable by `Runtime.RenderExtraction` when `ExtrinsicSandbox` runs, with all placement, modularity, extensibility, and performance decisions recorded before any implementation. This is a planning/owner task; no engine behavior changes here.

## Non-goals
- No implementation, no new modules added to the build, no new ECS components, no shader/material/pipeline changes in this slice.
- No content authored inside `src/app/Sandbox/*`; sandbox stays policy-light per the app/runtime boundary.
- No GPU upload, no `GpuWorld::SetInstanceGeometry()` binding (those are GRAPHICS-030 territory).
- No asset-file loading; asset-backed candidates are GRAPHICS-034 territory.
- No GPU-typed components inside `src/ecs/Components/` (per AGENTS.md §2 and GRAPHICS-028).
- No renderer pass body or device backend changes; null device remains the supported execution path until GRAPHICS-032 / GRAPHICS-033 land.

## Context
- Owner layer: `runtime`. Source-tree home is a runtime sample/bootstrap seam, composed by `Runtime.Engine` when reference config opts in. Final module name and namespace are decided in this task.
- `src/app/Sandbox/Sandbox.cppm` lifecycle hooks intentionally do not mutate engine behavior; `src/app/Sandbox/main.cpp` already obtains configuration through `Extrinsic::Runtime::CreateReferenceEngineConfig()`.
- `src/runtime/Runtime.RenderExtraction.cppm` qualifies a renderable by requiring `ECS::Components::Transform::WorldMatrix` plus at least one render hint from `Graphics::Components::RenderSurface`, `RenderLines`, or `RenderPoints`. With no ECS content, extraction sees zero candidates today.
- The 2026-05-08 review (`docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md`, sections "Exact missing pieces / 1" and "minimal milestone plan / 1") records this as the first gap.
- `docs/architecture/graphics.md` and the GRAPHICS-028 done task already require any residency-bridge consumer to work from the runtime cache keyed by stable entity ID.

## Design decisions to record
The planning slice must produce explicit, defensible answers (not "TBD") for each of the following:

1. **Module placement and naming.** Decide the module ID (suggested: `Extrinsic.Runtime.ReferenceScene`), source path (`src/runtime/Runtime.ReferenceScene.cppm`), and namespace. Decision must justify why the bootstrap does not live in `runtime/Runtime.Engine.*`, in a sandbox-only module, or in a graphics-side fixture.
2. **Bootstrap-seam shape.** Choose between (a) a config flag that `Runtime.Engine` reads at startup, (b) a registered `IReferenceSceneProvider` interface with one default implementation, or (c) a callback installed via reference engine config. Record the trade-off explicitly: option (a) is simplest; (b) is most extensible (multiple sample scenes, future demos); (c) is most flexible for tests but hardest to reason about. Recommend option (b) with one concrete implementation registered by reference config; record why if a different option is selected.
3. **Renderable composition.** Decide which components the candidate carries: `Transform::WorldMatrix`, exactly one render hint (`RenderSurface` for the first milestone), an optional `Renderable {}` core-only marker (per GRAPHICS-028), and the procedural-source link to GRAPHICS-030. Forbid any GPU-typed component on the entity.
4. **Camera / view authorship.** Decide whether the camera is (a) an ECS entity with a camera component the runtime extraction synthesizes into `RenderFrameInput::Camera`, or (b) populated directly into `RenderFrameInput` by `Runtime.Engine` on bootstrap. Record the canonical answer; future camera-controller work in GRAPHICS-017 (done) should compose with it without rework.
5. **Light / unlit policy.** Decide whether the first milestone requires a light. Recommend "no light, default debug material is unlit" so GRAPHICS-031 can land before any lighting work; record explicitly that lit-default policy is deferred.
6. **Stable ID and naming policy.** Decide how the bootstrap acquires a stable entity ID (deterministic seed vs allocator) and a debug name suitable for `GpuSceneSlot` diagnostics. Determinism is required so contract tests can assert IDs.
7. **Lifetime and reset semantics.** Decide whether the bootstrap runs once per engine init, on every reference-config reload, or every frame. Recommend once per engine init with explicit teardown on shutdown; record interaction with editor/runtime hot-reload paths.
8. **Extensibility surface.** Record how additional sample scenes (cube, multiple primitives, hierarchy demo) compose without forking the module — typically by registering additional `IReferenceSceneProvider` implementations behind feature flags. Limit the initial provider set to one (triangle); enumerate the next provider candidates without opening them.
9. **Configuration plumbing.** Decide the field name and default on `ReferenceEngineConfig` (e.g. `EnableReferenceScene = false`). Default must keep current null-device behavior unchanged so existing tests do not regress; the sandbox `main.cpp` flips it explicitly in a follow-up implementation child task.
10. **Performance budget.** Bootstrap runs once per init, so allocations are not hot-path. Record the upper bound on entity count for the default provider (one), the expected extraction overhead per tick (constant-small), and the absence of allocation/copy work on the steady-state path.
11. **Layering audit.** Record that the new module imports nothing from `src/graphics/*` or `src/graphics/rhi/` beyond what `runtime` already depends on; no `assets` imports either. The reference scene speaks ECS + procedural-source descriptors only.

## Required changes
- Author the planning content above into this task body (decisions, not aspirations).
- Cross-link the decisions into `docs/architecture/graphics.md` (or the runtime composition doc it cross-links) and `src/runtime/README.md` so the bootstrap seam is discoverable from architecture docs.
- Identify split points for the implementation follow-up children (do **not** open them in this slice):
  - **GRAPHICS-029-Impl-A** — module skeleton + `IReferenceSceneProvider` + config field + default registration.
  - **GRAPHICS-029-Impl-B** — triangle provider implementation + extraction contract test.
  - **GRAPHICS-029-Impl-C** (optional) — additional sample-scene providers gated behind GRAPHICS-030 / GRAPHICS-032 readiness.
- Cross-link decisions with GRAPHICS-016 (extraction handoff), GRAPHICS-017Q (camera ownership), GRAPHICS-028 (residency bridge planning), GRAPHICS-030 (geometry residency implementation), and the 2026-05-08 review.

## Tests
- Planning slice: task-policy, doc-link, and layering validators only.
- Future implementation children must add `contract;runtime` tests asserting:
  - With the flag enabled, extraction reports exactly one renderable candidate with the expected components.
  - With the flag disabled, extraction reports zero candidates (regression guard for sandbox staying policy-light).
  - Stable entity ID is deterministic across two runs of the bootstrap with the same seed.
- GPU coverage stays opt-in `gpu;vulkan` and outside the default CPU gate.

## Docs
- Update `docs/architecture/graphics.md` (or the cross-linked runtime composition doc) with the bootstrap-seam decision once recorded here.
- Update `src/runtime/README.md` with module ownership, lifecycle, and configuration field.
- Update `docs/migration/nonlegacy-parity-matrix.md` if a parity row exists for sandbox renderable content.
- Cross-link this task from `tasks/backlog/rendering/README.md`.

## Acceptance criteria
- All eleven design decisions above have explicit recorded answers in the task body before any implementation child is opened.
- Implementation follow-up children are identified with scope and dependency gates but not opened.
- Architecture and source-tree docs reference the bootstrap-seam decision; layering invariants are unchanged.
- No engine behavior, no new modules in the build, and no test changes land in this slice.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No implementation, no new modules added to `CMakeLists.txt`, no new ECS components, no new graphics or runtime behavior in this slice.
- No content authored under `src/app/Sandbox/*`.
- No GPU-typed component additions to `src/ecs/Components/`.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.
