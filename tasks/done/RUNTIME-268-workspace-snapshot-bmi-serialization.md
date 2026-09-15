---
id: RUNTIME-268
theme: F
depends_on: [RUNTIME-266]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive continuation; the diff, compiler traces, matched benchmark records and correctness checks own the evidence.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# RUNTIME-268 — Reduce workspace snapshot BMI serialization

## Goal
Identify the declarations and reachable types dominating snapshot module
serialization, then make the smallest beneficial ownership/export change.

## Starting evidence and scope
- Explicit operator-directed continuation of compile-time cleanup. RUNTIME-266
  already removed the complete registry from pointer/reference consumers; keep
  that verified boundary and do not count its work again.
- Its [report](../../ara/evidence/tables/runtime266_registry_borrow_measurement.md)
  retains a diagnostic snapshot trace: WriteAST dominates. A synthetic control
  keeps every direct import and global-fragment include while replacing the own
  declarations with a trivial export; its serialization footprint is much smaller.
  This implicates declaration shape and transitively reachable types, not merely
  source line count or the import list. The synthetic module is not feature-equivalent.
- Start with `src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.cppm`, its
  Models/Public implementation units and their actual consumers. Isolate coherent
  declaration groups before choosing a split, Pimpl or private record owner.
  Preserve canonical records, cached contexts, attachment epochs, visitor-bounded
  borrows, configuration and all UI/agent workflows. No duplicate DTOs or wrappers.
- Clang23 already enables reduced BMIs; both driver output and
  [LLVM documentation](https://clang.llvm.org/docs/StandardCPlusPlusModules.html#reduced-bmi)
  confirm it. Clang20 has a different default. Do not present an already-active
  flag as a new optimization. Full-BMI mode can invoke two compiler jobs: preserve
  each trace separately and measure the entire driver, not an overwritten final
  backend trace. No project-wide BMI mode change is currently justified.
- RUNTIME-267 owns config coupling and GRAPHICS-144 owns renderer changes. Avoid
  concurrent edits to shared session/context files; freeze this task's immediate
  source baseline after any intervening changes.

## Acceptance criteria
- [x] Reproduce the real snapshot trace with exact source, compiler, flags and
      dependency identities; capture outer wall/CPU time, all compiler jobs,
      WriteAST timing and BMI bytes. Separate diagnostic probes from benchmarks.
- [x] Use declaration-group controls to locate the dominant serialized footprint;
      include a control retaining the original imports and global fragment.
      Record what each control removes and why it does not prove feature parity.
- [x] Review the selected smallest change with Claude and existing owners before
      adding a boundary. Preserve all capabilities; reject a split/allocation or
      wider common module that just moves or duplicates serialization work.
- [x] Freeze a matched focused benchmark before timing; use at least five samples
      per arm in a balanced order, retaining slow samples and recording host noise.
      Reuse current tooling and avoid repeating whole clean builds when a focused
      producer/importer experiment answers the question. No stable speedup claim
      from the earlier two-sample noisy cohort.
- [x] Implement/review/test/fix the beneficial change, or record a measured
      no-change verdict. Preserve compiler-boundary guards and run focused/full
      CPU tests; verify new attachment/ownership surfaces with fresh cache-off
      minimum-supported Clang and all affected in-tree consumers.
- [x] Synchronize the canonical editor-boundary documentation and module inventory;
      retire with exact source, trace, benchmark and verification evidence.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'EditorCompilationLocality|SandboxEditor|SceneEditing|RuntimeConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Selected experiment
- Operator-directed compile/reuse continuation on `codex/editor-compile-locality`, starting at `4fa241cc2`. Root is the only writer; Claude reviews fixed source packets.
- Reuse the existing snapshot record owner and its Public implementation unit. Compare isolated declaration groups, then move only proven expensive inline initialization/equality work if that preserves semantics. Keep existing cache, context and attachment contracts.
- New modules, generic facades, duplicate records and runtime allocations are not the default solution. Record diagnostic controls separately from a frozen focused comparison.

## Implemented candidate and review
- Disjoint record controls, empty member initializer removal, equality relocation,
  namespace changes and pointer-only standard type controls do not isolate a
  useful record split. Single-import controls show that the cost grows with the
  imported graph; it is not an irreducible standard-type cost.
- Reuse the original standard entities through one narrow module interface,
  `Extrinsic.Runtime.Private.EditorSnapshotStd`. Its six using-declarations stay
  in an engine namespace and introduce no wrapper, allocation or duplicate type.
  Snapshot scalar headers remain local. Existing snapshot record definitions,
  value initialization, copy/move ownership and attachment guards remain.
- Correct rejected prototypes before measuring: no namespace-std additions; no
  implementation-partition reachability reliance; the CMake public fileset carries
  dependency metadata. Default cache-key comparison now compiles at the existing
  Public implementation owner, avoiding standard-operator namespace pollution.
  No in-tree constant-evaluation consumer of that comparison exists.
- The new contract test checks standard type/function identity, aggregate
  initialization, copied string independence and key comparison. Explicitly import
  the Attachment owner for the newly named test type. Existing lifecycle/cache and
  compiler-boundary tests remain authoritative.
- Charge the new declaration module to the candidate comparison. Time every
  affected runtime producer plus a genuine Sandbox consumer; retain fixed source
  paths, prerequisite BMI hashes, exact source commits and per-command records.
  This is serial focused compilation, not a clean/parallel/full-engine benchmark.

## Verification before the timing cohort
- Canonical `ci` configure and complete `IntrinsicTests` build pass on Clang 23.
  Focused tests: 271 passes. Full CPU: 4,664 passes, no failures and one expected
  ASan-only leak-control skip (4,665 selected). No GPU/sanitizer runtime claim.
- Fresh cache-off Clang 20 compiles the Sandbox editor library and its whole engine
  closure, plus the snapshot model contract object. Its snapshot and standard-owner
  dependency checks preserve the registry/device/spatial-cache exclusions.
- Claude reviewed the plan, discarded prototypes, final source and benchmark
  design. Keep cache equality out of line; constant-expression use is absent in
  current callers, and the comparison is no longer implicitly inline/constexpr.
  No runtime performance claim is made for that trade-off.
- The private-import policy permits only the exact new edge at the exact snapshot
  path and continues scanning after a missing-edge failure. Negative controls
  reject an additional private import and a re-export; exact source restoration
  passes. All original private-import restrictions remain.
- Strict layering, task policy, documentation links, test layout and source
  documentation checks pass. The retained explicit-entity comment explains a
  required selection contract. Module inventory now includes the one new owner.

## Clean-workshop review
| Row | Result | Evidence |
|---|---|---|
| 1: layer imports | pass | Strict layering check; one runtime-local declaration dependency. |
| 2: target links | pass | Same target; no new target-link edge. |
| 3: public types | pass | Exact standard type/function assertions; no new higher-layer type flow. |
| 4: renderer ownership | n/a | No renderer state change. |
| 5: pass identity | n/a | No pass change. |
| 6: recipe dependencies | n/a | No recipe change. |
| 7: maturity | pass | CPUContracted refactor endpoint; no backend capability promotion. |
| 8: exceptions | pass | No layering exception; the exact private declaration edge is guarded. |

## Completion — 2026-09-16
Retired at CPUContracted, the intended compile-locality refactor endpoint.
Commit reference: implementation `ca164c10c`; frozen manifest `01e92cdf6`; this retirement/evidence
commit records the reviewed result. No further backend maturity is implied.

[Report, all ten local samples and raw evidence](../../ara/evidence/tables/runtime268_snapshot_std_measurement.md)
record the final five-sample-per-arm comparison. Including the new owner, snapshot
compilation median changes from 15.183 to 1.646 seconds; the runtime producer
subtotal changes from 42.715 to 29.120 seconds. Consumer ranges overlap; no consumer,
whole-engine or runtime speedup is established. C99 records the bounded observation.
All ten results remain claim_eligible:false. The initial preflight failed before
any compiler invocation and is retained separately; no timed sample was discarded.

Claude's final audit verified arithmetic and found no blockers. Corrected reporting
of medians of per-sample sums, the +21/+5/-3 production-line accounting, and the
separate diagnostic/benchmark BMI identities. RUNTIME-267 and GRAPHICS-144 remain
open with current-source measurement requirements. No second implementation was
added to this slice.
