---
id: METHOD-069
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: proposal filed from REVIEW-007 E7; blocked on missing image-stack input; implementation follows the method workflow and owes research evidence before any claim.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
---
# METHOD-069 — Robust photometric stereo with RobustPCA

## Goal
- Fixed camera, K images under known distant lights: the P x K intensity matrix (pixels x images)
  of a Lambertian surface is **low-rank (rank <= 3)**; cast/attached shadows and specular highlights
  are **sparse**. From L, solve per-pixel albedo-scaled normals with the light matrix.
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any normal-error statement needs an `ara/logic/claims.md` row first (AGENTS.md §8b).

## Missing infrastructure (prerequisite, not in scope)
- **Image-stack input:** the engine decodes images only as model companion textures for the GPU
  (`Runtime.AssetWorkflowModelTextureDecode.cpp`); it has no CPU image or image-sequence input.
  A task for a CPU image-stack asset (equal-size grey/RGB frames, linear intensity) must be filed and
  retired first. METHOD-070 and METHOD-071 need the same input; file it once.
- **Light calibration:** per-image light directions (and intensities) as input data; a light-file
  format is part of that prerequisite, not of this task.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | P x K linear intensities plus K light directions; optional saturation mask. |
| Compatible entity sources | The image-stack asset from the prerequisite; geometry sources N/A for input. |
| RuntimeModule | New `Runtime.PhotometricStereoOperations` on existing editor processing commands, after the prerequisite. |
| Config/agent | `sandbox.photometric_stereo`: image stack, light file, lambda, tolerance, outputs; `preview_operation`/`run_operation`. |
| UI | Panel listing the stack and lights, with residual/sparse-fraction diagnostics. |
| Publication | New point-cloud entity on the pixel grid (pixel centres, image plane) with `vec3` normal, scalar albedo and scalar sparse-magnitude properties; optional height integration is a follow-up. |
| End-to-end tests | Rendered synthetic sphere stack with shadows/highlights → panel/agent → normals within tolerance on lit pixels. |

## Acceptance criteria
- [ ] Prerequisite image-stack input task filed and retired (this task does not add it).
- [ ] Kernel extensions in `Geometry.Linalg` (reuse if present): thin/partial SVD — full-U Jacobi SVD
      on a P x K matrix allocates P x P; optional observation mask for saturated pixels.
- [ ] Paper intake recorded; literature to verify: Wu–Ganesh–Li–Matsushita–Ma (ACCV 2010, robust
      photometric stereo via low-rank recovery), Woodham 1980.
- [ ] CPU reference on synthetic Lambertian sphere stacks with injected shadow/specular masks;
      fewer than three non-coplanar lights fail closed.
- [ ] Benchmark against least-squares photometric stereo on the same synthetic stacks, sealed.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|PhotometricStereoOperations|PhotometricStereoConfig|AgentOperations|SandboxConfigSections|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```
