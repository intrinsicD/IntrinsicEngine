---
id: METHOD-071
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
# METHOD-071 — Background/foreground separation with RobustPCA

## Goal
- Frames of a static camera as columns of a P x F matrix: the static background is **low-rank**,
  moving objects are **sparse**. Outputs: background image(s) and per-frame foreground masks.
- Engine-relevant variants decided at intake: depth/range frames from a fixed sensor, or frames from
  the engine's own deterministic view capture (RUNTIME-281/GRAPHICS-109) for testing.
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any segmentation-quality statement needs an `ara/logic/claims.md` row first
  (AGENTS.md §8b).

## Missing infrastructure (prerequisite, not in scope)
- **Image/video-sequence input:** no CPU image-sequence or video input exists (shared prerequisite
  with METHOD-069 and METHOD-070; file once). Video decoding would be an additional dependency
  decision; an image-sequence directory is the smaller first step.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | P x F matrix of equal-size frames (intensity or depth). |
| Compatible entity sources | The image-sequence asset; geometry sources N/A for input. |
| RuntimeModule | New `Runtime.BackgroundSeparationOperations` on existing editor processing commands, after the prerequisite. |
| Config/agent | `sandbox.background_separation`: sequence, lambda, tolerance, mask threshold; `preview_operation`/`run_operation`. |
| UI | Panel with frame scrubber showing background, foreground mask and residual. |
| Publication | Pixel-grid point-cloud entity with background value and per-frame mask properties, or image output once an image writer exists (GRAPHICS-109); decided at intake. |
| End-to-end tests | Synthetic sequence (static background, moving square) → panel/agent → masks within tolerance. |

## Acceptance criteria
- [ ] Prerequisite image-sequence input task filed and retired (this task does not add it).
- [ ] Kernel extension in `Geometry.Linalg` (reuse if present): thin/partial SVD — full-U Jacobi SVD
      on a P x F matrix allocates P x P; memory bound documented.
- [ ] Paper intake recorded; literature to verify: Candès–Li–Ma–Wright 2011 (surveillance video
      example), Bouwmans–Zahzah review of RPCA background subtraction.
- [ ] CPU reference on synthetic sequences; static-only and all-moving inputs handled.
- [ ] Benchmark against a per-pixel temporal median baseline, sealed.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|BackgroundSeparationOperations|BackgroundSeparationConfig|AgentOperations|SandboxConfigSections|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```
