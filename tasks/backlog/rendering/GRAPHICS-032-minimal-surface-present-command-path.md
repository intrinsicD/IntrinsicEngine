# GRAPHICS-032 — Minimal surface and present pass command recording path

## Goal
Implement the smallest visible-path command recording bodies needed to draw the reference triangle: one surface (or debug-surface) pass body, one present pass body, render-target setup, and required barriers/synchronization, while keeping the null renderer's existing CPU-testable contracts intact for non-operational devices.

## Non-goals
- No clustered/deferred lighting, shadows, or postprocess implementation (GRAPHICS-009 / GRAPHICS-013A own those).
- No Vulkan operational-readiness changes — the device path may remain null in this slice. Operational Vulkan lives in GRAPHICS-033.
- No new material/shader features beyond the default debug material from GRAPHICS-031.
- No editor/ImGui overlay command body work (GRAPHICS-013C territory).
- No live ECS access from graphics layers.
- No `GpuWorld` contract expansion.

## Context
- Owner layer: `graphics` (renderer + framegraph, with backend-local recording in `src/graphics/vulkan` only when a real device is present).
- `src/graphics/renderer/Graphics.Renderer.cpp` currently implements `Graphics::CreateRenderer()` by returning `NullRenderer`; the null renderer covers frame lifecycle, render graph construction, extraction snapshots, pass status accounting, and non-operational-device behavior. Visible pass bodies are not complete.
- Existing renderer lifecycle tests intentionally expect surface/present passes to be skipped under current conditions; this task changes that for the minimal triangle case behind a clearly named flag/label so unrelated tests do not regress.
- The 2026-05-08 review (`docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md`, section "Exact missing pieces / 5. Operational renderer pass command bodies" and "minimal milestone plan / 3. Renderer command milestone") requires a narrowly scoped minimal surface triangle acceptance test behind appropriate labels (CPU-mock vs `gpu;vulkan`).
- GRAPHICS-008/008Q already specify renderpass attachment/load-store/depth-write/compare ops for prepass-on/off cases; this task must respect those contracts rather than redefining them.
- GRAPHICS-013C / GRAPHICS-013CQ already specify `Pass.Present` keeping the CPU-testable fullscreen-triangle finalization form; this task must align with that.

## Required changes
- Define the minimal visible recipe:
  - One surface (or debug-surface) pass with a single-bucket draw using the default debug material from GRAPHICS-031 against the residency binding produced by GRAPHICS-030.
  - Render-target setup for `SceneColorHDR` (or the agreed minimal color attachment) and depth, following GRAPHICS-008Q load/store contracts.
  - Required barriers/synchronization between surface and present, and into the imported backbuffer per GRAPHICS-013CQ.
  - One `Pass.Present` body that finalizes onto the backbuffer using the existing fullscreen-triangle form.
- Implement command recording bodies:
  - In the renderer/framegraph layer for CPU-mock command recording so the pass produces a deterministic, testable command stream under the null device.
  - Defer real Vulkan recording to GRAPHICS-033; this slice's Vulkan implementation may remain a stub that satisfies `IsOperational() == false` semantics.
- Keep the non-minimal-triangle code path intact: existing skipped pass statuses on broader recipes must remain explicit and untouched, and existing renderer lifecycle tests must continue to pass.
- Add diagnostics for the minimal path (counter for "minimal surface pass executed" / "minimal present pass executed") so the acceptance test does not depend on a real GPU frame.
- Cross-link decisions with GRAPHICS-003 (frame recipe), GRAPHICS-008/008Q (depth/surface), GRAPHICS-013C/CQ (present), GRAPHICS-018 / GRAPHICS-018R (Vulkan), GRAPHICS-030 (residency), and GRAPHICS-031 (default material).

## Tests
- Add a `contract;graphics` minimal-surface acceptance test that:
  - Composes the minimal frame recipe.
  - Submits the snapshot from a runtime fixture matching GRAPHICS-029 + GRAPHICS-030 + GRAPHICS-031.
  - Asserts the surface and present pass diagnostic counters increment.
  - Asserts the recorded command stream contains the expected bind/draw/present sequence under the CPU-mock backend.
- Add an opt-in `gpu;vulkan` smoke test (kept outside the default CPU gate) that exercises the same recipe against a real device only when the host supports Vulkan; this test stays a stub or quarantined until GRAPHICS-033 lands operational Vulkan.
- Verification gate: default CPU-supported correctness target.
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/rendering-three-pass.md` with the minimal-triangle recipe and its label expectations.
- Update `src/graphics/renderer/README.md` and `src/graphics/framegraph/README.md` for the new minimal command bodies and diagnostic counters.
- Update `tasks/backlog/rendering/README.md` DAG to insert this task between GRAPHICS-031 and GRAPHICS-033.

## Acceptance criteria
- A minimal surface + present command path is implemented at the CPU-mock backend level, gated by a clearly named recipe/label, and proven by a `contract;graphics` acceptance test.
- Existing renderer lifecycle tests continue to pass, including the soft-skip tests for non-minimal recipes under non-operational devices.
- Layering invariants hold: graphics imports nothing live from ECS; runtime composes the recipe.
- The Vulkan backend remains fail-closed (`IsOperational() == false`) until GRAPHICS-033 lands; null renderer remains the supported execution path.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No silent-skip behavior for the minimal recipe; missing prerequisites must be diagnosable.
- No clustered/deferred lighting, shadows, or postprocess additions.
- No Vulkan operational-state changes (defer to GRAPHICS-033).
- No live ECS access from graphics layers.
- No mixing of mechanical file moves with semantic refactors.
