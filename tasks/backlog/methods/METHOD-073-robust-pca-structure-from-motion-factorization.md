---
id: METHOD-073
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: proposal filed from REVIEW-007 E7; blocked on missing multi-view correspondence input; implementation follows the method workflow and owes research evidence before any claim.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
---
# METHOD-073 — Robust factorization of a multi-view measurement matrix (structure from motion)

## Goal
- 2D feature tracks of P points in F views form the 2F x P measurement matrix W. Under an affine
  camera, centred W is **low-rank (rank 3)** (Tomasi–Kanade); wrong correspondences are **sparse**;
  unobserved tracks are **missing**. Factor L into cameras and 3D points; S flags outlier matches.
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any reconstruction-accuracy statement needs an `ara/logic/claims.md` row first
  (AGENTS.md §8b).

## Missing infrastructure (prerequisite, not in scope)
- **Multi-view correspondences:** the engine has no image input, feature detection or matching, and
  no track container. A smaller first step than image processing is importing tracks exported by an
  external SfM tool (e.g. a COLMAP/Bundler track file) as a numeric matrix with missing entries; that
  import task must be filed and retired first. The nearest existing seam is GEOIO-005 (CSV/NPY
  property tables), which binds tables to one element domain and does not yet model a free matrix.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | 2F x P matrix of image coordinates with an observation mask. |
| Compatible entity sources | The track asset from the prerequisite; geometry sources N/A for input. |
| RuntimeModule | New `Runtime.FactorizationSfmOperations` on existing editor processing commands, after the prerequisite. |
| Config/agent | `sandbox.factorization_sfm`: track source, camera model (affine first), lambda, tolerance; `preview_operation`/`run_operation`. |
| UI | Panel with view/track counts, outlier fraction and reprojection residual. |
| Publication | New point-cloud entity of the P recovered points with scalar residual and Bool outlier properties; camera frames as empty entities (orientation via existing TRS), up to the affine ambiguity documented. |
| End-to-end tests | Synthetic affine views of a known point set with outlier matches and missing tracks → panel/agent → points within tolerance after alignment. |

## Acceptance criteria
- [ ] Prerequisite track/correspondence import filed and retired (this task does not add it).
- [ ] Kernel extension in `Geometry.Linalg` (reuse if present): observation mask
      (`RobustPCAOptions` has no missing-entry set).
- [ ] Paper intake recorded; literature to verify: Tomasi–Kanade 1992, robust/incomplete
      factorization (e.g. Eriksson–van den Hengel L1 Wiberg), PCP with missing data.
- [ ] CPU reference with affine metric upgrade; the projective case is a documented follow-up.
- [ ] Benchmark against plain SVD factorization on the same synthetic tracks, sealed.
- [ ] New test suites `FactorizationSfmOperations`, `FactorizationSfmConfig` are added to `IntrinsicRuntimeContractTests`; they do not exist yet, and `--no-tests=error` cannot detect their absence while other selectors match.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|FactorizationSfmOperations|FactorizationSfmConfig|AgentOperations|SandboxConfigSections|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Context
- Shares the masked formulation with METHOD-068 and METHOD-074; METHOD-068 can supply rotation
  synchronization once cameras are calibrated.
