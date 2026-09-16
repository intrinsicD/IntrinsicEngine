# GRAPHICS-145 — Renderer implementation frontend work

Keep the bounded standard-library call changes. On this host, the median settled
renderer implementation rebuild, including the real graphics-library archive link,
changes **12.896 → 9.474 seconds (26.5% lower)** over five samples per arm. Every
after rebuild sample is below every before rebuild sample. This is a local warm-input incremental
build observation, not a clean-build, whole-engine, importer-fanout, runtime, GPU,
cross-host or statistical claim. All ten records remain `claim_eligible: false`.
The separate GRAPHICS-144 clean/interface improvements are not counted again.

## Matched comparison

Exact clean source commits `07a872ac9d87fe6e1491d12f38906539779b74b4` →
`51bdc9b6562e831707ce09fb1dbea9940a5d709c`; manifest checkpoint `f73dcbd39`.
Benchmark `build.engine_compile_iteration.renderer_frontend.v1`: Clang23 Debug,
ci-derived Null/headless, `ExtrinsicGraphics`, four build jobs, compiler caches and
launchers disabled. One untimed baseline target build prepares prerequisites;
ABBAABBAAB samples (A = before, B = after) then touch only the implementation source and rebuild the target,
followed by a no-op. Same source/build path throughout. No sample discarded.

Seconds: median [minimum–maximum]. Target walls include scanning and archive linking;
producer durations come from the same invocation's Ninja window, not a separate replay.

| Scenario | Before | After |
|---|---:|---:|
| Renderer implementation edit + graphics archive | 12.896 [12.793–12.968] | 9.474 [9.441–9.616] |
| Renderer compiler within that rebuild | 12.753 [12.647–12.820] | 9.330 [9.293–9.464] |
| Settled no-op | 0.032 [0.031–0.033] | 0.034 [0.032–0.037] |

| Attempt | Target wall | Renderer compiler | No-op |
|---|---:|---:|---:|
| 01-before-1 | 12.930 | 12.785 | 0.032 |
| 02-after-1 | 9.528 | 9.373 | 0.034 |
| 03-after-2 | 9.464 | 9.320 | 0.032 |
| 04-before-2 | 12.968 | 12.820 | 0.032 |
| 05-before-3 | 12.793 | 12.647 | 0.031 |
| 06-after-3 | 9.441 | 9.293 | 0.032 |
| 07-after-4 | 9.474 | 9.330 | 0.037 |
| 08-before-4 | 12.896 | 12.753 | 0.033 |
| 09-before-5 | 12.891 | 12.741 | 0.032 |
| 10-after-5 | 9.616 | 9.464 | 0.034 |

Each edit compiles exactly one source and relinks only the graphics archive; each
no-op compiles/relinks none. All 777 configured compiler commands, the renderer's
module response file, 158 prerequisite BMI hashes and dependency-package fingerprint
remain identical before/after every sample. The production/build input diff is
restricted to Graphics.Renderer.cpp. Sources match the exact commits and are clean.
Full logs, Ninja windows/graphs, weighted critical paths, CPU time and maximum
single-process RSS are retained. RSS is not concurrent aggregate memory; no memory
or no-op speedup is claimed. The after no-op median is 1.77 ms higher;
ranges overlap, and its cause was not isolated.

Inputs are read during hashing, making this a warm-input regime. Normal desktop
noise remains; no affinity, governor, turbo or filesystem-cache control. No competing
build/test jobs ran during sampling. Five samples per arm do not establish a stable
future speedup or statistical significance. Configure/setup is outside the comparison.

## Why this small change

Existing renderer-owned vector lvalues now construct the same destination spans
through pointer/count instead of the range constructor. Destination types preserve
constness, including the intentionally mutable render-prep sync view; empty vectors
keep their original pointer and zero count. No storage/lifetime/ownership change.
Two ranges::sort calls use std::sort with the identical comparators and identity
projection; neither promises stable ordering for equivalent keys. Both use the same
underlying sorting implementation in this host's libstdc++14.

Reuse the standard library and existing pointer/count pattern; no new adapter,
helper, interface, state or module. Unrelated range calls stay unchanged. The one
production file grows by 11 lines (10,957 → 10,968); this reduces compiler work,
not source lines. No public API, import, CMake edge, shader or algorithm changes.

Separate single-run traces helped select the expressions. They use the canonical ci
compile command/prerequisites, which have Vulkan/GLFW configured, unlike the timed
Null/headless cohort. No GPU execution occurs. These are not matched benchmarks;
phase totals overlap and must not be summed or combined with target wall times.

| Diagnostic source | Frontend seconds | Constraint-check seconds | Backend seconds |
|---|---:|---:|---:|
| Exact baseline | 12.640 | 5.216 | 0.903 |
| Only two sort substitutions | 12.014 | 4.684 | 0.900 |
| Only aggregate/prep span substitutions | 9.779 | 2.429 | 0.892 |
| Final combined source | 9.050 | 1.585 | 0.886 |

The final diagnostic source hash matches the verified implementation commit.
Controls retain distinct source hashes and temporary copies. The unformatted
combined prototype is also retained, not silently substituted for the final source.

## Verification and review

Canonical ci configure and complete IntrinsicTests build passed with Clang23.
Focused renderer/visualization/compiler-boundary/profiler/ownership tests: 347 passed
(30.51 s). Full CPU: 4,664 passed, no failures, one expected ASan-only lifecycle skip
out of 4,665 selected (140.20 s). Existing snapshot retention, aliasing, barriers,
queue plans, profiler and visualization tests exercise the affected contracts.
Strict layering, task/lifecycle, documentation, root/test-layout, skill-mirror and
benchmark/claim checks pass. The lifecycle check exposed stale retired-task links;
BUG-198 repairs the root/runtime indexes and retains the original failure.
No behavior was added; no implementation-mirror tests were introduced. No module
attachment changed, so this slice does not require or claim a fresh minimum-compiler
build. No GPU or sanitizer execution claim.

Claude reviewed the plan, exact source and measurement protocol. All six braced
function arguments were checked against their concrete span signatures. Protocol
concerns were checked against actual compiler selection, nested metric validation
and header-preserving Ninja helpers; explicit build-path, response-file and tmpfs
headroom guards were added before freeze. Source and benchmark identities were
frozen before timing. The result review and resolutions are retained with the data.

GRAPHICS-145 closes at CPUContracted, its intended compile-refactor endpoint.
Clean-workshop rows 1–4 and 8 pass with unchanged ownership/imports/API and no new
state or exception; rows 5–6 do not apply; row 7 names this endpoint. RUNTIME-267,
UI-037, GRAPHICS-105, LEGACY-043 and BUILD-006 retain their independent scope and
prerequisites. This closes one measured hotspot, not the entire cleanup program.

## Evidence and reproduction

The archived runner reuses canonical dependency fingerprint, Ninja analysis,
sealing and validation helpers. Recreate the exact commits and captured toolchain/
dependencies, prepare a detached source worktree at the recorded baseline, and run
the saved runner with fresh task-owned output/build paths. Absolute paths describe
this host. The runner performs its untimed prerequisite build; prebuilt BMI/object
binaries are not checked in. The archive retains all ten canonical records, raw
logs, commands/maps, diagnostic traces/controls and fixed Claude review packets.

[Manifest](../../../benchmarks/ci/manifests/renderer_frontend.yaml) ·
[Samples and recalculation](../diagnostics/graphics145_renderer_frontend/summary.json) ·
[Verification](../diagnostics/graphics145_renderer_frontend/verification.json) ·
[Archive index](../diagnostics/graphics145_renderer_frontend/evidence-index.json) ·
[Claude results review](../diagnostics/graphics145_renderer_frontend/results-review.txt) ·
[Raw evidence](../diagnostics/graphics145_renderer_frontend/raw-evidence.tar.gz).
