---
id: BUG-030
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-030 — Default CPU gate red on headless hosts: `engine.Run()` tests lack the windowless guard (main CI failing since 2026-06-09)

## Goal

- The default CPU correctness gate (`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`) is green again on headless hosts — agent containers and the `ci-linux-clang` GitHub runner — with an explicit, recorded decision about where the BUG-017/BUG-019/BUG-024 regression coverage that depends on a ticking engine loop lives.

## Non-goals

- No weakening or deletion of the three affected regression tests' assertions.
- No xvfb/virtual-display workaround in CI (hides the class instead of handling it).
- No platform-layer redesign beyond what Slice B explicitly scopes.

## Context

- Symptom: `ci-linux-clang` on `main` has concluded **failure on every run since at least 2026-06-09** (runs #854–#866; latest #866 on merge `3330100` fails at step "Run full CPU test suite", all other steps green). The same three tests fail identically in this agent container (2026-06-11 local run, 2902/2907 otherwise green):
  - `RuntimeSandboxAcceptance.ViewportLeftClickSubmitsSelectionPick` (tests/integration/runtime/Test.RuntimeSandboxAcceptance.cpp:410-424)
  - `RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame` (same file, 477-486+)
  - `SandboxEditorUi.DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade` (tests/contract/runtime/Test.SandboxEditorUi.cpp:2722-2750+)
- Mechanism (verified in source): the `ci` preset compiles the GLFW platform backend; `Platform::CreateWindow` selects it at compile time with no runtime fallback (src/platform/Platform.CreateWindow.cpp:15-20). Without a display, GLFW init/window creation fails and the window is **born closed** — `m_ShouldClose = true` in the constructor (src/platform/backends/glfw/Platform.Backend.Glfw.cpp:85, 102). `Engine::Run()` therefore returns before a single variable tick. All failing assertions are consistent with "no tick ever ran": click counters 0, `LastStatus` still its initialized `NoChange`, no import pumped.
- The repository already has the house pattern for this: every other `engine.Run()` test guards with `if (engine.GetWindow().ShouldClose()) { engine.Shutdown(); GTEST_SKIP() << "window backend unavailable…"; }` (tests/contract/runtime/Test.ImGuiAdapterEngineWiring.cpp:120-124, 156-160; also the Gizmo/RenderWorldPool wiring tests — all show as "Skipped" in the same gate runs). The three failing tests were added without the guard (BUG-017/019 and BUG-024 lineage) and verified green on display-ful developer hosts only.
- Why this is severe by repo policy: AGENTS.md §10 — "PR checks must remain green"; merges #977/#978/#979 landed over a red required lane, and every future PR on a headless runner inherits the red gate, masking *new* regressions in the noise.
- Coverage tension the fix must resolve honestly: plain skip-guards restore green but remove the BUG-017/019/024 regression pins from **every** headless run (containers and CI both) — they would then only execute on display-ful hosts, which no automated lane uses. `EngineConfig` currently has no platform-backend selector (src/core/Core.Config.Engine.cppm — Render/Simulation/Window/ReferenceScene/Camera only), so tests cannot opt into a Null *window* loop today, even though BUG-019's retirement explicitly aimed for acceptance coverage "without depending on a concrete platform backend".
- Owner/layer: `tests` (Slice A); `platform` + `runtime` config plumbing (Slice B).

## Slice plan

- **Slice A (this slice — gate restoration).** Add the established `ShouldClose()` skip-guard to the three tests, matching the wiring-test wording so the skip is greppable. Restores green everywhere; coverage temporarily becomes display-host-only, made explicit by the skip message. Defers the loop-coverage decision to Slice B.
- **Slice B (headless loop coverage).** Make the engine loop drivable headless so the three tests can un-skip: either a `WindowConfig`/`EngineConfig` platform-backend selector resolving to the Null window backend at runtime (preferred — keeps the acceptance tests backend-agnostic per BUG-019's goal; requires the Null backend to be compiled alongside GLFW), or an `Engine`-level fallback to the Null window when the configured backend is born closed. Architecture-touching: run the architecture review checklist; record the decision in this task (ADR only if the three-condition rule applies).

## Required changes

- [ ] Slice A: guard `RuntimeSandboxAcceptance.ViewportLeftClickSubmitsSelectionPick` with the house `ShouldClose() → GTEST_SKIP` pattern after `Initialize()`.
- [ ] Slice A: same for `RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame`.
- [ ] Slice A: same for `SandboxEditorUi.DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade` (guard before the `engine.Run()` at Test.SandboxEditorUi.cpp:2750; keep the pre-`Run` negative assertions unguarded — they don't need the loop).
- [ ] Slice B: runtime-selectable Null window path (per the slice plan decision) and un-skip the three tests on headless hosts.
- [ ] Sweep for any other unguarded `engine.Run()` call sites in `tests/` and guard them in Slice A (grep `engine.Run()` across tests; the five wiring tests are already guarded).

## Tests

- [ ] Slice A: default CPU gate green in a headless container: the three tests report **Skipped** (not Failed); no other status changes.
- [ ] Slice A: on a display-ful host the three tests still run and pass (record host in this task, BUG-024B style).
- [ ] Slice B: the three tests pass headless through the Null-window loop, and a contract test pins that a born-closed window yields skip/diagnostic rather than silent no-tick assertions.

## Docs

- [ ] Add the rule to `tests/README.md`: any test driving `Engine::Run()` must either guard with the `ShouldClose()` skip pattern or force a headless-capable window backend — with a pointer to the wiring tests as the reference.

## Acceptance criteria

- [ ] `ci-linux-clang` "Run full CPU test suite" step is green on `main` after merge.
- [ ] Default CPU gate green in a headless agent container (this environment).
- [ ] The coverage location for BUG-017/019/024 regression pins is explicit: skipped-headless (Slice A interim) or headless-capable (Slice B done), never silently absent.
- [ ] No assertion weakened; no test deleted.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ViewportLeftClickSubmitsSelectionPick|InspectorTransformEditFlushedToRenderStateSameFrame|DroppedFilePathsRouteAmbiguousPly' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Deleting or assertion-weakening the three regression tests.
- Adding xvfb or a virtual display to CI to make them "pass".
- Quarantine labels (`flaky-quarantine`) — these are deterministic environment failures, not flakes.
- Slice B landing platform changes without the architecture-review checklist.

## Maturity

- Target: `CPUContracted` for Slice A (gate green, skips explicit). Slice B owns restoring headless `Operational`-equivalent loop coverage; if Slice B is deferred past this task's retirement, it must be split out under its own ID at that point — the coverage gap may not retire silently.
