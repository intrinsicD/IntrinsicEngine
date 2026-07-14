# GRAPHICS-038D — Camera-transition skip heuristic + selection-bucket exemption

## Status
- Commit reference: this task-landing commit.
- Status: `completed`
- Owner/agent: Codex
- Branch: `main`
- Started: 2026-06-04
- Completed: 2026-06-04
- Current slice: single-slice `CPUContracted` implementation. Added extracted
  camera-transition inputs, deterministic heuristic/counter accounting, hard
  selection-bucket occlusion exemption, contract/integration coverage, docs,
  and default CPU verification.
- Next verification step: retired. `Operational` opt-in GPU/Vulkan
  conservatism proof remains owned by `GRAPHICS-038E`.

## Goal
- Add the first-frame-after-teleport phase-1 skip heuristic (`GRAPHICS-038` decision 7)
  and the hard selection-bucket occlusion exemption (decision 9), driven by extracted
  camera-snapshot fields, with `contract;graphics` + integration tests.

## Non-goals
- No HZB resource/build/cull changes beyond consuming the camera-transition signal.
- No new RHI surface.

## Context
- Owner layer: `graphics/renderer` (heuristic + bucket routing), reading extracted
  camera-snapshot fields (decision 13 — no live ECS access).
- Depends on `GRAPHICS-038C` (two-phase cull).
- Decision 7: the first frame after a hard camera teleport skips phase-1 occlusion
  (stale HZB) and treats every instance as phase-1 visible; trigger on **both** a
  camera position/orientation delta threshold AND an explicit runtime teleport/scene-
  change flag on the extracted camera snapshot (either fires the skip); count
  `HzbStaleSkipCount`. Decision 9: the three selection buckets are never HZB-occlusion-
  culled (frustum-only, single phase-1 set, no phase-2 split) — a hard rule for stable picking.

## Required changes
- [x] Add a teleport/scene-change flag (+ delta threshold) to the extracted camera snapshot.
- [x] Skip phase-1 occlusion on the first frame after a transition; count `HzbStaleSkipCount`.
- [x] Exempt the three selection buckets from occlusion culling (frustum-only, phase-1 only).
- [x] `contract;graphics` + integration tests for the skip heuristic and selection exemption.

## Tests
- [x] `contract;graphics` — delta-threshold and explicit-flag skip both fire; selection
      buckets never occlusion-culled; `HzbStaleSkipCount` accounting.
- [x] `integration` — teleport frame treats all instances as phase-1 visible.
- [x] CPU gate green.

## Docs
- [x] Document the camera-transition heuristic + selection exemption in
      `src/graphics/renderer/README.md`.

## Acceptance criteria
- [x] Both skip triggers fire; selection picking stays stable under occlusion.
- [x] Camera transitions arrive through the snapshot, not live ECS.
- [x] No new layering violations.

## Verification
```bash
glslc assets/shaders/culling/instance_cull.comp -I assets/shaders -o /tmp/culling_instance_cull_038d.comp.spv --target-env=vulkan1.3
glslc assets/shaders/instance_cull.comp -I assets/shaders -o /tmp/instance_cull_038d.comp.spv --target-env=vulkan1.3
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsCullingContracts' --timeout 60
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsCullingSystem.TeleportSnapshotTreatsCandidatesAsPhase1Visible' --timeout 60
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeCameraControllers' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'GraphicsCullingContracts|GraphicsCullingSystem.TeleportSnapshotTreatsCandidatesAsPhase1Visible|RuntimeCameraControllers' --timeout 60
ctest --test-dir build/ci --output-on-failure -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target ExtrinsicSandbox
python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require culling/instance_cull.comp.spv --require instance_cull.comp.spv
```

## Forbidden changes
- Occlusion-culling the selection buckets (breaks picking determinism).
- Direct ECS observation of camera transitions from graphics.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the heuristic + exemption contract.
- `Operational` owned by `GRAPHICS-038E` (opt-in `gpu;vulkan` conservatism smoke).
