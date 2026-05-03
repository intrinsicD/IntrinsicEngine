# GRAPHICS-013CQ — ImGui/present backend clarification follow-ups

## Goal
- Clarify concrete backend/runtime details that remain after the CPU/null `GRAPHICS-013C` ImGui overlay and present/finalization contracts.

## Non-goals
- No C++ behavior changes.
- No postprocess or debug-view behavior work.
- No platform/window ownership migration into graphics.

## Context
- `GRAPHICS-013C` established `ImGuiOverlaySystem` draw-data summary import, overlay diagnostics, guarded `ImGuiPass` command recording, `PresentPass` finalization command recording, and render-graph rejection of non-present writes to imported backbuffers.
- Remaining questions affect concrete Dear ImGui backend translation, descriptor buffers/textures, swapchain finalization implementation, and runtime/platform wiring.

## Required changes
- Clarify how runtime/editor translates `ImDrawData` into `ImGuiOverlayFrame` records and when those records are submitted.
- Clarify overlay vertex/index buffer upload ownership, font/user texture descriptor policy, and backend pipeline state.
- Clarify whether present/finalization uses fullscreen draw, texture copy, or backend-native swapchain resolve once concrete backends are wired.
- Clarify platform/backend responsibility boundaries for acquire/present timing, swapchain image ownership, and resize handling.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update renderer/backend docs and `docs/architecture/rendering-three-pass.md` with selected backend/runtime handoff policies.

## Acceptance criteria
- Concrete backend/runtime ImGui and present work can proceed without changing the CPU/null graphics contracts from `GRAPHICS-013C`.
- Graphics remains decoupled from platform/window ownership.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Introducing platform/window ownership into graphics.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

