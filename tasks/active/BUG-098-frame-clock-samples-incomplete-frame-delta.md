---
id: BUG-098
theme: G
depends_on: []
maturity_target: Operational
---
# BUG-098 — Frame clock samples an incomplete frame delta

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`.
- Next gate: completed-frame clock regression plus the production-delay real
  File / Import hover integration.

## Goal

- Drive simulation, runtime hooks, camera motion, and Dear ImGui timers with
  the clamped duration of the previous completed frame instead of the few
  microseconds elapsed near the start of the current frame.

## Non-goals

- No immediate-hover override or UI-private timer workaround.
- No fixed-step scheduler, frame-pacing, or render-loop phase redesign.
- No synthetic production clock, global time service, or new timing
  abstraction.
- No change to the configured maximum-frame-delta policy.

## Context

- Symptom: a pointer held over the disabled `File / Import` button for more
  than two wall-clock seconds never shows the documented prerequisite
  tooltip. The same undersized delta reaches fixed-step accumulation,
  variable ticks, runtime modules, and camera controls.
- Expected behavior: `FrameClock::FrameDeltaClamped()` returns the previous
  completed frame duration, clamped to the configured maximum. The first
  frame may report zero; deliberate minimized-window sleeps remain excluded.
- Root cause: `Engine::RunFrame()` calls `BeginFrame()` and then samples
  `FrameDeltaClamped()` before simulation/render/present. The current
  implementation recomputes `now - m_FrameStart`, so it omits the dominant
  work later in the frame even though `EndFrame()` already stores the correct
  completed duration for telemetry.
- Live GDB evidence on 2026-07-16 held the disabled import control under a
  stable cursor and ID while `ImGuiIO::DeltaTime` stayed near 10 microseconds;
  107 frames advanced the 0.15-second hover timer by only about 0.003 seconds.
  Forcing one valid 0.25-second delta made the exact runtime-owned tooltip
  appear immediately.
- Owner: `core` owns the dependency-free clock contract; `runtime` consumes it
  in the composition loop; `app` remains an unchanged presentation consumer.

## Required changes

- [ ] Make the clamped frame-delta query use the stored previous completed
      duration and keep its result in the documented non-negative bounded
      range.
- [ ] Update FrameClock API comments and runtime documentation so
      `LastRawDelta()` is no longer described as telemetry-only while the
      simulation/UI delta is sampled from the same completed-frame record.
- [ ] Preserve `BeginFrame()`/`Resample()`/`EndFrame()` ownership and the
      minimized-window sleep exclusion contract.
- [ ] Keep Dear ImGui's production `Stationary | DelayShort` tooltip policy;
      do not hide the runtime defect with `DelayNone`.

## Tests

- [ ] Add a core unit regression proving the clamped delta remains exactly the
      stored completed-frame duration after `EndFrame()` instead of continuing
      to accumulate current wall time.
- [ ] Extend the real Null-window `File / Import` presentation test to retain
      production tooltip flags, advance completed frame time deterministically,
      and observe the disabled control's tooltip after the configured delay.
- [ ] Preserve zero-limit, non-negative, resample, and first-frame behavior.

## Docs

- [ ] Update `src/core/README.md` and `src/runtime/README.md` with the
      completed-frame delta contract.
- [ ] Regenerate the module inventory only if the exported module surface
      changes structurally rather than semantically.
- [ ] Refresh task indexes/session brief and retirement records on closure.

## Acceptance criteria

- [ ] Every engine time consumer receives a non-negative delta derived from
      the previous completed frame and bounded by `m_MaxFrameDelta`.
- [ ] A default-delay disabled-control tooltip appears through the real
      `Engine::Run()`/ImGui adapter path without changing production hover
      flags.
- [ ] Deliberate idle/minimized sleep does not inject a catch-up timestep, and
      the first frame remains deterministic.
- [ ] Focused core/runtime tests and the default CPU-supported gate pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCoreUnitTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure \
  -R '^CoreFrameClock\.|^SandboxEditorPresentation\.FileImportDisabledReasonRendersThroughRealHoveredControl$' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Making disabled tooltips immediate or adding a special UI-only delta.
- Sampling current-frame elapsed time before the frame's meaningful work and
  calling it the frame delta.
- Counting deliberate minimized/idle sleep as simulation catch-up time.
- Adding a new clock service, scheduler dependency, or app-to-core control
  path.

## Maturity

- Target: `Operational` through the real Null-window `Engine::Run()` and
  ImGui-adapter tooltip path, with the dependency-free clock semantics pinned
  at `CPUContracted` by core unit tests. No Vulkan-specific follow-up is owed.
