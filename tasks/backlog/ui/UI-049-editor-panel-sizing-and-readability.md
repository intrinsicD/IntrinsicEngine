---
id: UI-049
theme: J
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
  Editor panel sizing, label/text layout, table columns, and default control
  values. No geometry element-domain, property, support-radius,
  parameterization, or method-integration surface changes.
---
# UI-049 — Editor panels are sized so that labels clip and results are hidden

## Goal
- Make editor panels show their own content: readable labels, visible run
  results, readable tables, and wrapped diagnostics.

## Non-goals
- No change to which controls or results each panel computes.
- No workspace/menu/persistence work (`UI-048` owns that).
- No diagnostic classification or scoping changes (`BUG-141` owns that).

## Context
Observed across a full pass over the mesh domain and processing panels:

- **Domain windows default to 340×300** (`SetNextWindowSize(ImVec2(340, 300),
  ImGuiCond_FirstUseEver)` in `Sandbox.MeshProcessingPanels.cpp:511-512`), which
  is smaller than their content. The consequence is not cosmetic: after clicking
  **Denoise**, the run result (`Last denoise run: Applied`, moved/written
  counts, pinned-boundary count, sigma values) is **below the fold behind a
  scrollbar**, so the panel looks as though nothing happened. The result was
  only visible after manually enlarging the window.
- **Labels clip** at the panel edge — "Normal iteratio…", "Vertex iteration…",
  "Max error (0 = unlimit…", "Sizing law" — because the label column is not
  budgeted against the window width.
- **Diagnostic text does not wrap**:
  `UnsupportedGeometryDomain: PointCloud window req…` is cut at the window edge
  rather than wrapped.
- **The AssetIO queue table is unreadable.** Columns truncate to `Mes`, `sculp`,
  `Com`, `0.2`, `Dia…` — every column is narrower than its content and the
  header labels themselves are elided.
- **`File / Import` opens roughly 850 px tall for ~250 px of content**, about
  two thirds empty grey.
- **`Mesh / Processing / Simplify` defaults `Target faces` to 0**, which
  correctly disables the Simplify button but gives no usable starting point.
  With `BUG-139` open (no typing) the only way to set it is to drag a slider
  blindly; a proportional default (e.g. half the current face count) would make
  the panel usable immediately.
- **The OS window is titled "Modular Vulkan Engine"**, not the application name.
- Impact: individually minor, collectively these make the processing panels feel
  broken — the Denoise case in particular reads as "the button does nothing".
- Owner: `app` owns panel layout, sizing, and default control values; the OS
  window title is set on the `platform` window creation path.

## Required changes
- [ ] Size domain/processing windows to their content, or make results visible
      without manual resizing.
- [ ] Budget the label column so control labels are not clipped.
- [ ] Wrap diagnostic and status text instead of clipping it.
- [ ] Give AssetIO queue columns readable widths/stretch behavior.
- [ ] Fit the `File / Import` window to its content.
- [ ] Give `Target faces` a proportional non-zero default derived from the
      selected mesh.
- [ ] Set the OS window title to the application name.

## Tests
- [ ] Add a contract test asserting the simplify command default target-face
      count is derived from the selected mesh rather than fixed at 0.
- [ ] Extend an editor panel model test to assert result text is part of the
      panel model when a run has completed (guards against results existing but
      being unreachable).
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `src/app/Sandbox/README.md` where it describes panel layout and the
      AssetIO queue columns.

## Acceptance criteria
- [ ] A run result is visible in its panel without resizing the window.
- [ ] No control label or diagnostic line is clipped at the panel edge.
- [ ] AssetIO queue cells show their values.
- [ ] Simplify opens with a usable target-face default.
- [ ] The OS window carries the application name.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'SandboxEditor|EditorWorkspaceSnapshots' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Hiding results or diagnostics to make panels fit.
- Hard-coding pixel sizes that break at a different UI scale.
