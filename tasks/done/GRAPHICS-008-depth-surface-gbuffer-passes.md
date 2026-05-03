# GRAPHICS-008 — Depth, surface, and G-buffer passes

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-007` completion.
- Completed: 2026-05-03.
- Result: implemented CPU/null-testable depth prepass, forward surface, and deferred G-buffer command contracts over the surface opaque culling bucket.
- Follow-up task: `GRAPHICS-009 — Deferred lighting and shadows` promoted to `tasks/active/`.

## Completion metadata
- Implementation commit: pending local agent workflow handoff.
- Task-state commit: pending local agent workflow handoff.
- Verification: focused surface/frame-recipe contract tests, aggregate build, and default CPU gate passed; commands recorded below.

## Goal
- Reimplement depth prepass, forward surface, and deferred G-buffer command/resource behavior through current RHI and framegraph abstractions.
## Non-goals
- No post-process, picking, selection outline, or debug overlay work.
- No new material model beyond the registry contracts needed by the passes.
- No copied legacy pass code.
## Context
- Owner: `src/graphics/renderer/Passes` with dependencies on `GRAPHICS-003`, `GRAPHICS-006`, and `GRAPHICS-007`.
- Canonical resources include `SceneDepth`, `SceneColorHDR`, `SceneNormal`, `Albedo`, and `Material0`.
## Required changes
- Fill `Pass.DepthPrepass`, `Pass.Forward.Surface`, and `Pass.Deferred.GBuffers` contracts.
- Bind pipelines, geometry buffers, material state, push constants or descriptor payloads, and indirect draw buckets via RHI seams.
- Bind the canonical scene table and renderable-instance, transform, material, geometry, and bounds/culling SSBOs instead of adding pass-specific push-constant payloads for scene data.
- Preserve forward/deferred/hybrid recipe gating.
## Tests
- Add mock command-context tests for pipeline bind, descriptor/material bind, index/vertex bind, push-constant budget, and indirect draw calls.
- Add mock checks that the scene-table and required SSBO descriptors are bound before indirect draws.
- Add framegraph tests for resource load/store and optional G-buffer allocation.
- Label these CPU/mock-backend tests `contract;graphics` so they run in the default CPU gate.
## Docs
- Update pass resource contracts and any push-constant budget notes affected by implementation.
## Acceptance criteria
- Forward and deferred surface lanes are independently schedulable.
- Depth prepass is recipe-gated and shares compatible draw streams.
- CPU-supported tests validate command sequencing without requiring Vulkan.
## Verification
```bash
cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsSurfacePassContracts|FrameRecipeContract' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Growing push constants beyond guaranteed backend limits without moving data to descriptors.
