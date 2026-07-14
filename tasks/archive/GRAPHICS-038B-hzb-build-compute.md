# GRAPHICS-038B — HZB build compute shader + dispatch wiring

## Status
- Commit reference: this task-landing commit.
- Landed 2026-06-04 at maturity `CPUContracted`. Added
  `assets/shaders/hzb_build.comp`, the pure
  `ComputeHZBBuildDispatchPlan(...)` selector, `RecordHZBBuild(...)`, the
  default-recipe `HZBBuildPass` after `DepthPrepass`, renderer-owned
  `HZB.Current` import/recording, HZB pipeline descriptor/lease plumbing, null
  RHI dispatch/barrier contract coverage, shader-output verification, and docs.
  The CPU/null path records the backend-neutral per-mip fallback shape by
  default; single-pass/SPD-style storage-image publication and
  `gpu;vulkan` conservatism proof remain `GRAPHICS-038E`.

## Goal
- Implement the HZB build compute pass (`hzb_build.comp`, max-reduction down-sampler)
  with the single-pass SPD-style mip-chain path where supported and the per-mip dispatch
  fallback otherwise (`GRAPHICS-038` decision 2), with null-RHI dispatch-shape tests.

## Non-goals
- No cull-shader phase-1/phase-2 extension (that is `GRAPHICS-038C`).
- No camera-transition heuristic (that is `GRAPHICS-038D`).

## Context
- Owner layer: `graphics/renderer` (build pass) producing into the `GRAPHICS-038A` HZB.
- Depends on `GRAPHICS-038A` (HZB resource + lifetime).
- Decision 2: compute shader, one workgroup per output tile using `subgroupMax` where
  available else shared-memory tree max-reduction; single-pass mip-chain (SPD-style,
  last-workgroup mip stitching via global atomics) where available, per-mip dispatch
  fallback otherwise.

## Required changes
- [x] Add `assets/shaders/.../hzb_build.comp` with subgroup + shared-memory reduction paths.
- [x] Wire the build pass (single-pass mip-chain + per-mip fallback) into the recipe.
- [x] `contract;graphics` null-RHI tests for dispatch shape, mip-stitch ordering, and
      the fallback path selection.

## Tests
- [x] `contract;graphics` — dispatch count per path; correct per-mip coverage; fallback
      selection when single-pass capability is absent.
- [x] CPU gate green.

## Docs
- [x] Document the HZB build pass in `src/graphics/renderer/README.md`.
- [x] Update durable rendering/debug-view docs for `HZB.Current`.

## Acceptance criteria
- [x] The HZB build pass is wired and CPU-tested for dispatch shape (both paths).
- [x] No new layering violations.

## Verification
```bash
glslc assets/shaders/hzb_build.comp -I assets/shaders -o /tmp/hzb_build.comp.spv --target-env=vulkan1.3
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R '(GraphicsHZB|HZBBuild|FrameRecipeContract|RendererFrameLifecycle)' --timeout 60
ctest --test-dir build/ci --output-on-failure -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox
python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require hzb_build.comp.spv
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
```

Results:
- Focused HZB/frame-recipe/renderer subset: 85/85 passed.
- Full graphics label subset: 388/388 passed.
- Default CPU-supported gate: 2706/2706 passed.
- Shader output check found 80 SPIR-V shader outputs, including `hzb_build.comp.spv`.
- Structural/docs checks reported no findings.

## Forbidden changes
- Live ECS access from renderer code.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the build-pass dispatch contract.
- `Operational` owned by `GRAPHICS-038E` (opt-in `gpu;vulkan` conservatism smoke).
