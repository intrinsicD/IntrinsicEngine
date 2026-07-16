---
id: RUNTIME-174
theme: F
depends_on:
  - CI-003
  - RUNTIME-159
maturity_target: Operational
---
# RUNTIME-174 — Privatize the ImGuiEditorBridge surface

## Status

- Completed on 2026-07-16 at `Operational`.
- Implementation commit: `724a7489`.
- Verification: focused CPU coverage passed `52/52`, runtime layering passed
  `23/23`, and the final default CPU-supported gate passed `4178/4178` after a
  successful `IntrinsicTests` build. Strict structural/review gates passed.

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
- Current consumer inventory: only `Runtime.Engine.cppm` and
  `Runtime.Engine.cpp` import the bridge in production; no test imports the
  module directly.
- Right-sized shape: keep the value member and separate implementation unit,
  but attach the declaration and implementation to `Runtime.Engine` rather
  than introducing a pimpl allocation or a replacement public module.
- Reintroduce a standalone bridge module only when a tracked non-Engine
  production consumer lands.

## Required changes
- [x] Confirm all production consumers are Engine implementation/interface
      details.
- [x] Move the bridge declaration to private runtime header/source glue or fold
      it into the Engine implementation seam.
- [x] Preserve diagnostics and mouse/keyboard capture queries.
- [x] Update source-scan tests that assert module presence to assert ownership
      and behavior instead.
- [x] Remove the `.cppm` from module file sets if retired and record metrics.

## Tests
- [x] Run runtime engine layering, Sandbox editor, ImGui/editor bridge, and
      sandbox acceptance tests.
- [x] Run strict layering and the default CPU-supported CTest gate.

## Docs
- [x] Update runtime/editor docs if `ImGuiEditorBridge` stops being a named
      module.
- [x] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [x] Engine still owns Begin/End bracketing, capture queries, diagnostics, and
      renderer overlay attachment.
- [x] The bridge no longer exposes a public module surface unless an explicit
      consumer inventory justifies it.
- [x] Sandbox editor behavior is unchanged under focused tests.

## Evidence

- Consumer inventory: two named-module production imports, both in Engine,
  became zero; no app or test imported the bridge module.
- Surface metrics: runtime modules `83 -> 82`, repository modules `390 -> 389`,
  runtime public `.cppm` entries `83 -> 82`, and explicit import directives
  across the affected Engine/bridge units `143 -> 138`. The replacement is a
  38-line include-only declaration with no module directive.
- Compile diagnostics: a local `CCACHE_DISABLE=1` rebuild of the old standalone
  BMI recorded a 13.006s compiler edge (16.58s command wall time). After the
  change that edge and BMI do not exist; the current ninja log records the
  bridge implementation edge at 4.143s and the Engine interface edge at
  68.753s versus its previous 67.883s entry. These are structural diagnostics,
  not an overall build-speed claim.
- Focused CPU coverage passed `52/52`; the directly invoked
  `RuntimeEngineLayering.*` suite passed `23/23`; the default CPU-supported
  gate passed `4178/4178` after a successful `IntrinsicTests` build.
- Strict layering, test layout, task policy, doc links, root hygiene, PR
  contract, skill-mirror sync, and the clean-workshop validator bundle passed.
  Three independent reviews found no implementation, architecture, scope, or
  test blocker.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEnginePrivateGlue|ImGuiAdapter|EditorUiHost|SandboxEditorPresentation|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
build/ci/bin/IntrinsicRuntimeIntegrationTests --gtest_filter='RuntimeEngineLayering.*'
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
