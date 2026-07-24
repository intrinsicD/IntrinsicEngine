---
id: RUNTIME-189
theme: I
depends_on: [RUNTIME-199, METHOD-032]
---
# RUNTIME-189 — Sandbox debug-draw view for orientation parity diagnostics

## Goal
- Visualize `METHOD-032` orientation diagnostics in the sandbox by producing an
  orientation-specific copied debug snapshot after `RUNTIME-199` retires the
  unused generic registry: oriented-normal glyphs on the
  selected point-cloud entity (colored by flip state and confidence),
  parity-conflict corner markers (colored by conflict count), and an optional
  corner-sign-field slice plane.

## Non-goals
- No new render passes, renderer subsystems, immediate-debug service, adapter
  registry, or opaque binding — use the typed snapshot and existing debug
  packet/pass path.
- No changes to `METHOD-032` method code; this task consumes its public `Result`/`Diagnostics` only. If the diagnostics payload lacks a field the view needs, that change goes through a `METHOD-032` follow-up, not here.
- Not required for `METHOD-032` closure; the method's `CPUContracted` gate is independent of this view.

## Context
- Layering: `runtime` consumes copied method results and encodes the typed
  snapshot into existing graphics debug packets; no method logic or borrowed
  method state lives in runtime.
- Config lane (P3): every view toggle (glyphs on/off, conflict markers, slice plane position, color-by mode) is serializable config reachable by config file, agent/CLI, and UI panel through the same validated apply path — never UI-only.
- Primitive volume: a large cloud can produce millions of glyphs; extraction applies a deterministic budget (cap + stable selection order) and reports truncation in its diagnostics rather than silently dropping.
- Operational proof follows the opt-in `gpu;vulkan` readback smoke policy.

## Required changes
- [ ] Deterministic extraction from `NormalOrientation` result/diagnostics to
      an owner-specific copied orientation-debug snapshot and existing
      primitives
      (glyphs, conflict markers, slice quads), with a documented primitive
      budget and truncation reporting.
- [ ] Config section + sandbox panel toggles driving the same validated apply path (schema-id + version + diagnostics per the config-lane reference model).
- [ ] Wire the view for the selected point-cloud entity in the sandbox editor flow.

## Tests
- [ ] Headless contract test (`unit`/`contract`, `runtime;headless` labels): extraction determinism, primitive budget enforcement, truncation diagnostics, and empty/no-diagnostics inputs producing zero primitives without error.
- [ ] Opt-in `gpu;vulkan` readback smoke proving glyphs/markers render on the promoted path (non-black sample at projected glyph positions), per the GPU smoke authoring policy.

## Docs
- [ ] Sandbox/editor doc section for the view and its config keys.

## Acceptance criteria
- [ ] View renders orientation diagnostics for a selected cloud in the sandbox with all toggles reachable via config and UI.
- [ ] Headless contract tests pass in the default CPU gate.
- [ ] `gpu;vulkan` smoke cited from an actual run for the `Operational` claim.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'OrientationDebugDraw|DebugDraw' --timeout 120
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'OrientationDebugDraw' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No renderer/pass additions, general debug-draw seam/registry, or method-code
  edits.
- No UI-only control paths (config lane parity is mandatory).

## Maturity
- Target: `Operational` on Vulkan-capable hosts via the opt-in smoke; `CPUContracted` (extraction contract) everywhere else.
- `Operational` owned by `RUNTIME-189` (this task): the smoke is part of its own acceptance criteria. If a landing session cannot run the smoke, the slice stops at `CPUContracted` and this task stays open until the cited run exists — it does not retire early.
