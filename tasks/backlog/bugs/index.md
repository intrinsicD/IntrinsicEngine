# IntrinsicEngine — Active Bug Notes

This file tracks **currently reproducible correctness bugs, flaky tests, and test-harness defects**.
Each entry includes the observed repro, the likely affected symbols, and a fix plan aimed at a robust engine-level correction rather than a one-off patch.

## Active Issues

The 2026-08-07 Sandbox UI workflow pass (`sculpt.obj` end-to-end through the
promoted Vulkan build) opened `BUG-137` through `BUG-142`. `BUG-137` is upstream
of `BUG-140` and of the parameterization rejection recorded in `BUG-141`.

- [`BUG-155` — Native Vulkan timestamp smoke intermittently publishes zero
  duration](BUG-155-vulkan-native-timestamp-zero-duration-flake.md): a repeated
  54-case promoted-Vulkan gate produced one present `NativeGpu` `SurfacePass`
  duration of zero while every other case passed; the same full gate had
  passed immediately before it and three isolated repetitions passed
  afterward. Preserve raw query/availability/slot evidence and distinguish
  legal timestamp quantization from a query-lifecycle defect without retries,
  quarantine, or clamping.
- [`BUG-157` — Clang 20 fails IntrinsicTests on glm anonymous-union
  redeclaration](BUG-157-clang20-glm-module-union-build-break.md): on a
  Clang-20-only host, `Test.CameraModule.cpp` deterministically fails with
  `glm/detail/type_vec3.hpp: class member cannot be redeclared` when textual
  glm inclusion meets the imported RHI module chain, so the full
  `IntrinsicTests` build breaks on the documented minimum toolchain while
  geometry-only targets build and pass. Hosted CI installs clang-20 and is
  expected to hit this once its earlier shallow-checkout validator failure is
  fixed.

- [`BUG-154` — Restore PMP curvature parity without normal-seam topology loss](../../active/BUG-154-curvature-pmp-parity-corner-normal-topology.md):
  OBJ authored normals currently participate in the topology remap key, so a
  face-normal bunny becomes 1,485 disconnected corners instead of its 259
  position vertices. The repaired curvature estimator also needs PMP's signed
  3x3 eigensystem, boundary interpolation, and damped reusable property
  smoothing rather than full neighbour replacement.

- [`BUG-149` — Benchmark sealer escapes dotted output directories](BUG-149-benchmark-sealer-dotted-output-directory.md):
  `run_and_seal.py` interprets a dotted directory name as a file, seals its
  parent (observed as all of `/tmp`), and can return zero after the sealer
  reports failure. Resolve the exact output type/root and propagate both stage
  statuses without touching sibling JSON.

- [`BUG-134` — ImGui adapter panel draw-list test fails intermittently](BUG-134-imgui-adapter-panel-draw-list-intermittent.md):
  one default CPU run reported the panel draw-list contract as its sole
  failure, followed by ten passing isolated repetitions and a clean 4,103-case
  full rerun. Preserve the next failed assertion, establish a deterministic
  cause, and fix the owning test or adapter surface without quarantine,
  retries, or weakened coverage.
- [`BUG-122` — Runtime asset ASan tests retain expired callback and snapshot state](BUG-122-runtime-asset-asan-test-lifetimes.md):
  one shutdown test lets a queued hook retain loop-local synchronization state,
  while three progressive model-scene tests retain pointers into temporary
  `SnapshotAll()` vectors. The required serial ASan gate reports one
  stack-use-after-scope and three heap-use-after-free failures on
  `origin/main`; repair the test lifetimes without weakening their semantics or
  changing production contracts absent an independent repro.
- [`BUG-121` — GLM anonymous-union copy-assignment fails to compile through a C++23 module boundary](BUG-121-glm-anonymous-union-module-copy-assign.md):
  `clang++-20` rejects `glm/detail/type_vec3.hpp`'s `union { T x, r, s; }` when the implicit copy
  assignment for `glm::vec<3,float>` is first required in a TU that reaches the type through
  `Extrinsic.RHI.Types`. Breaks the build of `Test.CameraModule.cpp`, so `full-cpu`, `ci-asan`,
  and `ci-ubsan` never reach their test phase. Cold cache, so not a stale-BMI artifact.
- [`BUG-118` — GLFW X11 input-method LeakSanitizer recurrence](BUG-118-glfw-x11-input-method-lsan-recurrence.md):
  the standalone lifetime contract again retains the unsuppressed 408-byte
  libX11 input-method allocation despite proving process-static
  `glfwTerminate()` ran once; compare the live XIM environment and teardown
  path with the retired `BUG-082` evidence without weakening leak detection.
- [`BUG-097` — Progressive model-scene UV job publishes a zero atlas](BUG-097-progressive-model-scene-zero-uv-atlas.md):
  the default-off progressive enrichment path labels an all-zero authoritative
  `v:texcoord` property as an atlas and can publish it after newer UV/topology
  edits; replace it with real atlas output plus generation-safe stale discard.
- [`BUG-091` — GoogleTest PRE_TEST discovery times out on a cold start](BUG-091-gtest-pretest-discovery-cold-timeout.md):
  CMake's implicit five-second PRE_TEST discovery limit can abort CTest while
  an unrelated cold sanitizer binary enumerates tests, before the selected
  tests run; collect cold/warm/contention evidence and set an explicit,
  evidence-backed discovery policy without weakening per-test timeouts.
## Verified / Closed

- Closed 2026-08-11: [`BUG-153` — Restore edge-dihedral Taubin curvature
  estimation](../../done/BUG-153-curvature-taubin-edge-dihedral-estimator.md).
  The public full curvature field now uses one signed edge-dihedral/two-ring
  estimator, supported boundary vertices are no longer blanket-zeroed, and
  analytic regressions cover scale, orientation, cylinder/saddle directions,
  degeneracy, and scalar/direction coherence. Live Sandbox runs applied the
  command to 1,485-vertex bunny and 21,582-vertex armadillo OBJ inputs without
  publishing a non-finite value.

- Closed 2026-08-11: [`BUG-151` — Work graph cannot advance a declared
  multi-slice task](../../done/BUG-151-work-graph-multi-slice-cycle.md).
  `advance-slice` now preserves one hash-chained run while rebasing a declared
  repeated subgraph to an exact clean commit with fresh bounded attempts. The
  real METHOD-038 run advanced from graph slice 1 to 2 with its prior
  unreviewed checkpoint and stale source binding recorded explicitly; unsafe
  lifecycle, claim, recipe, and task-state transitions fail closed.

- Closed 2026-08-11: [`BUG-150` — Completed workflow reports lose their
  historical surface seal](../../done/BUG-150-workflow-report-historical-surface-seal.md).
  Completed dirty reports now carry a post-commit `seal.yaml` that binds the
  unchanged report and review records to an exact source/artifact tree. The
  existing `GEOM-071` and `METHOD-037` reports validate at their retirement
  commits without regeneration; active dirty reports remain live-bound.

- Closed 2026-08-11: [`BUG-152` — Geometry index presents retired GEOM-071 as
  active backlog work](../../done/BUG-152-geometry-index-retired-task-state-link.md).
  The active geometry list now cites the retired prerequisite as non-link
  history, preserving the `GEOM-072` narrative while restoring the strict
  task-state-link gate. No `GEOM-071` source, evidence, or lifecycle state
  changed.

- Closed 2026-08-10: [`BUG-147` — Editor UV regeneration replaces the mesh with
  the atlas chart-split
  mesh](../../done/BUG-147-uv-regeneration-shatters-mesh-topology.md).
  "Regenerate UVs" published `ToHalfedgeMesh(atlas.OutputMesh)` as the entity
  mesh — `BUG-137`'s defect at an entry point its slice C never touched — so
  the one command a user reaches for when they want a usable parameterization
  silently converted the mesh into a triangle soup: a closed icosahedron went
  in as 12 V / 30 E / 60 H / 20 F and came back as 60 V / 60 E / 120 H / 20 F,
  twenty charts for twenty faces, reporting `Applied`. The published mesh is
  now built from the source soup and the generated UVs are mapped back onto its
  own corners, on the corner domain when the atlas cut a seam and the vertex
  domain when it did not. `atlas.OutputMesh` is no longer used to build
  topology anywhere in that path, so the guarantee is structural rather than
  per branch.

- Closed 2026-08-09: [`BUG-146` — Topology-changing mesh operations silently
  destroy corner-domain
  UVs](../../done/BUG-146-topology-edits-destroy-corner-uvs.md). Simplify now
  forwards corner UVs into its scratch mesh through the canonical corner walk,
  so a corner-parameterized mesh keeps its UVs and FA-QEM's `PreserveUvSeams`
  pins the seam it can now see — 5 of 25 grid vertices where the unfixed source
  pinned 0 and deleted the property outright. Remesh and subdivide legitimately
  cannot carry UVs onto the topology they produce, so they now report the
  discard in both the result and the message instead of losing a
  parameterization under a success line. The audit's fourth target became
  [`BUG-147`](../../done/BUG-147-uv-regeneration-shatters-mesh-topology.md).

- Closed 2026-08-09: [`BUG-137` — Direct mesh import replaces halfedge topology
  with the UV-atlas chart-split
  mesh](../../done/BUG-137-direct-mesh-import-atlas-replaces-topology.md).
  Importing `tests/data/sculpt.obj` now yields its own 3669 V / 11013 E /
  22026 H / 7342 F and zero boundary vertices, because atlas UVs are published
  on the corner domain (`h:texcoord`) and the seam split happens once, at GPU
  upload, where 17795 duplicated vertices are reported instead of silently
  rewriting the authoritative mesh. Slice D's last entry claimed its four
  remaining consumers were wording only; two of them lost behavior and are
  fixed: FA-QEM's `PreserveUvSeams` pinned nothing on a corner-UV mesh, and a
  parameterization published `v:texcoord` underneath a surviving `h:texcoord`
  that wins the resolution order, so its result was read by nothing.
  Topology-replacing operations still destroy `h:texcoord` outright; that is
  [`BUG-146`](../../done/BUG-146-topology-edits-destroy-corner-uvs.md), opened with a
  probe rather than widened into this fix.

- Closed 2026-08-08: [`BUG-143` — Corner-UV `gpu;vulkan` smoke exceeds the 30 s
  cohort
  timeout](../../done/BUG-143-corner-uv-gpu-smoke-exceeds-cohort-timeout.md).
  The 13 s ↔ 34 s variance was `vkQueuePresentKHR` blocking ~0.9 s per frame
  whenever the display is not being scanned out: the engine's own frame-pacing
  capture measures 1000 ms frames with the monitor DPMS-off against 103 ms with
  it on, from the same binary in the same session. A frame count is therefore
  not a time budget, so the smoke now waits on the readiness condition it
  actually needs — corner UVs published — bounded by a frame cap and a
  wall-clock budget. Every assertion from `e1416f08` is restored verbatim; the
  smoke runs 3.7 s awake and 6.8 s throttled, and `BUG-137` slice B closes
  `Operational`.

- Closed 2026-08-08: [`BUG-124` — Geometry-presentation GPU smoke expects a
  retired unsupported
  slot](../../done/BUG-124-geometry-presentation-gpu-smoke-stale-unsupported-slot.md).
  The unsupported-slot contract was not retired; `RUNTIME-198` narrowed it so
  that scalar fields became backend-resident, which left the fixture's only
  property-buffer surface slot (`f:heat`) supported. A property-buffer
  `Displacement` slot — still unsupported today, and covered nowhere else in
  the tree — now carries that coverage, and the assertion names the slot
  instead of counting, so a future narrowing fails with its semantic,
  readiness, and diagnostic. No production code changed; the full `gpu;vulkan`
  gate passes 53/53.

- Closed 2026-08-08: [`BUG-142` — AssetIO queue shows 0% progress on a completed
  import row](../../done/BUG-142-assetio-queue-terminal-row-zero-progress.md).
  The reported `Completed` + `0%` screenshot does not reproduce — a complete row
  maps to `1.0` determinate through the whole chain — but two real defects in
  that code are fixed: `Failed` and `Cancelled` returned `1.0f` and drew a full
  bar labelled `100%`, and indeterminate stages carried a plausible-looking
  `0.45` that could not be told apart from real progress. Both terminal states
  now report no progress and are labelled by stage; indeterminate stages carry
  `0.0`, so determinate `0.0` means exactly `Queued`.

- Closed 2026-08-09: [`BUG-096` — ICP point-to-plane ignores target
  normals](../../done/BUG-096-icp-point-to-plane-target-normals.md). Both
  runtime branches now pass validated, world-space target normals to
  `AlignICP`, and a point-to-plane request that cannot be satisfied is refused
  rather than silently degraded to point-to-point behind a point-to-plane
  label. The result carries the requested and effective variants and the normal
  count.

- Closed 2026-08-09: [`BUG-145` — Editor geometry operations report Applied from
  written
  counts](../../done/BUG-145-editor-operations-report-applied-from-written-counts.md).
  Vertex normals, curvature, outlier removal, remesh, simplify, and UV
  regeneration all derive their terminal status from a changed quantity now, and
  a no-op reports `NoChange` with a reason, publishes nothing, and leaves no undo
  entry. Subdivide is the one audited operation without a gate: it cannot run and
  change nothing, and a test pins that reasoning.

- Closed 2026-08-09: [`BUG-141` — Editor geometry-processing diagnostics are
  mislabeled, duplicated, unscoped, and
  unactionable](../../done/BUG-141-editor-geometry-diagnostics-mislabeled-and-unscoped.md).
  A queued job no longer reports under a failure code, one operation's outcome
  no longer prints in every other panel, each stored outcome can be dismissed
  per slot on top of already being superseded by the next run of its own
  operation, and a parameterization rejection carries the connected-component
  and boundary-loop counts it was refused on.

- Closed 2026-08-08: [`BUG-140` — Mesh denoise reports Applied/Success after
  moving zero
  vertices](../../done/BUG-140-mesh-denoise-reports-success-after-zero-movement.md).
  Both denoise paths now derive the terminal status from the moved count rather
  than the slot-derived written count: zero moved reports `NoChange` with a
  message naming the pinned count, and returns before the history commit so a
  no-op leaves no undo entry. The panel reports the pinned ratio and keeps
  showing diagnostics for `NoChange`. Sibling operations sharing the defect are
  audited in that task and spun out as `BUG-145`.

- Closed 2026-08-08: [`BUG-144` — Work-graph stale-lock breaker can steal a live
  lock](../../done/BUG-144-work-graph-lock-breaker-and-claim-path-validation.md).
  Lock holders now publish a token/pid/host record inside the lock directory,
  and a waiter breaks only on a provably dead same-host pid, an unreadable
  record past a grace window, or a stale foreign-host record — never on elapsed
  time alone. Release compares the token and refuses to delete a successor's
  lock, reporting the takeover instead of cascading. `task_claim.py`
  `release`/`recover` now validate task IDs before building a path.

- Closed 2026-08-08: [`BUG-110` — Implicit smoothing applies boundary pins after
  rather than during
  solve](../../done/BUG-110-implicit-smoothing-boundary-dirichlet-solve.md).
  Boundary vertices are now Dirichlet-eliminated inside each axis solve via a
  new narrow `Geometry::Sparse::SolveCGShiftedFixed`, which folds the fixed
  columns into the free right-hand side and replaces fixed rows/columns with
  identity — keeping the operator SPD — instead of solving all-free and
  overwriting afterwards. An independent hand-assembled reduced-system oracle
  pins every interior coordinate, and a companion regression requires the result
  to differ from the old solve-then-overwrite. Both fail against the unfixed
  source; the CPU gate passes 4138/4138.

- Closed 2026-08-08: [`BUG-109` — Voxel downsampling invalid-input and
  deterministic-cell
  ordering](../../done/BUG-109-voxel-downsample-invalid-input-ordering.md).
  `VoxelSize` now requires a finite positive value — the old `<= 0` guard let
  NaN through — and every position component is validated and range-checked in
  `double` before the `int` cell cast, returning `nullopt` before any partial
  result exists. Occupied cells are emitted in ascending lexicographic key
  order instead of hash order. Five regressions fail against the unfixed
  source; the CPU gate passes 4131/4131. This unblocks `GEOM-061`.

- Closed 2026-08-07: [`BUG-108` — Fibonacci sphere sampling small-count and
  endpoint safety](../../done/BUG-108-fibonacci-sphere-small-count-endpoints.md).
  Every lattice now resolves the shared `0`/`1`/`2` contract before computing
  any index or divisor, which removes the `num_samples - 1` underflow, and the
  explicit-pole variants write the poles to indices `0` and `n - 1` instead of
  leaving element zero defaulted at the origin. The unused `SampleSurfaceUniform`
  helper is gone. Three regressions over every enum value fail against the
  unfixed source; the default CPU gate passes 4126/4126.

- Closed 2026-08-07: [`BUG-119` — Test.CheckTaskStateLinks asserts an inline
  SHA expression the docs-sync step no longer
  uses](../../done/BUG-119-check-task-state-links-docs-sync-env-assertion.md).
  The test now pins the docs-sync step's five event-payload `env:` bindings and
  asserts each event branch guards both SHAs before assigning them, instead of
  grepping the `run:` body for an expression that moved into `env:`. A second
  stale literal in the same test — the structural-policy step had grown from
  one script to three — is fixed the same way. Seven mutation probes confirm
  the assertions still fail on genuinely broken routing; `ci-docs.yml` is
  unchanged.

- Closed 2026-08-06: [`BUG-135` — LOP benchmark lacks manual CTest
  classification](../../done/BUG-135-lop-benchmark-missing-manual-ctest-classification.md).
  The standalone LOP-family GPU benchmark now appears in the explicit manual
  CTest producer set alongside the existing standalone runners. Both live
  aggregates and all 19 hermetic fail-closed regressions pass without changing
  benchmark behavior, registration, labels, or aggregate membership.

- Closed 2026-08-06: [`BUG-136` — Test-gate routing affected-case baseline
  drift](../../done/BUG-136-test-gate-routing-affected-case-baseline-drift.md).
  The exact BUG-106 inventory now replaces two retired Runtime Engine layering
  names with all six live cases and synchronizes its explicit count ratchets to
  39 Runtime contract cases / 226 total. Both live aggregates and all 19
  hermetic fail-closed regressions pass without changing ownership or checker
  semantics.

- Closed 2026-08-06: [`BUG-133` — Method backlog links a retired task outside
  history](../../done/BUG-133-method-backlog-retired-link-outside-history.md).
  Live LOP-family guidance now cites retired `METHOD-020` as non-link prose;
  strict task-state, task-policy, and task-format validation pass without a
  lifecycle exemption or task reopening.

- Closed 2026-08-05: [`BUG-132` — Vulkan method/render synchronization and
  resource lifetime](../../done/BUG-132-vulkan-method-render-synchronization.md).
  Loaded color attachments now compile to one read-write state, transfer
  readback barriers execute in the transfer submission's graphics-queue
  serialization domain, and repeated LOP grid clears carry the reverse
  compute-to-transfer dependency. The validation-enabled child.obj Vulkan LOP
  regression completes with zero synchronization or lifetime errors.

- Closed 2026-08-05: [`BUG-130` — Rejected experiment runs require their
  historical task seal](../../done/BUG-130-rejected-run-historical-task-seal.md).
  Independently rejected clean-source runs may verify only an exact same-ID,
  same-profile blob under the canonical active/backlog/done/archive lifecycle
  roots. Accepted, unaudited, dirty-source, forged, malformed, stale-bundle,
  and non-lifecycle decoy evidence remains current-task-bound and fail-closed.

- Closed 2026-08-05: [`BUG-129` — Claim custody accepts skipped benchmarks as
  positive evidence](../../done/BUG-129-claim-custody-skipped-benchmark-positive-evidence.md).
  The claim-grade METHOD-020 runner now returns nonzero for non-passed
  execution and reports requested Vulkan separately from actual `none` and
  unknown fallback on non-execution. Positive audit requires passed canonical
  status plus completed cells; rejected runs remain valid evidence and no
  longer block a later accepted run.

- Closed 2026-08-03: [`BUG-128` — Parallel pre-policy retired task
  baselines](../../done/BUG-128-parallel-prepolicy-retired-task-baselines.md).
  Exact `done/` or `archive/` snapshots may now bind one immutable parallel
  pre-policy revision and SHA-256 digest under a named high-risk policy task;
  open records and every later byte change remain prospectively enrolled.

- Closed 2026-08-01: [`BUG-123` — Retired queued scene save intermittently
  loses its terminal event](../../done/BUG-123-retired-scene-save-terminal-event-race.md).
  `IsComplete()` and terminal reaping now wait for a finalizer-owning job's
  main-thread reconciliation. The forced ordering and original scene-save
  selectors each pass 200 registered executions, and the normal, ASan, and
  UBSan CPU gates are green without weakened assertions.

- Closed 2026-08-01: [`BUG-127` — Task claim corrupts multiline dependency
  front matter](../../done/BUG-127-task-claim-multiline-dependency-front-matter.md).
  Claim metadata insertion now advances past the complete multiline
  `depends_on` sequence before writing workflow fields; the eight-case
  regression suite preserves inline and multiline dependencies plus every
  existing concurrency contract.

- Closed 2026-08-01: [`BUG-126` — Claim custody validates historical source
  seals against the current worktree](../../done/BUG-126-claim-custody-historical-source-seals.md).
  Clean claim-grade inputs now validate at their exact source revision, while
  post-run evidence remains current-only. Both 28-case tooling suites and the
  global METHOD-016/METHOD-017 custody/workflow gates pass.

- Closed 2026-08-01: [`BUG-125` — Queued-import contracts raced asynchronous state transitions](../../done/BUG-125-queued-import-contract-state-observation-races.md).
  Test-only decode/read interlocks now order the active-world transition and
  initial cancellability snapshot explicitly. Both cases passed 100/100
  normally and 100/100 pinned to one CPU; all 26 format-coverage cases, the
  19 concurrency-policy checks, and the 4,010-case CPU gate pass. Three exact
  multi-worker cases now carry source-audited CTest reservations.

- Closed 2026-07-31: [`BUG-095` — Direct-mesh postprocess can overwrite newer editor geometry](../../done/BUG-095-direct-mesh-postprocess-stale-overwrite.md).
  Deferred enrichment now validates the exact live mesh-source content and
  binding signature, asset-workflow binding epoch, and entity-sidecar job token
  before apply; callback-time world lookup makes world destruction and entity
  recycling lifetime-safe. It publishes stale/cancelled diagnostics without
  overwriting newer state and gates all mutating processing actions while
  pending. Seven deterministic real-engine contracts passed 20 consecutive
  repetitions each and the canonical CPU gate passed 4,065/4,065.

- Closed 2026-07-30: [`BUG-120` — Test.WorkflowConcurrency drifted from the CPU test sources it mirrors](../../done/BUG-120-workflow-concurrency-ctest-processors-drift.md).
  The exact source-derived reservation parity is restored at 73 cases
  (`PROCESSORS` distribution `{3: 49, 4: 22, 8: 2}`), stale one-worker config
  roots and clustering call-count guards are synchronized, and the focused
  workflow/cohort regressions plus canonical `IntrinsicTests` build pass.
- Closed 2026-07-19: [`BUG-117` — Dropped-import tests exhausted frame-count wait budgets](../../done/BUG-117-dropped-geometry-reimport-frame-budget-flake.md).
  Both test-local asynchronous completion helpers now use ten-second
  steady-clock deadlines and yield one millisecond between unsuccessful
  polls. Real-worker regressions prove the old `128`-frame ceiling can expire
  while the worker remains pending in `Decoding`, report timeout/cancellation
  phases directly, and retain the exact reimport and ambiguous-PLY assertions.

- Closed 2026-07-19: [`BUG-116` — Sandbox process tests lacked aggregate build dependencies](../../done/BUG-116-sandbox-process-test-aggregate-dependency.md).
  The canonical and Vulkan-only aggregates now build `ExtrinsicSandbox` only
  when the promoted-Vulkan + GLFW process contracts are registered. Both real
  process tests pass, while the ordinary CPU aggregate retains no Sandbox
  executable dependency.

- Closed 2026-07-19: [`BUG-115` — Test-gate routing case baseline drift](../../done/BUG-115-test-gate-routing-case-baseline-drift.md).
  The exact `BUG-106` affected-case baseline now names the four current
  `TaskPlanGraph` contracts plus the stable streaming task-kind contract and
  carries matching `105`/`234` count ratchets. The live 4,154-case aggregate
  and all 19 synthetic fail-closed routing regressions pass.

- Closed 2026-07-18: [`BUG-114` — Release architecture SLOs used mismatched metrics and uncalibrated budgets](../../done/BUG-114-ci-release-architecture-slo-calibration.md).
  The repaired Release contract forces worker-local steals, measures direct
  signal-to-resume latency, removes the false critical-path timing claim, and
  retains four parseable SLO metrics. Five sequential unchanged-SHA hosted
  runs passed without retry or threshold adjustment.

- Closed 2026-07-18: [`BUG-113` — Runtime-world reload contract assumed one-frame asset completion](../../done/BUG-113-runtime-world-reload-assumes-one-frame-completion.md).
  The contract now explicitly completes each submitted CPU load/reload and
  asserts submission separately from completion. The old one-worker case
  failed by iteration 7 under pinned CPU contention; the fixed case passed
  100/100 under the same pressure, all eight owning contracts passed, and all
  15 hosted CI-008 timing samples completed without retry or exclusion.

- Closed 2026-07-17: [`BUG-112` — Clang source coverage is unstable on two production paths](../../done/BUG-112-clang-source-coverage-production-path-instability.md).
  One retained scheduler observation removes a schedule-dependent fallback,
  and one renderer branch now computes its correlated descriptor fields
  without Clang 20's repeated-conditional mapping underflow. Matched atomic
  runs normalized without saturated counters and lost zero regions or branch
  arms.

- Closed 2026-07-17: [`BUG-111` — GitHub artifact finalization can discard passing CI evidence](../../done/BUG-111-github-artifact-finalization-403.md).
  The one failing artifact finalization was isolated to GitHub's intermediary:
  its sibling upload succeeded, and a specific-job rerun at the same SHA passed
  and finalized both artifacts without rebuilding UBSan or unsanitized jobs.
  Required evidence remains fail closed; matching incidents use bounded
  `gh run rerun --job`.

- Closed 2026-07-17: [`BUG-106` — Test-gate capability routing hides CPU coverage](../../done/BUG-106-test-gate-capability-routing-drift.md).
  The corrected graph has one owner per affected case, routes all 4,061
  CPU-selected GoogleTest cases through truthful targets, and isolates the
  real Vulkan readback. Hosted Linux passed 4,062/4,062 selected entries;
  hosted Vulkan retained a three-test operational JUnit with no skips and a
  passing 2.22-second readback.

- Closed 2026-07-17: [`BUG-107` — Backend target graph depends on configure history](../../done/BUG-107-backend-target-graph-configure-history.md).
  Root-owned platform/backend defaults now precede every consumer; explicit
  preset identity and one configure diagnostic expose the requested/resolved
  graph. A real fresh/reconfigure/fresh matrix proves stable target, label, and
  CTest inventories for Null, Vulkan, and Auto configurations. Both preset
  aggregate builds and the 3,830-case default CPU gate passed.

- Closed 2026-07-16: [`BUG-088` — Benchmark smoke hard timeout flakes under host contention](../../done/BUG-088-benchmark-smoke-hard-timeout-host-contention.md).
  The 22-result fixture pair is now explicitly `slow` with a 120-second opt-in
  bound, while the dedicated PR workflow owns a two-minute runner step, strict
  validation, and complete artifact retention. Exact implementation-head docs,
  benchmark, CPU, pr-fast, Vulkan, ASan, and UBSan checks all passed.

- Closed 2026-07-16: [`BUG-105` — Runtime module reader races ECS structural mutation](../../done/BUG-105-runtime-module-ecs-structural-hazard.md).
  Runtime module systems now conservatively declare structural reads because
  their context exposes the live world, while all promoted baseline ECS passes
  that add or remove components declare structural writes through the existing
  FrameGraph token. Deterministic layer-order regressions, the original harness
  repeated 100/100 under sanitizers, the 3,791-test CPU gate, and repaired
  exact-head `pr-fast` all passed.

- Closed 2026-07-16: [`BUG-104` — Kernel-convergence regression asserts a retired snapshot](../../done/BUG-104-kernel-convergence-regression-stale-snapshot.md).
  The repository-snapshot regression then moved to the policy's `42/21`, two
  export imports, 31 getter names, and debt-free diagnostic. ADR-0027 later
  reclassified `Runtime.WorldHandle` as substrate and updated only that live
  expectation to `42/20`; the retired BUG-104 evidence remains historical.

- Closed 2026-07-16: [`BUG-081` — Warm-configure CI budget still flakes on hosted-runner variance](../../done/BUG-081-warm-configure-budget-runner-variance.md).
  Seven configure guards now share the finite 40-second budget derived from
  the slowest contemporary hosted-context p95 plus a declared 25% margin and
  five-second rounding. Exact cache keys, images, raw samples, and inactive
  self-hosted transfer policy remain auditable; the wrapper still hard-fails
  synthetic exact-hit overruns, and every repaired-head PR context reached
  compilation.

- Closed 2026-07-16: [`BUG-103` — Render-graph lifetime test culls its history chain](../../done/BUG-103-rendergraph-lifetime-test-culls-history-chain.md).
  The history read is now connected to the live present root through the
  existing dependency contract, preserving current execution-rank lifetimes
  and pass culling while pinning deterministic order `{0, 1, 2}`.

- Closed 2026-07-16: [`BUG-102` — Object-space bake layering test asserts pre-ratchet import placement](../../done/BUG-102-object-space-normal-bake-layering-test-import-placement.md).
  The source-layering contract now recognizes RUNTIME-178's implementation-unit
  CPU queue import while preserving every Engine-side GPU queue exclusion and
  the interface convergence ratchet.

- Closed 2026-07-16: [`BUG-101` — Fast-staged UV edge grouping is quadratic](../../done/BUG-101-fast-staged-uv-edge-grouping-quadratic.md).
  Normalized edge-key lookup now preserves deterministic first-seen ordering
  and reuses source groups for seam recording. Declared baseline comparison
  records exact output parity and scoped local sanitized scaling evidence;
  generated runtime coverage proves bounded enrichment and close.

- Closed 2026-07-16: [`BUG-100` — Manual geometry import blocks the Sandbox frame loop](../../done/BUG-100-manual-geometry-import-blocks-frame-loop.md).
  Every frame-driven payload now uses the existing queued import lane, with
  worker-only decode, bounded main-thread apply, fail-closed cancellation, and
  shutdown cancellation before policy teardown.

- Closed 2026-07-16: [`BUG-099` — Binary PLY point-cloud import rejects face-list elements](../../done/BUG-099-binary-ply-pointcloud-skips-face-lists.md).
  The binary point-cloud reader now safely consumes unrelated scalar/list rows
  in either endian order while retaining strict vertex-list rejection and
  truncation/overflow checks.

- Closed 2026-07-16: [`BUG-098` — Frame clock samples an incomplete frame delta](../../done/BUG-098-frame-clock-samples-incomplete-frame-delta.md).
  Runtime consumers now receive the bounded previous completed-frame duration,
  restoring real production-delay ImGui hover timing without changing the
  minimized-window sleep contract.

- Closed 2026-07-16: [`BUG-094` — Model-scene import drops node semantics and standard selection behavior](../../done/BUG-094-model-scene-node-semantics-selection.md).
  CPU decode now retains active-scene hierarchy, transforms, and shared
  primitive instances; runtime materializes node and selectable primitive-leaf
  entities through the canonical authoring/completion path. A no-skip promoted
  Vulkan smoke proves transformed imported geometry visible and click-pickable.

- Closed 2026-07-16: [`BUG-093` — File / Import prerequisite gating and disabled-reason tooltips](../../done/BUG-093-file-import-prerequisite-gating-tooltips.md).
  One runtime evaluator now owns route, promoted-importer, and payload-hint
  readiness for both presentation and dispatch. The real Null-window File /
  Import workflow exposes runtime-owned disabled reasons on hover and prevents
  invalid requests from reaching the import callback.

- Closed 2026-07-16: [`BUG-083` — Vulkan Sandbox shutdown reports driver and DBus leaks under LeakSanitizer](../../done/BUG-083-vulkan-sandbox-shutdown-lsan-leaks.md).
  The exact NVIDIA report is partitioned across three diagnosed external
  retention call paths. A no-skip five-frame process contract applies only
  those entries, validates renderer completion plus final device operation,
  and first proves an unrelated 4,096-byte engine leak remains visible;
  general test binaries embed no suppression.

- Closed 2026-07-16: [`BUG-082` — GLFW X11 input-method initialization leaks under LeakSanitizer](../../done/BUG-082-glfw-x11-input-method-lsan-leak.md).
  The unchanged GLFW 3.4/Xlib shutdown path reaches input-method unregister and
  close before normal exit. A standalone, unsuppressed live-X11 process now
  proves process-static teardown calls `glfwTerminate()` exactly once and
  detects a named 4,096-byte synthetic leak, without production lifetime or
  sanitizer-suppression changes.

- Closed 2026-07-16: [`BUG-092` — Scene lifecycle async wait exhausts its frame budget under delayed I/O](../../done/BUG-092-scene-lifecycle-async-wait-frame-budget.md).
  The test-local helper now uses a ten-second steady-clock budget, yields one
  millisecond after unsuccessful polls, and reports explicit success/timeout
  state. A 257-call regression pins the retired frame ceiling; repeated scene
  I/O and a five-second injected worker-write delay pass without production
  runtime changes.

- Closed 2026-07-16: [`BUG-090` — Async-work layering test asserts stale shutdown call spelling](../../done/BUG-090-async-work-layering-test-stale-shutdown-owner.md).
  The source contract now recognizes `ShutdownHooks::AsyncWork` delegation
  without changing production shutdown behavior or weakening Engine/service
  ownership checks. The exact regression passed 1/1 and all 24 layering
  contracts passed.

- Closed 2026-07-16: [`BUG-089` — Root-hygiene strict mode rejects canonical and ignored state](../../done/BUG-089-root-hygiene-rejects-canonical-and-ignored-state.md).
  Root ownership now comes from one repository policy shared by both checker
  entrypoints. Exact `ara/` tracking and named `imgui.ini`/`.ruff_cache/`
  local state pass, while unknown roots, missing roots, malformed or broad
  policy, and global Git-ignore hiding fail closed. Twelve focused regressions
  and the strict repository structural checks passed.

- Closed 2026-07-16: [`BUG-087` — Documented task-validator root silently validates zero tasks](../../done/BUG-087-task-validator-documented-root-silent-noop.md).
  The canonical invocation now names the `tasks` root, strict mode rejects
  zero-file discovery with every searched lifecycle directory, and a CI-run
  tooling regression pins both the real repository invocation and an empty
  task root. Task, mirror, link, and workflow-policy checks passed.

- Closed 2026-07-15: [`BUG-086` — ImGui adapter omits the vertex-offset renderer capability](../../done/BUG-086-imgui-adapter-omits-vtx-offset-capability.md).
  The runtime adapter now advertises `RendererHasVtxOffset`, and the existing
  pointer-free overlay/upload/pass path preserves each non-zero command base
  vertex. A generated draw list above 65,535 vertices and a dense-mesh live
  Vulkan replay completed without the former Dear ImGui assertion.

- Closed 2026-07-15: [`BUG-085` — ImGui overlay drops draw-command clip rectangles](../../done/BUG-085-imgui-overlay-drops-command-clip-rectangles.md).
  Runtime now converts finite Dear ImGui clip rectangles to framebuffer
  scissors, graphics preserves and validates them through upload, and
  `Pass.ImGui` applies the renderer-convention Y transform before each draw.
  CPU command-order contracts and the live Vulkan `UI-036` replay verified
  that checker/grid content remains inside its UV child pane.

- Closed 2026-07-14: [`BUG-084` — TransformSyncSystem contract test uses an unqualified test namespace](../../archive/BUG-084-transform-sync-test-mock-device-namespace.md).
  The two mock-device declarations now name their existing
  `Extrinsic::Tests` namespace, restoring the sanitizer-enabled graphics CPU
  contract and aggregate builds. The default CPU-supported gate passed
  3,698/3,698; no production source changed.

- Closed 2026-07-13: [`BUG-074` — Orphaned GpuAssetCache slot causes per-entity bake retry livelock](../../archive/BUG-074-object-space-normal-bake-orphaned-cache-slot-livelock.md).
  Both post-open failure paths now retire only the exact GPU-produced texture
  generation they own, so cleanup cannot destroy a replacement or recreate a
  removed slot. After forced record and ready-frame failures, an explicit
  second schedule succeeds immediately; the six-test causal selection passed
  100 repetitions and the complete 3,692-test default CPU gate passed.

- Closed 2026-07-13: [`BUG-064` — ci-vulkan FramePacingDiagnosticCapture cannot run headless](../../archive/BUG-064-ci-vulkan-framepacing-headless-display.md).
  The strict capture now runs under isolated Xvfb with Mesa lavapipe rather
  than producing a zero-frame report or taking a capability skip. Three
  sequential hosted runs at exact head `7e735868` —
  [29277091536](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29277091536),
  [29278614647](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29278614647),
  and [29280699135](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29280699135) —
  passed the capture, broader `gpu;vulkan` batch, strict timing-result
  validation, and artifact upload.

- Closed 2026-07-13: [`BUG-067` — JobService completion state lost-update race](../../archive/BUG-067-jobservice-completion-state-lost-update-race.md).
  Production fix `ce1f590c` stores `AwaitingGate` under the completion-queue
  lock before insertion. A real-service condition-variable interlock now forces
  the drain/worker-return window without sleeps; restoring the old ordering
  deterministically reproduces the terminal-state clobber and phantom stats,
  while the fixed path passes 100 repetitions and the 3,679-test default CPU
  gate.

- Closed 2026-07-13: [`BUG-073` — Object-space normal bake may be sampled before its GPU write completes](../../archive/BUG-073-object-space-normal-bake-read-before-gpu-write.md).
  Ready-frame accounting now has a named `issueFrame + FramesInFlight`
  contract. A deterministic three-frame-in-flight regression proves the cache
  remains uploading and the material remains unbound through issue+1/+2, then
  promotes and binds at issue+3. The existing graphics Vulkan bake/readback
  passed on an RTX 3050; retired `RUNTIME-129` now supplies the runtime
  `Operational` closure.

- Closed 2026-07-13: [`BUG-071` — Sim-systems registered during OnResolve bypass FinalizeForBoot](../../archive/BUG-071-onresolve-sim-systems-bypass-finalizeforboot.md).
  The module schedule now remains mutable through every resolve callback and
  finalizes the complete register-plus-resolve contribution set before boot
  returns. A real-engine cross-phase duplicate regression fails five of five
  times on the exact pre-fix parent and passes on the production fix, while the
  existing schedule contracts pin cycle and unprovided-signal rejection.

- Closed 2026-07-13: [`BUG-076` — AsyncWorkService::ShutdownAndDrain does not drain the derived job registry](../../archive/BUG-076-asyncworkservice-shutdown-skips-derived-job-registry.md).
  Shutdown now joins executor work, drains and applies ready derived results,
  then cancels every non-terminal survivor. Ready and unreadied readback
  regressions each pass 100 repetitions under ASan/UBSan, and the complete CPU
  gate passes.

- Closed 2026-07-13: [`BUG-075` — A world can be made active while its destroy is pending](../../archive/BUG-075-worldregistry-activate-while-destroy-pending.md).
  Destruction now wins over activation: direct activation rejects pending or
  announced destruction, and Maintenance drops a queued activation whose target
  stopped being live. Two deferred-ordering regressions and the complete CPU
  gate pass under ASan/UBSan.

- Closed 2026-07-13: [`BUG-068` — AssetModelSceneHandoff not rebound on active-world change](../../archive/BUG-068-asset-scene-handoff-not-rebound-on-active-world-change.md).
  Active-world maintenance now rebuilds asset/import scene borrowers during
  the switch pass, before the previous registry can retire. A real-Engine ASan
  regression failed with the rebind removed, then passed 50 repetitions and
  the complete CPU gate after restoration.

- Closed 2026-07-13: [`BUG-079` — CoreTasks abandoned wait continuation leaks coroutine frame](../../archive/BUG-079-coretasks-abandoned-wait-continuation-leak.md).
  Wait-token release and scheduler shutdown now transfer parked single-use
  continuation handles under the wait mutex and destroy their frames after
  unlocking. Deterministic destructor sentinels failed before the fix, then
  passed 100 repetitions each under ASan/UBSan and the complete CPU gate.

- Closed 2026-07-10: [`BUG-080` — UV-atlas promotion smoke flakes on one-sided scheduler stalls](../../archive/BUG-080-uv-atlas-promotion-smoke-timing-flake.md).
  The promotion gate now times five alternating fast-staged/xatlas pairs per
  fixture and gates on their median ratio while retaining every raw sample.
  Twenty-five loaded-host runs, strict result validation, and the complete
  default CPU gate pass with the stable benchmark ID and 1.0/1.25 thresholds
  unchanged.

- Closed 2026-07-10: [`BUG-070` — RuntimeModule schedule dropped BUG-066 fail-closed guards](../../archive/BUG-070-runtime-module-schedule-failclosed-guards-regressed.md).
  Schedule finalization again returns deterministic errors for duplicate
  identities, cycles, and unprovided signals. Direct contracts pin each error,
  and a real-engine death test proves invalid duplicates terminate boot before
  any fixed-step pass can execute.

- Closed 2026-07-10: [`BUG-072` — Declarative sim-system signal fields create no per-tick FrameGraph edge](../../archive/BUG-072-declarative-sim-signal-fields-no-per-tick-edge.md).
  `WaitForSignals` and `SignalLabels` now materialize as named edges in every
  fixed-step `FrameGraph`. A direct schedule regression registers the consumer
  first, enables parallel execution on both systems, and proves the compiled
  graph and execution still order producer before consumer.

- Closed 2026-07-10: [`BUG-069` — RuntimeModule sim-systems scheduled before the baseline ECS bundle](../../archive/BUG-069-runtime-module-systems-scheduled-before-ecs-bundle.md).
  Runtime now registers the promoted ECS bundle before module sim systems,
  accepts its external signal labels at boot, and materializes declarative
  waits per tick. A real-engine regression proves a module waiting on
  `TransformUpdate` observes the current substep's `WorldMatrix`. `BUG-072`
  subsequently closed the durable signal-unification and parallel-pass audit.

- Closed 2026-07-10: [`BUG-077` — Architecture backlog index links retired ARCH tasks](../../archive/BUG-077-architecture-backlog-index-links-retired-arch-tasks.md).
  Commit `09183ea1` promoted the `Retired seam tasks` lead-in to a recognized
  history heading, so all seven `ARCH-007`..`ARCH-013` links remain available
  without being classified as active backlog. The strict task-state-link
  validator passes with zero findings.

- Closed 2026-07-10: [`BUG-078` — CoreTasks CounterEvent rearm can race coroutine destruction](../../archive/BUG-078-coretasks-counterevent-rearm-uaf.md).
  Detached task frames now self-destroy at final suspend, and scheduler workers
  never inspect or destroy a handle after `resume()` returns. A deterministic
  unit regression forces another worker to resume and destroy the frame before
  the original `await_suspend()` unwinds; its destructor sentinel passed 100
  repetitions under the sanitizer-enabled `ci` preset.

- Closed 2026-07-09: [`BUG-063` — Streaming-import contract tests flaky on main](../../archive/BUG-063-streaming-import-contract-tests-flaky-on-main.md).
  Three parallel format-coverage CTest processes shared
  `assetio004_triangle.bin`; the fast representative test could remove it while
  the two streaming decoders still needed it, producing terminal
  `AssetDecodeFailed` results. Each fixture now owns a distinct matching glTF
  buffer URI/path; the exact three-process repro and broader repeated contract
  coverage pass.

- Closed 2026-07-09: [`BUG-066` — RuntimeModule system order followed module registration order](../../archive/BUG-066-runtime-module-system-registration-order.md).
  `ModuleRegistrationSink` now canonicalizes unique system pass names under
  explicit named signal edges before appending passes to the sequential-hazard
  FrameGraph; duplicate names and signal cycles fail closed. The reversed
  registration regressions pass, and the default CPU gate passed 3635/3635.

- Closed 2026-07-08: [`BUG-062` — Warm-configure CI budget flakes on shared-runner variance](../../archive/BUG-062-warm-configure-budget-flaky-runner-variance.md).
  The 10 s warm-cache configure budget was calibrated at the runner median and
  killed five merge-gating workflows across three PR #1010 heads (including a
  markdown-only diff) before any build step ran. Budget raised to 20 s in all
  seven invocations across six workflows; guard semantics and telemetry
  unchanged. Three consecutive PR #1010 CI rounds completed every configure
  step with zero budget kills.

- Closed 2026-07-06: [`BUG-056` — ExtrinsicSandbox default Vulkan validation gate fallback](../../archive/BUG-056-extrinsic-sandbox-default-vulkan-validation-gate.md). The default deferred GBuffer fragment now consumes the full `default_debug_surface.vert` interface and resolves visualization color from the shared config stream, eliminating the SPIR-V interface warnings that blocked promoted Vulkan readiness. The frame-pacing report now records final `IDevice::IsOperational()`, and the validator fails shader-interface warnings or a non-operational `BarrierValidationFailed` result while still allowing documented environment capability skips. The selected promoted Vulkan sandbox/ImGui/frame-pacing envelope passes 18/18.

- Closed 2026-07-04: [`BUG-055` — TaskGraph::Execute / CounterEvent latch-destruction race](../../archive/BUG-055-taskgraph-counterevent-latch-destruction-race.md). Parallel `TaskGraph::Execute()` now keeps completion state alive through shared ownership by the caller and dispatched worker closures, stores completion callbacks on that state instead of stack captures, and hardens `CounterEvent::Signal()` so publishing zero is its last event-member access. The focused `CoreTaskGraph` repeat gate passed 50/50 under the sanitizer-enabled `ci` preset, and the default CPU gate passed 3476/3476.

- Closed 2026-07-02: [`BUG-054` — Sandbox window close shutdown ordering](../../archive/BUG-054-sandbox-window-close-shutdown.md). Sandbox window close requests now emit an `[INFO]` runtime breadcrumb, stop `Engine::Run()`, and keep runtime-owned K-Means GPU job resources alive until after the shutdown device-idle wait before renderer/device teardown.

- Closed 2026-07-02: [`BUG-053` — Sandbox K-Means GPU backend queue](../../archive/BUG-053-sandbox-kmeans-gpu-backend-queue.md). Sandbox Vulkan K-Means requests now submit to a runtime-owned frame-driven GPU queue instead of the synchronous CPU fallback seam; completions publish the same label/color properties as CPU K-Means while device-unavailable cases still report honest fallback telemetry.

- Closed 2026-07-02: [`BUG-052` — Sandbox selection and visualization regressions](../../archive/BUG-052-sandbox-selection-visualization-regressions.md). Selection outline frames now avoid primitive picking/readback work unless a click-pick request is pending, visualization override materials stay lit by default so normals continue shading scalar/label colors, and runtime auto property-buffer extraction covers mesh, graph, and point-cloud scalar/color domains with fail-closed diagnostics.

- Closed 2026-06-24: [`BUG-046` — Flaky `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering`](../../archive/BUG-046-flaky-coretaskgraph-mainthread-ready-queue-ordering.md). `TaskGraph::Execute()` now batches simultaneously-ready main-thread successors under one ready-queue lock before the executor can drain them, so priority/cost ordering is applied to the full batch. The regression no longer relies on the fixed `40ms` `WorkerBlocker` sleep, preserved the `[HighHeavyMain, HighMain, LowMain]` assertions, passed 50/50 under `--repeat until-fail`, and the default CPU-supported gate passed 3024/3024.

- Closed 2026-06-21 (retired from backlog 2026-06-22): [`BUG-049` — GpuWorld geometry rebind lacks upload-to-read barriers](../../archive/BUG-049-gpuworld-geometry-rebind-upload-barriers.md). `GpuWorld` now tracks one-shot pending upload barriers for direct buffer writes, renderer drains them before consumers, and focused geometry-rebind plus dirty-extraction coverage passed during the 2026-06-22 backlog audit.

- Closed 2026-06-21 (retired from backlog 2026-06-22): [`BUG-048` — Direct mesh post-process overwrites recomputed normals](../../archive/BUG-048-direct-mesh-postprocess-overwrites-recomputed-normals.md). Direct mesh post-process apply now preserves count-matched current `v:normal` values so editor-authored normals survive deferred materialization, with focused sandbox editor regressions passing during the 2026-06-22 backlog audit.

- Closed 2026-06-21 (retired from backlog 2026-06-22): [`BUG-047` — Surface normal texture overrides vertex-normal shading](../../archive/BUG-047-surface-normal-texture-overrides-vertex-normals.md). Promoted surface shader contracts now use packed vertex normals for current shading and assert absence of `mat.NormalID` / `normalTex` sampling; focused renderer lifecycle coverage passed during the 2026-06-22 backlog audit.

- Closed 2026-06-22: [`BUG-051` — Mesh color visualization lacks automatic property-buffer extraction](../../archive/BUG-051-mesh-color-visualization-property-buffer.md). Runtime extraction now auto-emits mesh `glm::vec4` color property-buffer packets from mesh `GeometrySources` for per-element color-buffer visualizations, and graphics sync forwards the selected vertex/face/edge domain into `GpuEntityConfig::VisDomain`.

- Closed 2026-06-22: [`BUG-050` — Direct mesh first upload lacks computed normals](../../archive/BUG-050-direct-mesh-first-upload-normals.md). Geometry-only runtime mesh materialization now publishes explicit or area-weighted fallback `v:normal` data before the first ECS/render extraction upload for direct mesh imports and progressive raw model-scene primitives.

- Closed 2026-06-16: [`BUG-044` — Runtime mesh import blocks on derived post-processing](../../archive/BUG-044-runtime-import-postprocess-queue.md). Direct mesh import now publishes decoded raw geometry before derived missing-normal, UV-atlas, and generated-texture work. The derived work runs through `Runtime.StreamingExecutor`, applies back to the same entity, stamps geometry dirty tags, and registers the generated normal material binding after the deferred result is ready.

- Closed 2026-06-14: [`BUG-043` — Dropped OBJ without UVs loads but is invisible](../../archive/BUG-043-dropped-obj-missing-uvs-invisible.md). Runtime mesh materialization now preserves authored `v:texcoord`; after `ASSETIO-008`, missing or invalid source UVs are replaced by generated xatlas-backed atlas UVs before ECS population and generated texture bakes. Direct OBJ imports without `vt` lines upload surface geometry under CPU/null extraction instead of reporting `MeshGeometryMissingTexcoords`.

- Closed 2026-06-12: [`BUG-041` — Asset mesh vertex normals are lost during runtime materialization](../../archive/BUG-041-asset-mesh-vertex-normals.md). Runtime mesh materialization now copies explicit decoded `v:normal` vectors, computes deterministic area-weighted fallback normals when source normals are absent, and applies the shared path to direct mesh imports and model-scene primitive handoff; surface mesh U/V packing is now owned by `RUNTIME-108` and carries texture coordinates only.

- Closed 2026-06-12: [`BUG-040` — Orbit camera vertical drag sign](../../archive/BUG-040-orbit-camera-vertical-drag-sign.md). Orbit pitch drag now uses `+yDelta` in the quaternion trackball update, so mouse-up moves the camera above the target and mouse-down moves it below while keeping target centering, yaw, cross-pole rotation, focus, and other camera-controller coverage passing.

- Closed 2026-06-12: [`BUG-039` — Orbit camera rotation lock](../../archive/BUG-039-orbit-camera-rotation-lock.md). Promoted orbit now matches the legacy trackball model: seed forward/up become accumulated orientation state, drag deltas rotate around the current camera-local up/right axes, view up is no longer fixed world-up, and the focused regression proves a vertical drag can cross the pitch pole and invert camera up while existing orbit/focus/controller coverage still passes.

- Closed 2026-06-12: [`BUG-038` — Dropped file imports fail silently in the sandbox](../../archive/BUG-038-sandbox-dropped-file-diagnostics.md). Runtime now logs file-drop receipt, per-path routing/queue decisions, and shared import completion. Focused contract coverage pins a missing OBJ drop producing receipt/queue/failure logs plus a failed `RuntimeAssetImportEvent`, while existing drop/import coverage keeps valid OBJ/OFF/materialization paths covered.

- Closed 2026-06-12: [`BUG-035` — Vulkan slot-recycling smoke](../../archive/BUG-035-vulkan-slot-recycling-smoke.md). Added an opt-in `gpu;vulkan` smoke that destroys buffer/texture resources, advances the real promoted Vulkan frame loop past the retirement window, and observes the destroyed slots return through public handle reuse with bumped generations. This upgrades BUG-034's Vulkan proof to `Operational`.

- Closed 2026-06-12: [`BUG-034` — VulkanDevice ResourcePool reclamation](../../archive/BUG-034-vulkan-resource-pool-reclamation.md). `VulkanDevice` now runs resource-pool deletion processing for buffer/image/sampler/pipeline pools from the frame loop, including fail-closed `EndFrame()` exits, while keeping Vulkan-object destruction in the existing deferred deletion queue. A CPU Null-device contract pins slot reuse/generation bump behavior.

- Closed 2026-06-12: [`BUG-033` — Mesh IO untrusted header counts](../../archive/BUG-033-mesh-io-untrusted-header-counts.md). OFF/PLY import now bounds untrusted header counts against available payload before reserve, uses overflow-safe byte-count checks, rejects non-integral and negative PLY list counts, and fails closed on degenerate OFF face rows. Malformed-input regressions cover huge ASCII/binary counts and valid comment-before-row behavior.

- Closed 2026-06-12: [`BUG-032` — Triangle edge and point rendering is invisible on Vulkan](../../archive/BUG-032-triangle-edge-point-vulkan-rendering.md). The root cause was a C++/GLSL `GpuGeometryRecord` stride mismatch plus shader-side double application of vertex offsets and too-small default screen-space point size. Runtime sidecars, shader indexing, point sizing, and readback diagnostics now prove reference-triangle edge/point lanes on Vulkan.

- Closed 2026-06-12: [`BUG-031` — Benchmark smoke missing from `IntrinsicTests`](../../archive/BUG-031-benchmark-smoke-not-in-intrinsictests-aggregate.md). The current tree registers `IntrinsicBenchmarkSmoke` through the shared aggregate target path; `cmake --build --preset ci --target IntrinsicTests` now builds the smoke runner and the focused benchmark CTest pair passes.

- Closed 2026-06-12: [`BUG-030` — Headless `Engine::Run()` tests red-gate](../../archive/BUG-030-headless-engine-run-tests-red-gate.md). Guarded live-window `Engine::Run()` tests with the house `ShouldClose() -> GTEST_SKIP()` pattern and documented the rule in `tests/README.md`; the broader headless execution follow-up is retired by [`RUNTIME-107`](../../archive/RUNTIME-107-headless-engine-loop-coverage.md).

- Closed 2026-06-12: [`BUG-029` — Ray/AABB slab NaN poisoning](../../archive/BUG-029-ray-aabb-slab-nan-poisoning.md). Ray/AABB overlap and raycast now use NaN-free slab intervals for axis-parallel/on-boundary rays, `RayCast(Ray, Sphere)` uses a deterministic finite center-origin fallback normal, and BVH/query regressions pin boundary-coincident traversal.

- Closed 2026-06-11: [`BUG-028` — Mesh primitive view UI toggles do not render](../../archive/BUG-028-mesh-primitive-view-ui-rendering.md). The promoted mesh primitive-view implementation is runtime sidecar state, not legacy ECS `MeshEdgeView` / `MeshVertexView` components. BUG-028 fixed the sidecar mechanics through the then-current `MeshPrimitiveViewSettings` command path: vertex mode/radius, halfedge-derived wireframe when explicit edge rows are absent, retained point `GpuEntityConfig::PointMode` / `PointSize`, and flat-circle / impostor-sphere / normal-aligned surfel shader modes. RUNTIME-106 later made `RenderEdges` / `RenderPoints` component presence the authoritative toggle while retaining the same sidecar implementation. Focused CPU/null coverage proves UI command routing, edge/vertex sidecar extraction, imported OBJ mesh primitive views, point config propagation, and shader compilation.

- Closed 2026-06-11: [`BUG-026B` — Vulkan click-pick readback smoke (entity id + depth round trip)](../../archive/BUG-026B-vulkan-click-pick-readback-smoke.md). The opt-in `gpu;vulkan` smoke `ClickPickReadbackSelectsReferenceTriangleAndBackgroundClears` waits for the promoted Vulkan device to become operational, submits a real center-pixel click pick against `ReferenceTriangle`, verifies the GPU readback selects the entity and refines a mesh face with depth-derived cursor data on the triangle plane, then submits a far-background click and verifies the no-hit clear. Passed on NVIDIA RTX 3050 / driver 590.48.01, upgrading the BUG-026 fix to `Operational`.

- Closed 2026-06-11: [`BUG-027` — Sandbox drag/drop, close, and mesh primitive-view regression](../../archive/BUG-027-sandbox-dragdrop-close-mesh-views.md). Platform `WindowCloseEvent` now requests engine exit, `Engine::RunFrame()` aborts immediately after `PollEvents()` observes close before entering ImGui/render work, and standalone geometry materialization records/selects the imported entity after direct and dropped imports. Drag/drop platform-event regression coverage imports OBJ and OFF meshes through the runtime event handler, verifies the imported mesh becomes the active selection, drives the promoted mesh primitive-view command surface, and proves edge/vertex view uploads through `RenderExtractionCache`; frame-loop/layering regressions pin the live close-button timing.

- Closed 2026-06-10: [`BUG-026` — Viewport click selection dead: render-id zero collision, UINT clear punning, and missing depth readback](../../archive/BUG-026-click-pick-readback-entity-zero-and-depth.md). Clicking selected nothing because the raw-entt render id of the first registry entity (the default triangle) collided with the `EntityId == 0` background sentinel, and the light-blue float clear bit-punned the R32_UINT background into a phantom-hit garbage id. Render ids are now `entt handle + 1` (0 reserved, owned by `StableEntityLookup::ToRenderId`), the ID targets clear to zero with format-aware Vulkan clear conversion, and the picking readback gained the designed `SceneDepth` sample: 16-byte slots, per-`Sequence` camera context replay, `UnprojectPickDepth` world/local cursor reconstruction, and depth-anchored closest face/edge/vertex (mesh), edge/node (graph), and point (cloud) refinement. `Operational` Vulkan click smoke owned by `BUG-026B`.

- Closed 2026-06-10: [`BUG-025` — Geometry contact manifold normals violate the documented A→B convention](../../archive/BUG-025-contact-manifold-normal-convention.md). `EPA_Solver` negated the A−B polytope's closest-face outward normal (already the A→B direction) and the sphere-AABB analytic path computed B→A normals in both branches; both now honor the documented convention with per-pair `ContactManifold.Convention_*` unit tests across analytic, reversed-dispatch, and GJK/EPA fallback paths in both argument orders. The physics-layer orientation guard stays as defense in depth.

- Closed 2026-06-10: [`BUG-024B` — Vulkan pixel-shift smoke for sandbox transform edits](../../archive/BUG-024B-sandbox-transform-edit-vulkan-pixel-shift-smoke.md). The opt-in `gpu;vulkan` smoke `InspectorTransformEditShiftsReferenceTrianglePixels` applies the promoted Inspector transform-edit command mid-run (after the fixed-step phase) and proves the rendered `ReferenceTriangle` moves: the frame center returns to background and the projected shifted sample contains the triangle. Passed on NVIDIA RTX 3050 / driver 590.48.01 (suite 6/6), upgrading the BUG-024 fix to `Operational`.

- Closed 2026-06-10: [`BUG-024` — Sandbox transform UI edits do not move rendered triangle](../../archive/BUG-024-sandbox-transform-edit-rendering.md). Post-fixed-step Inspector/gizmo transform edits were never flushed through `TransformHierarchy`/`BoundsPropagation`/`RenderSync` before render extraction, so the snapshot model matrix stayed stale. `Engine::RunFrame()` now invokes those three existing systems directly after the variable tick, ImGui editor hook, and gizmo drive — before gizmo packet build and extraction — so UI edits reach the rendered model matrix in the same frame. `RUNTIME-203` retired the intermediate one-consumer flush helper without changing that order. Closed at `CPUContracted`; upgraded to `Operational` by `BUG-024B`.

- Closed 2026-06-08: [`BUG-022` — Sandbox reference triangle camera frustum visibility](../../archive/BUG-022-sandbox-reference-triangle-camera-frustum-visibility.md). The reference camera seed and all controller modes now prove the triangle vertices are inside clip space, and promoted triangle-list/backface-culling pipelines use clockwise front-face winding to match the Vulkan Y-flipped camera projection so the centered triangle is not culled.

- Closed 2026-06-08: [`BUG-021` — Sandbox camera scene-table shader wiring](../../archive/BUG-021-sandbox-camera-scene-table-shader-wiring.md). The promoted runtime camera remains controller-backed rather than an ECS camera entity; the renderer now publishes the extracted camera into `GpuSceneTable` before `GpuWorld::SyncFrame()`, active BDA vertex shaders transform through `scene.CameraViewProj`, and focused CPU plus Vulkan smoke coverage proves default triangle/readback/sandbox visibility.

- Closed 2026-06-08: [`BUG-020` — Sandbox reference triangle camera modes](../../archive/BUG-020-sandbox-reference-triangle-camera-modes.md). The default `ReferenceTriangle` now round-trips through the promoted scene-document seam as a mesh-domain authored renderable with stable/selectable identity and white `VisualizationConfig`, and top-down camera seeding now uses the seed focus point so orbit, fly, free-look, and top-down modes keep the triangle centered.

- Closed 2026-06-07: [`BUG-019` — Sandbox selection, camera, and outline regressions](../../archive/BUG-019-sandbox-selection-camera-outline-regressions.md). Selection outline now samples `EntityId` from dedicated frame-sampled descriptor slot 3, promoted Vulkan real bindless texture leases start at slot 4, camera controls accept right- or middle-mouse rotation with visible UI help, and the sandbox selection acceptance test covers the runtime input-to-pick bridge without depending on a concrete platform backend.

- Closed 2026-06-07: [`BUG-018` — Sandbox hierarchy selection Vulkan ID target validation](../../archive/BUG-018-sandbox-hierarchy-selection-vulkan-id-targets.md). Hierarchy selection now keeps the selection-ID producer active without importing picking readback, marks readback-enabled ID targets as transfer sources, and binds `EntityId` explicitly for the outline shader so promoted Vulkan records a visible selected frame without `EntityId` / `PrimitiveId` validation errors.

- Closed 2026-06-07: [`BUG-017` — Sandbox selection click and outline black frame](../../archive/BUG-017-sandbox-selection-click-and-outline-black-frame.md). Viewport left-clicks now submit runtime selection pick requests unless ImGui or a gizmo owns the click, and `SelectionOutlinePass` alpha-blends into the current present source instead of replacing it with an outline-only texture.

- Closed 2026-06-06: [`BUG-016` — ExtrinsicSandbox operational frame reads back black](../../archive/BUG-016-extrinsic-sandbox-operational-frame-black-readback.md). The black readback was caused by two output-stage defects: frame-sampled bridge slot 0 was overwritten by late barrier/ImGui descriptor writes before submission, making the tonemap sample the wrong image, and recipe clear colors were dropped during framegraph compilation. The renderer now owns explicit per-pass frame-sampled bindings, barriers no longer auto-bind slot 0, ImGui no longer clobbers the shared bridge slot, and compiled render-pass attachments preserve the light-blue clear. Focused `gpu;vulkan` smokes passed 20/20; the default CPU gate passed 2787/2787 during retirement verification.

- Closed 2026-06-06: [`BUG-015` — ExtrinsicSandbox clustered Vulkan validation cascade](../../archive/BUG-015-extrinsic-sandbox-clustered-vulkan-validation-cascade.md). Clustered compute shaders moved to the engine BDA convention, record helpers pass buffer device addresses via push constants, `CreateBuffer` guards the validation debug-name function pointer, default-recipe clears are light blue, and graphics-only framegraph profiles collapse async/transfer ownership transfers so the QFOT validation cascade is gone. The promoted Vulkan sandbox reaches an operational frame with visible ImGui after BUG-016's downstream output fix. The remaining ccache/modules vtable hardening is not part of this bug and stays tracked by `HARDEN-073`.

- Closed 2026-06-05: [`BUG-014` — ExtrinsicSandbox ImGui black window regression](../../archive/BUG-014-extrinsic-sandbox-imgui-black-window.md). The black frame was caused by a Vulkan descriptor collision: framegraph bridge slots for DebugView/Present overlapped real bindless texture leases, so `Pass.Present` could overwrite the retained ImGui font-atlas slot. The promoted Vulkan bindless allocator now reserves framegraph bridge slots before real texture leases; BUG-019 expands the reserved range to slots 0..3 and starts real leases at slot 4. The app-default `gpu;vulkan` regression asserts recorded `Present`/`ImGuiPass` plus non-black backbuffer readback with validation enabled.

- Closed 2026-05-29: [`BUG-013` — Default-recipe + minimal-debug backbuffer readback contract tests SEGV under clang-20 modules](../../archive/BUG-013-backbuffer-readback-contract-vtable-segv.md). **Not reproducible on a clean `ci` preset build.** In a freshly-cloned tree the two `ConfiguredHandleRecordsReadbackTripletOnce` cases pass through the default CPU gate (CTest #25/#87, label `contract`; the full `IntrinsicGraphicsContractCpuTests` binary is 225/225). The single module-owned `ICommandContext` vtable shows no cross-TU divergence, and the exact crash site (`CopyTextureToBuffer` dispatched through a base `ICommandContext&` to a non-overriding `MockCommandContext`) executes correctly. The reported SEGV was a stale incremental module-BMI artifact after `cc06edef` added the inline-bodied `BindFrameSampledTexture` virtual — a known clang-20 / C++23-modules hazard that a clean preset rebuild (the authoritative verification per AGENTS.md §7) eliminates. Prevention documented in `src/graphics/rhi/README.md`; no engine/test source changed. Unblocks `GRAPHICS-076E` CPU contract closure.

- Closed 2026-05-28 (record retired 2026-06-06): [`BUG-012` — Default-recipe `vkCmdPipelineBarrier2` SEGV in NVIDIA driver](../../archive/BUG-012-default-recipe-vkcmdpipelinebarrier2-segv-nvidia.md). The default-recipe Vulkan command-stream blocker was resolved under GRAPHICS-076's Slice D graduation on 2026-05-28: synthetic framegraph transient handles were replaced with real per-frame RHI allocations before barrier submission, attachment access scopes are preserved through RHI/Vulkan Sync2 translation, staging uploads moved to dedicated one-shot command buffers, `drawIndirectCount` was enabled, and default-recipe draw passes declare dynamic-rendering attachments. CPU-visible contract coverage (`FrameRecipeContract.DefaultRecipeDoesNotDepthTransitionColorResources`, `…DrawPassesDeclareRenderPassAttachments`, `RHICommandContext.MemoryAccessCombinesAttachmentBitsWithoutTruncation`) pins the barrier classes. On the NVIDIA RTX 3050 / driver 590.48.01 host the GPU smoke gate passed 4/4 and `IsOperational()` flips true within the first frame. The resolved task file lingered in `tasks/backlog/bugs/`; this entry records its retirement to `tasks/done/`.

- Closed 2026-05-17: [`BUG-011` — `docs-validation` rejects `ci-vulkan.yml` as an unexpected workflow file](../../archive/BUG-011-ci-vulkan-workflow-allowlist.md). `tools/ci/check_workflow_names.py::ALLOWED_WORKFLOW_FILES` now includes `ci-vulkan.yml` (mirroring the `nightly-deep.yml` allowed-but-not-required precedent), and `docs/migration/target-repo-layout.md` lists the file in the canonical `.github/workflows/` layout. The GRAPHICS-080-introduced workflow now passes the `ci-docs` row's "Validate workflow file naming policy" step under both default and `--strict` modes.
- Closed 2026-05-14: [`BUG-010` — Minimal recipe present-pass barrier acceptance asserts wrong layout transition](../../archive/BUG-010-minimal-recipe-present-barrier-contract.md). The acceptance test now scans for the first backbuffer barrier with `After == Present` and asserts the canonical `Undefined -> Present` shape, matching the framegraph's imported-backbuffer policy.
- Closed 2026-05-14: [`BUG-009` — Minimal recipe surface pass executes when culling output is unavailable](../../archive/BUG-009-minimal-recipe-surface-pass-culling-prerequisite.md). `RecordMinimalDebugSurfacePass` now also gates on `m_CullingOutputAvailable` so a failed culling-pipeline create skips the recipe's `DrawIndexedIndirectCount` rather than recording against bucket buffers the culling dispatch never wrote.
- Closed 2026-05-14: [`BUG-008` — Vulkan `:Device` partition cannot name `VulkanOperationalInputs` under clang-20](../../archive/BUG-008-vulkan-device-partition-operational-types.md). The operational-status surface is extracted into a `:OperationalStatus` partition that the umbrella and `:Device` partition both re-export; `EvaluateVulkanDeviceOperationalStatus` is a friend of `VulkanDevice` so it can read the private gate inputs without widening `IDevice`. `ExtrinsicBackendsVulkan` and `IntrinsicGraphicsVulkanContractTests` build cleanly under clang-20.
- Closed 2026-05-13: [`BUG-007` — GpuAssetCache uploads remain pending in default CPU gate](../../archive/BUG-007-gpu-asset-cache-default-gate-failures.md). `RHI::ITransferQueue::UploadTextureFullChain(...)` now remains appended after the original upload/poll/collect virtuals, preserving the `IsComplete()` slot used by existing module consumers; the focused `GpuAssetCache`/material-system repro and default CPU CTest gate pass.
- Closed 2026-05-09: [`BUG-002` — CI full build compiles ImGuizmo upstream target without ImGui includes](../../archive/BUG-002-ci-full-build-imguizmo-upstream-target.md). ImGuizmo is populated as source-only and repository consumers use `imguizmo_lib` with the ImGui dependency wired explicitly.
- Closed 2026-05-09: [`BUG-003` — FetchContent cache corruption breaks dependency checkouts during CI retries](../../archive/BUG-003-fetchcontent-cache-corrupts-shared-dependency-checkouts.md). Dependency source trees are validated before reuse and incomplete online caches are removed before repopulation.
- Closed 2026-05-09: [`BUG-004` — Compile-hotspot gate baseline references stale runtime source paths](../../archive/BUG-004-compile-hotspot-baseline-stale-runtime-paths.md). The current baseline uses promoted `src/geometry/` paths only; the retired `src/legacy/` target was removed during the 2026-07-01 legacy sweep.
- Closed 2026-05-09: [`BUG-005` — CI dependent steps report missing artifacts as primary failures](../../archive/BUG-005-ci-dependent-steps-report-missing-artifacts-as-primary-failures.md). CI dependent steps now run explicit prerequisite guards and benchmark validation reports missing result roots as blocked prerequisites.
- Closed 2026-05-09: [`BUG-006` — Mesh-backed graph views abort ShortestPath tests on connectivity type collision](../../archive/BUG-006-shortest-path-mesh-backed-graph-connectivity-view.md). Mesh-backed graph view construction now uses the correct property-set order and graph-specific compatibility connectivity until `GEOM-003` performs the semantic split.
- Closed: the older `BuildDefaultPipelineRecipe(...)` link-failure note is stale in the current tree. The symbol is declared in `Graphics.Pipelines.cppm`, defined in `Graphics.Pipelines.cpp`, and referenced by the runtime graphics tests. Full local link verification is currently blocked in this container because CMake configure stops in GLFW dependency discovery before test targets are generated (`libxrandr` development headers missing).
- Verified: mesh vertex indices are recovered from picked local-space points via KD-tree lookup.
- Verified: mesh edge/face, graph node/edge, and point-cloud point indices are covered by the focused picker regression suite.
- Closed: pick-domain policy now enforces mesh→surface face IDs, graph→edge IDs, and point-cloud→point IDs in `PickingPass`; GPU primitive IDs are authoritative while CPU is refinement-only in `ResolveGpuSubElementPick`.
