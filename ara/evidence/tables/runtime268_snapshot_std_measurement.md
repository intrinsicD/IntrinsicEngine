# RUNTIME-268 — Shared standard declarations for workspace snapshots

## Decision and measured scope

Keep the narrow declaration-owner change. In this retained local five-sample-per-arm
comparison, the median snapshot-plus-owner compilation changes from **15.183
to 1.646 seconds (89.2% lower)**. The affected runtime producer
subtotal changes from **42.715 to 29.120 seconds**.
The real Sandbox consumer's ranges overlap; its small median difference establishes
no consumer improvement or regression. These are focused serial compile-command
observations, not whole-engine, parallel-build, runtime or cross-host results.
All ten canonical records remain `claim_eligible: false`; C99 records only this
bounded local observation.

`build.workspace_snapshot_std.v1`, exact sources `4fa241cc2` → `ca164c10c`;
manifest committed at `01e92cdf6`. ABBAABBAAB, five samples per arm, Clang23 Debug
canonical ci flags, one compiler at a time, fixed detached source/output paths,
cache/launchers/tracing off. The candidate pays for the added standard-owner
compilation before compiling the same six files. Configure, scanning, linking,
tests and all other compilation are excluded. Runtime plus consumer totals are
sums of per-command walls; outer instrumentation time is separate.

All six common commands match byte-for-byte before path projection. The after maps
are captured from actual CMake output and differ only by the added standard owner.
232 frozen prerequisite BMI hashes and the 1,728-file package fingerprint are
checked before/after every sample. Compiler-inspected module metadata verifies
that none of the frozen prerequisites (other than the unused old snapshot copy)
imports the snapshot, including transitively. Every sampled consumer uses the
newly emitted snapshot BMI and, in the candidate, the newly emitted standard BMI.

Prerequisites originate from ci with Vulkan/GLFW configured, distinct from earlier
Null/headless timed cohorts; there is no GPU execution. Inputs are pre-read, but
normal desktop filesystem/compiler first-use caches, CPU affinity, governor and
turbo are uncontrolled. No competing builds/tests ran during sampling. Every
measured sample is retained, including the slow first baseline. No statistical
significance, publication-grade eligibility or stable future speedup is asserted.

## Timing observations

Seconds, median [minimum–maximum]. The combined snapshot row charges the new owner;
the primary-only row is shown for attribution, not as the headline saving.
Subtotal rows are medians of each sample's summed walls, not sums of the
per-command medians.

| Producer or subtotal | Before | After |
|---|---:|---:|
| Snapshot + added standard owner | 15.183 [15.060–15.748] | 1.646 [1.637–1.710] |
| Snapshot primary only | 15.183 [15.060–15.748] | 0.706 [0.702–0.739] |
| Snapshot Public implementation | 2.905 [2.888–2.999] | 2.930 [2.899–3.013] |
| Snapshot Models implementation | 6.912 [6.831–7.118] | 6.863 [6.834–7.095] |
| Context adapters | 8.777 [8.750–9.112] | 8.723 [8.669–8.890] |
| Workspace session | 9.019 [8.964–9.312] | 8.981 [8.968–8.998] |
| Sandbox PanelSupport consumer | 9.080 [9.047–9.396] | 9.158 [9.087–9.384] |
| Runtime subtotal, including new owner | 42.715 [42.593–44.288] | 29.120 [29.070–29.706] |
| Runtime + consumer sum | 51.763 [51.647–53.684] | 38.316 [38.206–38.900] |

The candidate's standard owner alone is 0.940 [0.935–0.970] seconds.

| Attempt | Snapshot + owner | Runtime subtotal | Sandbox consumer |
|---|---:|---:|---:|
| 01-before-1 | 15.748 | 44.288 | 9.396 |
| 02-after-1 | 1.710 | 29.706 | 9.194 |
| 03-after-2 | 1.646 | 29.120 | 9.087 |
| 04-before-2 | 15.183 | 42.715 | 9.047 |
| 05-before-3 | 15.060 | 42.593 | 9.054 |
| 06-after-3 | 1.637 | 29.070 | 9.384 |
| 07-after-4 | 1.641 | 29.158 | 9.158 |
| 08-before-4 | 15.138 | 42.639 | 9.080 |
| 09-before-5 | 15.498 | 43.863 | 9.145 |
| 10-after-5 | 1.655 | 29.106 | 9.134 |

Changed BMI storage totals 28,482,868 → 10,178,676 bytes (snapshot versus snapshot **plus** new owner; constant within each arm). This is emitted storage for these changed modules, not the whole prerequisite cache, peak process memory or engine runtime memory. Per-command maximum single-process RSS and user/system CPU records are retained in the results.

## What changed

Keep the existing snapshot records and value ownership. A 21-line runtime-local
module compiles the five standard-library headers independently of the editor
import graph and exposes six using-declarations in an engine namespace. The
snapshot interface imports those declarations; they name the original standard
types. There are no replacement containers, duplicate records, constructors,
Pimpl allocations, compatibility wrappers or new target-link edges.

Cache-key equality is defaulted out of line in the existing Public implementation
unit, where the ordinary headers make its standard comparison operators visible.
It is no longer implicitly inline/constexpr; no in-tree constant-expression user
exists. A runtime cost from that change has not been measured. The new module's
CMake fileset is public for BMI metadata propagation; its C++ helper names are
not re-exported through the snapshot interface. The source import policy checks
that exact edge and rejects re-exporting it or adding an arbitrary private edge.

Source accounting: the new file has 21 lines; the existing Public implementation
adds five and the primary interface loses three, for **+23 C++ production lines**
across one added module/file. There is also one
CMake source-list entry (**+24 under src/**); 419 module inventory entries. This
reduces repeated compiler work rather than production line count. Splitting the
records or changing their ownership would add more engine complexity without
addressing the isolated mechanism.

## Why this boundary

The preserved exact-original-source diagnostic (`duplicate-declarations`) records
15.131 seconds outer wall, 15.062 ExecuteCompiler, 13.333 WriteAST and a
28,482,752-byte BMI. It enables the duplication-warning flag, emits no warnings,
and has one compiler job. Its separate source/output paths and extra warning flag
differ from the benchmark command; the diagnostic BMI is not byte-identical to the
benchmark baseline (28,482,868 bytes). Trace categories overlap; do not sum them.
A control retaining every direct import and global-fragment include, but replacing
own declarations with a trivial export, takes 1.477 seconds and emits 1,510,328
bytes. This is an isolation control, not a feature-equivalent implementation.

Every disjoint record group remains expensive (roughly 15.1–15.5 seconds in these
single diagnostic runs). Removing empty member initializers, moving equality
alone, changing namespaces and using a pointer to string do not remove that cost.
A transform-only record is cheap; mentioning string/vector with the whole import
graph is expensive. The same string with no engine imports takes 1.022 seconds;
single-import controls vary substantially. This rejects the initial explanation
of an irreducible standard-type cost: the declaration/import interaction matters.
Debugger samples land in ASTWriter references and unresolved lookup serialization;
bitcode statistics put 85.6% of the original BMI in DECLTYPES.

A separate standard-header declaration owner resolves the measured interaction.
Retaining those same headers textually in the primary interface as well as
importing the owner remains expensive, so a shared textual prelude is insufficient.
This is consistent with [Clang's module guidance on reducing duplicated header declarations](https://clang.llvm.org/docs/StandardCPlusPlusModules.html#reduce-duplications).
No compiler-wide BMI flag changes: reduced BMIs were already enabled on Clang23.

Rejected prototypes added declarations to namespace std or relied on optional
implementation-partition reachability; neither shipped. Their timings are excluded
from the final comparison. The final independent module interface uses ordinary
using-declarations in SnapshotStd, and fresh Clang20 compilation verifies the
minimum-supported compiler and affected consumer graph.

## Verification and review

Canonical ci configure and complete IntrinsicTests build pass on Clang23.
The full CPU gate selects 4,665 tests: **4,664 pass, zero fail, one expected
ASan-only leak-control skip** (153.51 seconds). The earlier focused gate has
271 passes; final import-policy refinements are included in the full CPU run.
The added contract test checks aggregate initialization, exact standard types and
factory signature, copied string independence and cache-key comparisons.

A fresh cache-off Clang20 Null/headless build compiles the Sandbox editor and
its engine closure, plus the snapshot model contract object. Snapshot/standard-owner
compiler dependency checks retain the registry/device/spatial-cache exclusions.
This is Clang20 compilation evidence, not Clang20 test execution. There is no
GPU or sanitizer execution claim. Negative source controls make the import-policy
test fail for an extra private import and a re-export; restoring the exact source
and timestamps passes it. No production source changed after these checks.

Claude reviewed the plan, diagnostic interpretation, discarded prototypes, fixed
source, import policy, timing runner and final results. Review corrections include
minimum-compiler/reachability verification, out-of-line equality, the precise import
exception, real after-command validation, frozen-BMI transitive-import checks,
output safety, sample accounting and narrowly scoped claims. Early review hypotheses
that diagnostics disproved are retained as superseded, not supporting conclusions.

## Reproduction and custody

The task-specific runner is archived with its SHA-256 and frozen protocol; it
reuses the canonical dependency-fingerprint, result-sealing and validation helpers.
To reproduce, recreate both exact commits and canonical ci prerequisites with the
recorded compiler/dependencies, capture the six common commands and after-owner
command/maps, freeze their BMI closure, validate that none of those prerequisites
imports the snapshot, and replay the recorded protocol in owned output paths.
Absolute scratch/build paths in archived commands describe this run; the archive
is not a portable prebuilt SDK. Prerequisite binaries are deliberately not checked
in; identities and the checked import graph are retained.

The initial runner preflight rejected CMake's valid `-x c++-module` response-file
line before any compiler invocation. Its directory, log, runner and original
protocol are retained separately. The correction admits only that exact line for
module-interface producers; the runner hash/protocol was frozen again before the
ten successful samples. No timed sample was discarded or repaired.

An earlier debugger probe reused the initial `real` diagnostic trace path. The
replacement is explicitly named `real.debugger.trace.json`; the original summary
is not paired with it. The independent `duplicate-declarations` trace/summary is
the report's full-source phase evidence. All diagnostic controls, failures and
rejected prototypes remain separate from the matched benchmark population.

RUNTIME-268 closes at CPUContracted, the intended refactor endpoint. RUNTIME-267
still owns configuration dependencies and GRAPHICS-144 the renderer. The renderer
may have a similar declaration-serialization issue, but that requires its own
trace, layer-correct reuse design and matched measurements before adoption.

## Evidence

[Manifest](../../../benchmarks/ci/manifests/workspace_snapshot_std.yaml) ·
[All samples, ranges and recalculation](../diagnostics/runtime268_snapshot_std/summary.json) ·
[Source/protocol/archive index](../diagnostics/runtime268_snapshot_std/evidence-index.json) ·
[Verification](../diagnostics/runtime268_snapshot_std/verification.json) ·
[Claude results review](../diagnostics/runtime268_snapshot_std/results-review.txt) ·
[Raw traces, commands, controls, logs and scripts](../diagnostics/runtime268_snapshot_std/raw-evidence.tar.gz).
