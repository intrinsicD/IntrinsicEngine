---
id: GEOM-099
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog planning; implementation evidence is the diff, tests and matched compile measurements
contract_schema: 1
contracts: [repo.source-documentation]
---
# GEOM-099 — Share parameterization disk-topology preflight

## Goal

Replace three copies of the connected-manifold/Euler-characteristic check with
one compiled geometry function, retaining each solver's existing eligibility
and diagnostics without increasing compile time.

## Evidence and reuse decision

At `7bdffefb3`, private `HasDiskTopology` bodies repeat vertex traversal,
deleted-slot handling, isolated/nonmanifold rejection and `V - E + F == 1` in:

- `src/geometry/Geometry.Parameterization.Bff.cpp:81`.
- `src/geometry/Geometry.Parameterization.Harmonic.cpp:107`.
- `src/geometry/Geometry.HalfedgeMesh.Parameterization.cpp:156`.

Extend `Geometry.HalfedgeMesh.Utils.cpp/.cppm`, which already owns
`MeshUtils::CollectBoundaryLoops`, with a compiled predicate and narrow
declaration named for connected/manifold/Euler-one validation. BFF and LSCM
already import Utils; add its import only to Harmonic's implementation.
`Geometry.HalfedgeMesh.Boundary` instead owns mutating feature marking and is
not the shared owner. `MeshQuality::EulerCharacteristic` supplies statistics
but no traversal; `MeshRepair::ComputeConnectedComponents` allocates labels
that these callers do not need. Neither replaces the common predicate. Do not merge solver preparation, boundary extraction,
status enums or numerical kernels. The common function takes a valid live seed;
callers retain their empty-boundary and exactly-one-loop checks. Keep the seed
precondition explicit: connectivity plus Euler characteristic alone is not a
complete arbitrary-mesh disk validator.

This does not duplicate GEOM-070, which owns LSQR and replacement of LSCM's
private sparse builders. Both touch the parameterization file, so rebase and
re-review combined changes if either lands first; neither is a prerequisite.
Operator direction on 2026-09-21 authorizes this behavior-preserving cleanup
outside the paused research work-selection track. No method formulation,
backend or engine integration changes.

## Acceptance criteria

- [ ] Use one compiled predicate for all three callers; delete the duplicate
      traversals. Preserve traversal/handle bounds assumptions, live counts and
      sparse/deleted slots. Do not allocate converted boundary arrays just to call it.
- [ ] Preserve each caller's validation order, triangle requirement, boundary-loop
      checks, optional/status failure representation, pin validation and UV output.
      No runtime/ECS publication or config/UI changes.
- [ ] Keep or add meaningful public-entry regressions for a valid disk, closed mesh,
      multiple loops, disconnected components, isolated/nonmanifold vertices,
      deleted slots and a punctured genus-one mesh. Cover BFF, Harmonic/Tutte and
      LSCM/dispatch, retaining each solver's existing numeric checks.
- [ ] Before extraction, close the public-entry topology coverage gaps: BFF's
      `BoundaryFirstFlattening.InvalidTopologyAndGeometryFailClosed` currently
      covers empty/closed/quad input, but needs punctured genus, disconnected and
      nonmanifold cases. Harmonic already has `PuncturedGenusOneMeshIsNotDiskTopology`;
      add disconnected/nonmanifold cases in `Test.HarmonicParameterization.cpp`.
      LSCM is covered for genus and disconnected dispatch by
      `ParameterizationDispatch.InvalidInputsFailClosedThroughStatus` and
      `RejectionCarriesConnectedComponentAndBoundaryLoopCounts`; add direct LSCM
      disconnected/nonmanifold checks in `Test_Parameterization.cpp`. Preserve
      BFF's boundary-count-before-predicate ordering and the valid-live-seed
      precondition. Keep this separate from numeric algorithm changes.
- [ ] Keep solver-heavy imports out of the shared declaration. Update the chosen
      owner's synopsis/contracts, geometry documentation if ownership changes,
      and generated module inventory for the Utils public surface change. Add a
      concise canonical reuse owner-route row and synchronize skill mirrors.
      Measure the Utils interface-edit fan-out in both arms; its many importers
      are a continuing iteration cost, not exempt from the regression gate. The added Harmonic importer
      rebuilds another sparse-solver implementation and may exceed the frozen
      2% compiler-duration limit; if it does, reject this extraction rather
      than relaxing the limit after measurement.

## Size and compilation acceptance

- [ ] Record before/after physical lines for the complete affected implementation,
      helper, declaration, caller and CMake set, including newly added files.
      Require a net reduction; report added regression tests separately and also
      report the total diff. Moving bodies or compressing formatting is insufficient.
- [ ] Reuse `tools/analysis/benchmark_compile_iteration.py` with a task-specific
      manifest `benchmarks/ci/manifests/geom099_reuse_compile.yaml`, stable ID
      `build.reuse.geom099.v1`, exact clean before/after revisions, identical
      dependencies, Clang >=20, ci-derived Null/headless preset, disabled ccache,
      fixed jobs and at least three alternating samples per arm. Use the focused
      target below with tests enabled. Measure clean,
      no-op and the following edit probes: each solver implementation and the common owner declaration/module interface.
      Record wall time, compiler-duration sum and observed rebuild fan-out.
      Stop if the clean-build median regresses by more than 2%; for no-op
      and edit probes allow at most max(2% of the before median, 100 ms).
      Apply the same relative limit to compiler-duration sums (zero-work
      no-op must stay zero); do not relax limits after viewing results. Preserve all raw runs; inconclusive evidence
      keeps this task open. On a confirmed gate failure, restore only this
      task's implementation changes, retain the raw runs and a negative decision
      record, and retire explicitly as rejected, not implemented. Do not discard
      unrelated work. Do not infer compile speed from line count.
- [ ] Run focused tests and the default CPU gate; preserve test names/labels and
      existing assertions. Validate the measured manifest/results. Any repeatable
      performance claim follows AGENTS.md §8/§8b; this task asserts no speedup.

## Verification

The compile manifest and disposable source worktree are implementation deliverables;
create the worktree from the measured revision before invoking the runner. Keep
compiler timing separate from concurrent builds/tests.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'BoundaryFirstFlattening|HarmonicParameterization|ParameterizationDispatch|LSCM' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/analysis/benchmark_compile_iteration.py --source /tmp/intrinsic-geom099/source --build /tmp/intrinsic-geom099/build --output /tmp/intrinsic-geom099/results --manifest benchmarks/ci/manifests/geom099_reuse_compile.yaml
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/intrinsic-geom099/results/results --manifests-root benchmarks --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/sync_skills.py --write
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Planning review

Codex and Claude Code reached consensus on 2026-09-21 before this task was
filed. The [shared planning review](../../evidence/GEOM-099/claude-planning-review.md)
records the agreed scope, corrections, rejected job-lifecycle candidate and
compile-regression stop rules. Implementation and timing evidence remain due.

## Execution plan and operator amendment — 2026-09-21

The operator explicitly superseded the automatic 2% compile-rejection rule for
this task before after-arm measurements: accept the shared owner when it makes
the three callers more consistent and the code smaller/clearer; still measure
and report the compile cost. The remaining three tasks retain their frozen
limits. Original acceptance wording above is retained as planning history;
this amendment governs GEOM-099's compile disposition.

Use `MeshUtils::IsConnectedManifoldWithEulerOne(mesh, liveSeed)` in the existing
Utils implementation, retaining boundary validation and solver-specific order
at each caller. Add public-entry characterization before extraction, including
a closed component plus a disk (only one boundary), bowtie, isolated vertex,
deleted slots, and the missing BFF genus/multiple-loop cases. Test fixtures use
the existing compiled MeshBuilders owner. Review the fixed diff independently.
Measure identical characterization tests in both final clean source arms with
Clang 23, ci-derived Null/headless Debug, ccache disabled, four jobs, three
alternating samples per arm; retain the original pre-edit pilot separately.
