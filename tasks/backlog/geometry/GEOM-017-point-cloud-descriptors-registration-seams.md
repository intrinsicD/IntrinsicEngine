---
id: GEOM-017
theme: none
depends_on: [GEOM-008]
---
# GEOM-017 — Point-cloud descriptors and registration seams

## Goal
- Add the generic keypoint, descriptor, correspondence, and coarse-registration seams needed for robust point-cloud registration methods without baking paper-specific backends into `src/geometry`.

## Non-goals
- No TEASER, CPD, generalized ICP, colored ICP, or other paper-specific method implementation in this task.
- No multiway registration graph optimizer in this task.
- No surface reconstruction implementation in this task.
- No GPU backend.
- No renderer/runtime/ECS/assets/platform/app integration.

## Context
- Status: implemented on branch `claude/intrinsic-engine-framework24-migrate-cew18n`;
  full `ctest` gate deferred to CI (see Status below).
- Owning subsystem/layer: `geometry` (`geometry -> core` only); future paper-specific robust/global registration variants belong under `methods/geometry`.

## Status

- Implemented module `Geometry.PointCloud.Features`
  (`src/geometry/Geometry.PointCloud.Features.cppm` + `.cpp`): ISS keypoints
  (default family), FPFH descriptors (33-D default family) with explicit
  `HasNormals()` precondition, deterministic brute-force descriptor matching
  (mutual-best + optional Lowe ratio, lower-index tie-break), and a deterministic
  RANSAC feature-based coarse alignment returning a rigid transform plus
  inlier/RMSE/iteration diagnostics and a status enum. No Eigen types cross the
  public interface; Kabsch/SVD and covariance eigen are internal (reusing
  `Geometry.PCA` and Eigen in the `.cpp`).
- Existing `Geometry.Registration` ICP is untouched and explicitly covered by a
  reachability test; coarse alignment initializes it rather than replacing it.
- Verified in-session: `check_layering`, `check_test_layout`, `check_doc_links`,
  `check_task_policy` pass; module inventory regenerated (529 modules); a
  standalone clang-20 + glm + Eigen sanity check of the Kabsch rigid recovery
  (max error 1.2e-15), FPFH Darboux binning, and ISS eigenvalue-ratio gate passes.
- Deferred to CI: `cmake --preset ci` + the `ctest -R 'PointCloud|Registration'`
  gate — the sandbox cannot bootstrap vcpkg (microsoft/vcpkg clone denied by the
  egress policy), so the full C++23-module build/test must run in CI before this
  task retires to `tasks/done/`.
- Paper-specific robust/global registration (TEASER/FGR/CPD-class) remains
  deferred to `methods/geometry` follow-ups that declare this task in
  `depends_on`, per the Non-goals.
- Spawned by [`GEOM-010`](../../done/GEOM-010-point-cloud-algorithm-pack-roadmap.md) and the [point-cloud algorithm roadmap](../../../docs/architecture/point-cloud-algorithm-roadmap.md).
- Existing `Geometry.Registration` provides point-to-point and point-to-plane ICP; this task adds the reusable descriptor/correspondence/coarse-alignment contracts that can initialize ICP and feed later robust method packages.
- Depends on `GEOM-008` dense numerical helpers for covariance/eigen local frames and on `GEOM-012` domain-view semantics when algorithms borrow mesh/graph point positions. Both prerequisites are retired, so this task is unblocked.
- The keypoint/descriptor/correspondence contracts defined here are the prerequisite seam for future paper-specific robust/global registration method packages under `methods/geometry` (TEASER/FGR/CPD-class follow-ups per `docs/roadmap.md`); those method tasks should declare this task in `depends_on` when opened.

## Required changes
- [x] Define public descriptor/keypoint/correspondence records with stable
      ownership, dimensions, backend identity (`DescriptorKind`), and diagnostics
      (`CoarseAlignmentResult` status/inliers/RMSE).
- [x] Deterministic keypoint selection: ISS saliency (selected first family),
      eigenvalue-ratio gated with deterministic non-maximum suppression.
- [x] Local descriptor storage + matching suitable for FPFH/SHOT-style
      descriptors (FPFH implemented, 33-D), with mutual-best + Lowe-ratio and
      lower-index deterministic tie-breaking.
- [x] Feature-based coarse-alignment seam (RANSAC) returning a rigid transform,
      correspondence inlier/RMSE diagnostics, and a status enum
      (`Success`/`InsufficientCorrespondences`/`NoConsensus`/`DegenerateInput`).
- [x] Existing ICP path kept reachable (reachability test) and used as the
      refinement stage; `Geometry.Registration` is unchanged.
- [x] Paper-specific robust/global registration recorded as deferred
      `methods/geometry` follow-ups (Non-goals + Context), not implemented here.

## Tests
- [x] `tests/unit/geometry/Test.PointCloudFeatures.cpp` (`unit;geometry`):
      descriptor `Row` bounds, deterministic match tie-break, invalid descriptor
      dimensions, normals precondition.
- [x] Rigid-transform fixture: FPFH rotation-invariant matching (target = rigidly
      transformed source) recovers mostly identity correspondences.
- [x] Partial-overlap/outlier fixture: coarse alignment rejects injected outlier
      correspondences and recovers the known transform within tolerance.
- [x] Degenerate neighborhoods: collinear + duplicate points handled without
      crashing; insufficient-support keypoint case returns `nullopt`.
- [x] ICP reachability: `Geometry.Registration::AlignICP` still callable and
      behaviorally intact.
- [ ] (Deferred to CI) `cmake --preset ci && ctest -R 'PointCloud|Registration'`
      — not runnable in the authoring sandbox (vcpkg egress blocked).

## Docs
- [x] Documented the selected ISS keypoint / FPFH descriptor families and the
      coarse-alignment seam in [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md).
- [x] Regenerated `docs/api/generated/module_inventory.md` (adds
      `Geometry.PointCloud.Features`; 529 modules).
- [ ] (Deferred) Follow-up `methods/geometry` task records for any selected
      robust/global registration paper backend — opened when such a method is the
      next priority (per Non-goals); the roadmap pack boundary is unchanged.

## Acceptance criteria
- [ ] Point-cloud keypoints, descriptors, correspondences, and coarse-registration results have explicit records and diagnostics.
- [ ] Coarse alignment can initialize existing ICP without making ICP the only public registration path.
- [ ] Deterministic tests cover descriptor matching tie-breaks, known rigid transforms, partial overlap, and degenerate neighborhoods.
- [ ] Paper-specific robust/global registration variants are deferred to method tasks with CPU-reference-first workflow.
- [ ] The implementation preserves `geometry -> core` layering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloud|Registration|PointCloudNormals|LinearAlgebra' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not implement TEASER, CPD, generalized ICP, colored ICP, or another paper-specific backend in `src/geometry` without a method task.
- Do not remove or silently replace the existing ICP API.
- Do not require normals/descriptors without an explicit precondition or documented generation path.
- Do not introduce renderer/runtime/ECS/assets/platform/app dependencies.

## Maturity
- Target: `CPUContracted` (generic CPU seams; the reference correctness oracle for later method packages).
- No `Operational` follow-up is owed; this task has no backend seam.
