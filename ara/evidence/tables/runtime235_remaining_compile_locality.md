# RUNTIME-235 remaining-family reconciliation — 2026-09-13

The remaining scoped mesh families are implemented and reviewed with Claude in
the uncommitted checkout. Registration, parameterization/UV, mesh fields and
mesh topology use independent contracts with existing generic commands, guarded
callbacks and shared property/config owners. The broad mesh unit retains
cross-cutting discovery; kernels and renderer architecture are unchanged.
Six equivalent catalog wrappers and the obsolete immediate outlier API are gone.
Unique old behavior coverage now exercises configured outlier removal.

This batch reduces native production code only modestly. Its principal measured
result is reduced compilation fan-out for a representative public result edit.
It does not complete every remaining processing adapter or Framework24 convergence.

## Matched local edit diagnostic

| Measure | Before | After |
|---|---:|---:|
| Curvature-result edit build | 344.537 s | 74.229 s |
| Physical compiler invocations | 57 | 16 |
| Production / test invocations | 31 / 26 | 11 / 5 |
| Ninja compile outputs | 60 | 17 |
| Complete changed production physical lines | 40,193 | 40,055 |
| Complete changed production nonblank lines | 37,899 | 37,818 |

The footprint includes all 47 changed native production/build files, including
16 added files and one removed file: −138 physical lines, −81 nonblank,
net +15 files. These counts exclude the preceding normal slice. Splitting owners
is useful for compile locality but does not by itself make the repository smaller.

Both variants insert the same boolean member into `EditorMeshCurvatureResult`:
in the broad geometry-processing module before, the mesh-field module after.
The stable benchmark is `build.processing_locality.incremental`, dataset
`local.processing_family_content_edits_20260913`. Conditions: Clang 23 Debug,
`ci` with Sandbox ON, `IntrinsicRuntimeContractTests` and `ExtrinsicSandbox`,
eight build jobs, compiler cache disabled, filesystem cache not flushed,
shared preinstalled vcpkg dependencies, one sample per variant, before then after.
The manifest named curvature and Sandbox correctly from the start.

Both edits were restored and builds reconciled. All native source hashes still
match the final pre-probe snapshot; only the later one-line CTest timeout
registration differs. Compiler metadata checks cover every migrated family;
only curvature received a timed edit. The largest remaining after-probe compiler
was `Test.SandboxEditorMeshMethods.cpp` at 35.621 s. No arbitrary timing gate,
separate impact database or per-method library was introduced.

Source is dirty at HEAD `12d61d7597a60b29f80232cc1797c30fded1c7e0` and includes
prior uncommitted work. These are **non-claim-eligible local diagnostics**, not
repeated speedups, clean-build results or cross-host estimates. Before the final
probe, 23 generated orphan build artifacts (2,004.8 MiB) were removed after
checking that neither Ninja nor CTest referenced them; no timed target output
was removed. Cache/environment effects are not experimentally excluded.

## Review and repairs

Claude performed the bounded source migration and fixed-source reviews; Codex
reconciled builds, checked findings and ran verification. Packets and intermediate
reviews are preserved. Repairs include missing explicit imports and six lost
symbols, shared UV controls and dismissal order, retained atlas sizing,
exactly-once terminal delivery, and unpublished UV completion. Lifetime guards
now run before Scene dereferences. New tests free scenes with work queued for
all three affected job owners and check failed-kernel diagnostic delivery.
A shared test harness now takes only the JobCommands contract it actually uses.

Initial compile/link failures and five focused failures remain in the evidence.
Fixture corrections preserve property/history/dirty-state assertions. The final
Claude source review found no remaining blocking findings. The timeout review
independently confirmed a pre-existing test-registration omission.

## Verification

| Gate | Final evidence |
|---|---|
| ci build | IntrinsicTests + ExtrinsicSandbox, including post-probe reconciliation, pass |
| Focused family/config/UI/lifetime/locality | 537 passed |
| Full CPU | 4,560 selected; zero failures, six expected skips |
| Full separate ASan | 2,938 grouped entries; zero failures/skips |
| Full separate UBSan | 2,938 grouped entries; zero failures, one ASan-only skip |
| Actual Vulkan | 22 initial passes plus the corrected remaining case; 23 distinct checks, no skips |
| Structural | Strict layering, task policy/state links, docs links, test layout, mirrors and inventory pass |
| Root hygiene | Existing BUG-177 `.agents/` finding remains; not suppressed |

Sanitizers use fresh matching presets, IntrinsicCpuTests and the full
exclusion-only CPU selector with serial test execution. BUG-188's existing
host workaround avoids sandbox LeakSanitizer discovery failure without changing
instrumentation. GPU execution uses ci-vulkan's combined instrumentation on the
RTX 3050; the general GPU cohort's pre-existing leak settings are unchanged.

The initial Vulkan suite timed out only
`LocalDistanceRatioPublishesAcrossDomains` at its inherited 30-second deadline.
The unchanged isolated CTest reproduced it. A direct bounded run completed all
assertions in 44.721 s with zero reference error; its cold/warm/k63 phases each
took roughly 10 s. `xset q` reported Monitor Off, and unrelated ICP/query timings
match the earlier controlled display-off findings in BUG-179/143. This supports
that pacing explanation without claiming a new controlled performance result.

BUG-189 adds this case to its shared OutlierApp fixture's existing 120-second
registration, above the unchanged internal 95-second watchdog. Registry comparison
confirms identical commands, labels, working directory and sanitizer environment.
The registered rerun passes in 44.72 s. No assertions or numerical tolerances
changed. The direct diagnostic accidentally omitted that environment and exited
with a leak report after its assertions; it is never counted as a passing gate.
Those 115,869 bytes in 24 allocations are recorded under the existing BUG-180
retention investigation; ownership remains unproven.

## Reproduction and custody

The [diagnostic JSON](../diagnostics/runtime235_remaining_compile_locality.json)
binds source hashes, every changed file, complete sealed probe results,
compiler counts, gate attempts, review hashes, registry comparisons and limitations.
The [task note](../../../tasks/active/RUNTIME-235-mesh-processing-compilation-locality.md)
records scope, owner/reuse decisions and the clean-workshop sweep.
Raw source snapshots, logs, Claude packets, scripts and manifests are retained in
local ignored output `build/analysis/remaining-processing-locality-2026-09-13/`.
C92 remains a hypothesis; no commit or push was performed in this turn.
