---
id: BUG-193
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive cleanup and follow-up tracking; no new performance or capability claim.
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-193 — Investigate GPU pacing variability and watchdog margin

## Goal
Explain variable Vulkan acceptance pacing under controlled host/display conditions
and evaluate the remaining WLOP watchdog margin. BUG-189/191/192 repaired fixture
budget mismatches; they did not establish an engine pacing fix or a speedup.

## Evidence
RUNTIME-244 surface-appearance evidence records display-off low clocks and variable
runs, without a new controlled display-state comparison. RUNTIME-236's combined
LBVH sequence passed after its finite-work fixture deadline correction, but its
WLOP case took about 264 seconds against the unchanged 300-second CTest limit.
Keep these historical observations distinct from a causal diagnosis.

BUG-194 follow-up on 2026-09-14 used the actual dragon and the real keypoint
panel with AsyncWorkModule, ci-vulkan and existing frame-phase diagnostics.
Before and after Show, total frames were roughly one second; presentation
accounted for about 0.65–0.77 seconds, UI for 0.12–0.13 seconds, and renderer
preparation for 0.09 seconds. The Show frame added roughly 0.05 seconds of UI
work, and displaying the scalar added roughly 0.049 seconds of extraction per
frame. These are local diagnostic samples, not matched benchmark evidence or a
confirmed presentation cause. The user reports responsive UI/camera during the
missing saliency display, so this slow-frame probe does not reproduce that
failure. Source/host display-state controls remain necessary here. Full local
logs: `build/analysis/bug194-saliency-followup-2026-09-14/dragon-ui-pacing.log`.

## Acceptance criteria
- [ ] Reproduce serially with no concurrent compilation, fixed source and recorded
  GPU/display/power state; compare display states when supported.
- [ ] Separate presentation pacing, asynchronous job progress and fixture scheduling
  using bounded diagnostics and finite-work/timeout reasons.
- [ ] Repair the confirmed owner and verify finite progress with adequate measured
  margin while preserving pixel, history, backend and sanitizer assertions.

## Verification
Use the supported ci-vulkan preset and exact registered surface-appearance,
distance-ratio and consolidation LBVH cases recorded in the retired bug notes.
Run capability tests serially after compilation, retaining raw diagnostics and
actual backend identity. Do not weaken assertions or infer leak freedom from
cohorts with leak detection disabled; BUG-180 owns that separate investigation.

## Diagnosis — 2026-10-24

### Plan and predictions
P1–P3 are not in `predictions.txt`. They are recorded only in this note, which
was first committed at 02:37 (945c79d15), after the runs, so the artifacts
cannot show that they preceded the runs. The sampler, gdb, unwind and Xephyr
predictions are in `predictions.txt`, which says each was appended before its run.
Host and source: RTX 4090 with NVIDIA 580.159.04 (the retired notes were
measured on an RTX 3050 with 590.48.01), `ci-vulkan`, 2df2a2837 (`origin/main`
599f02ff1 plus the BUG-182 test change). The three registered cases run
serially on `DISPLAY=:1` (`Monitor is On`, session locked), on a quiet host
(1-minute load < 4, no compiler running):
`PointLBVHGpuSmoke.LocalDistanceRatioPublishesAcrossDomains`,
`RuntimeSandboxAcceptanceGpuSmoke.SurfaceAppearanceBakesSelectedPropertyAndRestoresAttributes`,
`PointCloudConsolidationGpuParity.VulkanLbvhWlopAcrossDomainsAndFailures`.
Each runs with its registered command, environment and TIMEOUT. A temporary
probe prints, once per frame, `Engine::GetLastFramePacingDiagnostics()` and
`JobService::Stats()` from the `RuntimeTestModule` frame hook. It is enabled
by an environment variable, gets reverted and is never committed. The probe's
numbers set no budget, timeout or threshold.
- P1, presentation: `present_micros` plus `render_begin_frame_micros`
  (fence wait and acquire) is < 5 % of summed frame time in each case.
  `render_prepare_micros` is the largest phase, as in BUG-228's probe
  (about 69 ms per frame). If presentation exceeds 25 %, display pacing is
  the cause even with the monitor on.
- P2, completion: every case passes within its registered TIMEOUT. WLOP needs
  < 150 s here, compared with 264 s in RUNTIME-236.
- P3, job progress vs fixture scheduling: at least 80 % of operational frames
  have a job in flight (`InFlightJobs > 0`). At most 10 % are fixture gaps,
  meaning no job in flight while the case has not finished. More gaps would
  mean the fixture's per-frame phase stepping costs time.

### Results
Raw logs, the probe diff, the analysis script and the GPU/display states are
in `tasks/evidence/BUG-193/2026-10-24/` (ignored by git). Runs took place
02:16–02:38 CEST with `Monitor is On`, the session locked, and 1-minute
load 1.02–3.03 at the case boundaries (`state-d*.txt`). The state files count
no compiler process, except one from another lane when the `:1`
surface-appearance run started. The GPU went from
P8 (idle) to P0 at 2520 MHz during the runs. Every case selected the RTX 4090
with the NVIDIA 580.159.04 driver.

| Case | Wall `:1` / `:9` | Operational frames | Median frame | Present + begin | `render_prepare` | Frames with a job in flight | Frames with no job (gaps) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| LocalDistanceRatio | 6.16 s / 5.47 s | 26 / 26 | 87 ms | 1.3 % | 55 % | 69 % | 27 % |
| SurfaceAppearance | 20.82 s / 18.66 s | 63 / 63 | 93 ms / 121 ms | 0.8 % / 19.3 % | 38 % | 18 % | 81 % |
| WLOP | 25.55 s / 25.11 s | 264 / 263 | 81 ms | 1.3 % / 2.7 % | 81 % | 97 % | 3 % |

Shares are fractions of summed frame time on `:1`; where two values appear,
the second is `:9`. The clean registered runs without the probe, on `:1`,
passed in 6.26 s, 17.39 s and 25.37 s. The probe was reverted before them:
`build-revert.log` recompiles `tests/support/RuntimeTestModule.cpp` and relinks
the three test executables, and `git grep -E 'BUG193|BUG228' -- src tests`
finds nothing at this branch's HEAD.
- P1 was met on `:1`. Presentation plus acquire is at most 1.3 %, and
  `render_prepare` is the largest phase.
- The Xephyr prediction failed for surface appearance. It predicted, for `:9`,
  frame counts within ±2 of `:1`, wall time within ±15 % and a present share
  below 2 %. Frame counts (26/26, 63/63, 264/263 on `:1`/`:9`) and wall times
  (−11 %, −10 %, −2 % on `:9`) were within range. Present alone was 0.4 % for distance ratio and 1.7 %
  for WLOP, but 18.8 % for surface appearance (19.3 % with begin), whose median
  frame rose from 93 to 121 ms.
- P2 was met. WLOP passed in 25.55 s; its internal bound is 300 s and its
  CTest limit 360 s. Frame counts on `:1` and `:9` agree within one frame.
- P3 was met only by WLOP. In WLOP a job is in flight in 97 % of frames, but
  a worker is running one in only 7.6 %. In 80 % of frames the job is
  `AwaitingApply`. Job progress therefore waits for the per-frame apply and
  drain (BUG-191: eight completions per frame), not for computation. The
  surface-appearance case spends 81 % of its frames with no job in flight.
  Those frames are fixture-scheduled transition and settle frames
  (BUG-192: at least 52). The distance-ratio case has 27 % such gaps.
- The frame time is main-thread CPU work. During WLOP the main thread was
  in state `R` in 4478 of 4515 `/proc` samples (99.2 %), and only one thread
  was runnable in 97.6 % of samples.
- Where `render_prepare` goes (hypothesis): the gdb sampler gave 1 effective
  stack sample. Between its SIGINT stops the inferior did not advance: in
  `gdb-samples3.log` the three stops have the same PC, the same
  `this=0x7bfff55adea0` and the same `__i=15843`, and the probe's frame 4
  reports `prepare_us=30,273,119`. `gdb-samples.log` (3 identical stops) and
  `gdb-samples2.log` (two identical groups of 4) show the same sampler fault.
  The gdb prediction (≥ 60 % of about 12 stacks inside
  `RenderPrepPipeline::Run`) was therefore not tested. The one stack, taken
  after frame 3 of `KeypointResidentPreviewDiscardRepeatPagedParity`, is in
  `GpuWorld::SyncFrame` → `FlushDirtyRuns<GpuGeometryRecord>`, in
  `std::vector<bool>` element access. Code reading: `FlushDirtyRuns` tests
  every dirty bit up to capacity on every frame, 4 × `MaxInstances` (100,000)
  + `MaxGeometryRecords` (65,536) = 465,536 bits, however few are dirty.
  Hypothesis, not measured: this scan is most of the constant 68–70 ms median
  `render_prepare` in this Debug ASan/UBSan build, which would be about 146 ns
  per bit. Its support is the one stack, the code reading and the same median
  `render_prepare` in every case and in the Sandbox (69 ms). The scan itself
  was not timed.
- Ruled out as the prepare cost: ASan slow unwinding. Setting
  `fast_unwind_on_malloc=1` (direct runs, order A B A B) leaves median
  `render_prepare` at 67.8–68.1 ms; it lowers `render_execute` from 7.9 to
  2.4 ms.
- Backtest: WLOP runs 264 frames here. RUNTIME-236 recorded 263.97 s on the
  RTX 3050 host, and BUG-189 recorded `Monitor is Off` there on the same day.
  At BUG-143's ~1 s per display-off frame, 264 frames would take about 264 s.
  This fits only if that host ran the same number of frames, which was not
  checked. The distance-ratio phases do not fit the same simple model:
  the warm phase here is 0.33 s (about 4 frames), versus 9.9 s on that host.
- Margin, as facts and not a budget decision: with the monitor on, WLOP uses
  25.5 s of its 300 s internal bound. At ~1 s per frame with the display off,
  264 frames would need about 267 s, roughly 11 % below that bound. The
  margin therefore depends on display state times frame count. Inference,
  not measured: at ~1 s per display-off frame, the ~68 ms `render_prepare`
  would be a small share of each frame, so its cost would be masked.

### What remains
- Acceptance 1: comparing display states needs DPMS off on the operator's
  display (see BUG-228).
- Acceptance 3: no owner was repaired. The candidates are the
  `GpuWorld::SyncFrame` full-capacity dirty scan (hypothesized monitor-on
  frame cost; the measured ~68 ms `render_prepare` total is an upper bound on
  it, not a measurement of it),
  the display-off presentation (display policy, BUG-228) and frame-paced job
  apply. Which one to take on is a scope decision for the operator.

## Display-off comparison and dirty-scan timing — 2026-10-24 (afternoon)

### Predictions (committed before the runs)
Same branch, host and display procedure as BUG-228's afternoon section.
- W1, `PointCloudConsolidationGpuParity.VulkanLbvhWlopAcrossDomainsAndFailures`
  on `:1` with the monitor off, FIFO (current code): about 264 frames at about
  1 s each, so it passes in 240–300 s, inside the 300 s internal bound, with
  under 60 s of margin. A failure at the 300 s bound is also consistent with
  display pacing; a pass in under 60 s rejects display pacing for this case.
- W2, after a fix that honours `Render.EnableVSync=false`: the same run with
  the monitor off passes in under 40 s, close to the 25.5 s monitor-on time.
- S1, `GpuWorld::SyncFrame` dirty scan: a temporary timer (tagged, reverted)
  around the `FlushDirtyRuns` calls in the keypoint case on `:1`, monitor on.
  The hypothesis predicts that the scan takes at least 50 % of the 68–69 ms
  median `render_prepare` (at least 34 ms per frame). It is rejected if the
  scan takes under 10 % (under 7 ms). Only a confirmed scan cost is changed.

### Results
Logs are in `tasks/evidence/BUG-228/2026-10-24-display-off/` (shared with
BUG-228; same host and states).
- W1 met: WLOP on `:1` with the monitor off and FIFO passed in 261.75 s,
  38 s inside its 300 s internal bound (25.5 s with the monitor on this morning).
  The display-off pacing is consistent with RUNTIME-236's 264 s; the RTX 3050
  host's frame count was never checked.
- S1 confirmed: a temporary timer around the five `FlushDirtyRuns` calls
  (tagged `[DBG-BUG193]`, reverted) gave, over 93 frames of the keypoint case
  on `:1` with the monitor on, a median of 67.6 ms (min 67.2, max 113.3) for
  465,536 dirty bits, about 145 ns per bit. That is the whole 68–69 ms median
  `render_prepare` measured this morning.
- Fix: the five dirty-flag vectors are `std::vector<std::uint8_t>` instead of
  `std::vector<bool>`, and `FlushDirtyRuns` finds run starts and ends with
  `memchr` and clears runs with `memset`. The runs it uploads are unchanged.
  `GraphicsGpuWorld.SyncFrameUploadsContiguousDirtyRunsOnce` pins them (first
  frame uploads all 8 slots; dirty slots 1, 2, 5, 7 give runs (1,2), (5,1),
  (7,1); a clean frame uploads nothing). It passes with the old
  `vector<bool>` code and with the new code, and fails at two assertions when
  the `memset` is removed.
- After both fixes: Sandbox monitor on, FIFO: median `render_prepare` 0.3 ms
  (69.1 ms this morning), median frame 16.6 ms (89.2 ms), now paced by the
  display refresh. WLOP on `:1` with the monitor off passed in 6.41 s
  (W2 met). `LocalDistanceRatio` with the monitor off passed in 3.74 s.
  Surface appearance passed in 71.27 s with the monitor off because its
  Sandbox-default fixture keeps VSync on (see BUG-228).
- CPU gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`, `-j16`): 5942 of 5943
  passed. `PropertySmoothingWorkflows.VariationalFitVariantsAndSolverSwitches`
  timed out at 1-minute load of about 12 and passed alone in 28.52 s.

### Evidence status — 2026-10-24
Predictions for S1, W1 and W2 were committed beforehand (25550e64e); the
other runs listed had none. A committed
prediction does not by itself make a run verification: a run whose numbers
informed a keep or change choice is decision-bearing, with or without one.
- Decision-bearing:
  - S1, the dirty-scan timing (median 67.6 ms). It decided whether to change
    the dirty scan.
  - BUG-228's fix-condition runs and 25 s watchdog trial (listed there).
- Verification only (no other use of the numbers): W1 (WLOP 261.75 s, monitor
  off, FIFO), W2 (WLOP 6.41 s after the fix), the monitor-off
  `LocalDistanceRatio` and surface-appearance runs, the Sandbox monitor-on
  `render_prepare` after the fix, and the CPU gate.
- No experiment pre-flight was run: no frozen packet, no prospective review
  and no HTML report exists for these runs. The operator's pending
  disposition covers every decision-bearing measurement listed above.
