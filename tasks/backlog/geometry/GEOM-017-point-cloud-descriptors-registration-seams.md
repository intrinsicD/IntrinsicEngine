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
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only); future paper-specific robust/global registration variants belong under `methods/geometry`.
- Spawned by [`GEOM-010`](../../done/GEOM-010-point-cloud-algorithm-pack-roadmap.md) and the [point-cloud algorithm roadmap](../../../docs/architecture/point-cloud-algorithm-roadmap.md).
- Existing `Geometry.Registration` provides point-to-point and point-to-plane ICP; this task adds the reusable descriptor/correspondence/coarse-alignment contracts that can initialize ICP and feed later robust method packages.
- Depends on `GEOM-008` dense numerical helpers for covariance/eigen local frames and on `GEOM-012` domain-view semantics when algorithms borrow mesh/graph point positions.

## Required changes
- [ ] Define public descriptor/keypoint/correspondence records with stable ownership, dimensions, backend identity, and diagnostics.
- [ ] Add deterministic keypoint candidate selection for ISS/Harris-style point-cloud saliency or document the selected first keypoint family before implementation.
- [ ] Add local descriptor storage and matching APIs suitable for FPFH/SHOT-style descriptors, including deterministic tie-breaking.
- [ ] Add a feature-based coarse-alignment seam that returns an estimated rigid transform, correspondence diagnostics, and rejection/convergence reasons.
- [ ] Keep the existing ICP path reachable as a refinement stage rather than replacing `Geometry.Registration`.
- [ ] Record paper-specific robust/global registration candidates as follow-up `methods/geometry` tasks only after the generic descriptor/correspondence seam is tested.

## Tests
- [ ] Add `unit;geometry` tests for descriptor/keypoint records, deterministic tie-breaking, and invalid descriptor dimensions.
- [ ] Add a rigid-transform synthetic fixture proving keypoint/descriptor matching is stable under translation/rotation within documented tolerances.
- [ ] Add a partial-overlap fixture with injected outliers that exercises coarse-alignment diagnostics and known transform error.
- [ ] Add degenerate local-neighborhood tests for coplanar, collinear, duplicate, and insufficient-support cases.
- [ ] Add focused registration tests proving ICP refinement remains reachable and existing `Geometry.Registration` behavior is not silently replaced.

## Docs
- [ ] Update [`docs/architecture/point-cloud-algorithm-roadmap.md`](../../../docs/architecture/point-cloud-algorithm-roadmap.md) if the selected first keypoint/descriptor family changes the pack boundary.
- [ ] Update [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md) with public descriptor/correspondence and registration-seam semantics if module surfaces change.
- [ ] Add follow-up method task records for any selected robust/global registration paper backend.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

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
ctest --test-dir build/ci --output-on-failure -R 'PointCloud|Registration|NormalEstimation|LinearAlgebra' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
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

