# GRAPHICS-144 — Shared standard declarations for renderer and snapshots

## Decision and scope

Keep the shared declaration owner and narrower renderer GLM include. In this local
five-sample-per-arm comparison, median **clean graphics-library build time changes
from 79.403 to 71.319 seconds
(10.2% lower)**. A settled renderer-interface edit
changes from **24.523 to 14.071 seconds
(42.6% lower)**, including its implementation consumer.
Implementation-only rebuild ranges overlap; no implementation speedup is established.
The snapshot interface is also unchanged within overlapping ranges; compiling its
expanded owner in isolation adds a small cost, retained below.

These are compilation observations on this host, not whole-engine build, engine
runtime, GPU or cross-host results. No statistical significance or general speedup
is asserted. All twenty canonical records remain `claim_eligible: false`; C100 records only
this bounded observation.

Exact source commits `dc606482aecec0f0412b06b7a811a62835e98438` →
`5537a4dda505959e6e818713963ce43ad7805c90`. The implementation was fully verified
before measurement. Manifest checkpoints are `7061e4e8a` and `70fb1222c`; the final
premeasurement freeze binds both manifests and runners. Earlier protocol refinements
are retained; no timed sample was discarded, repaired or overwritten.

## Matched graphics-library comparison

`build.engine_compile_iteration.graphics_std.v1`, Clang23 Debug, ci-derived
Null/headless configuration, target `ExtrinsicGraphics`, four compiler jobs,
cache/launchers disabled. Ten fresh tmpfs builds use one fixed detached source/build
path in ABBAABBAAB order. Every sample runs clean, settled no-op, implementation
mtime edit and interface mtime edit; source bytes remain at the exact clean commit.
Configure is recorded separately, outside the table's build wall times.

Seconds: median [minimum–maximum]. Every clean candidate build pays the new Core.Std
producer; settled edit probes reuse it. Build walls include scanning and linking.

| Scenario | Before | After |
|---|---:|---:|
| Clean graphics library | 79.403 [79.140–79.622] | 71.319 [71.089–74.158] |
| Renderer interface edit + consumer | 24.523 [24.336–25.229] | 14.071 [13.894–15.648] |
| Renderer implementation edit | 12.938 [12.782–13.280] | 12.929 [12.853–14.344] |
| Settled no-op | 0.033 [0.032–0.042] | 0.035 [0.032–0.037] |

Compiler invocations: clean 267 → 268, interface edits 2 → 2, implementation edits
1 → 1, no-op 0 → 0, constant across samples. The clean addition is Core.Std; the
old runtime-local owner lies outside this graphics target. The interface/implementation
source sets are identical, so the improvement does not come from omitting consumers.
776 common configured compile commands match byte-for-byte across all
samples; the only added/removed source entries are the moved declaration owner.
The same dependency-package fingerprint holds before, during and after every sample.

| Attempt | Clean | Implementation edit | Interface edit |
|---|---:|---:|---:|
| 01-before-1 | 79.403 | 12.938 | 24.336 |
| 02-after-1 | 71.117 | 12.886 | 13.894 |
| 03-after-2 | 71.089 | 12.853 | 14.091 |
| 04-before-2 | 79.140 | 12.782 | 25.229 |
| 05-before-3 | 79.622 | 12.951 | 24.523 |
| 06-after-3 | 71.319 | 13.080 | 14.071 |
| 07-after-4 | 71.442 | 12.929 | 13.978 |
| 08-before-4 | 79.234 | 12.795 | 24.481 |
| 09-before-5 | 79.442 | 13.280 | 24.540 |
| 10-after-5 | 74.158 | 14.344 | 15.648 |

The retained final candidate (10-after-5) has the largest after-arm clean,
implementation-edit and interface-edit times. It is not the no-op maximum.
Its cause was not isolated; the reported five-sample medians include it.

Ninja invocation windows and dependency-DAG weighted critical paths, per-producer
walls, user/system CPU and maximum single-process RSS are retained in the raw
records and summary. Critical-path durations are not aggregate compile time;
maximum process RSS is not the sum of concurrently running compilers. No memory
improvement is claimed.

## Snapshot cost control

`build.engine_compile_iteration.snapshot_shared_std.v1` replays two serial producer
commands per sample: declaration owner, then snapshot. Both arms use the original
runtime-owner compiler flags to isolate declaration cost; actual Core-target flags
remain captured and are used naturally in the separate graphics-build cohort.
The snapshot command is byte-identical. These ci prerequisite flags have Vulkan/GLFW
configured, unlike the Null/headless graphics cohort; there is no GPU execution.

Ninety-one frozen prerequisite BMIs have source bytes identical in both commits;
compiler-inspected import metadata verifies that none imports either standard owner,
the renderer or the snapshot. Every sample rebuilds its owner/snapshot and uses those
fresh BMIs. All frozen BMI hashes, 831 recorded input-file hashes, exact worktree
sources and dependency-package fingerprints are checked before/after each sample.
The root source and shared inputs remain unchanged during sampling.

| Producer or pair | Before | After |
|---|---:|---:|
| Standard declaration owner | 0.966 [0.957–0.991] | 1.029 [1.027–1.031] |
| Snapshot interface | 0.723 [0.722–0.734] | 0.724 [0.721–0.733] |
| Serial owner + snapshot pair | 1.690 [1.680–1.725] | 1.753 [1.748–1.762] |

The pair row is the median of each sample's summed walls, not the sum of medians.
In a graphics-plus-runtime build Core.Std would already be built; do not add its
cost twice or sum these two independent benchmark cohorts. This control is not a
whole-runtime measurement. All samples and per-command RSS remain in the summary.

## Source change and alternatives

Move the existing 21-line runtime declaration owner to the 26-line Core.Std module,
adding only renderer-required function/span/unique_ptr declarations. Both measured
consumers name the original standard types through ordinary using-declarations in
an engine namespace. No replacement containers, wrappers, allocation, forwarding,
new target-link edge or compatibility path; helper names are not re-exported by
these consumers. Required renderer subsystem imports and all implementation state
remain. The former snapshot-specific private-import exception is removed.

Source accounting: **+6 production C++ lines**, no net source-file/module addition,
419 modules total. CMake has one added and one removed fileset entry. This reduces
repeated compiler work; it is not a source-line reduction claim. A separate graphics
copy would duplicate the existing owner. Pimpl or record splits would change more
ownership without directly addressing the measured serialization interaction.

The renderer needs only the visible vec3 name from `<glm/fwd.hpp>`. Its required
SpatialDebugVisualizers import exports SpatialDebugAabb with by-value vec3 members,
which makes the complete definition reachable. An explicit `sizeof(glm::vec3)`
assertion precedes the spans. A forward declaration alone would be insufficient:
[span requires a complete element type](https://eel.is/c++draft/span.overview), and
[reachable declarations carry completeness](https://eel.is/c++draft/module.reach).
Concrete implementation files keep their own full headers. Both minimum-supported
Clang20 and current Clang23 compile the final assertion and real consumers.

Single trace controls below are diagnostics, not matched performance comparisons.
Every control keeps the required renderer imports and data/API shape. The GLM-only
control is a temporary copy of the exact baseline with only its include narrowed;
the standard-only prototype retains the full GLM include. The final assertion is
included only in the final combined diagnostic and verified implementation.

| Renderer source variant | Emitted BMI bytes | WriteAST seconds |
|---|---:|---:|
| Exact baseline | 30,132,708 | 9.513907 |
| Shared standard owner only | 15,069,644 | 5.798290 |
| GLM forward include only | 25,100,172 | 7.318660 |
| Final combined change | 807,440 | 0.046537 |

The new owner independently emits 10,729,460 bytes in its single diagnostic;
the controlled snapshot replay emits 10,729,716 bytes. These are separate
artifacts with different captured flags and source/output paths, not one
byte-identical artifact.
Renderer-only BMI reduction does not include this producer or the whole prerequisite
cache. The matched clean target charges it. Trace categories overlap and cannot be
summed. Source hashes, exact commands/maps, outer wall, phase totals, BMI sizes and
all controls are retained. No project-wide compiler flag or BMI-mode change shipped.

## Verification, review and remaining work

Canonical ci configure and complete IntrinsicTests build passed. Focused CPU:
444 passed (35.19 seconds). Full CPU: 4,664 passed, zero failures, one expected
ASan-only GLFW leak-control skip out of 4,665 selected (141.22 seconds). Existing
material, UV, extraction, frame-hook, snapshot and compiler-boundary tests remain.
New static assertions verify exact renderer factory, hook and span standard types;
existing snapshot contracts verify aggregates, standard types and value semantics.

A fresh cache-off Clang20 Null/headless Sandbox editor closure passed. Final
reconciliation includes the completeness assertion and renderer lifecycle/snapshot
model test objects. This is Clang20 build evidence, not Clang20 test execution.
There is no GPU or sanitizer execution claim. No runtime/GPU lifetime code changed.
Strict layering/task/test-layout/root/documentation checks pass; inventory is current.
Source-documentation audit has zero objective errors; 23 review flags were inspected,
retaining existing lifecycle and ownership comments outside this slice.

Claude reviewed the plan, fixed source, completeness reasoning, measurement protocol
and final results. Resolutions include explicit complete-type proof, common compiler
flags, immutable prerequisite input/source checks, safe source/output rewriting,
full source IDs, separate owner costs and bounded claims. Hypothetical objections
contradicted by captured command/metadata evidence are documented as such.

GRAPHICS-144 closes at CPUContracted. The renderer implementation remains a compile
cost; this slice establishes no improvement there. A separate final-source trace
records 14.489 seconds ExecuteCompiler, 13.554 Frontend and 0.906 Backend;
NullRenderer class parsing occupies 6.374 seconds and constraint checking totals
5.495 seconds. These nested phase values overlap and must not be summed.
GRAPHICS-145 owns testing smaller implementation/template changes against those
observations; the trace is not a speedup result. RUNTIME-267 stays open: its
by-value config/results and embedded prepared frame need an ownership decision;
replacing the panel frame with references to construction temporaries is invalid.
No session facade was added. UI-037, GRAPHICS-105, LEGACY-043 and BUILD-006 retain
their independently owned work and prerequisites.

## Reproduction and evidence

Inputs are pre-read, with no cache flushing or CPU affinity/governor/turbo control.
Normal desktop noise remains; no competing build/test jobs ran during timing.
No cold-filesystem, publication-grade or stable future-speedup claim is made.

The canonical graphics runner is unchanged. The archived snapshot replay reuses
canonical fingerprint/sealing/validation helpers. Recreate the exact source commits,
recorded toolchain and dependency versions; for the focused replay, rebuild and
freeze the unchanged prerequisite closure and input hashes before invoking the
saved runner. Absolute paths describe this host; prebuilt BMI/object binaries are
not checked in. All 20 canonical results, full raw commands/logs/Ninja graphs, source
hashes, diagnostic controls, runner scripts and fixed Claude packets are archived.

[Graphics manifest](../../../benchmarks/ci/manifests/graphics_standard_declarations.yaml) ·
[Snapshot manifest](../../../benchmarks/ci/manifests/snapshot_shared_std.yaml) ·
[All samples and recalculation](../diagnostics/graphics144_shared_std/summary.json) ·
[Verification](../diagnostics/graphics144_shared_std/verification.json) ·
[Source/protocol/archive index](../diagnostics/graphics144_shared_std/evidence-index.json) ·
[Claude results review](../diagnostics/graphics144_shared_std/results-review.txt) ·
[Raw evidence](../diagnostics/graphics144_shared_std/raw-evidence.tar.gz).
