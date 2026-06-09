---
id: BUG-024B
theme: G
depends_on: [BUG-024]
maturity_target: Operational
---
# BUG-024B — Vulkan pixel-shift smoke for sandbox transform edits

## Goal

- Prove the BUG-024 fix `Operational` on a Vulkan-capable host: an opt-in `gpu;vulkan` smoke edits the `ReferenceTriangle` local position through the promoted UI/editor command path and observes the rendered pixels move.

## Non-goals

- No changes to the runtime pre-render transform flush shipped by `BUG-024` unless the smoke exposes a real defect.
- No shader or pipeline changes unless the smoke fails with correct CPU snapshot state (BUG-024 hypothesis 3/4 territory; open a focused follow-up if that happens).
- No CPU-gate test changes; the CPU/null contract is owned and closed by `BUG-024`.
- No new editor/UI features.

## Context

- `BUG-024` (done) fixed the stale-`WorldMatrix` path by adding `FlushPreRenderTransformState` to `Engine::RunFrame()` and closed at `CPUContracted` with engine-level and extraction-level CPU regression coverage. The Vulkan operational proof was explicitly deferred to this task per the BUG-024 Maturity statement.
- The smoke belongs in `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` next to the existing default-recipe present smokes, using the existing readback helpers.
- The edit must go through the UI/editor command path (`ApplySandboxEditorTransformEdit` or the live `EditorCommandHistory` route) — not the manual test helper that writes `Transform::WorldMatrix` directly (forbidden by BUG-024).

## Required changes

- [ ] Add an opt-in `gpu;vulkan;runtime;regression` smoke in `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` that renders the default `ReferenceTriangle`, applies a UI/editor-command transform edit large enough to move the triangle out of the original sample location, runs enough frames for the pre-render flush plus swapchain latency, and reads back pixels.
- [ ] Assert the original triangle sample location returns to the background color and the expected shifted sample location contains the triangle color.
- [ ] If pixels do not move while CPU snapshot state is correct, record diagnostics (pass stats, culling buckets, pipeline pairing) in this task and open the focused follow-up instead of patching shaders blind.

## Tests

- [ ] The new smoke runs under `ctest --test-dir build/ci-vulkan -L gpu -L vulkan` and is excluded from the default CPU gate by its labels.
- [ ] Existing `RuntimeSandboxAcceptanceGpuSmoke` cases keep passing on the same host/run.

## Docs

- [ ] Record the executed Vulkan verification (host/device, pass counts) in this task file on retirement.
- [ ] Update `tasks/backlog/bugs/index.md` and the retirement log when closing.

## Acceptance criteria

- [ ] The pixel-shift smoke passes on a Vulkan-operational host and is cited in `Verification` as actually run.
- [ ] BUG-024's fix maturity is upgraded to `Operational` by this task's retirement narrative.

## Verification

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- Do not write `Transform::WorldMatrix` or GPU instance buffers directly from the test to make pixels move.
- Do not weaken or skip the existing CPU regression coverage from `BUG-024`.
- Do not change default-recipe shaders/pipelines without a failing smoke plus recorded diagnostics.

