---
id: BUG-153
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-bug153"
branch: "codex/bug-153-curvature-taubin"
worktree: "/home/alex/Documents/IntrinsicEngine-bug153"
claimed_at: "2026-08-11T14:18:36Z"
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "This repair materially changes the documented numerical semantics of a public geometry module, consumes an oriented triangle mesh and its vertex/edge/face adjacency, and republishes canonical same-cardinality curvature properties through the existing runtime/UI operation. No layer edge, backend axis, topology/cardinality edit, support-radius policy, or parameterization contract changes."
maturity_target: Operational
---
# BUG-153 — Restore edge-dihedral Taubin curvature estimation

## Goal
- Replace the current curvature tensor approximation with the signed
  edge-dihedral integral estimator, two-ring support, and deterministic
  post-smoothing used by the known-good framework24 Taubin implementation.
- Keep every published scalar and principal direction in the same
  edge-dihedral estimator, derive H/K from its reference-smoothed principal
  values, and prove the fix through the live Sandbox on small and large OBJ
  meshes.

## Non-goals
- No new curvature backend, service, registry, public tuning API, or config
  schema.
- No mesh topology/cardinality edits, import changes, visualization feature,
  or curvature-segmentation redesign.
- No mechanical source moves or broad cleanup of unrelated geometry code.

## Context
- `Geometry.HalfedgeMesh.Curvature` currently fits one-ring normal variation,
  transforms the two tangent eigenvalues with `3 * lambda_a - lambda_b`, and
  rejects every boundary vertex. `ComputeCurvature` then publishes Meyer
  mean/Gaussian scalars while taking directions from that separate tensor, so
  the public field is not one estimator.
- The reference implementation in
  `experimental/framework24/lib_bcg_framework/src/bcg_mesh_curvature_taubin.cpp`
  instead integrates signed interior-edge dihedral angles times half-edge-length
  outer products over the vertex plus its one-ring neighbours, divides by their
  mixed area, identifies two tangent eigenpairs with a smallest-absolute-value
  normal heuristic, and performs three nonnegative-cotan smoothing passes.
  Restricting the same tensor to the known vertex tangent plane removes that
  heuristic's cylinder ambiguity; complementary direction pairing is required
  because a hinge measures bend across the edge whose tangent stores the tensor
  contribution.
- A live pre-fix Sandbox reproduction imported
  `/home/alex/Dropbox/Work/Datasets/obj/bunny_decimated.obj` and successfully
  executed Mesh / Processing / Curvature, confirming the defect is in numerical
  estimation rather than import, command routing, or property publication.

## Control surfaces
- Config: unchanged; this repair restores the fixed estimator behind the
  existing curvature command rather than adding tunable behavior.
- UI: unchanged Mesh / Processing / Curvature action and result diagnostics.
- Agent/CLI: unchanged runtime operation surface.

## Backends
- Backend axis: not applicable. The curvature estimator remains one
  deterministic geometry-owned CPU implementation.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | An owning oriented triangle surface with finite vertex positions, edge/halfedge/face adjacency, and mixed-area/cotan geometric operators. Faces and signed dihedrals are semantic inputs. |
| Compatible entity sources | Mesh geometry entities only; point sets and abstract graphs do not supply oriented incident faces. |
| `RuntimeModule` | Reuse the existing synchronous and queued mesh curvature operation, readiness checks, source revision validation, and stale-result handling. |
| Config/agent | Preserve the existing command surface; there is no new parameter or UI-only path. |
| UI | Reuse Mesh / Processing / Curvature and its existing status/diagnostic display. |
| Publication | Publish float scalar/vector properties on the originating vertex domain, preserve all unrelated properties and topology, and retain the existing undo/history transaction. |
| End-to-end tests | Extend focused geometry regressions, retain the runtime editor-operation contract, and execute the real Sandbox command on representative small and large OBJ assets. |

## Required changes
- [ ] Add a regression that fails on the current normal-fitting/boundary-zero
      implementation and distinguishes the reference edge-dihedral estimator.
- [ ] Port the framework24 signed edge-dihedral tensor assembly, two-ring
      support, tangent-plane decomposition, complementary eigenpair publication,
      and three deterministic nonnegative-cotan smoothing passes using internal
      double precision and existing public float properties.
- [ ] Make `ComputeCurvature` and `ComputeCurvatureTensor` publish coherent
      principal, mean, Gaussian, normal, and direction fields without changing
      their public signatures.
- [ ] Preserve fail-closed handling for deleted, isolated, degenerate, and
      non-finite geometry without blanket-zeroing estimable boundary vertices.

## Tests
- [ ] Pin analytic plane/sphere/cylinder behavior, signed orientation,
      scale covariance, ordinary triangulation, boundary support, and
      full-field/tensor consistency.
- [ ] Run the focused curvature and runtime editor-operation tests.
- [ ] Run the default CPU gate plus isolated ASan and UBSan CPU gates.
- [ ] Drive the live Sandbox curvature command on a small OBJ and a materially
      larger OBJ from `/home/alex/Dropbox/Work/Datasets/obj`, recording element
      counts, terminal status, publication counts, and non-finite diagnostics.

## Docs
- [ ] Correct the module-interface numerical contract and implementation
      rationale without narrating task history.
- [ ] Synchronize the geometry architecture/numerical-method description,
      generated module inventory, active bug index, session brief, and
      retirement records.

## Acceptance criteria
- [ ] The new regression fails against the pre-fix implementation and passes
      with the edge-dihedral estimator.
- [ ] Directions and pre-smoothing values come from one signed symmetric tensor;
      mean and Gaussian curvature equal the reference-smoothed principal-value
      invariants exactly.
- [ ] Finite supported boundary vertices are estimated when their neighbourhood
      contains valid interior edges; degenerate unsupported vertices fail
      closed with finite zero sentinels.
- [ ] The live Sandbox applies curvature successfully to both selected OBJ
      scales with zero non-finite published values.
- [ ] Required CPU, sanitizer, structural, documentation, and fixed-surface
      independent-review gates pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Curvature|SandboxEditorMeshMethods' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1

cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1

python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --path src/geometry
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
```

Live acceptance on 2026-08-11 used the ASan+UBSan `ci-vulkan` Sandbox in a
nested Xephyr display. The promoted device correctly remained fail-closed with
`VulkanRequestedButNotOperational ... BarrierValidationFailed`, so these are CPU
curvature-command and publication observations rather than Vulkan capability
evidence:

- `bunny_decimated.obj`: `Applied`; 1,485 runtime vertex slots; 2,970 scalar
  values; 2,970 direction values; 5,940 changed values; zero non-finite scalar
  or direction values.
- `inputmodels/armadillo.obj`: `Applied`; 21,582 vertices and 43,160 faces;
  43,164 scalar values; 43,164 direction values; 86,328 changed values; zero
  non-finite scalar or direction values.

## Forbidden changes
- Mixing the numerical repair with source moves or unrelated refactors.
- Adding a second curvature implementation or selectable backend without a
  present caller and a separately reviewed task.
- Silently retaining Meyer scalars beside Taubin principal directions in one
  published curvature result.
- Weakening finite-value, topology, sanitizer, or live Sandbox verification.

## Maturity
- Target: `Operational` through the existing live Sandbox mesh-curvature
  command on representative small and large imported meshes.
- No parity or accelerated-backend follow-up is owed; this is the canonical CPU
  estimator repair.
