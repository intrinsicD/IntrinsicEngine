---
id: RUNTIME-298
theme: I
depends_on: [RUNTIME-292, GRAPHICS-154]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-298 — Kernel density, point spacing and density weights on the GPU property residency

## Goal
- RUNTIME-294 row "Kernel density, point spacing, density weights" (one task: the three share one neighbor kernel over the LBVH).
- Input: positions from the canonical residency slot through the LBVH.
- Output: a scalar ring per method (observed by the colormap); Accept through the scalar transaction.
- CPU stage today: reductions on downloaded neighborhoods. Port: one kernel over the LBVH neighbors with fixed-order sums.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: a count-matched vec3 position property on any point domain. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | `Extrinsic.Runtime.PointFieldOperations` / `PointAnalysisOperations` (existing density, spacing and weight operations). |
| Config/agent | Unchanged backend enums; IO counters in the result and agent output. |
| UI | The three panels: Accept / Discard, observation state, IO counters. |
| Publication | One scalar on the input domain, same cardinality. GPU preview: yes (colormap scalar); commit via the scalar transaction. |
| End-to-end tests | Contract tests on the mock device; one gpu;vulkan parity + IO smoke covering the three methods. |

## Completion — 2026-09-30
Commit: see RETIREMENT-LOG (`claude/cpd-nystrom`). Kernel density, point spacing and density
weights run on the GPU property residency (`vulkan_lbvh`):
- One LBVH neighbor kernel (`point_scalar_analysis.comp`), scalar rings observed by the
  colormap, and the shared `Runtime.PointScalarTransaction` (Accept -> publication ->
  `BindRevision`).
- By operator decision, the CPU references of outliers, KDE, spacing and density weights now
  compute distances, kernels, sums and statistics in double. The GPU kernels use the same
  expressions in the same order.
  - Neighbor selection uses the same double squared distances on both sides.
  - Terms that would be double-subnormal flush to 0 identically on both sides. Float
    denormals are no longer involved, so a GPU that flushes them cannot diverge.
- The Opus review findings are fixed:
  - KDE with stray points: the run succeeds and far terms become 0;
  - the panel publishes the full result once;
  - the kNN insertion cost is reduced;
  - the transaction drift between outliers and scalars is fixed (full unification filed as
    RUNTIME-311);
  - the double-order contract of the `*FromNeighbors` APIs is documented.

Evidence:
- Contract, panel and CPU unit tests, including float-subnormal boundary cases.
- gpu;vulkan `RUNTIME298PointScalarResidency` and `RUNTIME297OutlierResidency`: measured max
  delta 0 for every method and variant, including the boundary cases. Zero-upload second run;
  Discard keeps the rows.
- `PointLBVHGpuSmoke` density phases: the Accept-submission rejection is covered, and the
  isolated live sample has density 0.
- Implemented by Codex 6 Astra (medium/high), reviewed by Codex and Claude Opus 5.5 (medium);
  re-review clean.
- CPU gate 5391/5391. GPU suite green except the environmental `VulkanShutdownLsanContract`
  (1 opt-in skip). Operational.

## Acceptance criteria
- [x] gpu;vulkan parity smoke: the accepted rows equal the CPU reference within a stated, justified tolerance.
- [x] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [x] Panel Accept / Discard (Accept disabled with its reason when stale); batch and agent commands accept automatically.
- [x] `method.engine-integration` publication row states "GPU preview: yes/no; commit via X" and the method docs record the backend identity and parity delta.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```

## Review correction log — 2026-09-30

Operator direction: fix the three review findings on `claude/cpd-nystrom`, retain
useful uncommitted implementation, do not commit, build both existing presets
without option changes, run the full CPU gate, and leave GPU execution to the
operator. The operator explicitly permits changing the CPU reference arithmetic
to double. This work also covers the same numerical and initial-submit exposure
in RUNTIME-297 outliers; its retired task record is unchanged.

- Outliers: promote coordinates before subtraction; statistical neighbor means,
  LDR means/ratios, population moments and masks use uncontracted double
  expressions. LDR returns its pre-publication mask so runtime cannot reclassify
  rounded float scores. Statistical CPU LBVH uses the same pre-self-removal
  candidate width as octree/GPU, including coincident peers.
- KDE: double distances, sqrt-then-square exponent expression, NN bandwidth
  mean/variance, Gaussian, normalization and ordered reductions. Host double pow
  constants are shared with the shader. Exponents below -708, or a normalized
  kernel/division that could become a double subnormal, refuse publication.
- Spacing: double differences, distances, distance sums, scale products, centroid,
  diagonal and spacing/radius means. Float public properties/statistics remain.
- Density weights already used double coordinate differences, compact kernels
  and ordered sums. Contraction is now explicitly disabled. The conservative
  radius avoids forming h squared below 1e-100, where its contribution is far
  below the float-normal floor's rounding error. CPU tiny-support coincident and
  outside-support branches remain valid; Vulkan refuses subnormal double support
  parameters. Existing nonzero subnormal input-coordinate/float-parameter
  refusals remain because shared LBVH building and input conversion use float.
  The obsolete radius-squared-below-FLT_MIN refusal is removed.
- Reuse/neighbor decision: extend existing octree/LBVH queries with an opt-in
  double-distance mode instead of bounded float-ranked candidate oversampling
  (which cannot guarantee enough slack candidates at arbitrarily dense ties).
  Default consumers retain float semantics and the 8-byte neighbor ABI. Scalar
  queries use double box pruning, final distance keys, radius inclusion and
  index tie-breaks; the GPU helper reuses the existing nodes and result storage.
  Octree radius broad phase uses outward-rounded coordinate bounds and exact
  double final inclusion. Density kd-tree retains its existing 8*epsilon relative
  plus 8*FLT_MIN absolute expansion, rounded outward, followed by strict double
  support filtering. This covers the normal subtraction/square/sum rounding
  error (less than 4 float epsilons) and absolute FTZ error; caps count the same
  expanded-radius candidates in CPU LBVH and GPU paths.
- GPU expression mapping: double scratch persists means, nearest distances,
  unrounded scalar values and bandwidth across dispatches. `precise` decorates
  multiply/add chains; the CPU uses `#pragma clang fp contract(off)`. SPIR-V
  inspection confirmed NoContraction on those chains and push offsets 112/120
  for the double KDE constants (128-byte push range). Stats buffers have matching
  double CPU/GLSL layouts. Subnormal float outputs are rounded in double to an
  integer mantissa and stored as bits, avoiding float FTZ at final publication.
- Exponential check: an exact host translation of `exp_double.glsl`, compiled
  with Clang 23 `-O2 -ffp-contract=off`, sampled 4,000,001 uniform points over
  [-708,0] and 3,063 points at/adjacent to split-ln(2) reduction boundaries.
  Maximum relative difference from `std::exp`: 2.2189062043037831e-16 at
  -451.93126799999999. The documented conservative binary64 arithmetic budget
  is 8e-15 relative (degree-13 truncation below 5e-18 plus rounding); this host
  check does not claim executed device parity. The shared exponential retains
  its existing deeper tail for other consumers; KDE rejects before that range.
- Notification fixes: initial Submit rejection clears the sink before failure
  cleanup in both transactions. Immediate failure is returned exactly once;
  queued Accept rejection still delivers one callback and discards the ring.
  The density-weight failure smoke keeps initial rejection, restores ordinary
  data after the 1030-coincident-point overflow, rejects `Accept point scalar`,
  checks callback count/unchanged rows/history/no ring, then succeeds again.

Changed expectations (no tolerances widened):

- The radius boundary fixture `.3f,.4f` has double squared distance
  0.25000001192092913, exceeding .5 squared by 1.1920929132713809e-8. Its origin
  count changes from 2 to 1; both CPU backends agree.
- Spacing and LDR formerly rejected a coordinate of 1e30 due to float-square
  overflow. Double evaluation now yields finite radii of order 1e30 and LDR
  scores [2, 2/3, 2/3], rather than no result. Wrapper publication follows suit.
- The multi-domain KDE fixture adds bandwidth 2 for successful history coverage
  and explicitly requires atomic CPU/backend refusal for narrower tails below
  -708 (e.g. distance 10, h=.2 gives exponent about -1250). Those tails formerly
  rounded to zero. Existing numerical tolerance bounds are unchanged.
- Tiny normal outlier radii now pass numerical admission instead of the old
  FLT_MIN-square guard; service availability remains independently required.
- New boundary fixtures measure spacing 1e-20 scaled by 1e20: old float result
  0.9999973177909851 versus new 1.0 (delta 2.682209014892578e-6). KDE at h=1e-12,
  separation 1.34e-11 has exponent about -89.77999884 and density about
  6.48294179e-5, rather than GPU FTZ zero. Density weights preserve the analogous
  tiny-distance compact kernel. Double-ranking tests distinguish float ties.

GPU execution remains pending under this task. The two transaction parity smokes
include tiny-distance/exponential cases with relative checks; the scalar smoke
also publishes a subnormal float radius (~1e-40) through the integer store path.

Review/verification notes:

- Public float bandwidth remains range-checked even though its derivation is
  double; an unrepresentable diagnostic refuses KDE publication.
- Scratch views explicitly use 8-byte reference alignment (the second scalar
  array starts at live_count*8), and LDR scratch transitions include shader
  writes. CPU/GLSL layout assertions cover both stats records and scalar push
  offsets. No dependency edge, gate or tolerance was weakened.
- Focused CPU verification: 51 geometry tests and 66 runtime contract tests pass.
  Both existing build directories are built with `cmake --build <dir> -j$(nproc)`;
  CMake options are unchanged. Layering, changed-file docs sync and strict task
  policy checks pass. Full CPU gate details are reported in the session result.
- No GPU test was executed and no commit was made. Real-device smoke execution
  and the existing Operational acceptance checkboxes remain with the operator.


## Review follow-up — 2026-09-30

Operator direction: fix all six independent-review findings without committing;
build both existing trees without CMake option changes, run the full CPU gate,
and leave GPU execution to the operator. This entry supersedes the earlier
KDE-tail refusal and double-subnormal support-admission decisions above.

- KDE flushes exponents below -708, double-subnormal normalized terms and
  per-point means to zero on CPU and Vulkan, retaining all selected neighbors
  in the denominator. Automatic and .2 bandwidth runtime fixtures must succeed.
- Density weights encode double-subnormal support as zero on the device. Float
  coordinates cannot have distinct points inside that support; the exact
  coincident branch remains valid and no support square is needed.
- Reuse/right-sizing: the existing `FoldGpu` adapters own typed result assembly.
  Their compiled overloads serve both transaction callbacks and panel snapshots;
  panels omit Start sinks and publish one complete terminal result. Test-front
  seams accept simulated diagnostics to verify retained panel results.
- Double LBVH insertion stops when the carried ID becomes invalid before loading
  another old position; ranking expressions and tie breaks are unchanged.
- Outlier currentness checks every captured ring generation. Scalar Accept now
  mirrors the outlier's optional guarded sink replacement after admission.
  [RUNTIME-311](../backlog/runtime/RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md)
  owns unification over N rings; no lifecycle unification is attempted here.
- Public supplied-neighbor comments and method docs specify double ordering.
  No dependency edge or shader push-constant layout changes in this follow-up.

Verification for this follow-up:

- `CCACHE_DISABLE=1 cmake --build build/ci -j$(nproc)` and the same command
  for `build/ci-vulkan` both pass. The environment variable bypasses the
  sandbox's read-only ccache directory; no CMake option was changed.
- Focused CPU selector `KernelDensity|PointScalarTransaction|OutlierTransaction|ScalarTransactionTerminalResultsPersistAndDetachDiscards`:
  66/66 pass. Coverage includes `KernelDensity.DoubleSubnormalTailsFlushToZero`,
  restored automatic/.2 bandwidth publication across domains, all three retained
  panel results, Accept delivery, and replaced scalar/outlier ring generations.
- Exact requested full CPU command:
  `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)`.
  CTest exit 8: 5,391 selected, 5,381 passed, 6 skipped, 4 failures; 51.79 seconds.
  The only failures are `LocalSocket.RoundTripsDataBetweenListenerAndClient`,
  `LocalSocket.RejectsLiveOwnersAndReplacesStaleFiles`,
  `SandboxAgentServer.ClientRunsSmoothingThroughTheSocketAndUndoesIt`, and
  `SandboxAgentServer.ConnectionWindowShowsTheClientAndDisconnectsIt`.
  AF_UNIX creation independently reports errno 1 (EPERM); these four sandbox
  socket failures are ignored under the operator's explicit instruction.
- Strict layering, changed-file docs sync and strict task policy pass; generated
  module inventory and session brief are current. The optional doc-link check
  exits 0 in warning mode with five existing links in untouched task files.
- Review: scope is the six requested corrections; existing numerical and typed
  publication owners are reused, dependency direction and shader ABI are intact,
  and no device parity/performance claim is added. The broader lifecycle cleanup
  remains RUNTIME-311. No GPU tests were run and no commit was made.
- Full local evidence: `/tmp/runtime298-ci-build.log`,
  `/tmp/runtime298-vulkan-build.log`, `/tmp/runtime298-focused.log`,
  `/tmp/runtime298-cpu-gate.log`, and `/tmp/runtime298-cpu-details.log`.
