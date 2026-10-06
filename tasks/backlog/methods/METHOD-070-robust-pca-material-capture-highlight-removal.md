---
id: METHOD-070
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
# METHOD-070 — Highlight and defect removal for texture/material capture with RobustPCA

## Goal
- A series of K registered photographs of the same surface (texture or atlas space, varying
  lighting or small viewpoint changes): the T x K matrix (texels x images) has a **low-rank** diffuse
  appearance; specular highlights, dust, sensor defects and occluders are **sparse**. The cleaned
  texture is a robust combination of L columns; S yields a defect mask.
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any quality statement needs an `ara/logic/claims.md` row first (AGENTS.md §8b).

## Missing infrastructure (prerequisite, not in scope)
- **Image-stack input:** no CPU image/image-sequence input exists (shared prerequisite with
  METHOD-069 and METHOD-071; file once).
- **Registration into texture space:** images must be resampled into one texel grid. A fixed camera
  or flat target needs nothing more; images from several viewpoints need the calibrated multi-view
  acquisition that METHOD-048 plans. Without one of these, the task stays blocked.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | T x K registered intensities (per channel) in one texel grid. |
| Compatible entity sources | The image-stack asset; for mesh atlases, a mesh with UVs as the target of the resampling. |
| RuntimeModule | New `Runtime.AppearanceLowRankOperations` on existing editor processing commands, after the prerequisites. |
| Config/agent | `sandbox.appearance_low_rank`: stack, channel handling, lambda, tolerance, outputs; `preview_operation`/`run_operation`. |
| UI | Panel with stack list, cleaned-texture preview and defect fraction. |
| Publication | Cleaned texture through the existing texture-bake/atlas path (`Runtime.TextureBake`, METHOD-047 baseline) and a defect mask; exact binding decided at intake. |
| End-to-end tests | Synthetic texture stack with injected highlights and defects → panel/agent → cleaned texture within tolerance, mask recall reported. |

## Acceptance criteria
- [ ] Prerequisites filed and retired: image-stack input; texture-space registration (fixed camera,
      or METHOD-048 multi-view acquisition).
- [ ] Kernel extension in `Geometry.Linalg` (reuse if present): thin/partial SVD — full-U Jacobi SVD
      on a T x K matrix allocates T x T.
- [ ] Paper intake recorded (PCP for specular/shadow removal; per-channel vs joint colour formulation).
- [ ] CPU reference on synthetic stacks with known diffuse texture; saturation and misregistration
      limits documented.
- [ ] Benchmark against per-texel median on the same stacks, sealed.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|AppearanceLowRankOperations|AppearanceLowRankConfig|TextureBakeModule|AgentOperations|SandboxConfigSections|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```
