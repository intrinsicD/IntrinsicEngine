# Roadmap: near-term architecture sequencing (archived)

> **Archived 2026-10-30** (operator decision). Not a current target and not design guidance.
> The only current architecture target is [Engine and feature modules](../../architecture/engine-and-feature-modules.md).
> This is the section `docs/roadmap.md` carried until then, moved here unchanged apart from link paths.

1. **Baseline + checkpoints**
   - Lock current telemetry, pass ordering, and render-contract snapshots before each migration phase.
   - Keep rollback shims and feature-flagged cutovers until each phase clears its pass/fail gates.
   - Use `FrameLoop.StagedPhases` / `FrameLoop.LegacyCompatibility` as the explicit cutover toggle pair documented in `docs/architecture/frame-loop-rollback-strategy.md`.
2. **Frame-stage extraction**
   - Introduce typed `RenderFrameInput`, `WorldSnapshot`, `RenderWorld`, and `FrameContext` seams.
   - Ensure render preparation consumes immutable extracted state instead of late live ECS traversal.
3. **Fixed-step runtime evolution**
   - Move toward a fixed-step simulation lane with variable-rate rendering and bounded frames in flight.
   - Make GPU completion, deferred retirement, and per-frame ownership explicit runtime concepts.
4. **Renderer-owned preparation**
   - Keep three-pass/deferred/post/overlay behavior expressed as renderer-owned render-graph composition rather than top-level loop branching.
   - Preserve headless/testable paths by isolating platform and swapchain specifics from simulation, extraction, and maintenance logic.

This architecture track is intentionally cross-cutting: it enables later rendering, streaming, and interaction work without reordering the feature roadmap below.

**GPU-driven pipeline plan:** [docs/archive/architecture/gpu-driven-modular-rendering-pipeline-plan.md](gpu-driven-modular-rendering-pipeline-plan.md) provides the concrete implementation plan for GPU-driven visibility, unified GPU scene topology, and modular rendering boundaries. It is consumed by the structured rendering backlog (see [tasks/backlog/rendering/README.md](../../../tasks/backlog/rendering/README.md)); the original parity seed is retired as [GRAPHICS-001 — Rendering parity inventory and task index](../../../tasks/archive/GRAPHICS-001-rendering-parity-inventory.md).

