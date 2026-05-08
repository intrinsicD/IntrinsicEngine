# GRAPHICS-029 — Runtime-owned reference scene bootstrap and minimal renderable extraction contract

## Goal
Define and land a runtime-owned, opt-in reference scene bootstrap that creates one deterministic renderable candidate (transform + render hint + camera) inside the promoted ECS registry so that `Runtime.RenderExtraction` observes at least one candidate when `ExtrinsicSandbox` runs, without pushing any scene authoring or feature wiring into `Sandbox::App`.

## Non-goals
- No scene/content code in `src/app/Sandbox/*`; the app/runtime boundary stays `app -> runtime` only.
- No GPU upload, no `GpuWorld::SetInstanceGeometry()` binding, no material/shader selection (those land in GRAPHICS-030 / GRAPHICS-031 / GRAPHICS-032).
- No asset-file loading; the first reference geometry is procedural and runtime-owned (asset-backed path lives in GRAPHICS-034).
- No new ECS components carrying GPU handles (per AGENTS.md section 2 and the GRAPHICS-028 residency contract).
- No renderer pass body work; null renderer remains acceptable as the device path for this slice.

## Context
- Owner layer: `runtime`. Source-tree home: a runtime sample/bootstrap seam such as `src/runtime/Runtime.ReferenceScene.cppm` (final module name decided in this task), composed by `Runtime.Engine` when the reference config opts in.
- `src/app/Sandbox/Sandbox.cppm` lifecycle hooks intentionally do not mutate engine behavior; `src/app/Sandbox/main.cpp` already obtains configuration through `Extrinsic::Runtime::CreateReferenceEngineConfig()`. Sandbox must remain policy-light.
- `src/runtime/Runtime.RenderExtraction.cppm` qualifies a renderable entity by requiring `ECS::Components::Transform::WorldMatrix` plus at least one render hint from `Graphics::Components::RenderSurface`, `RenderLines`, or `RenderPoints`. With no ECS content, extraction sees zero candidates today.
- The 2026-05-08 review (`docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md`, section "Exact missing pieces / 1. Runtime-owned reference scene bootstrap") records this as the first gap on the "minimal milestone plan / 1. CPU/null contract milestone".
- `docs/architecture/graphics.md` and the GRAPHICS-028 done task already require any residency-bridge consumer to work from the runtime cache keyed by stable entity ID; this bootstrap must not bypass that ownership.

## Required changes
- Add a runtime-owned bootstrap module (suggested: `Extrinsic.Runtime.ReferenceScene`) that:
  - Creates one renderable entity with stable ID, `Transform::WorldMatrix`, and a `Graphics::Components::RenderSurface` hint (line/point variants are out of scope for this slice).
  - Creates one camera/view entity or `RenderFrameInput` camera packet sufficient for downstream extraction; choice recorded in the task before implementation.
  - Optionally creates one light entity if a default debug material requires it; otherwise documents that the first surface uses an unlit/debug path so lighting setup can defer to GRAPHICS-031.
- Wire the bootstrap into `Runtime.Engine` behind an explicit reference-config flag (`ReferenceEngineConfig::EnableReferenceScene` or similar) that defaults consistent with current null-device behavior. The flag is the single seam that flips sandbox from empty registry to one candidate.
- Keep all GPU-typed state (handles, sidecars) outside ECS components per GRAPHICS-028; this task only adds CPU-side authoring data.
- Cross-link decisions with GRAPHICS-016 (extraction handoff), GRAPHICS-028 (residency bridge planning), GRAPHICS-030 (geometry residency bridge implementation), and the 2026-05-08 review.

## Tests
- Add a `contract;runtime` test under `tests/contract/runtime/` that constructs the runtime with the reference scene flag enabled, runs one extraction tick, and asserts that `Runtime.RenderExtraction` reports exactly one renderable candidate with the expected components.
- Add a `contract;runtime` test that constructs the runtime with the flag disabled and asserts zero candidates (regression guard for sandbox staying policy-light).
- Verification gate runs the default CPU-supported correctness target:
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` (or the runtime composition doc it cross-links) with the reference-scene bootstrap seam and its config flag.
- Update `src/runtime/README.md` to describe the new module's ownership and lifecycle.
- Update `docs/migration/nonlegacy-parity-matrix.md` if a parity row exists for sandbox renderable content.
- Cross-link this task from `tasks/backlog/rendering/README.md`.

## Acceptance criteria
- `ExtrinsicSandbox` running under the reference engine config produces at least one renderable candidate observed by `Runtime.RenderExtraction` without any code in `src/app/Sandbox/*` mutating engine state.
- The reference scene module imports nothing from `src/graphics/*` or `src/graphics/rhi/` beyond what runtime already depends on, and adds no GPU-typed component to `src/ecs/Components/`.
- Reference-scene flag default and behavior are documented; sandbox lifecycle hooks remain unchanged.
- All new tests are categorized (`contract;runtime`) and pass under the default CPU gate.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No scene authoring code or feature wiring inside `src/app/Sandbox/*`.
- No GPU handle storage in canonical ECS components.
- No mixing of mechanical file moves with semantic refactors.
- No renderer pass body or device backend changes in this slice.
- No asset-file loading, glTF integration, or material registry expansion in this slice.
