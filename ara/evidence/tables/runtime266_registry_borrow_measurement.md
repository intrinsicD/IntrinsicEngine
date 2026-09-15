# RUNTIME-266 — Scene-registry borrow result

## Decision

Keep the narrow, verified dependency change. The existing registry remains the
sole storage/lifecycle owner; ten pointer/reference interfaces no longer import
its definition. Snapshot dependency closure is 92 → 91, with only
`Extrinsic.ECS.Scene.Registry` removed. No new production file/module/allocation;
net production change is +29 declaration/comment lines, plus 17 CTest lines.

The local timing samples trend downward, but do **not** establish a stable
2.1% clean / 7.2% snapshot / 6.1% scene speedup. The final before sample is slower
across multiple probes, including the untouched workspace implementation. Two
samples per arm cannot distinguish that variation from the code effect. No
general timing, memory, runtime or GPU claim follows from these measurements.

## Retained matched observations

`build.engine_compile_iteration.registry_borrow.v1`; exact sources
`45d5a4f1fc0c7e51172a0e4a0edbca2f675448ea` →
`08728e2e1e05c1881d4ff82b36f1022fd9fbbb2b`. ABBA, two samples per arm;
Clang23 Debug ci, Null/headless ExtrinsicRuntime dependency closure, four jobs,
caches/launchers off, identical preinstalled dependency fingerprints and all 776
compiler command lines. No competing builds/tests during sampling. Source bytes
are clean exact commits; probes touch mtimes only. Input pre-read, no discarded
pilots, fresh owned tmpfs build each sample; no isolated-machine/cold-filesystem
claim. Tests/Sandbox are outside timing. All four canonical result files retain
`claim_eligible: false`.

Values below are all observed seconds; medians of two are their midpoints.

| Scenario | Before samples | After samples | Compiler units |
|---|---:|---:|---:|
| clean | 334.647 / 346.917 | 332.952 / 334.097 | 776 → 776 |
| noop | 0.075 / 0.079 | 0.073 / 0.072 | 0 → 0 |
| workspace_impl | 7.265 / 8.145 | 7.263 / 7.302 | 1 → 1 |
| snapshot_interface | 26.306 / 29.077 | 25.441 / 25.929 | 5 → 5 |
| scene_interface | 36.450 / 39.972 | 35.622 / 36.171 | 10 → 10 |

All counts are unchanged: this removes dependency reachability, not compiler
units from these selected rebuilds. Raw CPU time, weighted Ninja paths and
maximum single-process RSS are retained in the summary/results. RSS is neither
aggregate concurrent memory nor tmpfs storage. A no-op percentage at this scale
has no useful performance interpretation.

## Diagnostic: the remaining snapshot cost

After the timed cohort, one isolated compile of the verified snapshot interface
used the existing root dependency BMIs and separate output object/PCM. Its
Clang trace records:

| Trace total | Seconds |
|---|---:|
| ExecuteCompiler | 16.690 |
| WriteAST | 14.727 |
| Frontend | 1.924 |
| ReadAST | 0.028 |

These trace categories are not additive and include instrumentation overhead.
The emitted BMI is 28,482,868 bytes. This identifies serialization as the
dominant phase in that diagnostic; it is not a before/after performance result.

A synthetic control retains the same global fragment/includes and every direct
import, but replaces the snapshot declarations with one trivial export and a
distinct diagnostic module name. It records 1.678 seconds outer wall time,
1.636 seconds ExecuteCompiler and a 1,509,248-byte BMI. It is **not feature
equivalent**. The control implicates the declaration shape and transitively
reachable types; it does not identify one guilty record or prove that importing
is free. WriteAST may itself trigger work on imported declarations.

Clang23 already enables reduced BMIs, confirmed by the actual driver command and
[LLVM documentation](https://clang.llvm.org/docs/StandardCPlusPlusModules.html#reduced-bmi).
An exploratory full-BMI driver run used two compiler jobs; its shared trace path
was overwritten by the final backend job. That 0.309-second trace is excluded
from whole-compilation comparisons. No compiler-mode setting was changed.

RUNTIME-268 owns declaration-group isolation and a controlled serialization
experiment before any further split/Pimpl/export change. RUNTIME-267 retains
config-chain ownership; GRAPHICS-144 retains renderer ownership. This task does
not declare the wider editor compile problem finished.

## Verification and review

- Canonical ci configure and complete IntrinsicTests rebuild pass on Clang23.
- 408 focused passes; full CPU suite has 4,663 passes, zero failures and one
  expected ASan-only leak-control skip (4,664 selected, 141.31 seconds).
- All eleven proposed boundary checks reject original compiler metadata and
  pass on final metadata; all existing compilation guards pass.
- Fresh cache-off Clang20 compiles the Sandbox editor library and complete engine
  prerequisites; all eleven dependency checks pass there. No Clang20 test or GPU
  execution claim. Test configuration is enabled only to expose the headless app
  library; two earlier unavailable target requests compiled no sources.
- Claude reviewed the plan, fixed code diff, results and diagnostic interpretation.
  Corrected line accounting (+29 production, +17 test), avoided causal RSS and
  stable-speedup claims, and excluded the overwritten full-BMI trace.
- Task, documentation links, manifests, layering and source inventory checks
  pass; inventory regeneration changes no module names.

## Evidence

[Manifest](../../../benchmarks/ci/manifests/engine_compile_iteration_registry_borrow.yaml) ·
[All samples and producers](../diagnostics/runtime266_registry_borrows/summary.json) ·
[Source/accounting index](../diagnostics/runtime266_registry_borrows/evidence-index.json) ·
[Verification](../diagnostics/runtime266_registry_borrows/verification.json) ·
[Trace summary](../diagnostics/runtime266_registry_borrows/snapshot-trace-summary.json) ·
[Synthetic control](../diagnostics/runtime266_registry_borrows/snapshot-imports-trace-summary.json) ·
[Raw archive](../diagnostics/runtime266_registry_borrows/raw-evidence.tar.gz).
