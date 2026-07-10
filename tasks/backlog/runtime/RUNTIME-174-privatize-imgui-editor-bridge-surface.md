---
id: RUNTIME-174
theme: F
depends_on:
  - CI-003
  - RUNTIME-159
maturity_target: Operational
---
# RUNTIME-174 — Privatize the ImGuiEditorBridge surface

## Goal
- Keep `ImGuiEditorBridge` as an Engine-owned implementation service while
  removing its low-fanout exported module surface and preserving editor overlay,
  callback, capture, and diagnostics behavior.

## Non-goals
- No ImGui behavior, panel, docking, renderer overlay, or input-capture changes.
- No app ownership of renderer overlays or runtime lifecycle.
- No changes to `SandboxEditorUi` content migration owned by `ARCH-006`.

## Context
- Owner/layer: `runtime`; the bridge owns runtime-side Dear ImGui bracketing
  and renderer overlay attachment.
- Local 2026-07-10 triage measured `Runtime.ImGuiEditorBridge.cppm` at up to
  17.877s with only Engine-side production consumers.
- `RUNTIME-159` extracted this bridge; this follow-up keeps that service
  boundary but makes it private if no public module value remains.

## Required changes
- [ ] Confirm all production consumers are Engine implementation/interface
      details.
- [ ] Move the bridge declaration to private runtime header/source glue or fold
      it into the Engine implementation seam.
- [ ] Preserve diagnostics and mouse/keyboard capture queries.
- [ ] Update source-scan tests that assert module presence to assert ownership
      and behavior instead.
- [ ] Remove the `.cppm` from module file sets if retired and record metrics.

## Tests
- [ ] Run runtime engine layering, Sandbox editor, ImGui/editor bridge, and
      sandbox acceptance tests.
- [ ] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [ ] Update runtime/editor docs if `ImGuiEditorBridge` stops being a named
      module.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] Engine still owns Begin/End bracketing, capture queries, diagnostics, and
      renderer overlay attachment.
- [ ] The bridge no longer exposes a public module surface unless an explicit
      consumer inventory justifies it.
- [ ] Sandbox editor behavior is unchanged under focused tests.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'ImGuiEditorBridge|SandboxEditorUi|RuntimeEngineLayering|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Moving renderer overlay ownership into `app`.
- Changing input capture or callback order while privatizing the surface.
- Using this task to move Sandbox panel content; that is `ARCH-006`.

## Maturity
- Target: `Operational`; this is a behavior-preserving private-shape change to
  an operational editor bridge, so no `Operational` follow-up is owed.
