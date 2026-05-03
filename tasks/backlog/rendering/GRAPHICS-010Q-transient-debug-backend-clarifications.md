# GRAPHICS-010Q — Transient debug backend clarification follow-ups

## Goal
- Clarify backend/runtime handoff decisions for expanding `GRAPHICS-010` transient debug line/point/triangle packets into concrete GPU buffers and draw calls.

## Non-goals
- No C++ behavior changes.
- No Vulkan-only implementation.
- No persistent editor overlay entity factory work.

## Context
- Owner: graphics renderer architecture and future backend integration notes.
- `GRAPHICS-010` established renderer-owned transient debug packet spans, sanitization rules, line/point pass command contracts, and line/point frame-recipe cull resources.
- Remaining questions affect backend upload strategy and should not block spatial debug visualizer packet generation in `GRAPHICS-011`.

## Required changes
- Decide whether transient debug packets are expanded into dedicated per-frame GPU buffers or folded into canonical `GpuWorld`/culling buckets by a staging upload system.
- Clarify whether debug triangles reuse the surface/G-buffer path, a dedicated debug-surface pass, or a future hybrid/overlay path.
- Clarify per-packet depth-tested versus overlay lane routing and whether that becomes separate buckets or pipeline variants.
- Clarify diagnostics/limits for excessive transient primitive counts and backend allocation failures.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/rendering-three-pass.md` and `src/graphics/renderer/README.md` with the chosen upload and backend routing policy.

## Acceptance criteria
- Later Vulkan/backend work can implement transient debug packet upload without changing graphics/runtime ownership boundaries.
- Spatial visualizer work can generate packet spans without depending on backend upload internals.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Reintroducing live ECS/editor ownership into graphics.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

