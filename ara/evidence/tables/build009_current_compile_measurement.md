# BUILD-009 — Current-source compile comparison

Measured 2026-09-15 with `build.engine_compile_iteration.followup.v1`. This compares
`07a8b29147ccd642fcf827c6a6361ba3e1c29f13` (post-overnight cleanup) with
`45d5a4f1fc0c7e51172a0e4a0edbca2f675448ea` (through RUNTIME-265 and GRAPHICS-143).
The new RUNTIME-266 registry experiment is excluded. The earlier BUILD-007 4.7% result
is a separate historical comparison; these percentages must not be added.

## Matched local measurements

Three retained samples per arm in before/after/after/before/before/after order.
Cells give median seconds [minimum–maximum]; all raw values remain in the bundle.

| Scenario | Before | Current | Median reduction | Compiler units |
|---|---:|---:|---:|---:|
| clean | 354.475 [349.875–354.507] | 333.377 [326.934–335.906] | +6.0% | 775 → 776 |
| noop | 0.073 [0.071–0.075] | 0.072 [0.071–0.075] | +0.4% | 0 → 0 |
| workspace_impl | 7.268 [7.237–7.384] | 7.288 [7.245–7.446] | -0.3% | 1 → 1 |
| renderer_impl | 13.178 [13.102–13.201] | 12.880 [12.803–13.343] | +2.3% | 1 → 1 |
| config_impl | 0.413 [0.411–0.415] | 0.419 [0.411–0.425] | -1.3% | 1 → 1 |
| config_interface | 33.533 [33.475–33.610] | 22.743 [22.469–23.209] | +32.2% | 13 → 12 |
| snapshot_interface | 29.337 [29.330–29.583] | 25.978 [25.927–26.672] | +11.4% | 5 → 5 |
| renderer_interface | 47.718 [47.457–47.936] | 43.732 [43.510–45.058] | +8.4% | 14 → 14 |

The clean and interface-probe populations are separated in this cohort. No-op,
workspace implementation and config implementation remain effectively unchanged;
renderer implementation ranges overlap, so its small median reduction is not
an established improvement. No runtime algorithm speedup is measured.

| Clean-build accounting | Before median | Current median |
|---|---:|---:|
| CPU user + system | 1365.590 s | 1284.420 s |
| Weighted Ninja critical path | 53.433 s | 49.492 s |
| Maximum single-process RSS | 2896.820 MiB | 2726.426 MiB |

The critical path weights actual Ninja invocations, including non-compiler work;
unmatched ancillary edges are retained in each result. RSS is the largest process,
not aggregate concurrent memory and not tmpfs storage.

## Remaining producers and owners

| Current clean-build producer | Before median seconds | Current median seconds | Follow-up |
|---|---:|---:|---|
| `src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.cppm` | 18.823 | 17.682 | RUNTIME-266 |
| `src/graphics/renderer/Graphics.Renderer.cpp` | 14.772 | 14.337 | GRAPHICS-144 |
| `src/graphics/renderer/Graphics.Renderer.cppm` | 15.589 | 13.093 | GRAPHICS-144 |
| `src/runtime/Rendering/Runtime.RenderExtraction.Internal.cpp` | 12.363 | 12.184 | No split selected by this measurement |
| `src/geometry/Geometry.HalfedgeMesh.CurvatureExtrema.cpp` | 12.325 | 12.000 | No split selected by this measurement |
| `src/runtime/Editor/Operations/Runtime.SceneEditingOperations.cppm` | 10.745 | 10.743 | RUNTIME-266 |
| `src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp` | 11.463 | 10.550 | No split selected by this measurement |
| `src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp` | 10.792 | 10.253 | RUNTIME-266 / RUNTIME-267 |
| `src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp` | 12.888 | 9.948 | RUNTIME-266 |
| `src/geometry/Geometry.Linalg.cpp` | 9.736 | 9.656 | No split selected by this measurement |

RUNTIME-266 remains the first experiment: scene-editing and snapshot interfaces
are on the critical path. Its ten registry-only pointer/reference consumers are
a concrete dependency cut, with existing ownership and lifetime preserved.
RUNTIME-267 follows on shared workspace/session files and freezes a new immediate
config baseline afterward. GRAPHICS-144 must inspect the remaining subsystem
device imports; dropping only the direct renderer imports is insufficient.
The other large implementation units above are candidates for diagnosis, not
evidence that splitting them would help. BUILD-006 remains a separate gated
build/cache decision; this experiment does not compare backends.

## Identity, controls and limits

- Ubuntu Clang 23.0.0, matching scanner, CMake/Ninja, i9-11900KF; exact versions in protocol.json.
- `ci` Debug; explicit Null/headless configuration; target `ExtrinsicRuntime` and its library prerequisites. Tests, Sandbox, Vulkan execution and sanitizer runs are excluded from these timings.
- Four jobs, compiler launchers/ccache disabled; preinstalled dependencies fingerprinted before and after each build, package installation disabled. All six share the same 1,728-entry fingerprint and 775 common compiler command lines.
- The current source adds one `Graphics.SceneHandles.cppm` producer. Clean builds compile 776 units versus 775; faster compilation is not a reduction in this unit count.
- Fresh owned tmpfs build per sample; disk-backed clean detached source; identical input pre-read and no discarded pilots. Edits touch mtimes only; source bytes remain at exact commits.
- Normal desktop host, no affinity/governor/turbo isolation, no competing builds/tests. Source review, remote Claude review and preparation of RUNTIME-266 in the separate main checkout occurred during timing; measured source and dependency assertions stayed clean.
- Preflight captured roughly 20 GiB free tmpfs, 47 GiB available RAM and 3.5 GiB disk; source worktree about 343 MiB. See raw host-preflight.json for exact values. No aggregate-memory or cold-filesystem claim.
- Six canonical result files validate. `claim_eligible: false` is retained: this is a descriptive local comparison, not publication-grade, statistical or cross-host evidence.

## Evidence and review

[Manifest](../../../benchmarks/ci/manifests/engine_compile_iteration_followup.yaml) ·
[Summary and all raw metric samples](../diagnostics/build009_compile_followup/summary.json) ·
[Source/tooling/archive index](../diagnostics/build009_compile_followup/evidence-index.json) ·
[Raw evidence archive](../diagnostics/build009_compile_followup/raw-evidence.tar.gz).

Claude approved the medians, ranges, scope and accounting. All 24 reported critical-path
durations/counts were recomputed from raw DOT/Ninja logs; alternate tied path witnesses
have identical duration. Pre-timing canonical ci build and 90 focused CPU/compilation
checks pass, as do 26 hotspot-tool and 15 result-validator tests. All six measured
source builds succeed. The owned timed build was removed; its source checkout is
retained for the bounded RUNTIME-266 experiment. Current refactor verification is
recorded separately and is not attributed to these measured source revisions.

[Results review](../diagnostics/build009_compile_followup/claude-results-review.txt) ·
[Accounting review](../diagnostics/build009_compile_followup/claude-accounting-review.txt) ·
[Recalculation](../diagnostics/build009_compile_followup/recomputed-accounting.json) ·
[Scratch cleanup](../diagnostics/build009_compile_followup/cleanup.json).
