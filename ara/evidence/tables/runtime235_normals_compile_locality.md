# Normal processing compilation-locality diagnostic — 2026-09-13

RUNTIME-235's normal-estimation slice replaces three obsolete test-only normal APIs
with the existing configured normal operation. The surviving operation has its own
small module, reuses the generic processing commands and property/lifetime helpers,
and reuses compiled mesh helpers, including face-ring validation shared with mesh
extraction. CPU/GPU kernels,
current UI/config controls, Show actions and same-domain publication are retained.
This slice is complete and uncommitted; RUNTIME-235 remains active.

## Matched local content edit

| Normal result contract edit | Before | After |
|---|---:|---:|
| Build wall time, seconds | 334.094 | 59.089 |
| Physical compiler invocations | 57 | 12 |
| Production / test invocations | 31 / 26 | 10 / 2 |

The identical `bool CompileLocalityProbe{false};` member was inserted into
`EditorNormalEstimationResult`: before in the broad processing interface, after in
`Runtime.NormalOperations.cppm`. Both builds used Clang 23, Debug `ci`, Sandbox ON,
`IntrinsicRuntimeContractTests` plus `ExtrinsicSandbox`, eight jobs and disabled
compiler cache. Selected targets were reconciled before measurement. Exact source
restorations built successfully (345.819 seconds before; 59.604 seconds after), and
the final full target reconciliation plus focused test run passed.

**One dirty-worktree sample per variant; not a repeatable speedup or full clean-build
claim.** Order was before→after, filesystem cache was unflushed, and host contention
was uncontrolled. The source baseline includes earlier uncommitted RUNTIME-233/234
changes. No time threshold is introduced.

The copied manifest initially said `family: point_analysis`; the executed edit was
always the normal result. Corrected artifacts say `normals` and record Sandbox ON.
Original manifest/results are preserved beside the metadata-only reseal, which has
the same run IDs, unchanged measurements and explicit correction diagnostics. These
are two executions, not four. All canonical results remain claim-ineligible.

## Code footprint and review

The complete native production footprint across 23 changed files is 34,204→31,796
physical lines: **−2,408 lines** (−2,295 nonblank). This includes the new owners and
their integration; it excludes tests and prose documentation. Five files were added,
and no whole files were deleted. The reduction comes chiefly from deleting duplicate
normal command/capture/job/history implementations, not moving them out of count.

Claude reviewed a fixed packet with tools, hooks, MCP and persistence disabled.
Its conditional findings were checked against the canonical watch, lifetime,
catalog, status and orientation implementations. No missing capability was found.
Local reconciliation fixed a missed entity-chooser handle, module registration,
direct include and test bindings. One full-CPU source check still counted a retired
job factory; it now checks the four survivors and both normal jobs' world scopes.
The initial failed run and the passing corrected run are retained. Claude's original
packet is separate from the final diff; the final corrections were locally reviewed.

Nine obsolete API integration cases were replaced by configured-path coverage for
all five mesh weightings, graph/point normals, direct/queued execution, duplicate
submission, no-change/history, stale targets and freed/detached scenes. A session
test covers copied result frames and callback dismissal/detachment. Existing kernel,
domain/config/UI and GPU tests remain. Six compiler-metadata boundaries now include
normal production and test isolation.

## Verification

All rows below have zero failures. Counts are selected CTest registrations;
sanitizer presets use grouped registration and are not directly comparable to `ci`.

| Gate | Selected | Skipped | Wall seconds |
|---|---:|---:|---:|
| cpu | 4547 | 6 | 116.79 |
| focused_after_restore | 323 | 0 | 14.42 |
| asan | 2925 | 0 | 584.55 |
| ubsan | 2925 | 1 | 255.88 |
| vulkan | 22 | 0 | 189.83 |

The full CPU gate excludes `gpu|vulkan|slow|flaky-quarantine`; sanitizer CPU runs are
serial and use separate fresh presets. The affected Vulkan run intersects `gpu`
and `vulkan` labels for PointLBVHGpuSmoke and PointConstructionGpuSmoke. CPU skips
are the five native-window fixtures and the ASan-only leak-control case; UBSan
skips that ASan-only case. AddressSanitizer and Vulkan have no skips. Host execution
uses the existing BUG-188 sanitizer-discovery workaround.

Strict layering, task policy/state links, documentation links, test layout, skill
mirrors, source-documentation checks, generated module inventory/session brief and
diff whitespace pass. Existing root hygiene failure BUG-177 (`.agents/` metadata)
remains; it was not suppressed.

## After-edit compilation scope

- `src/app/Sandbox/Editor/Sandbox.DomainPanels.cpp`
- `src/app/Sandbox/Editor/Sandbox.EditorShell.cpp`
- `src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp`
- `src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp`
- `src/app/Sandbox/Editor/Sandbox.PanelSupport.cpp`
- `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Normals.cpp`
- `src/runtime/Editor/Operations/Runtime.NormalOperations.Frame.cpp`
- `src/runtime/Editor/Operations/Runtime.NormalOperations.cpp`
- `src/runtime/Editor/Operations/Runtime.NormalOperations.cppm`
- `src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp`
- `tests/contract/runtime/Test.NormalEstimation.cpp`
- `tests/contract/runtime/Test.SandboxEditorSessionLifecycle.cpp`

Unrelated numerical adapters, the broad mesh implementation, and shared point/mesh
capture implementations did not compile for this normal-result content edit.

## Evidence and remaining work

[Machine-readable diagnostic](../diagnostics/runtime235_normals_compile_locality.json)
contains corrected canonical results, source hashes, physical compiler lists,
full production footprint, review hashes and test-log hashes. Raw inputs, original
and corrected manifests/results, snapshot, logs, probe scripts, fixed review packet
and final diff are archived locally under `build/analysis/normal-processing-locality-2026-09-13/` (ignored build output).

[RUNTIME-235](../../../tasks/active/RUNTIME-235-mesh-processing-compilation-locality.md)
still owns curvature/segmentation, topology, UV/parameterization, registration,
the obsolete immediate outlier API comparison and later common-catalog cleanup.
This normal slice does not establish whole-engine or Framework24 completion.
