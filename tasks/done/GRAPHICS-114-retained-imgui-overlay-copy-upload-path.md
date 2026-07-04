---
id: GRAPHICS-114
theme: B
depends_on: [GRAPHICS-110]
maturity_target: Operational
completed: 2026-07-04
---
# GRAPHICS-114 — Retained ImGui overlay copy/upload path

## Completion
- Completed: 2026-07-04. Commit/PR: this retirement change.
- Maturity: `Operational` on Vulkan-capable hosts; `CPUContracted` for the
  backend-neutral retained-atlas, move-submit, single-pass command-upload, and
  byte-counter contracts.
- Summary: runtime adapter diagnostics now expose per-frame font-atlas,
  vertex, index, command, and total overlay copy bytes; graphics upload results
  expose vertex/index/total upload bytes. The retained font-atlas path copies
  atlas bytes only when the payload changes, unchanged steady frames retain the
  previous atlas payload, accepted draw lists move through the graphics
  boundary after validation, and command upload records are built once per draw
  list.
- Evidence: the measurement report is
  `docs/reports/2026-07-04-graphics-114-imgui-overlay-retention.md`. Focused
  CPU/null ImGui adapter/overlay/upload/pass contracts passed, and
  `ImGuiSurfaceGpuSmoke.LargeSelectedEntityPayloadRetainsAtlasOnOperationalVulkan`
  passed on the `ci-vulkan` preset.

## Goal
- Reduce per-frame ImGui overlay CPU copy/upload work so larger selected-entity editor panels do not multiply main-thread latency, while preserving the runtime-to-graphics ownership boundary and the in-flight buffer safety established by `GRAPHICS-110`.

## Non-goals
- Do not change Dear ImGui widgets, editor model semantics, or selected-entity cache/job scheduling; `RUNTIME-138` owns the editor-side nonblocking path.
- Do not weaken the `Runtime.ImGuiAdapter` -> `Graphics.ImGuiOverlaySystem` data boundary by exposing `ImDrawData` or ImGui types to graphics.
- Do not change user texture binding semantics or bindless texture ownership.
- Do not solve upload-buffer in-flight safety here; `GRAPHICS-110` must land first.

## Context
- Owning subsystems/layers: runtime ImGui adapter producer and graphics overlay/upload consumer. Runtime owns ImGui translation; graphics owns retained atlas, upload buffers, and `Pass.ImGui` command recording.
- Current path copies font atlas bytes into every `ImGuiOverlayFrame`, copies ImGui vertices/indices/commands into runtime-owned vectors, copies accepted draw lists again in `ImGuiOverlaySystem::SubmitFrame(...)`, rebuilds command upload lists more than once, flattens all draw-list vertices/indices into new vectors, and uploads the full ImGui payload every frame.
- `GRAPHICS-110` addresses the safety issue that the upload buffers must be partitioned per frame/ring before upload-path churn is optimized.
- `RUNTIME-138` will reduce selected editor model work; this task keeps the overlay transport/upload path from becoming the next selected-frame bottleneck.

## Required changes
- [x] After `GRAPHICS-110`, measure/capture per-frame ImGui overlay copy bytes, font-atlas bytes, command count, deterministic flatten/copy byte proxies, upload bytes, and allocation counts during selected-entity editor frames.
- [x] Stop copying font atlas pixels every frame when the atlas payload is unchanged; preserve explicit dirty/upload diagnostics.
- [x] Avoid redundant draw-command upload list construction inside `ImGuiUploadHelper::UploadFrame(...)`.
- [x] Reuse CPU staging vectors or retained upload scratch storage where safe, without retaining stale pointers to ImGui-owned memory.
- [x] Evaluate whether `ImGuiOverlaySystem::SubmitFrame(...)` can move accepted draw lists instead of copying them, while preserving validation and diagnostics.
- [x] Preserve the runtime/graphics boundary: runtime submits POD overlay frames, graphics never imports `imgui.h`.
- [x] Add diagnostics that show cache/reuse hits and per-frame copy/upload byte counts.

## Tests
- [x] Add contract tests proving unchanged font atlas payloads are not recopied/requeued every frame while a dirty atlas still uploads.
- [x] Add upload-helper tests proving command upload construction is single-pass and preserves draw-call order/scissor/user-texture data.
- [x] Add overlay-system tests proving move/reuse paths preserve validation diagnostics and rejected-list behavior.
- [x] Run focused ImGui adapter/overlay/upload/pass tests in the default CPU gate.
- [x] Run opt-in `gpu;vulkan` smoke after `GRAPHICS-110` for selected-entity editor frames with large UI payloads.

## Docs
- [x] Update `src/runtime/README.md` and `src/graphics/renderer/README.md` to describe the retained/dirty ImGui overlay transport behavior once implemented.
- [x] Link this task from `UI-030`, `RUNTIME-138`, and `GRAPHICS-110` where they discuss selected-frame ImGui copy/upload costs.

## Acceptance criteria
- [x] Unchanged font atlas bytes are not copied through the runtime/graphics overlay path every frame.
- [x] ImGui command upload data is built once per draw list per frame.
- [x] CPU-side allocations and copied bytes for steady selected-entity UI frames are observable and reduced versus the baseline captured for this task.
- [x] The upload path remains in-flight safe under the `GRAPHICS-110` per-frame/ring model.
- [x] No graphics module imports ImGui/runtime/platform/app ownership.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ImGuiAdapter|ImGuiOverlay|ImGuiUpload|ImGuiPass' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
# On a Vulkan-capable host:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ImGui|Sandbox' --timeout 180
```

## Forbidden changes
- Reintroducing direct ImGui dependencies into graphics module interfaces.
- Bypassing the `ImGuiOverlayFrame` ownership boundary with borrowed ImGui pointers.
- Hiding UI panels or reducing UI content to reduce upload work.
- Mixing selected-entity model caching or renderer selection-outline work into this task.

## Maturity
- Retired at `Operational` on Vulkan-capable hosts after the selected-entity
  large-payload ImGui smoke passed under `ci-vulkan`.
- CPU flatten/copy time is captured as deterministic byte counters in this
  task's report, not as a wall-clock threshold. No speedup claim is made without
  a benchmark manifest; this task only claims reduced steady-frame atlas copy
  bytes and eliminated redundant command-upload construction/copy churn.
