---
id: BUG-228
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive recording of an environment-dependent verification failure; no performance claim.
contract_schema: 1
contracts: []
contract_review: Test display environment and timeout diagnosis only; no catalog engine integration contract is changed.
---
# BUG-228 — Vulkan smoke timeout depends on the display environment

## Goal
Make the supported local Vulkan verification environment explicit and diagnose
why the desktop display stalls small runtime smoke tests.

## Context
On 2026-10-01 the unchanged 30-second CTest limit expired for
`PointLBVHGpuSmoke.KeypointResidentPreviewDiscardRepeatPagedParity` on `DISPLAY=:1`,
including a retry after the sandbox and builds stopped. The same executable and
CTest case passed in 10.06 seconds on an owned Xephyr `:9` server. This comparison
isolates a display-environment dependency; it does not identify the compositor,
Vulkan presentation, or driver as the cause. No timeout was raised.

The [desktop log](../../evidence/BUG-228/2026-10-01-desktop-display.log) records an
interrupted focused run, not a full suite verdict. The
[isolated-display log](../../evidence/BUG-228/2026-10-01-xephyr-display.log) records
the completed single-case comparison. Other timeouts in the earlier broad run
were mixed with build/render load and are not sufficient to attribute a cause.

## Acceptance criteria
- [ ] Reproduce the display dependence and locate the presentation/frame wait responsible.
- [ ] Document or implement the supported display setup without weakening timeout or validation gates.
- [ ] Re-run the affected keypoint, scalar, normals, sampling and resident LOP tests in that environment.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
DISPLAY=:1 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.KeypointResidentPreviewDiscardRepeatPagedParity$'
# In a separate terminal, start an owned display server:
DISPLAY=:1 Xephyr :9 -screen 1280x900 -noreset -ac
# With that server available:
DISPLAY=:9 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.KeypointResidentPreviewDiscardRepeatPagedParity$'
python3 tools/agents/check_task_policy.py --root . --strict
```

## Diagnosis — 2026-10-24

### Predictions
H1 and H2 are not in `predictions.txt`. They are recorded only in this note,
which was first committed at 02:05 (16dd3b5ef), after the runs (01:47–02:03),
so the artifacts cannot show that they preceded the runs. The backtest,
iconify, offscreen, cohort, Sandbox and resident LOP predictions are in
`predictions.txt`, which says each was appended before its run.
Source: `origin/main` 599f02ff1, fresh `ci-vulkan` tree, RTX 4090 with NVIDIA
580.159.04 ICD selected. At 01:46 `DISPLAY=:1` was the operator's X desktop,
DPMS enabled with `Monitor is On`, and at 01:47 the session was locked
(`LockedHint=yes`). `predictions.txt` gives the desktop as 10240x2880; output
modes and refresh rates were not recorded. The GPU was in P8.
Code reading before the runs: `Render.EnableVSync=false` reaches no device.
Nothing calls `IDevice::SetPresentMode`, so `VulkanDevice` keeps
`PresentMode::VSync`, and `ToVkPresentMode` returns `VK_PRESENT_MODE_FIFO_KHR`.
Each frame therefore waits in `vkQueuePresentKHR`
(`VulkanDevice::Present`, timed as `FramePacingDiagnostics::PresentMicros`)
and in the swapchain acquire inside `BeginFrame`.
- H1, display-owned FIFO pacing (the BUG-143 mechanism): presentation on `:1`
  is slow (about 1 s per frame) when the window is not scanned out. Expected
  outcomes: on `:1` the case exceeds the 30 s CTest limit; the probe shows
  `present_micros` + acquire near 1 s per frame and sub-millisecond keypoint
  work per frame. On `:9` the case passes in 10 ± 4 s with presentation
  < 20 ms per frame.
- H1 rejected if `:1` passes in under 20 s with presentation < 20 ms per frame.
  In that case the 2026-10-01 timeout depended on a display state that is not
  present now, and the note records that state.
- H2, job or fixture stall rather than presentation: the frame count on `:1`
  matches `:9` but the wall time is spent outside present/acquire. The
  `Started` 50 s bound in `KeypointResidentApp::Frame` cannot fire under the
  registered 30 s CTest limit, so a stall shows only as a CTest timeout.

### Results
Raw logs, the probe diff and the display states are in
`tasks/evidence/BUG-228/2026-10-24/` (ignored by git). Host at run starts:
1-minute load 0.64–4.24 (`state-*.txt`; 4.24 at the `:9` cohort, 4.10 and 4.01
at the Sandbox runs). Compiler processes were not recorded. This lane's own
incremental builds (`build-probe*.log` 01:49–01:54, `build-bug182.log` 01:56,
about 28 s each) ran between runs; by file times none overlapped a run. Other
lanes' compiles were not recorded.
- Task commands, clean binary: `:1` passed in 10.79 s and `:9` in 10.11 s. The
  display dependence did not reproduce in this state (`Monitor is On`, session
  locked). The H1 prediction for `:1` (more than 30 s) failed. This matches the
  stated alternative: the 2026-10-01 timeout needed a display state that is
  not present now.
- Temporary probe (`probe.diff`, reverted, never committed): each frame prints
  `Engine::GetLastFramePacingDiagnostics()` and
  `GetVulkanFrameLifecycleDiagnosticsSnapshot()`. On both displays the case runs
  92 operational frames. The first paged transaction reaches `ReadyToAccept` at
  frame 47 and the accept finishes at frame 91. A frame takes 80–83 ms, of which
  `render_prepare` is 68–69 ms. Presentation usually takes 0.1–0.5 ms, at most 3.0 ms
  (13 ms in total on `:1`, 32 ms on `:9`), acquire about 20 µs and the fence wait about 13 µs.
  The case's wall time is therefore its fixed frame count multiplied by the
  frame time. Its progress is paced by frames, not by elapsed time.
- H2 is moot in this state. Its observable (same frame count on both
  displays, wall time outside present/acquire) is what these monitor-on runs
  show, but neither display was slow, so there was no slow run to attribute.
- Backtest against 2026-10-01: the degenerate sibling
  `KeypointResident4096IdenticalPointsRetainsDevice` runs 8 operational frames
  and passes in 3.90 s now; its frame loop takes 0.95 s. With 8 frames at about
  1 s each (BUG-143's display-off present), the expected time is about 10.9 s;
  2026-10-01 recorded 10.93 s. The cases that timed out on 2026-10-01
  (`PreviewDiscardRepeatPagedParity`, `SubnormalScores`, `DeletedSlotPreservesBothFronts`)
  are the 92-frame cases. At about 1 s per frame each would need about 92 s.
  These numbers fit H1, but the 2026-10-01 display state was not recorded, so
  this is a fit, not a reproduction.
- H3 (window minimized, frame phases skipped) is rejected for 2026-10-01.
  Iconifying the test's own window on `:1` gives a 30.16 s CTest timeout whose
  last log line matches the 2026-10-01 log, with zero `Frame` calls: minimized
  frames skip every phase. Under H3 the degenerate case would also hang, but it
  passed on 2026-10-01.
- Moving the test's own window outside both outputs did not work; the window
  manager clamped it to (5056, 2816), so that run tells us nothing.
- Where the wait is: `Render.EnableVSync=false` has no effect, so the swapchain
  is always FIFO. `VulkanDevice::Initialize` reads `EnableValidation` but not
  `EnableVSync`, yet `docs/architecture/engine-config.md` lists VSync as a
  boot-only toggle. When presentation is slow, the wait is `vkQueuePresentKHR` in
  `VulkanDevice::Present` (`present_micros`) plus `vkAcquireNextImageKHR` with
  an `UINT64_MAX` timeout in `VulkanDevice::BeginFrame`. BUG-143 measured
  about 900 ms of present plus about 100 ms of acquire per frame with
  `Monitor is Off`. The probe shows both take well under 1 ms while the
  monitor is on.
- Untested expectation: Xephyr's presentation does not depend on the host
  monitors' DPMS state. There was no display-off run on `:9`, and the
  2026-10-01 display state is unknown. `/usr/bin/Xephyr` does contain the DPMS
  extension: its strings include `noDPMSExtension` and the option
  `-dpms disables VESA DPMS monitor control`.
- Measured with the monitor on, Xephyr presentation is slower, presumably its
  copy of each frame into the host window. `ExtrinsicSandbox
  --frame-pacing-report` (32 frames, 1600x900) shows a median `present_micros`
  of 141 µs on `:1` and 28,108 µs on `:9`. That is 0.2 % and 21.5 % of frame
  time; the prediction of < 20 ms on `:9` failed. Median `render_prepare` is
  69 ms on `:1` and 74 ms on `:9`.
- The fixture's 50 s `Started` bound is longer than the registered 30 s
  `TIMEOUT`, so it can never fire. A run slowed by the display ends as a CTest
  kill with no `Step` diagnostic.
- Re-run of the affected tests in both environments, at 2df2a2837 with BUG-182
  merged: 21 cases (`Keypoint*`, `NormalNeighborhoods*`,
  `RUNTIME290/292/293/296/297/298/299`). On `:1` 19 of 21 passed (170.3 s); on
  `:9` 19 of 21 passed (158.9 s). The slowest case took 14.71 s and nothing
  timed out. The same two cases fail on both displays with the same failure
  lines, so the failures do not depend on the display. `predictions.txt`
  predicted the `KeypointResidentSubnormalScores` failure on both displays;
  only the `PlanarZero` failure was not predicted. Failure lines in
  `Test.PointLBVHGpuSmoke.cpp`, the same in `cohort-d1.log` and `cohort-d9.log`:
  `KeypointVulkanComputePlanarZeroScoresAndIsotropicFloor` has 8 at `:1981`
  (`KeypointCount` 5, expected 8) and 8 at `:1989` (keypoint mask differs
  from the reference): 4 of the 8 domains in 2 of the 3 `Phase<3` passes; the
  log names neither the domains nor the phases.
  `KeypointResidentSubnormalScores` has 12 mask mismatches at `:2830` (preview)
  and 12 at `:2849` (after accept), and the saliency L∞ bound fails at both
  `:2833` and `:2855` (7.58e-42 against 1.12e-44). RUNTIME-291's note (its
  worktree) records both cases failing with its `src/` change reverted. These
  failures are separate from this display bug;
  [BUG-248](../../done/BUG-248-keypoint-gpu-parity-smokes-fail-on-main.md) fixed them
  in the keypoint kernel (retired 2026-10-24).
- Resident LOP, built afterwards: both `PointCloudConsolidationGpuParity.ResidentLop*`
  cases passed on both displays: `PreviewDiscardAcceptParityAndRepeatInputIo`
  in 6.00 s on `:1` and 5.29 s on `:9` (limit 30 s), and
  `MultipleProjectionAndReducePagesMatchCpu` in 24.35 s on `:1` and 24.54 s on
  `:9` (limit 120 s).

### What remains
- Reproducing the slow display state on `:1` means switching the operator's
  monitors off (DPMS), which needs the operator's decision. Choosing the
  supported display setup is also a policy decision. The options are in the
  lane report.
- Acceptance item 3 waits for the operator to choose the environment.

## Display-off reproduction — 2026-10-24 (afternoon)

### Predictions (committed before the runs)
The operator allowed switching the monitor off on `DISPLAY=:1` for bounded
runs. Each run is one command, DPMS is forced off before it and forced on by a
shell trap after it; `xset q` and `LockedHint` are recorded before and after.
Source: branch `claude/bug-228-193-display-off-pacing` at 4c30f2b82 (unchanged
`src/`), `ci-vulkan`, `VK_DRIVER_FILES` set to the NVIDIA ICD. Raw logs go to
`tasks/evidence/BUG-228/2026-10-24-display-off/` (ignored by git).
- D1, keypoint case on `:1`, monitor off, FIFO (current code): CTest kills it
  at the 30 s limit, with no `Step` diagnostic (the 50 s bound cannot fire).
  Rejected if it passes in under 20 s.
- D2, the same case on an owned Xephyr `:9` with the host monitor off: it
  passes in 10 ± 4 s, because Xephyr does not page-flip to the host outputs.
  Rejected if it times out or takes more than 20 s.
- D3, `ExtrinsicSandbox --frame-pacing-report` (32 frames) on `:1` with the
  monitor off: median `present_micros` + `render_begin_frame_micros` is at
  least 500 ms per frame (BUG-143: about 900 + 100 ms).
- Fix condition: the device honours `Render.EnableVSync=false` by choosing an
  uncapped present mode (IMMEDIATE when supported). The fix is kept only if,
  after it, D1 with the monitor off passes in under 20 s and the Sandbox with
  `enable_vsync: false` shows median present + begin below 20 ms with the
  monitor off, while the Sandbox with `enable_vsync: true` (FIFO) still shows
  at least 500 ms (control: the present mode, not something else, removes the
  stall). If IMMEDIATE also blocks with the monitor off, the fix is not made.

### Results
Runs 12:58–13:16 CEST, one command per monitor-off period; every state file
shows `Monitor is Off` during the run and `Monitor is On` after it. The
session stayed locked (`LockedHint=yes`), 1-minute load 1.2–4.3, no compiler
running at any run boundary, RTX 4090, NVIDIA 580.159.04. Logs and states:
`tasks/evidence/BUG-228/2026-10-24-display-off/`.
- D1 met: on `:1` with the monitor off the keypoint case was killed by CTest
  at 30.5 s (`Timeout`), with no `Step` diagnostic. It passed in 10.8 s with
  the monitor on this morning. The display dependence is reproduced.
- D2 met: on an owned Xephyr `:9` with the host monitor off it passed in
  9.98 s.
- D3 met: Sandbox, 32 frames, monitor off: median frame 1000 ms, median
  `present_micros` 918 ms (91 % of frame time), `render_begin_frame` 0.19 ms.
  The wait is in `vkQueuePresentKHR` with FIFO, not in acquire.
- Owner: the device ignored `Render.EnableVSync`, so every swapchain was FIFO
  and every present waited for a display that does not scan out. This NVIDIA
  X11 surface reports IMMEDIATE, FIFO and FIFO_RELAXED (mask `0xd`), no MAILBOX.
- Fix: `VulkanDevice::Initialize` maps `EnableVSync=false` to
  `PresentMode::Uncapped` (IMMEDIATE, FIFO fallback) and records
  `VSyncRequested`, `SupportedPresentModeMask` and `SwapchainPresentMode` in
  the bootstrap diagnostics, plus one log line. The `gpu;vulkan` fixtures
  already set `EnableVSync=false`; none was changed.
- Fix condition met. After the fix, monitor off: the keypoint case passed in
  3.74 s; Sandbox with `enable_vsync: false` had a median frame of 4.8 ms and
  median present + begin of 0.17 ms; the control, Sandbox with the default
  `enable_vsync: true` (FIFO), still had a 999.9 ms median frame with 991.6 ms
  present. The present mode alone removes the stall.
- Regression: `DefaultRecipeSurfaceGpuSmoke.VSyncOffSelectsImmediatePresentWhenSupported`
  failed with the assignment commented out (actual mode 2 = FIFO, expected 0)
  and passes with it.
- Stall bound: `KeypointResidentApp`'s `Started` bound is now 20 s, below the
  30 s CTest `TIMEOUT`, and its message names the step and frame. With FIFO
  and the monitor off, an intermediate 25 s bound fired at 28.66 s
  (`Resident keypoint timeout after 25 s step=1 frame=25`), which left only
  1.3 s to the kill; that is why it is 20 s. The fixture's frame count is
  unchanged.
- Acceptance item 3, after the fix, 23 cases (this morning's 21 plus both
  `ResidentLop*` cases), `:1`: all 23 passed with the monitor on (92.1 s) and
  all 23 with the monitor off (91.9 s). The slowest case took 5.72 s (on) and
  5.63 s (off). The cohort prediction in `predictions.txt` was met.
- Also seen: `ExtrinsicSandbox` run directly with only `lsan.supp` exits 1 on
  LeakSanitizer reports from unsymbolized driver frames, libX11 XIM and dbus,
  with the monitor off before the fix and with it on after the fix. The
  registered CTest sets `detect_leaks=0`; BUG-180 owns leak findings.
- Not changed: `RuntimeSandboxAcceptanceGpuSmoke` intentionally keeps the
  Sandbox defaults, including VSync. With the monitor off its surface-appearance
  case still passed, but in 71.27 s (17–21 s with the monitor on).
  Whether those smokes should switch VSync off is an open decision.
- Whole `gpu` label at 3d39d1737, `:1`, monitor on: of 161 cases 160 passed
  and one was a capability skip (`METHOD056VulkanCpdEStep.ScalingProfile`), 682 s, at a
  1-minute load of 3.6–6.0.
- Review of 3d39d1737: a fresh Claude reviewer that did not write the change
  read the code only and answered "Verdict: APPROVE", with two nits (a test
  inserted under another test's comment; loose doc wording) that are fixed in
  the next commit. No Codex review was run.

### Evidence status — 2026-10-24
Predictions for D1–D3, the fix condition and the cohort were committed
beforehand (25550e64e) or appended to `predictions.txt` before the run; the
25 s trial and the `gpu` label run had none. A committed prediction does not
by itself make a run verification: a run whose numbers informed a keep or
change choice is decision-bearing, with or without one.
- Decision-bearing:
  - D1–D3 (monitor-off keypoint timeout, Xephyr pass, Sandbox present 918 ms):
    they located the wait and led to the choice to change the present mode.
  - The fix-condition runs: the keypoint case with the monitor off after the
    fix (3.74 s), Sandbox `enable_vsync: false` with the monitor off (median
    present + begin 0.17 ms) and the FIFO control (991.6 ms present). They
    decided whether the present-mode fix was kept.
  - The 25 s watchdog trial, which fired at 28.66 s, 1.3 s before the CTest
    kill. It set the final 20 s bound. No run exercised the 20 s bound firing.
- Verification only (no other use of the numbers): the 23-case cohort with the
  monitor on and off, and the whole `gpu` label run.
- No experiment pre-flight was run: no frozen packet, no prospective review
  and no HTML report exists for these runs. The operator's pending
  disposition covers every decision-bearing measurement listed above.

## Sandbox acceptance smokes with VSync off — 2026-10-24 (evening)

### Predictions (committed before the runs)
Operator decision on the open item above (verbatim): "4. Vsync aus und bei den
offenen: prüfen und neu formulieren". `BootstrapDefaultSandboxAppEngineWithApp`
in `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` now sets
`Render.EnableVSync=false` after `CreateReferenceEngineConfig`, like the other
`gpu;vulkan` fixtures; validation and every other Sandbox default stay. No gate,
assertion or `TIMEOUT` changes. Same host (RTX 4090, NVIDIA 580.159.04 ICD via
`VK_DRIVER_FILES`), `DISPLAY=:1`, `ci-vulkan`, one CTest command per display
state over every `RuntimeSandboxAcceptanceGpuSmoke.*` case, run serially with
`-V`; the monitor-off command is bounded and forces DPMS on again in a shell
trap, with `xset q` recorded before, during and after. Logs go to
`tasks/evidence/BUG-228/2026-10-24-vsync-off-acceptance/` (ignored by git).
- V0, the setting reaches the device: no `VSync requested=true` swapchain log
  line in either run. Rejected by any such line.
- V1, monitor on: every case passes (42 cases in the `gpu` label run at 3d39d1737);
  surface appearance takes at most 21 s (17–21 s before, VSync on).
- V2, monitor off: every case passes; surface appearance takes under 25 s
  (71.27 s with VSync on); no case takes 30 s or more; the summed case time is
  at most 30 % above V1's. Rejected if any case fails or times out, or surface
  appearance takes 30 s or more.
- B1, the lowered `PointLBVHGpuSmoke` stall bounds (below): the seven
  `FramedKNearest*`, `FramedRadius*` and `Keypoint{Publishes,Rejects,VulkanCompute}*`
  cases pass with the monitor on, each in under 10 s (3.1–6.5 s in the `gpu`
  label run), so neither 20 s bound fires.

### Stall bounds in `Test.PointLBVHGpuSmoke.cpp`
Effective `TIMEOUT`s from `ctest --show-only=json-v1`: the registered value in
an unscaled tree, twice that in this sanitized `ci-vulkan` tree (BUG-240).
- `KnnBatchApp` (`FramedKNearestReusesBuffersAndRejectsStaleTarget`,
  `FramedRadiusPreservesOverflowExclusionAndBufferReuse`): 40 s bound,
  `TIMEOUT` 30 s (60 s here). It could not fire in an unscaled tree, and it
  set `TimedOut` without a message. Now 20 s, with a failure naming the round
  and frame.
- `KeypointApp` (the five `Keypoint{Publishes,Rejects,VulkanCompute}*` cases):
  50 s bound. Four cases are in the 120 s fixup (240 s here), so the bound could
  fire for them. `KeypointVulkanComputeRejectsStaleCancellationAndIgnoresHybridCapacity`
  is not: the fixup names `...RejectsStaleCancellationAndOverflow`, which no
  test has, so that case keeps 30 s (60 s here) and the 50 s bound could not
  fire in an unscaled tree. The shared bound is now 20 s, with a failure naming
  the phase, result count and frame. The stale fixup name is left as is:
  correcting it would raise that case's `TIMEOUT` to 120 s, which is not this
  change's decision.
