# BUG-139 artifacts

Screenshots from the live `ExtrinsicSandbox` session described in
`tasks/done/BUG-139-imgui-adapter-drops-key-events.md`, cropped to the
`File / Import` window's `Path` field. The session used the `ci-vulkan` build
with the promoted Vulkan device on a nested `Xephyr` display, because the host
seat was locked and its lock screen holds the real seat's input grab. Every
keystroke was delivered as a real X key event through the GLFW backend and the
engine-owned `ImGuiAdapter::PumpEvents` translation — nothing was written into
`ImGuiIO` directly.

- `path-corrected-with-backspace.png` — the proof this task's `## Maturity`
  section names. Top row: `tests/data/sculpt.objXX` as typed, with a deliberate
  two-character typo. Bottom row: `tests/data/sculpt.obj` after two
  `BackSpace` presses.
- `editing-keys-and-clipboard-chords.png` — the remaining acceptance-criteria
  keys, one row per step, top to bottom:
  1. `tests/data/sculpt.obj` (starting contents)
  2. `Home` then `Delete` ×6 → `data/sculpt.obj`
  3. `End`, `Left` ×4, `Delete` → `data/sculptobj`
  4. `Ctrl+A`, `Ctrl+X` → empty
  5. `Ctrl+V` → `data/sculptobj` (the clipboard round-trip the report called
     unreachable)
  6. `Ctrl+Z` → empty
