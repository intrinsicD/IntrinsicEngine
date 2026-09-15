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
- Its [report](../../../ara/evidence/tables/runtime266_registry_borrow_measurement.md)
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
- [ ] Reproduce the real snapshot trace with exact source, compiler, flags and
      dependency identities; capture outer wall/CPU time, all compiler jobs,
      WriteAST timing and BMI bytes. Separate diagnostic probes from benchmarks.
- [ ] Use declaration-group controls to locate the dominant serialized footprint;
      include a control retaining the original imports and global fragment.
      Record what each control removes and why it does not prove feature parity.
- [ ] Review the selected smallest change with Claude and existing owners before
      adding a boundary. Preserve all capabilities; reject a split/allocation or
      wider common module that just moves or duplicates serialization work.
- [ ] Freeze a matched focused benchmark before timing; use at least five samples
      per arm in a balanced order, retaining slow samples and recording host noise.
      Reuse current tooling and avoid repeating whole clean builds when a focused
      producer/importer experiment answers the question. No stable speedup claim
      from the earlier two-sample noisy cohort.
- [ ] Implement/review/test/fix the beneficial change, or record a measured
      no-change verdict. Preserve compiler-boundary guards and run focused/full
      CPU tests; verify new attachment/ownership surfaces with fresh cache-off
      minimum-supported Clang and all affected in-tree consumers.
- [ ] Synchronize the canonical editor-boundary documentation and module inventory;
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
