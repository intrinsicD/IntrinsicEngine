---
id: BUG-139
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Input-event plumbing between platform and the ImGui adapter. No geometry
  element-domain, property-coherence, support-radius, parameterization, or
  method-integration surface is consumed or published.
---
# BUG-139 — ImGui receives no key events, so every editor text field is append-only

## Goal
- Deliver platform key events to ImGui so editor text fields support editing
  keys (Backspace, Delete, arrows, Home/End, Enter, Tab, Escape) and clipboard
  shortcuts.

## Non-goals
- No engine-wide input-action rebinding system.
- No change to camera/gizmo/pick input capture gating semantics.
- No adoption of the upstream `ImGui_ImplGlfw` platform backend if the
  engine-owned adapter is kept; either wire the engine adapter or adopt the
  upstream backend, not both.

## Context
- Symptom: in every `ImGui::InputText` in the Sandbox (`File / Import` path,
  `File / Scene` path, the render-recipe draft buffer, and every numeric
  input/drag), printable characters insert but **Backspace, Delete, Left/Right,
  Home/End, Enter, Tab, Escape and all Ctrl shortcuts do nothing**. A typo
  cannot be corrected, a field cannot be cleared, text cannot be selected or
  pasted. Only a fresh empty field can be filled, in a single uninterrupted
  pass.
- Mechanism: `Runtime.ImGuiAdapter.cpp:PumpEvents()` (lines ~341-385) translates
  `CursorEvent`, `MouseButtonEvent`, `ScrollEvent`, and `CharEvent` into ImGui
  IO and deliberately drops `Platform::KeyEvent`. The inline comment states the
  omission is intentional: "GLFW key-code -> ImGuiKey mapping is owned by the
  editor input-binding slice (Slice A non-goal)". Repo-wide there is exactly one
  ImGui input call — `io.AddInputCharacter` at
  `Runtime.ImGuiAdapter.cpp:367` — and no `io.AddKeyEvent` anywhere.
- Compounding defect: `Platform.Backend.Glfw.cpp` guards five callbacks on
  `HasImGuiGlfwBackend()` (lines 299, 309, 319, 327, 335), which tests
  `ImGui::GetIO().BackendPlatformUserData != nullptr`. `ImGui_ImplGlfw_Init*` is
  **never called anywhere in the repo**, so that predicate is always false and
  all five `ImGui_ImplGlfw_*Callback` forwards are unreachable. The code reads
  as if key forwarding is already wired when it is not.
- Note: `KeyEvent` *is* consumed for polled input — `Platform.Backend.Glfw.cpp:244`
  routes it to `GetInput().SetKeyState(...)` — so camera/WASD-style polling is
  unaffected. Only ImGui is starved.
- Clipboard is otherwise wired (`Runtime.ImGuiAdapter.cpp:708` / `:717` call
  `GetClipboardText` / `SetClipboardText`) but is unreachable because Ctrl+V can
  never be delivered.
- Impact: combined with the absence of any file chooser (`UI-047`), entering an
  import or scene path is close to unusable; drag-and-drop is currently the only
  practical way to load a file.
- Owner: `runtime` ImGui adapter; `platform` GLFW backend owns the dead
  forwarding branch.

## Required changes
- [ ] Translate `Platform::KeyEvent` into `io.AddKeyEvent` with a GLFW-keycode →
      `ImGuiKey` mapping, including modifier key state.
- [ ] Deliver key-repeat as well as press/release, or state explicitly why
      repeat is out of scope (`Platform.Backend.Glfw.cpp:301` currently filters
      `GLFW_REPEAT` out before `Emit`).
- [ ] Remove the five unreachable `ImGui_ImplGlfw_*Callback` forwards and the
      `HasImGuiGlfwBackend()` predicate, or initialize the upstream backend and
      remove the engine-owned pump — pick one and record which in `Context`.
- [ ] Confirm `io.WantCaptureKeyboard` remains correct once ImGui actually sees
      keys, so editor typing does not leak into engine input actions.

## Tests
- [ ] Add an ImGui adapter contract test asserting a pumped `KeyEvent` reaches
      `ImGuiIO` as the expected `ImGuiKey` down/up, with modifiers.
- [ ] Add a test asserting an `InputText` seeded with text is shortened by a
      Backspace key event.
- [ ] Add a test asserting `WantCaptureKeyboard` gates engine input actions
      while an `InputText` is active.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `src/platform/README.md` (event-consumption table) and the runtime
      ImGui adapter prose to describe key-event ownership accurately.

## Acceptance criteria
- [ ] Backspace, Delete, arrows, Home/End, Enter, Tab, Escape and Ctrl+A/C/V/X/Z
      work in Sandbox text fields.
- [ ] No unreachable ImGui platform-backend forwarding remains in
      `Platform.Backend.Glfw.cpp`.
- [ ] Camera/gizmo/pick input capture gating is unchanged in behavior.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'ImGuiAdapter|EditorInputCapture|SandboxEditor' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Leaving both an engine-owned pump and upstream-backend forwarding in place.
- Routing key events straight to ImGui while bypassing the existing capture
  snapshot that gates camera/gizmo/pick input.

## Maturity
- Target: `Operational` — proven by a live `ExtrinsicSandbox` session in which a
  typed path is corrected with Backspace, not by adapter unit coverage alone.
