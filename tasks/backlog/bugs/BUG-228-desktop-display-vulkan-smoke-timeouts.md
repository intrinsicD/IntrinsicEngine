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

### Predictions (written before the first run)
Source: `origin/main` 599f02ff1, fresh `ci-vulkan` tree, RTX 4090 with NVIDIA
580.159.04 ICD selected. At 01:46 `DISPLAY=:1` was GNOME/Xorg with two 4K
60 Hz outputs, DPMS enabled with `Monitor is On`, and the session locked
(`LockedHint=yes`). The GPU was in P8.
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
`tasks/evidence/BUG-228/2026-10-24/` (ignored by git). Host during the runs:
1-minute load 0.6–2.4, no compiler running.
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
- Xephyr has no DPMS extension, so the host display's power state cannot
  throttle it. It copies each presented frame instead. `ExtrinsicSandbox
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
  timed out. The same two cases fail on both displays, so the failures do not
  depend on the display. Not predicted:
  `KeypointVulkanComputePlanarZeroScoresAndIsotropicFloor` fails because
  `KeypointCount` is 5, not 8, in all eight domains.
  `KeypointResidentSubnormalScores` has 12 mask mismatches and a saliency L∞
  of 7.58e-42, against a bound of 1.12e-44. These failures are separate from
  this display bug and are not tracked yet.
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
