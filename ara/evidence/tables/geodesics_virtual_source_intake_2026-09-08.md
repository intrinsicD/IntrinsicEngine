# Virtual-source geodesics intake and CPU verification

Local development evidence for the Framework24 virtual-source port, based on
`314db9ea5be314c732458649775103c461992fbb` plus the dirty working tree.
[Source hashes and gate results](../diagnostics/geodesics_virtual_source_2026-09-08/verification.json)
bind the final native implementation. No clean-source performance claim,
Framework24 numerical parity, sanitizer result, or GPU execution is established.

## Numerical investigation

| Experiment / prediction | Observation and disposition |
|---|---|
| Initial interior-source flat-grid oracle expects exact planar distances within 1e-5 | Rejected: the retained [initial failures](../diagnostics/geodesics_virtual_source_2026-09-08/initial-oracle-failures.txt) contain nonzero interior-source errors. C75 records this refutation. |
| Folding and scaling decimal float coordinates preserves distances at roundoff tolerance | Initial comparison failed. Decimal input quantization changes the intrinsic metric, and competing face winners can change vertex approximations. Binary-exact coordinates now isolate the isometry test; perturbation continuity remains unestablished. |
| Dropped side flags alone explain the exact-oracle failure | A temporary no-flags variant still failed the interior oracle and decimal-scale comparison. Restoring all seed edges also failed those expectations. Neither variant was adopted. |
| Compare the authors' supplemental formulation before changing the oracle | The original float runner, a boundary-seeding/centroid-normalization adaptation, and a double adaptation also produced nonzero planar error. These temporary intake programs are diagnostic context, not a frozen Framework24 parity benchmark. |
| Final native CPU contract | Nine geometry tests cover bounded planar error, binary-exact folding/scaling, multiple sources, disconnected/isolated/deleted slots, concave boundaries, a closed octahedron, invalid input, and expansion exhaustion. |

The one-virtual-source-per-face formulation is approximate. Tests now state
fixture-specific acceptance bounds; no general accuracy guarantee is inferred.
The published formulation and the later CGAL investigation are linked in
[the method intake](../../../methods/geometry/geodesics_virtual_source/paper.md).

## Verification and results audit

| Statement | Evidence and disposition |
|---|---|
| CPU geometry/config/publication/undo contracts execute | [Focused CTest output](../diagnostics/geodesics_virtual_source_2026-09-08/focused-ctest.txt): all 36 selected cases pass, including 18 new geometry/runtime/panel cases. |
| The mesh menu exposes the port | `SandboxEditorGeodesics.RegistersAndDrawsWindow` registers once and opens the window in a Null-window UI frame. It does not exercise mouse picking or Vulkan scalar readback. |
| Full supported CPU gate succeeds | 4,326 selected, 4,320 passed, six skipped, zero failures. Five skips require a display; the GLFW leak-control test requires its capability/instrumentation. The first run's two menu-inventory failures were corrected by adding the new window to the expected inventory. |
| The declared local smoke passes | [Schema-v2 result](../diagnostics/geodesics_virtual_source_2026-09-08/benchmarks/smoke.json) uses `cpu_reference`, one warmup and eight measured solves, analytic planar distances, RMS and maximum absolute error. The validator independently recomputes all three threshold dispositions; all 31 emitted smoke payloads validate. |
| Exact planar output | Refuted, C75. Approximation checks replace that invalid oracle; the original failure output is retained. |
| Framework24 parity, speedup, or GPU operation | Unclaimed. Requires matched native/reference fixtures, a comparable baseline and clean source identity for performance, and separate backend evidence. |

The `ci` preset configured and built `IntrinsicTests`, `IntrinsicBenchmarkSmoke`
and `ExtrinsicSandboxEditor` with Clang 23. It disables the standalone sandbox
executable. Method/benchmark manifests, strict layering, test layout, docs sync,
doc links, and generated module inventory checks pass.

## Architecture review

| Clean-workshop row | Verdict |
|---|---|
| Promoted imports follow layer policy | pass: pure geometry kernel, runtime composition, app calls runtime exports |
| CMake links follow layer policy | pass: existing geometry/runtime/editor targets, no new link edge |
| Public API has no downward higher-layer types | pass: geometry takes mesh/spans/value records |
| Renderer ownership | n/a: no renderer change |
| Typed frame-pass identity | n/a: no new pass |
| Resource-driven recipe dependencies | n/a: no recipe change |
| Scaffold/parity retirement | n/a: no task or parity gate retired |
| Temporary exception ownership | n/a: no exception introduced |

Config files, programmatic callers and UI share the validated config section.
Output is limited to named properties on the original vertex slots. Synchronous
propagation has an explicit expansion budget; failed computation does not publish.
