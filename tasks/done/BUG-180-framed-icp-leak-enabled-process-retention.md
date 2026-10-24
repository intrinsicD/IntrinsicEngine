---
id: BUG-180
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive test-harness diagnosis; no new engine capability or performance claim."
contract_schema: 1
contracts: []
contract_review: "Existing GPU correctness and sanitizer test policies apply; no new engine API or ownership contract."
---
# BUG-180 — Leak-enabled framed ICP process reports 240 retained bytes

## Goal
- Isolate the three 80-byte allocations reported at process exit by the direct leak-enabled framed ICP diagnostic before classifying ownership or changing suppressions.

## Evidence
- The combined ASan/UBSan `ci-vulkan` test completed all registration assertions in 46.715 s, then exited 1 with `SUMMARY: AddressSanitizer: 240 byte(s) leaked in 3 allocation(s)`.
- Allocation stacks start in unloaded/unknown modules reached through `VulkanDevice::CreatePipeline`; this suggests a driver boundary but does not establish external ownership. Full log: `/tmp/icp-gpu-display-off-diagnostic.log`.
- The normal general GPU CTest cohort intentionally uses `detect_leaks=0`; dedicated Vulkan shutdown/negative-control tests own leak evidence. The diagnostic instead used `LSAN_OPTIONS=suppressions=.../lsan.supp`, enabling a distinct evidence class. No existing environment or suppression is changed by the ICP implementation.
- [ICP review](../../docs/reviews/2026-09-08-icp-spatial-integration.md) records the distinction. `BUG-083` is the existing dedicated shutdown/retention context; `BUG-179` owns present pacing, not these allocations.

- A subsequent read-only CTest discovery inside the filesystem sandbox could not run LeakSanitizer under its ptrace restriction. Repeating discovery outside that sandbox succeeded. This is a separate tooling limitation and supplies no allocation-ownership evidence.

## Additional diagnostic — 2026-09-13

RUNTIME-235's direct distance-ratio timing probe also completed all assertions
(44.721 s, zero reference error), then exited 1 with 115,869 bytes in 24
allocations. Its runner omitted the registered CTest environment; the resulting
leak-enabled run is not a passing sanitizer gate. Stacks include unloaded modules,
Vulkan instance creation and libdbus. This is a new observation in the existing
retention investigation, not proof that those allocations share the ICP cause or
are exclusively driver-owned. The general cohort environment is unchanged;
no suppression was added. Raw log:
`build/analysis/remaining-processing-locality-2026-09-13/ratio-diagnostic.log`.

## Additional diagnostic — 2026-09-14

The BUG-194 actual-dragon scalar-display probe completed its assertions, then
the direct executable exited 1 with 115,665 retained bytes in 21 allocations.
Stacks include Vulkan instance creation, libdbus and unloaded modules. The
runner omitted the registered general GPU cohort environment, so this remains
a leak-enabled diagnostic rather than a passing sanitizer gate. No ownership
classification or suppression change follows. Raw log:
`build/analysis/bug194-saliency-followup-2026-09-14/dragon-gpu.log`.

## Diagnosis — 2026-10-24

Host and build: `main` at `599f02ff1`, `ci-vulkan` (ASan+UBSan), RTX 4090,
NVIDIA 580.159.04 (`libnvidia-glcore` BuildId `181195ae…`), loader 1.3.296,
native `DISPLAY=:1` (DPMS enabled, monitor on, GPU idle in P8 before the runs).
Every run used the Verification command's executable, gtest filter, repository-root
working directory and `LSAN_OPTIONS=suppressions=lsan.supp`; the deltas are listed
below. Expected outcomes were written down before each run. The gtest assertions
passed in every run. Runs with a filled shader cache took 5.517–6.054 s; the
three empty-cache runs (`F-fresh-cache-run1`, `F2-fresh-cache-run1`, `I-icp-run1`)
were the slower ones at 13.3–13.5 s, presumably cold pipeline compiles. The
original 46.7 s was DPMS-off present pacing (BUG-179).

| Run | Change to the environment | Exit | LeakSanitizer |
| --- | --- | --- | --- |
| A | none (task command verbatim, every ICD enumerated) | 1 | 752 B / 5: 2×256 B + 3×80 B |
| B, C | `VK_DRIVER_FILES=…/nvidia_icd.json` | 1 | 240 B / 3×80 B (the original signature) |
| F, F2 run 1 | NVIDIA ICD and a new empty `__GL_SHADER_DISK_CACHE_PATH` | 0 | none |
| F run 2, F2 runs 2–3 | the same cache directory, now filled | 1 | 240 B / 3×80 B |
| H | filled cache and `LD_PRELOAD` of `libnvidia-glcore.so.580.159.04` | 1 | 240 B / 3×80 B |
| Control | Sandbox run with the shutdown-contract environment for 5 frames, NVIDIA ICD; default cache, then a new cache (empty, then filled) | 0 | none |
| Contract | `ExtrinsicSandbox.VulkanShutdownLsanContract` via CTest | pass (4.25 s) | unchanged |

Ownership evidence:
- Allocation site: run C recorded `LD_DEBUG=files` load addresses before the
  libraries unloaded. They map all three blocks to `malloc` called from
  `libnvidia-glcore.so.580.159.04+0xa0c0d1`. This is the same allocation site as
  BUG-229's swapchain allocations. The calls go through
  `libVkLayer_khronos_validation.so` `vkCreateComputePipelines`/`vkCreateGraphicsPipelines`,
  from `VulkanDevice::CreatePipeline`, for `CullComputeBuckets`
  (`Graphics.CullingSystem.cpp:379`), `Renderer.PostProcess.ToneMap`
  (`Graphics.Renderer.cpp:6335`) and `Renderer.ClusterGrid.Build`
  (`Graphics.Renderer.cpp:6891`). Pinning glcore (run H) keeps the module mapped,
  and LeakSanitizer still reports the blocks as direct leaks, now with glcore frames.
  So the driver lost the pointers. The leaks are not caused by the library unloading.
- Free evidence on the engine side: an api_dump trace (run D) matches all 51
  `VkPipeline` handles, these three included, to a `vkDestroyPipeline` call before
  `vkDestroyDevice`. `vkDestroyInstance` is called, and validation reported no
  object-lifetime error. The engine never holds these blocks.
- Dependence on driver state: with the same binary and inputs, an empty NVIDIA
  shader disk cache gives a clean run and a filled cache gives the three blocks.
  This repeated in two separate cache directories.
- Control comparison: the shutdown control creates and destroys the same three
  pipelines (api_dump, 42/42 destroyed). It stays clean with an empty and with a
  filled cache. Its cache files did not grow on the second run, so its pipelines
  were also loaded from the cache. It is not explained why loading from the cache
  loses the blocks only in this process. Differences between the two processes:
  executable and cache file, 51 versus 42 cached pipelines, 64×64 window, VSync off.
  No claim is made about the driver's internal mechanism.
- The two 256 B blocks in run A have the BUG-221 Mesa enumeration signature: the
  low 12 bits of their PCs match the BUG-221 mapped `libvulkan_radeon+0x206a08` and
  `libvulkan_lvp+0x9e428`. They were not remapped in this run, and selecting the
  NVIDIA ICD removes them.

Classification: the retention is external, in NVIDIA 580.159.04 `libnvidia-glcore`,
when pipelines are created from a filled shader disk cache. No retention owned by
the engine was found, so there is no source change and no regression test.
`lsan.supp`, the `detect_leaks=0` setting of the general GPU tests, the shutdown
contract and its synthetic 4,096-byte engine-leak negative control are unchanged.
The 2026-09-13/14 observations from other executables were not rerun.

Remaining: the operator decides between retiring as external retention and keeping
the task open (driver update or vendor report). On this driver, a leak-enabled
direct run of this test with a filled cache is not a passing sanitizer gate. Raw
logs are git-ignored under `tasks/evidence/BUG-180/`: run logs `A-…` to `I-…`,
`C-mapped-frames.txt`, `D-pipeline-pairs.txt`, `E-sandbox-pipeline-pairs.txt`,
`I-cache-growth.txt` and `predictions.txt`.

## Acceptance criteria
- [x] Reproduce with the same executable, working directory, source and leak-enabled environment, comparing against the dedicated Vulkan shutdown control. Reviewer: met (runs A-C; control runs E, G, I and the contract run).
- [x] Preserve the relevant loaded driver/module identity before unload and classify ownership using allocation/free evidence. Reviewer: met (`C-mapped-frames.txt`, run H, run D api_dump 51/51).
- [x] Fix engine-owned retention or document a narrowly evidenced external-retention decision through the existing shutdown contract; retain the engine-allocation negative control. Documented narrowly evidenced external retention per operator decision (2026-10-24); reviewer: "partially met" before the decision, which was the only missing part. `lsan.supp` and the negative control are unchanged.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
LSAN_OPTIONS=suppressions="$PWD/lsan.supp" timeout 120s build/ci-vulkan/bin/IntrinsicPointLBVHGpuTests --gtest_filter=PointLBVHGpuSmoke.FramedRegistrationReusesTargetAcrossRunsAndMatchesCpuSolve
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ExtrinsicSandbox.VulkanShutdownLsanContract' --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```

## Operator decision — 2026-10-24

Verbatim (German): "als treiberproblem aufschreiben und evt beim nächsten Treiber
nochmal überprüfen" (record it as a driver problem and possibly re-check with the next
driver). Disposition: retire as external retention in NVIDIA 580.159.04
`libnvidia-glcore`; no suppression, sanitizer setting or test change.

Re-check trigger: when the NVIDIA driver version on the host changes, re-run this
task's leak-enabled command (Verification, second line) twice with the same shader
cache directory (the second run uses the filled cache). A clean second run means the
driver no longer retains the blocks; a 3×80 B report on the new version keeps this
classification. The trigger is also recorded in `tests/README.md` beside BUG-229.

## Completion — 2026-10-24
Completed 2026-10-24. Commit: `9396405a5` and `d0eb692b5` (diagnosis), the retirement
commit on the night integration branch `claude/night-2026-10-24-bug-sweep`. Not yet on
`main`; no source change.

Reviewer decisions (coordinator's local review record `wfB.json`, lane `GPU-LANE`,
outside the repository): independent Claude review of `d0eb692b5` `approve`; AC1 "met",
AC2 "met", AC3 "partially met" ("The retire-or-keep decision waits for the operator").
The operator decision above supplies that part; AC3 is ticked only as documented,
narrowly evidenced external retention per that decision.

Open questions, kept:
- Why only this process loses the blocks: the shutdown control loads the same three
  pipelines from a filled cache and stays clean. Not explained.
- The 2026-09-13/14 observations (115,869 B and 115,665 B, other executables, libdbus and
  instance-creation stacks) were not rerun and are not classified by this decision.

Evidence: the raw logs stay git-ignored under `tasks/evidence/BUG-180/` in the `gpu` lane
worktree; nothing was deleted. The note's summary above is the durable
record; it is not a rerun.
