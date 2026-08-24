---
id: BUG-156
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-13T13:52:29Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "This repair replaces the public principal-curvature numerical contract with the deterministic sequential semantics of Framework24 CurvatureTaubin. It changes estimator support, area normalization, sign/order, boundary treatment, and post-smoothing while preserving the geometry-owned CPU operation, vertex-domain publication, runtime/config/UI command path, property names, topology, and cardinality."
maturity_target: CPUContracted
---
# BUG-156 — Adopt deterministic Framework24 Taubin curvature semantics

## Status

- Reporter rejected the one-ring unsmoothed correction after direct comparison
  with Framework24. The task is reopened around Framework24 interoperability;
  the previous accuracy claim is no longer the selected product contract.
- A local, non-claim-eligible differential of the deterministic direct port
  matched sequential Framework24 on all 3,669 sculpt vertices with maximum
  absolute errors of `6.22e-15` (`kmin`) and `2.78e-15` (`kmax`). The final
  unsanitized CPU gate passes all 4,256 selected tests and the final UBSan gate
  passes all 2,744 selected tests. The isolated ASan gate is blocked only by
  the repeatable pre-existing GLFW/X11 LeakSanitizer failure tracked as
  BUG-118. This repair's exact-revision independent review remains open;
  `BENCH-001` separately owns the frozen claim-grade matched run required for a
  claim-eligible Framework24 parity statement.
- The original BUG-156 live review cycle exhausted its fourth bounded writer
  attempt when the strict maturity validator required one final task-wording
  correction. Its state is retained without manual mutation; BUG-162 owned the
  exact combined branch-integration surface and retired on 2026-08-24.
- 2026-08-24: the runtime minimum/maximum publication gap is implemented. The
  mesh-curvature transaction now captures, publishes, compares, and restores
  `v:min_principal_curvature` and `v:max_principal_curvature` alongside
  mean/Gaussian on both the synchronous command and the queued CPU worker, and
  the two paths share the staging helpers so they cannot drift. Result counters
  report the truthful four-property surface (`ScalarPropertyCount` 4,
  `ScalarWrittenCount` four per vertex slot). Runtime and convergence docs are
  synchronized. Tests are extended for synchronous and queued publication,
  undo/redo, the scalar-only fallback, and a new stale-principal-scalar
  rejection case.
- That change is **not yet verified by the repository gate**. The session ran
  in a container whose egress policy returns HTTP 403 for every GitHub source
  archive, so vcpkg cannot fetch the dependency set and no preset configures.
  A system-package substitute is not available either: EnTT, xatlas, implot,
  and imguizmo are absent from the distribution index, and a non-preset tree is
  not valid verification for module changes under `AGENTS.md`. No engine target
  was compiled and no ctest case was executed.
- What was verified instead, and what it is worth: the changed transaction
  helpers were extracted verbatim into a standalone clang-20 C++23 harness with
  minimal `PropertySet`/`VertexProperty`/`CurvatureField` stubs, compiled
  `-Wall -Wextra` clean, and run under ASan+UBSan. It exercises first
  publication, undo, redo, idempotent recompute, the staleness comparison, the
  fail-closed paths, and capture rejection of an incompatible stored property.
  Five mutations that reproduce the pre-change behavior (comparison ignoring
  the principal scalars, staging only mean/Gaussian, capture not reading the
  columns back, the usability check dropping them, stale counters) are each
  detected. This checks the transaction logic and the well-formedness of the
  changed expressions; it does **not** exercise the real module graph, the ECS
  registry, the job lane, or the undo history, so it substitutes for nothing in
  the acceptance criteria below.
- The harness surfaced one real source hazard, now fixed by a comment: the
  validity conjuncts in `MeshCurvatureScalarsUsable` are load-bearing, because
  `Vector()` on an unset property dereferences null storage. Reordering them
  would convert a fail-closed diagnostic into undefined behavior.
- The remaining acceptance criteria stay unchecked until the focused selector,
  the full CPU gate, and the isolated ASan/UBSan gates run on a host that can
  build; the exact-surface review is open independently of that.

## Goal

- Adopt Framework24 `CurvatureTaubin`'s numerical choices in
  `ComputeCurvatureTensor` for identical finite triangle-mesh coordinates,
  using its default two-ring support and three smoothing passes with a
  deterministic sequential execution order suitable for tests and production.

## Non-goals

- No selectable curvature backend, factory, service, registry, or tuning UI.
- No automatic centering or AABB normalization inside the curvature routine;
  Framework24 `MeshIo` normalization is an input-unit transformation and parity
  comparisons supply identical coordinates.
- No copy of Framework24's `ParallelUnsequential` in-place smoothing race.
- No emulation of Framework24's private, pre-populated `v_feature` smoothing
  mask. The public Intrinsic operation has no feature-mask input, and the
  reference parity fixtures use Framework24's default all-false property.
- No change to OBJ topology/materialization, curvature property names, result
  cardinality, or unrelated reusable `Geometry.Smoothing` behavior.

## Context

- Owning layer: `geometry`; the operation consumes oriented triangle-surface
  adjacency and publishes same-cardinality vertex properties.
- Framework24 defaults to two-ring support, three cotan-weighted in-place
  smoothing passes, and `ParallelUnsequential`. Before this repair, Intrinsic
  used one-ring support, no smoothing, the opposite signed-dihedral convention,
  standard mixed areas, complementary direction pairing, and interpolated
  boundary values.
- On the closed unit-extent `tests/data/sculpt.obj`, every vertex not incident
  to one of the 25 obtuse triangles satisfies
  `framework_min = -0.5 * intrinsic_max` and
  `framework_max = -0.5 * intrinsic_min` to approximately `1e-15` under matched
  one-ring/raw settings. This rules out connectivity, hinge accumulation, and
  eigendecomposition defects and isolates sign/order plus Framework24's legacy
  mixed-area formula. The remaining raw discrepancies are confined to the 75
  vertices incident to obtuse faces.
- Framework24 computes boundary tensors directly rather than interpolating
  interior values. Its loader also divides coordinates by maximum AABB extent,
  so curvature produced after that loader carries the corresponding inverse-
  length conversion.
- Framework24's default parallel smoother reads and writes the same neighbor
  arrays concurrently. Twelve identical runs produced twelve different field
  hashes, so parity is defined against the same code under `Policy::Sequential`.
- Before this repair, the PMP corpus probe passed `(twoRing, true)` into
  `(use_tensor, use_two_ring)`, selecting the Meyer scalar path whenever
  `twoRing` was false. The diagnostic correction is independent of the
  Framework24 port.

## Control surfaces

- Config: unchanged fixed default; no new tuning state.
- UI: unchanged Mesh / Processing / Curvature action and diagnostics.
- Agent/CLI: unchanged runtime curvature-operation path.

## Backends

- Backend axis: not applicable; one deterministic geometry-owned CPU
  implementation remains.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | An oriented finite triangle surface with vertex/edge/halfedge/face adjacency. |
| Compatible entity sources | Any owning halfedge mesh satisfying that surface contract. |
| `RuntimeModule` | Reuse the existing synchronous and queued mesh-curvature operation. |
| Config/agent | Reuse the existing fixed command; no parameter surface is added. |
| UI | Reuse Mesh / Processing / Curvature and its existing diagnostics result. |
| Publication | Geometry publishes min/max/mean/Gaussian curvature and principal directions on the originating vertex domain. The runtime command persists all four scalars plus optional directions in one atomic transaction with undo/redo; the remaining gate is execution of the verification suite, not further implementation. |
| End-to-end tests | Geometry parity fixtures plus the existing runtime and Sandbox curvature-operation contracts. |

## Right-sizing

- Port the Framework24 loops directly into the existing implementation unit.
  Keep constants private and fixed; one implementation does not justify a new
  public strategy seam or configuration record.
- Use deterministic vertex-index order for Framework24's in-place smoothing.
  A buffered/Jacobi rewrite would be race-free but would not reproduce the
  sequential reference values.

## Required changes

- [x] Port Framework24's legacy vertex mixed-area calculation, including its
      acute/obtuse coefficients and cotangent clamp.
- [x] Port its signed hinge tensor, two-ring neighborhood accumulation,
      algebraic eigenvalue/direction pairing, and direct boundary evaluation.
- [x] Port three nonnegative-cotan full-neighbor replacement passes in stable
      vertex-index order and derive H/K from the final principal values.
- [x] Retain deterministic finite diagnostics and fail closed on unsupported or
      non-finite triangle support without changing well-conditioned parity.
- [x] Correct the PMP corpus probe's `use_tensor`/`use_two_ring` argument order.
- [x] Extend the existing runtime curvature transaction to persist canonical
      minimum and maximum principal scalar properties alongside mean/Gaussian,
      including undo/redo capture and truthful result counts.

## Tests

- [x] Replace the one-ring NumPy oracle with values generated by the actual
      Framework24 implementation under `Policy::Sequential`.
- [x] Cover an all-acute closed mesh, an obtuse-face mesh, and an open mesh so
      sign/order, legacy area normalization, two-ring support, smoothing, and
      boundary behavior cannot regress independently.
- [x] Retain deterministic, finite, orientation, analytic-shape, runtime,
      segmentation, remeshing, and publication-coherence coverage with
      expectations updated to the selected contract.
- [x] Run direct Intrinsic/Framework24 parity on `tests/data/sculpt.obj` using
      identical coordinates and record maximum and relative field errors.
- [x] Cover synchronous and queued runtime publication, stale-state rejection,
      undo, and redo for both principal scalar properties.
      (Authored, not yet executed — see the Status note on the blocked build.)

## Docs

- [x] Update the module-interface numerical contract and implementation
      rationale to describe deterministic Framework24 semantics.
- [x] Update `docs/architecture/geometry.md` and the curvature diagnostic docs
      with the selected oracle and coordinate-scale rule.
- [x] Correct the superseded BUG-156 report/ARA records so one-ring accuracy is
      not presented as the shipped contract; record the bounded local
      compatibility observation as explicitly non-claim-eligible.
- [x] Regenerate the module inventory and task session brief required by the
      changed module/task surfaces. No `docs/agent/*` skill mirror changed.

## Acceptance criteria

- [x] On identical well-conditioned coordinates, Intrinsic min/max curvature
      matches sequential Framework24 within the declared floating-point
      tolerance on closed acute, obtuse, and open fixtures.
- [x] The checked-in sculpt asset matches sequential Framework24 over the full
      supported field; discrepancies are not hidden by sign, slot, or scale
      transformations in the acceptance comparison.
- [x] Repeated Intrinsic runs are byte-identical despite Framework24's default
      parallel race.
- [x] Runtime publication remains finite and same-cardinality, with no topology
      or unrelated-property mutation.
- [ ] Runtime persists min/max principal scalars through the same atomic
      transaction, undo/redo, and property-discovery path as mean/Gaussian.
- [ ] Focused geometry/runtime tests, full CPU gate, isolated ASan/UBSan gates,
      and strict structural/documentation checks pass on the final surface.
- [ ] Independent fixed-surface review accepts the high-risk public numerical
      contract and runtime-publication surface.

## Verification

- Local non-claim-eligible actual-Framework24 differential: all three bounded
  fixtures and all 3,669 sculpt vertices passed; the sculpt maxima are
  `6.22e-15` (`kmin`) and `2.78e-15` (`kmax`), with minimum principal-line
  agreement above `0.999999959`. `BENCH-001` must reproduce any claim intended
  to enter the product-convergence scorecard under its clean frozen
  claim-grade protocol.
- Focused final curvature/runtime/segmentation selector: 74/74 passed.
- Final unsanitized CPU selector: 4,256/4,256 passed; the dedicated GLFW LSan
  capability case skipped outside ASan as designed.
- Final UBSan CPU selector: 2,744/2,744 passed; the same capability case
  skipped as designed.
- ASan CPU selector: every BUG-156-owned and other selected case passed, but
  `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` failed with a
  repeatable 408-byte `libX11.so.6` allocation. Three focused reruns reproduced
  it; BUG-118 owns the required diagnosis without weakening the gate.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^(CurvatureTensor\.|Curvature_|CurvatureSegmentation|CurvaturePatch|SandboxEditorUi\.MeshCurvature)' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1

cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```

## Forbidden changes

- Do not approximate Framework24 with a different smoother, area formula,
  sign conversion, or boundary interpolation while calling the result parity.
- Do not normalize or mutate caller geometry silently inside the curvature
  routine.
- Do not copy nondeterministic shared-array parallel writes.
- Do not weaken finite-value diagnostics merely to match undefined behavior on
  malformed geometry.

## Maturity

- Target: `CPUContracted` for the deterministic geometry kernel and complete
  runtime property-publication contract. No `Operational` follow-up is owed for
  this geometry-owned CPU operation; `UI-050` owns vector-field visibility and
  `BENCH-001`/`REVIEW-004` own the product-level evidence gate.
- Current: the geometry kernel and reference-derived regression anchors are
  CPU-contracted. Runtime min/max persistence is implemented with tests but
  unexecuted, so the runtime half of the publication contract is not yet
  `CPUContracted`; that plus the exact-surface high-risk review remain open.
  `BENCH-001` and `REVIEW-004` own any later claim-eligible `ParityProven`
  product verdict.
