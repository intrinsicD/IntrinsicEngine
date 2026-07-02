---
id: UI-030
theme: F
depends_on: []
maturity_target: Operational
---
# UI-030 — Sandbox EditorUI frame-pacing diagnostics

## Status
- Partial (2026-07-01) on branch `claude/ui-backlog-agentic-y3oap2`. Only the
  `ImGuiUploadHelper` buffer-ownership audit landed; the timing instrumentation
  and the measured bottleneck ranking are **not** landed here because they
  require a working C++23-module build (this sandbox has clang-18 and cannot
  bootstrap vcpkg) and, for the GPU/Vulkan lifecycle timers plus the measured
  ranking, a Vulkan-capable host. Neither is available in this environment, and
  the task explicitly forbids claiming a performance finding without
  measurement, so the evidence-backed ranking remains open for a build/Vulkan
  session.
- Completed audit finding: `Extrinsic.Graphics.ImGuiUploadHelper`
  (`src/graphics/renderer/Graphics.ImGuiUploadHelper.cpp`) owns exactly one
  growing host-visible vertex buffer and one growing host-visible index buffer,
  reused across frames. `BeginFrame()` is a no-op and `UploadFrame(...)` writes
  every frame's coalesced payload at byte offset `0` (`WriteBuffer(handle, data,
  bytes, 0u)`), overwriting in place with no per-frame partition, ring offset,
  or per-buffer fence. This is safe only under an effectively
  single-frame-in-flight (or otherwise fence-gated) model; with more than one
  frame in flight a new frame's overwrite can clobber storage an earlier
  in-flight frame's GPU draw is still reading (the device `BeginFrame` fence
  gates slot `N mod F`, i.e. frame `N-F`, not frame `N-1`, and the single buffer
  is shared across all slots). Per the acceptance criterion this is now tracked
  by a named follow-up rather than fixed in this diagnostic task.
- Follow-up filed: `GRAPHICS-110` — per-frame/ring ImGui upload buffers for
  in-flight safety
  (`tasks/done/GRAPHICS-110-imgui-upload-buffer-in-flight-safety.md`).
- Follow-ups filed from the selected-entity source diagnosis: `RUNTIME-138`
  owns the nonblocking selected-entity editor/cache/job pipeline,
  `GRAPHICS-113` owns selected-outline ID work pruning, and `GRAPHICS-114`
  owns retained ImGui overlay copy/upload cleanup after `GRAPHICS-110`.

## Goal
- Build a deterministic frame-pacing investigation loop for the Sandbox EditorUI, identify whether stutter is caused by editor CPU work, ImGui data upload, Vulkan frame lifecycle waits, present pacing, render-graph pass work, or synchronous upload/readback paths, and record scoped follow-up fixes with evidence.

## Non-goals
- Do not reorganize the domain-window information architecture; `UI-031` owns that refactor.
- Do not tune render-graph barriers, queue ownership transfers, or Vulkan fences without measurements showing they are the limiting factor.
- Do not claim a performance fix from a single manual run or from subjective smoothness alone.
- Do not introduce broad renderer rewrites or change default present mode policy in this diagnostic task.

## Context
- Owning subsystem/layer: runtime/editor and graphics renderer. Runtime owns `Extrinsic.Runtime.SandboxEditorUi` and `Extrinsic.Runtime.ImGuiAdapter`; graphics owns `Extrinsic.Graphics.ImGuiOverlaySystem`, `Extrinsic.Graphics.ImGuiUploadHelper`, `Pass.ImGui`, frame-graph stats, and Vulkan frame lifecycle.
- Current suspicion from source review:
  - `SandboxEditorUi::Attach(...)` rebuilds a broad panel/domain model every ImGui frame.
  - `ImGuiAdapter::EndFrame()` copies the font atlas plus all ImGui draw-list vertices, indices, and commands into engine-owned containers every frame.
  - `ImGuiOverlaySystem::SubmitFrame(...)` copies accepted overlay data again and compares font-atlas payloads.
  - `ImGuiUploadHelper` owns one growing vertex buffer and one growing index buffer; if those host-visible buffers are not per-frame or ring-offset protected, the renderer may overwrite UI vertices while an older frame is still reading them.
  - `VulkanDevice::BeginFrame(...)` waits on frame-slot fences and default present mode is FIFO/VSync; this is normal but can expose missed-frame spikes.
- Existing diagnostics already expose frame-graph compile/execute microseconds, barrier counts, queue handoffs, command-pass records, and readback-copy counters through the Frame Graph panel. This task should extend that evidence with missing CPU phase timing and frame lifecycle timing rather than replacing it.
- `RUNTIME-138` is the implementation follow-up for the architectural rule that the main loop should read cached selected-entity state and submit commands/jobs rather than synchronously deriving inspector data.

## Required changes
- [ ] Add focused, low-overhead timing probes for `Engine::RunFrame` phases that can distinguish editor callback time, ImGui draw-data copy time, render snapshot/extraction time, frame-graph compile time, execute/record/submit time, present time, and maintenance time.
- [ ] Add Vulkan frame lifecycle timing for `vkWaitForFences`, `vkAcquireNextImageKHR`, queue submit, and `vkQueuePresentKHR`, exposed through existing renderer/runtime diagnostics without lower layers importing runtime/UI.
- [ ] Add ImGui overlay diagnostics for per-frame copied font-atlas bytes, draw-list bytes, vertex/index counts, command count, CPU flatten/copy time, GPU buffer write bytes, buffer allocation count, and whether font-atlas upload actually queued.
- [x] Audit `ImGuiUploadHelper` transient buffer ownership and either prove it is safe for the current frames-in-flight model or file a named follow-up task for per-frame/ring upload buffers. Audit complete (see Status); the single-shared-host-visible-buffer overwrite pattern is not provably in-flight-safe, so `GRAPHICS-110` was filed for per-frame/ring upload buffers.
- [ ] Add a reproducible local diagnostic mode or test harness that runs the sandbox/editor frame loop for a bounded frame count and emits machine-readable frame timing samples.
- [ ] Write a short report under `docs/reports/` summarizing the measured bottleneck ranking, ruled-out hypotheses, backend/present-mode conditions, and the follow-up task IDs for fixes.
- [ ] If the investigation finds a specific bug or missing synchronization contract beyond `RUNTIME-138`, `GRAPHICS-113`, `GRAPHICS-114`, or `GRAPHICS-110`, open a scoped follow-up task under `tasks/backlog/bugs/`, `tasks/backlog/rendering/`, `tasks/backlog/runtime/`, or `tasks/backlog/ui/` rather than expanding this task into the fix.

## Tests
- [ ] Add or update contract tests proving the new diagnostics are populated in the Null backend path without requiring GPU/Vulkan.
- [ ] Add opt-in `gpu;vulkan` verification notes or tests for the Vulkan lifecycle timers when a Vulkan-capable host is available.
- [ ] Run focused runtime/graphics contract tests covering `SandboxEditorUi`, `ImGuiPass`, and renderer frame lifecycle diagnostics.
- [ ] Validate any emitted JSON/report schema if a machine-readable diagnostic artifact is added.

## Docs
- [ ] Update `tasks/backlog/ui/README.md` to keep the open UI backlog current.
- [ ] Add the measured frame-pacing report under `docs/reports/` and link every follow-up task it creates.
- [ ] If a new diagnostics panel/control is exposed, update `src/runtime/README.md` or the relevant renderer README to document the factual current state.

## Acceptance criteria
- [ ] A user can capture frame-pacing data that separates CPU editor cost from GPU/frame lifecycle stalls.
- [ ] The investigation reports whether stutter correlates with editor model rebuilds, ImGui copy/upload, frame-slot fence waits, acquire/present stalls, render-graph compile/execute spikes, synchronous upload/readback, or another measured cause.
- [x] `ImGuiUploadHelper` in-flight buffer safety is either proven by code/test evidence or tracked by a follow-up task with a concrete owner. Tracked by `GRAPHICS-110`.
- [ ] Follow-up implementation tasks exist for every measured fix candidate that should not be completed inside this diagnostic task.
- [ ] Selected-entity main-loop responsiveness fixes remain owned by `RUNTIME-138` and renderer/upload follow-ups remain owned by `GRAPHICS-113`/`GRAPHICS-114`; this task stays an evidence loop and report.
- [ ] The report does not claim a performance improvement unless it cites before/after measurements and the exact verification commands.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|ImGuiPass|RendererFrameLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
# On a Vulkan-capable host, after the diagnostic mode exists:
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Sandbox|ImGui|RendererFrameLifecycle' --timeout 180
```

## Forbidden changes
- Mixing the diagnostic instrumentation with broad UI reorganization.
- Disabling VSync, validation, barriers, readbacks, or passes as a "fix" without an A/B measurement proving causality.
- Adding runtime imports into graphics or graphics imports into platform.
- Leaving temporary debug logs, untagged printf/stdout probes, or ad hoc profiling code outside the accepted diagnostics surface.
- Introducing unrelated feature work.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the Null/backend-neutral diagnostics contract.
- This task may retire only after it provides an evidence-backed bottleneck ranking. Any actual performance fix is owned by follow-up tasks created from the report.
