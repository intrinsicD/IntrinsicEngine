# GRAPHICS-013AQ — Postprocess backend clarification follow-ups

## Goal
- Clarify concrete backend/shader details that remain after the CPU/null `GRAPHICS-013A` postprocess chain contracts.

## Non-goals
- No C++ behavior changes.
- No debug-view, ImGui, or present/finalization policy work.
- No Vulkan-only implementation in this docs-only clarification task.

## Context
- `GRAPHICS-013A` established `PostProcessSystem` settings, deterministic stage ordering, diagnostics, push-constant packet data, explicit `SceneColorHDR` to `SceneColorLDR` frame-recipe resources, and CPU/mock command contracts for `Histogram`, `Bloom`, `ToneMap`, `FXAA`, and `SMAA` pass shims.
- Remaining questions affect concrete shader kernels, descriptor binding, temporal/exposure history, and backend resource strategies and should not be mixed with CPU/null contract work.

## Required changes
- Clarify bloom implementation policy: downsample/upsample pyramid shape, scratch texture count, and how `PostProcess.BloomScratch` maps to concrete backend resources.
- Clarify histogram/exposure policy: bin count, luminance range, adaptation history ownership, readback/diagnostics format, and whether history is frame-transient or retained.
- Clarify anti-aliasing backend policy: `FXAA` and `SMAA` shader inputs, lookup textures, edge/blend intermediate ownership, and quality presets.
- Clarify descriptor/binding ownership for `SceneColorHDR`, `SceneColorLDR`, postprocess intermediates, and any retained LUT/history resources.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/rendering-three-pass.md`, renderer docs, and backend notes with selected postprocess backend policies.

## Acceptance criteria
- Vulkan/backend work can implement real postprocess effects without changing the CPU/null graphics contracts from `GRAPHICS-013A`.
- Frame-transient versus retained postprocess resources are documented explicitly.
- Debug-view, ImGui, and present policy remain out of scope.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Expanding into debug-view or ImGui/present ownership.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

