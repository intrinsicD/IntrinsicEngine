---
id: GEOM-114
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: deferred numerical defect from RUNTIME-299 review; implementation and scale-sweep regressions belong to this task
contract_schema: 1
contracts: []
contract_review: numerical correction inside the existing PCA kernel and its GLSL twin; no property-domain, publication, integration, or ownership contract changes
---
# GEOM-114 — Make the PCA isotropic cutoff relative to covariance scale

## Current status — 2026-10-16

The CPU and GLSL PCA correction, scale regressions and native normal checks are
implemented and verified under [HARDEN-090](../active/HARDEN-090-src-audit-remediation.md).
That task owns the canonical verification and review record. The reproducer below
describes the audited baseline; acceptance and retirement are recorded separately.

## Goal
Correct the absolute-scale isotropic test in `Geometry.PCA` and
`assets/shaders/include/pca_eigen_double.glsl` without changing their public
contracts. RUNTIME-299 originally deferred this pre-existing defect to this task.

## Reproducer and affected callers
At the audited baseline, `Eigen` / GLSL `pcaEigen` computed `p2` (squared covariance deviation) and tested
`p2 > epsilon * scale * scale`, with `scale` floored at 1.0. Below that floor,
the cutoff is absolute. Small planar patches (including spacing below roughly
1.6e-4, depending on neighborhood shape) can enter the isotropic branch, yielding
an identity eigenframe and a valid +Z normal regardless of the plane.

Concrete reproduction: pass the nine points `(0, i*1e-5, j*1e-5)`,
`i,j in {-1,0,1}`, to `ToPCA`. The covariance diagonal is approximately
`(0, 6.6666667e-11, 6.6666667e-11)`; the plane normal is +/-X. The original
isotropic branch returned the identity frame and three equal mean eigenvalues.
Its smallest eigenvector is +Z, and the point-normal collinearity check can
accept it. Compare the same neighborhood at unit spacing and sweep scales.

Audit every `ToPCA` caller: point-cloud normals; point-cloud features/keypoints;
graph vertex normals; `HalfedgeMesh.Utils::VertexOneRingPCA`; Plane, Segment and OBB
fitting. Also cover direct `PCA::SymmetricEigen3` consumers and the GPU normal
kernel. The sign convention added by RUNTIME-299 cannot repair a wrong eigenspace.

## Acceptance criteria
- [x] Replace the absolute isotropic cutoff with a justified relative criterion, with explicit zero-covariance and underflow handling; synchronize the CPU and GLSL algorithms.
- [x] Reproduce the nine-point YZ-plane defect and cover rotated planes, isotropic/repeated roots, degenerate inputs and scales on both sides of the old cutoff.
- [x] Test eigenpair residuals and fitting results, not only `Valid`; review all callers listed above and preserve intentional fallback policies.
- [x] Keep signed CPU/GPU normal parity and document the supported scale/precision limits; run the Vulkan parity smoke on a capable host before claiming GPU parity.

Acceptance source: independent Claude session `523e037c-7983-45ca-a4a9-9343e9459dd9`
explicitly judged “AC1 met”, “AC2 met, with one limitation”, “AC3 met”, “AC4 met”.
The optional direct zero-covariance test is not an acceptance blocker. Final
receipt reconciliation by `/root/implement_platform_assets` is recorded in
HARDEN-090. Formal retirement/commit association remains with that integrated task.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'Pca|PCA|PointCloudNormals|GraphVertexNormals|Plane|Segment|OBB' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RUNTIME299PointNormalsResidency|PointLBVHGpuSmoke.NormalNeighborhoods' --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```

## Completion
Completed 2026-10-19. Commit: `ffe7e8372` (HARDEN-090). Acceptance as
recorded above. Main [`ci-linux-clang` run 39168698007](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/39168698007)
on `64d691ae5` runs the full CPU suite, which reports 0 failures of 4076 (7 skipped at runtime). The Vulkan parity evidence is the
HARDEN-090 native record; no new GPU run was made for retirement.
