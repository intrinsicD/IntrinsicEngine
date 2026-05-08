# GRAPHICS-033 — Promoted Vulkan operational readiness and runtime fallback diagnostics

## Goal
Land the operational-readiness slice for the promoted Vulkan backend so that `IDevice::IsOperational()` can become `true` only after device, allocator, swapchain, command, synchronization, and validation contracts are satisfied; reconcile runtime config / CMake options / device fallback policy; and emit explicit diagnostics whenever runtime selects null fallback despite Vulkan being requested.

## Non-goals
- No new graphics passes, shaders, or material work (covered by GRAPHICS-031 / GRAPHICS-032 and earlier).
- No optional Vulkan extension expansion (mesh shaders, ray tracing, etc.) beyond what the minimal triangle recipe requires.
- No texture upload feature growth (covered by GRAPHICS-018T / GRAPHICS-026).
- No live ECS access from `src/graphics/vulkan/*`.
- No bypass of fail-closed behavior; the backend must remain fail-closed until contracts are demonstrably satisfied.
- No editor/ImGui present integration changes beyond what GRAPHICS-013CQ already records.

## Context
- Owner layer: `graphics/vulkan` (with runtime configuration reconciliation in `runtime`).
- `src/runtime/Runtime.Engine.cpp` returns `Backends::Null::CreateNullDevice()` when the promoted Vulkan path is not both compiled and enabled by configuration. `src/runtime/Runtime.Engine.cppm` sets the reference render backend to Vulkan, but Vulkan execution is still gated by promoted-device configuration and CMake build options.
- `src/graphics/vulkan/README.md` documents the promoted Vulkan backend as fail-closed (`IsOperational() == false`) until canonical renderer pass command recording, synchronization/barrier validation, queue-family ownership, and service fallback reconciliation are completed.
- The 2026-05-08 review (`docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md`, section "Exact missing pieces / 6. Operational Vulkan device path" and "minimal milestone plan / 4. Vulkan operational milestone") requires preserving fail-closed behavior until contracts are met and adding explicit diagnostics when runtime selects null fallback.
- GRAPHICS-018 / GRAPHICS-018Q / GRAPHICS-018R / GRAPHICS-018S / GRAPHICS-018T / GRAPHICS-026 already establish the integration scaffolding, sampler/border-color, texture-upload batching, and operational-transition planning.
- GRAPHICS-032 lands minimal CPU-mock command recording bodies that the operational Vulkan path must record against the real device once enabled.

## Required changes
- Define the operational-readiness checklist that flips `IsOperational()` to `true`:
  - Device + queue-family ownership + allocator initialization satisfied.
  - Swapchain creation, acquire/present, and resize/recreate paths satisfied.
  - Command pool / buffer / synchronization (semaphore/fence/timeline) contracts satisfied for the minimal recipe from GRAPHICS-032.
  - Barrier and layout-transition validation satisfied for the minimal recipe.
  - Validation-layer error policy and breadcrumb integration satisfied (extend the existing per-call pre-bring-up pattern from GRAPHICS-018Q).
- Implement Vulkan command-recording bodies for the minimal surface and present pass from GRAPHICS-032 against the real device, behind the existing CMake/option gates so the default CPU build remains null.
- Reconcile runtime / CMake / device fallback policy:
  - Single source of truth for "Vulkan requested" vs "Vulkan compiled-in" vs "Vulkan operational".
  - Explicit diagnostics (named counters and a single warn breadcrumb at startup) whenever runtime selects null fallback despite Vulkan being requested.
  - Document expected behavior when the host lacks Vulkan support or required extensions.
- Add an opt-in `gpu;vulkan` smoke test that exercises one visible-triangle frame on hosts that support Vulkan; this test must remain outside the default CPU correctness gate per AGENTS.md section 7.
- Cross-link decisions with GRAPHICS-018 / 018Q / 018R / 018S / 018T / 026, GRAPHICS-013CQ (present), GRAPHICS-032 (minimal recipe), and the 2026-05-08 review.

## Tests
- Add `contract;graphics` tests covering the runtime/CMake/operational reconciliation matrix without requiring a real device:
  - Vulkan compiled but not requested → null device, no diagnostic warning.
  - Vulkan requested but not compiled → null device, diagnostic counter and warn breadcrumb fire exactly once at startup.
  - Vulkan requested and compiled but not operational → null device, diagnostic counter increments, fail-closed preserved.
- Add an opt-in `gpu;vulkan` smoke test under `tests/integration/graphics/` (or the canonical GPU-labeled location) that runs the GRAPHICS-032 minimal recipe against a real device when present.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```
- Optional GPU smoke gate (only on hosts with Vulkan):
  ```bash
  ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
  ```

## Docs
- Update `src/graphics/vulkan/README.md` to record exactly which contracts gate `IsOperational()` and how diagnostics surface fallback decisions.
- Update `docs/architecture/graphics.md` and `docs/architecture/rendering-three-pass.md` for the operational-readiness gates.
- Update `docs/migration/nonlegacy-parity-matrix.md` rows for Vulkan operational status.
- Update `tasks/backlog/rendering/README.md` DAG to insert this task after GRAPHICS-032.

## Acceptance criteria
- `IsOperational()` becomes `true` only when the documented contracts are satisfied; otherwise the backend stays fail-closed.
- Runtime emits an explicit named diagnostic when null fallback is selected despite Vulkan being requested.
- CPU correctness gate continues to pass with the null device default; opt-in `gpu;vulkan` smoke test passes on hosts with Vulkan when run explicitly.
- Layering invariants hold; no live ECS access from `src/graphics/vulkan/*`.

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
- No relaxation of fail-closed behavior before the contracts are satisfied.
- No optional Vulkan extension growth beyond the minimal recipe.
- No live ECS access from Vulkan backend code.
- No texture-upload feature growth in this slice.
- No mixing of mechanical file moves with semantic refactors.
