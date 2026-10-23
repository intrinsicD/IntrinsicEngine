---
id: BUG-182
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive verification exposed an existing unavailable-backend fixture timeout."
contract_schema: 1
contracts: []
contract_review: "Existing capability-test skip/timeout policy applies; no engine API or architecture change."
---
# BUG-182 — Framed kNN smoke times out when promoted Vulkan is not compiled

## Goal
- Make the framed kNN smoke report unsupported capability before entering its frame loop when promoted Vulkan is not compiled.

## Observation — 2026-09-09
- A local focused regex accidentally selected `PointLBVHGpuSmoke.FramedKNearestReusesBuffersAndRejectsStaleTarget` in `build/ci` instead of the opt-in `ci-vulkan` lane.
- The unchanged fixture logged `Promoted Vulkan device requested but not compiled into this build; using Null device fallback`, then CTest killed it at 30.04 seconds. Its internal wait lasts 40 seconds, so it cannot reach the post-run skip. No actual Vulkan query ran.
- The corrected CPU-only selector passed all five selected geometry cases. Real GPU verification belongs to `ci-vulkan`; this does not indicate a query regression or justify weakening that lane.

## Acceptance criteria
- [ ] Capability-skip the unsupported compile/backend case before entering the framed wait.
- [ ] Retain hard failures after actual Vulkan execution and preserve the current query assertions.
- [ ] Verify the non-promoted skip and the actual `ci-vulkan` case.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R '^PointLBVHGpuSmoke.FramedKNearestReusesBuffersAndRejectsStaleTarget$' --timeout 60
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.FramedKNearestReusesBuffersAndRejectsStaleTarget$' -L gpu -L vulkan --timeout 120
```

## Resolution — 2026-10-24

**Root cause.** Reproduced on `ci` (Null) at 599f02ff1: `***Timeout 30.04 sec`, with
`VulkanRequestedButNotOperational status=NotCompiled`. `KnnBatchApp::Frame` returns
early while the device is not operational. Only radius mode has a cold-frame exit
(`ColdFrames > 4`). kNN mode therefore waits for its 40 s wall budget, but CTest
kills the case at its default 30 s first. The case's only capability skip came
after `engine.Run()`, so the case never reached it.

**Change** (test only, `tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp`).
After `Initialize()` and before `Run()`, the case now does two skips, then the assert:
1. It skips when `EditorDiagnosticsStream::ReadDeviceStatus().ActualBackend != "vulkan"`.
   The skip reason is the runtime's own `FallbackReason`.
2. It skips on the Keypoint-case bootstrap check (`GetVulkanDeviceOperationalInputs`).
3. The post-run skip is now `ASSERT_TRUE(IsOperational())`, the same as its siblings.

`KnnBatchApp`, every query assertion, and the `TimedOut`/`Done`/Stats asserts are
unchanged.

**Defaults chosen** (Delegate posture):
- **(a) Backend guard before the bootstrap check.** `GetVulkanDeviceOperationalInputs`
  does an unchecked cast to the Vulkan device. Its contract says the result is
  undefined for other devices, and `-fno-rtti` rules out a checked cast.
  `ActualBackend` comes from `SelectRuntimeDeviceBackend(config, compiled)`, the same
  selection `CreateRuntimeDevice` uses. It is not a Vulkan diagnostic counter, so it
  is the allowed pre-run "no Vulkan device" skip. It does not break the
  gpu-smoke-authoring rule against gating on Vulkan diagnostics.
- **(b) The post-run skip became a hard assert.** A skip after frames have run hides
  regressions (acceptance criterion 2). Under CTest that line was unreachable anyway
  when the device stayed cold.
- **(c) No CMake, timeout, or engine change.** The 4-line diff reuses existing owners,
  so no jscpd scan was run.

**Verification on `ci`.** Raw logs are in `tasks/evidence/BUG-182/`.

| Check | Command or method | Result |
|---|---|---|
| Build | `nice -n 10 cmake --build --preset ci --parallel 5 --target IntrinsicPointLBVHGpuTests` | rc 0 |
| Fixed case | `ctest --test-dir build/ci -V -R '^PointLBVHGpuSmoke.FramedKNearestReusesBuffersAndRejectsStaleTarget$' --timeout 60` | rc 0, `***Skipped 0.06 sec`, reason `Promoted Vulkan is not compiled into this build.`, no `Vulkan bootstrap unavailable`. The predicted grep check printed PASS. Before the fix: Timeout at 30.04 s, rc 8. |
| Whole fixture | `ctest --test-dir build/ci --output-on-failure -R '^PointLBVHGpuSmoke\.' -j 5 --timeout 60` | Sorted per-case outcome lists (`outcomes-{before,after}.txt`) differ in exactly one line, #13 Timeout → Skipped. Totals: 17 skipped, 12 failed, 0 timeouts. Wall time 30.03 s → 0.91 s. |
| Mechanism check | Deleted only the backend-guard line, rebuilt, re-ran the case | `***Skipped 0.08 sec` with `Vulkan bootstrap unavailable`. This outcome is reached through the undefined cast, so it holds for this build only. It shows the backend guard is what decides on Null. Restored the line, rebuilt, re-ran: PASS again (0.09 s). |

**Siblings.**
- No sibling has the timeout defect.
- The 12 sibling failures on Null are all fast (0.11–0.18 s). Each fails at its post-run
  `ASSERT_TRUE(engine.GetDevice().IsOperational())`. All 12 failure lines were checked.
- `FramedRadius` (#14) shares `KnnBatchApp` and also has no pre-run skip. Its
  `ColdFrames > 4` exit makes it fail fast instead of timing out.
- I left these unchanged (decision 1). The CPU gate does not select them, because the
  whole executable is labelled `gpu;vulkan`.

**Pending (GPU lane).** The `ci-vulkan` half has not run yet:
`ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.FramedKNearestReusesBuffersAndRejectsStaleTarget$' -L gpu -L vulkan --timeout 120`.
Expected result: Passed, with host GPU and driver recorded. The `ci-vulkan` compile of
this change was also not run here.

**Open operator decisions.** Each has the default I took:
1. **The 12 siblings that fail fast on Null.** Default: unchanged. They are a different
   symptom, and their failure is visible, not hidden. Option: give them the same pre-run
   guard in a separate bug.
2. **Unsafe `GetVulkanDeviceOperationalInputs` on non-Vulkan devices.** It is reached from
   6 unguarded call sites in this file (the new 7th is guarded) and 40 textual uses
   across `tests/` and `benchmarks/`. Default: not
   fixed here; the fix belongs in shared backend code. Option: file a `tasks/HINTS.md`
   entry or a backlog bug. HINTS entries need operator consent, so none was filed.
3. **Timeout mismatch on `ci-vulkan`.** `KnnBatchApp` has a 40 s wall budget, but
   `FramedKNearest` and `FramedRadius` run under the default 30 s CTest TIMEOUT. The other
   framed siblings get 120 s in `tests/CMakeLists.txt`. Default: no timeout change. A
   device that bootstraps but never becomes operational in kNN mode still ends as a CTest
   Timeout rather than the fixture's own assert: still red, not hidden.
