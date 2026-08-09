---
id: BUG-139
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug139"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-08T12:01:18Z"
contract_schema: 1
contracts: []
contract_review: >-
  Input-event plumbing between platform and the ImGui adapter. No geometry
  element-domain, property-coherence, support-radius, parameterization, or
  method-integration surface is consumed or published.
---
# BUG-139 — ImGui receives no key events, so every editor text field is append-only

## Status

- Completed and retired on 2026-08-09.
- Completion commit: this retirement commit.

The engine-owned pump landed on 2026-08-08 with its adapter contract coverage;
what stayed open was the live confirmation this task's `## Maturity` section
requires, because that session could not drive an interactive GUI. It has now
been driven.

### Live-session evidence (2026-08-09)

The host's seat was locked, so the session ran against a nested `Xephyr`
display (`:7`) whose XTEST input is independent of the lock screen's grab, with
the `ci-vulkan` `ExtrinsicSandbox` build and the promoted Vulkan device. Every
keystroke below was delivered as a real X key event through the GLFW backend
and the engine-owned pump — nothing was written into `ImGuiIO` directly.

In `File / Import`'s `Path` field:

| Step | Keys | Field contents |
| --- | --- | --- |
| typed | `tests/data/sculpt.objXX` | `tests/data/sculpt.objXX` |
| corrected | `BackSpace` ×2 | `tests/data/sculpt.obj` |
| | `Home`, `Delete` ×6 | `data/sculpt.obj` |
| | `End`, `Left` ×4, `Delete` | `data/sculptobj` |
| | `Ctrl+A`, `Ctrl+X` | *(empty)* |
| | `Ctrl+V` | `data/sculptobj` |
| | `Ctrl+Z` | *(empty)* |

That is the Backspace correction the Maturity section names, plus every other
editing key and clipboard chord the acceptance criteria list. `Ctrl+V`
restoring the cut text also exercises the clipboard round-trip through
`GetClipboardText`/`SetClipboardText`, which the report called unreachable.

The same session then typed a full absolute path into that field and imported
`tests/data/sculpt.obj`, which is the workflow the report said was close to
unusable. Screenshots are in `tasks/evidence/BUG-139/artifacts/`.

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
- [x] Translate `Platform::KeyEvent` into `io.AddKeyEvent` with a GLFW-keycode →
      `ImGuiKey` mapping, including modifier key state. Chord state
      (`ImGuiMod_Ctrl` and friends) is derived from the left/right modifier
      keys' own press/release and is left/right-aware, so releasing one side
      does not clear a modifier the other side still holds.
- [x] Repeat is explicitly out of scope, and the reason is recorded at the
      mapping: ImGui synthesises key repeat itself from how long a key has been
      held (`ImGuiIO::KeyRepeatDelay` / `KeyRepeatRate`), which is what
      `InputText` reads. Forwarding OS repeats on top of that would double
      them, so the GLFW backend keeps filtering `GLFW_REPEAT`.
- [x] Chose the engine-owned pump. The five unreachable
      `ImGui_ImplGlfw_*Callback` forwards, the `HasImGuiGlfwBackend()`
      predicate, and both ImGui includes are gone from
      `Platform.Backend.Glfw.cpp`; the platform target no longer links
      `imgui_lib` at all, so the layer names no ImGui type.
- [x] Confirmed: `BackspaceShortensSeededInputTextAndCapturesKeyboard` asserts
      `CaptureSnapshot().CapturedKeyboard` while the field is focused, and the
      capture snapshot is recorded from `WantCaptureKeyboard` exactly as
      before — the pump adds input, it does not touch the gate.

## Tests
- [x] Add an ImGui adapter contract test asserting a pumped `KeyEvent` reaches
      `ImGuiIO` as the expected `ImGuiKey` down/up, with modifiers
      (`PumpedKeyEventsReachImGuiIoWithModifierChords`, covering the Ctrl+V
      chord, left/right modifier independence, and all nine editing keys the
      report named).
- [x] Add a test asserting an `InputText` seeded with text is shortened by a
      Backspace key event
      (`BackspaceShortensSeededInputTextAndCapturesKeyboard`).
- [x] Add a test asserting `WantCaptureKeyboard` gates engine input actions
      while an `InputText` is active — asserted in the same test through the
      adapter's capture snapshot, which is the value `EditorUiModule` copies
      into the frame and which already gates camera/gizmo/pick.
- [x] Default CPU gate stays green (4146/4146, expected GLFW/LSan skip).

## Docs
- [x] Update `src/platform/README.md` (event-consumption table) and the runtime
      ImGui adapter prose to describe key-event ownership accurately.

## Acceptance criteria
- [x] Backspace, Delete, arrows, Home/End, Enter, Tab, Escape and Ctrl+A/C/V/X/Z
      work in Sandbox text fields. Proven at the adapter contract level and
      then in the live session recorded under `## Status`.
- [x] No unreachable ImGui platform-backend forwarding remains in
      `Platform.Backend.Glfw.cpp`.
- [x] Camera/gizmo/pick input capture gating is unchanged in behavior: the
      capture snapshot is computed from the same `WantCaptureKeyboard` /
      `WantCaptureMouse` reads, and no gating call site changed.

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
- Reached `Operational` on 2026-08-09; the session is recorded under
  `## Status`.
