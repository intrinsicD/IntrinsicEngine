---
id: BUG-154
theme: G
depends_on: [BUG-155]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-11T19:03:44Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "The repair introduces a reusable vertex-property smoothing contract with an optional active-support mask, corrects and quality-gates the public curvature estimator against its PMP reference, and preserves authored normals on their canonical corner domain through import, runtime materialization, and rendering. It does not change layer edges, mesh cardinality, method backend selection, support-radius policy, or parameterization numerics."
maturity_target: Operational
---
# BUG-154 — Restore PMP curvature parity without normal-seam topology loss

## Status

- The estimator, quality gate, reusable smoothing, corner-normal path, corpus
  diagnostic, and literature study are implemented. Final current-source CPU,
  isolated ASan/UBSan, strict structural, and task-specific promoted-Vulkan
  visibility/readback gates pass.
- The full promoted-Vulkan cohort passed once, then two identical reruns failed
  only an unrelated native-timestamp strict-positive-duration assertion while
  the other 53 cases passed; immediate isolated x3 reruns passed. `BUG-155`
  preserves that profiler flake without retrying it away. This task remains
  active for that full-gate dependency, the required independent high-risk
  fixed-surface review, and revision-bound final evidence binding.

## Goal

- Match the scalar curvature behavior of the known-good PMP implementation,
  including its signed full-tensor eigendecomposition, boundary interpolation,
  and damped nonnegative-cotan smoothing.
- Preserve OBJ authored normal discontinuities as corner-domain shading data
  without splitting the authoritative halfedge topology.
- Make the cotan smoothing operation reusable for canonical floating-point and
  float-vector vertex properties rather than embedding it in curvature.
- Establish whether that parity generalizes across the local OBJ corpus,
  distinguish reference-port errors from malformed-input behavior, and make
  local support failure deterministic and finite.
- Compare the selected PMP formulation with primary literature on
  value/direction quality and performance, without changing the default absent
  a matched accuracy benchmark.

## Non-goals

- No new solver dependency, curvature backend, service, registry, or tuning UI.
- No topology/cardinality edit, automatic vertex welding, or reinterpretation
  of genuinely distinct OBJ position indices.
- No generalized arbitrary-domain diffusion framework; this slice smooths
  vertex-domain values because mesh adjacency is a semantic input.
- No public persisted `glm::dvec*` property contract.
- No second curvature backend or universal performance/quality claim from a
  dirty-source, machine-local diagnostic run.

## Context

- The previous repair copied the edge-dihedral tensor assembly but replaced
  PMP's three updates of `0.5 * old + 0.5 * cotan_average` with full neighbour
  replacement, projected the tensor into a preselected tangent basis instead
  of solving the signed symmetric 3x3 problem, and estimated boundary centers
  directly instead of interpolating them after the interior pass.
- An authored-normal bunny OBJ contains 259 position records and 495 face
  normals. The current loader keys topology by `(position, texcoord, normal)`,
  materializing 1,485 disconnected vertex corners; every runtime vertex is
  consequently boundary-only and curvature publishes finite zeros while
  reporting `Applied`.
- The PMP reference has no sparse/global solve in this path. The discrepancy
  is local estimator, boundary, import-domain, and smoothing semantics.
- Corpus scale sweeps exposed absolute area/cotan/tensor gates: clean geometry
  at scale `1e-6` collapsed to zero before the repair. Corpus outliers after
  removing those gates localized to exactly degenerate or severely
  ill-conditioned triangles. The PMP reference can normalize zero face normals
  and leave a triangle Laplace output unwritten on invalid Heron area, so
  literal parity there is neither stable nor finite.
- The dimensionless `3.5e-4` reliability floor is the conservatively rounded
  square root of machine epsilon for public float positions, not a model-unit
  or corpus-specific tolerance.

## Control surfaces

- Config: unchanged; the fixed curvature operation retains its deterministic
  reference constants and does not expose a new tuning axis.
- UI: unchanged Mesh / Processing / Curvature action, with truthful support and
  rejected-face diagnostics added to its existing result line.
- Agent/CLI: unchanged runtime operation path and status enum.

## Backends

- Backend axis: not applicable. Curvature and property smoothing remain one
  deterministic geometry-owned CPU implementation; corner-normal rendering
  uses the existing runtime-to-RHI geometry plan.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Curvature consumes an oriented triangle surface with finite positions, faces, and vertex/edge/halfedge adjacency. Reusable smoothing consumes a canonical scalar or float-vector property on the mesh vertex domain plus cotan adjacency. |
| Compatible entity sources | Any owning halfedge mesh satisfying those sources; point sets and property domains without vertex adjacency are not eligible. OBJ authored normals bind to the halfedge/corner domain and do not determine topology. |
| `RuntimeModule` | Reuse the existing mesh import/materialization, geometry-plan extraction, and synchronous/queued curvature operation. Runtime owns payload-corner to live-halfedge mapping. |
| Config/agent | Preserve the current command surface; no parameter or UI-only path is introduced. |
| UI | Reuse Mesh / Processing / Curvature and expose supported/nonzero/range plus degenerate/ill-conditioned face diagnostics through the same result record. |
| Publication | Publish curvature on the originating vertex domain, preserve unrelated topology/properties, keep authored `h:normal` values on the corner domain, and perform any corner seam duplication only in the GPU upload plan. |
| End-to-end tests | Cover reusable scalar/vector smoothing, a PMP numeric oracle, OBJ normal-seam round trip, runtime materialization, geometry-plan splitting, and the existing Sandbox operation. |

## Right-sizing

- Extend the existing `Geometry.Smoothing` module with plain result/parameter
  records and concrete overloads for `float`, `double`, and canonical
  `glm::vec2/vec3/vec4` vertex properties. One private implementation template
  supplies the overloads; no interface, factory, backend, or extra module is
  justified.
- Keep the signed 3x3 Jacobi eigensolver private to curvature. It is a small
  allocation-free reference kernel, not a new public linear-algebra seam.

## Required changes

- [x] Add fail-closed, Jacobi-buffered nonnegative-cotan smoothing with a
      configurable iteration count, damping factor, and boundary pin policy to
      `Geometry.Smoothing`; instantiate the canonical scalar/vector property
      overloads.
- [x] Restore PMP-equivalent curvature tensor decomposition, interior-support
      handling, boundary scalar interpolation, and three damped smoothing
      passes while keeping published principal directions finite and coherent.
- [x] Parse discontinuous or non-lockstep OBJ `vn` indices into `h:normal`
      without placing normal identity in the topology remap key, retain the
      compact `v:normal` convention for exact `v`/`vn` lockstep files, and
      write corner normals back with per-corner indices.
- [x] Carry corner normals through runtime materialization and resolve them
      ahead of `v:normal` in the render plan, splitting GPU vertices by the
      complete corner shading tuple without mutating the mesh.
- [x] Report supported/nonzero curvature counts and scalar ranges; reject an
      operation with no estimable support instead of presenting an all-zero
      field as an informative success.
- [x] Replace absolute geometric epsilon gates with homogeneous predicates;
      reject tensor support at or below the dimensionless triangle-quality
      floor and exclude those rows/neighbours through the reusable smoother's
      active mask.
- [x] Retain an opt-in, dependency-neutral Intrinsic/PMP OBJ corpus diagnostic
      and document the primary-literature estimator tradeoff.

## Tests

- [x] Prove scalar and vector smoothing retain the PMP self-weight, use
      simultaneous buffers, preserve requested boundaries, and fail closed on
      invalid/non-finite input.
- [x] Pin principal curvatures against PMP numeric fixtures and distinguish the
      old 2x2/full-replacement/boundary-direct behavior.
- [x] Prove an authored face-normal OBJ remains connected, round-trips normal
      indices, materializes `h:normal`, and emits the expected split render
      vertices/normals.
- [x] Retain analytic curvature, runtime editor-operation, OBJ I/O, runtime
      asset-workflow, and geometry-plan coverage.
- [x] Prove extreme uniform-scale invariance and local fail-closed behavior for
      an ill-conditioned triangle while preserving finite scalar/direction
      publication elsewhere.
- [x] Run a deterministic size-stratified corpus differential against the
      pinned local PMP revision, including clean, rejected-support, direction
      invariant, uniform-scale, and matched-build timing views.
- [x] Run the default CPU gate, isolated ASan/UBSan gates, structural checks,
      and the applicable Vulkan smoke on a capable host.

## Docs

- [x] Document reusable property smoothing and the corrected curvature
      numerical contract at the module boundary.
- [x] Document `h:normal`, corner-over-vertex normal resolution, and GPU-only
      seam splitting in geometry property/import architecture.
- [x] Synchronize generated skill mirrors, module inventory, bug/active task
      indices, and session brief.
- [x] Record the corpus experiment, source/binary/result identities, solver
      diagnosis, literature comparison, limitations, and recommendation in a
      durable report.
- [ ] Bind the final high-risk workflow evidence after independent review.

## Acceptance criteria

- [x] The 259-position authored-face-normal bunny imports as one connected
      259-vertex surface rather than 1,485 disconnected corners.
- [x] Curvature scalar output matches the PMP reference within the declared
      floating-point tolerance on well-conditioned support and has estimable
      support on representative open and closed meshes.
- [x] The reusable smoother behaves identically for supported scalar and
      vector property value kinds and leaves unrelated properties/topology
      untouched.
- [x] Corner normals survive OBJ load/write/materialization and control render
      vertex normals without becoming an authoritative topology split.
- [x] Zero-support curvature cannot be mistaken for a successful informative
      field, and all published diagnostics are deterministic and finite.
- [x] Degenerate/ill-conditioned support cannot diffuse through smoothing;
      ordinary well-conditioned curvature remains stable under uniform scale.
- [ ] Required CPU, sanitizer, structural, documentation, and promoted-Vulkan
      gates pass on the final current-source surface.
- [ ] Independent fixed-surface review accepts the final high-risk revision.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^(Smoothing\.|CurvatureTensor\.|Curvature_|OBJ_|RuntimeAssetWorkflow\.|RuntimeGeometryPlan\.|SandboxEditorUi\.MeshCurvature)' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1

cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1

cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/sync_skills.py --write
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .

cmake --preset ci-release -DINTRINSIC_BUILD_DIAGNOSTIC_TOOLS=ON
cmake --build build/ci-release --target IntrinsicCurvatureCorpusProbe
python3 tools/diagnostics/curvature/compare_curvature_corpus.py \
  --dataset-root /home/alex/Dropbox/Work/Datasets/obj \
  --intrinsic build/ci-release/bin/IntrinsicCurvatureCorpusProbe \
  --pmp /tmp/PmpCurvatureCorpusProbe-clang23 \
  --output /tmp/bug154-corpus-final.json \
  --max-models 96 --max-source-bytes 50000000
```

Recorded results on 2026-08-11 and 2026-08-12:

- Focused smoothing, PMP curvature-oracle, OBJ I/O, runtime materialization,
  scene persistence, zero-support, and render-plan selectors passed. The
  frozen 25-slot PMP fixture matches within `2e-6` and reports 25 supported,
  nonzero samples.
- The real `bunny_decimated.obj` acceptance probe materialized 259 vertices
  and 495 faces with finite corner normals, rather than the previous 1,485
  disconnected vertices.
- The final default CPU selector passed 4,252/4,252 selected tests; its
  environment-gated GLFW/LSan case recorded the expected skip. That full pass
  followed correction of a pre-existing DEC test tolerance that demanded
  `1e-10` equality from a triangle whose coordinates were already rounded to
  float; the realized-area comparison now uses a documented `1e-8` tolerance.
- The final isolated ASan and UBSan selectors each passed 2,744/2,744 selected
  tests. The ASan identity ran its GLFW/LSan case; the UBSan identity recorded
  the expected skip of that LeakSanitizer-only case.
- The promoted Vulkan ASan+UBSan gate first passed 54/54 tests. Two subsequent
  evidence-recorded full reruns passed 53/54: exact normal-bake readback,
  seam-split geometry rendering, validation, and the shutdown LeakSanitizer
  contract all remained green, while only the unrelated native-timestamp
  strict-positive-duration assertion intermittently resolved zero. The exact
  failing case then passed three isolated repetitions; `BUG-155` preserves the
  failure and owns its raw-query/slot-reuse diagnosis. The two task-specific
  normal-path Vulkan cases pass 2/2 in the final required receipt.
- Strict layering, test-layout, task-policy/schema, documentation-link, ARA,
  workflow-evidence, source-documentation, generated-inventory, skill-sync,
  and clean-workshop checks pass. Source-documentation review-only findings are
  pre-existing and do not affect this surface.
- The final 96-model local corpus run directly compared 80 models and 2,530,726
  vertices. Median full-field relative L2 errors were `4.15e-8`/`3.31e-8`;
  across 62 models without rejected triangles the worst errors were
  `4.55e-7`/`2.23e-7`. More than four rings from rejected support, the worst
  errors were `3.48e-6`/`3.25e-6` over 2,520,863 vertices.
- The corpus found 129 degenerate and 376 ill-conditioned triangles. Large
  full-field differences localized to that support and smoothing vicinity;
  one PMP result was non-finite while Intrinsic remained finite. Clean controls
  retained parity through scales `1e-6` to `1e3`; scale `1e6` remained within
  `2.58e-6` relative L2, consistent with public float position quantization.
- A matched five-repetition local probe was faster than the pinned PMP build on
  all four clean timing controls (median Intrinsic/PMP compute ratio `0.207`),
  but is explicitly non-claim evidence. The literature review found no
  universally reliable estimator and recommends retaining this path as the
  clean-mesh baseline pending a matched Rusinkiewicz/corrected-measure accuracy
  benchmark.
- Two accidentally overlapping Ninja processes corrupted the incremental
  `build/ci-release` module state. Subsequent Clang-23 module serialization bus
  errors occurred in unrelated runtime interfaces. Per stale-build triage, no
  source workaround was made: focused geometry passed with ccache disabled and
  the authoritative verification was moved to the reconfigured canonical
  `build/ci` tree.

## Self-review

- Scope remains one end-to-end correction: estimator semantics, its reusable
  smoothing primitive, and the corner-normal import defect that destroyed the
  estimator's topology on the reported asset.
- Layer ownership is unchanged: geometry owns algorithms and OBJ semantics;
  runtime owns materialization, persistence, and ECS-to-render packing.
- The complete corner shading tuple is represented by one shared runtime
  helper, and explicit normal bindings override both corner and default vertex
  normals consistently in extraction and texture baking.
- No renderer member, typed frame pass, recipe dependency, or new higher-layer
  public type was introduced; the manual clean-workshop rows are not
  applicable. The strict automated clean-workshop gate passes.
- Remaining closure is outside the implemented geometry repair: `BUG-155` must
  restore the complete promoted-Vulkan cohort, then a label-distinct reviewer
  must audit the exact frozen surface before this high-risk task can retire.

## Forbidden changes

- Mixing the repair with source moves or unrelated cleanup.
- Welding vertices by coordinate tolerance or treating authored normal identity
  as an owning-mesh topology key.
- Adding a solver/dependency for the local 3x3 eigensystem or a second
  selectable curvature backend.
- Publishing public double-vector properties contrary to the geometry vector
  property policy.
- Weakening finite-value, topology, sanitizer, or visibility verification.

## Maturity

- Target: `Operational` through the existing OBJ import and Sandbox curvature
  command, including corner-normal control of the promoted render plan.
- CPU-only hosts may establish `CPUContracted`; Operational closure requires
  retained Vulkan-capable visible/readback evidence or an explicit follow-up.
