---
id: RUNTIME-329
theme: F
depends_on: [ARCH-020, RUNTIME-327]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: refactor of how the lane visualization style is carried and declared; scene format, panel behaviour and rendering are unchanged (the config command newly refuses values outside the declared types and enums); no catalog contract covers this component
---
# RUNTIME-329 — The lane visualization style is declared once (moonshot M2, slice 4)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `51d4dccf2`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator questions (verbatim, 2026-10-28)
3. "start improving the engine by implementing the Moonshot.md. Keep properly track of where you
   and your subagents are at so we can continue anytime properly."
6. "Do both" (the rendering half, `RUNTIME-328`, and the declare-once table for the next recipe)
7. "Full labe style please" (answer to: a small first slice that carries the config struct, or the
   whole lane style: struct carried once plus a scene field table)

## Goal
- Moonshot M2 ("declare once"), `ARCH-020` row 3: apply the `RUNTIME-327` pattern to the next
  authored recipe, the lane color style `Graphics::Components::VisualizationConfig` (17 fields:
  source, color, uniform-by-default, scalar field name and domain, color buffer name,
  interpretation, baked texture, and the scalar style: colormap, auto range, range, bins, isoline
  count, width, color, highlight values).
- Today the style is restated as flat copies in `EditorVisualizationConfigModel` and
  `EditorVisualizationConfigCommand` (`Runtime.VisualizationEditingOperations.cppm`), copied field
  by field in `ToVisualizationConfig` (Actions), `FromVisualizationConfig` (Models), and read back
  field by field by the panels; the scene writes and reads it with ~170 hand-written lines
  (`Runtime.SceneSerialization.cpp`), each with its own token tables; the panel lists the colormap
  names again. Three more field lists: the command's no-change equality (`SameVisualizationConfig`,
  Actions), the panel model's cache signature (`AppendVisualizationConfigSignature`, Models; it
  leaves out the baked-texture flag and the highlight values) and a point-set comparator
  (`PointSetOperations.Processing.cpp`). The command checks only the color interpretation enum.

## Plan
A. **Carry the struct once.** `EditorVisualizationConfigModel` becomes `{HasConfig, Config}` and
   `EditorVisualizationConfigCommand` becomes `{StableEntityId, Target, EnableConfig, Config}` with
   `Config` a `VisualizationConfig` (the command's default source stays UniformColor). Deleted:
   the flat members, `ToVisualizationConfig`'s field copy (the isoline value-count clamp stays),
   `FromVisualizationConfig`'s field copy. Every reader moves to `.Config.X` with unchanged
   behaviour: the "keep the lane's styling" copies in the property-preset and recipe commands keep
   exactly the fields they keep today; panels, editor shell and tests change member paths only.
   `EditorVisualizationPropertyCommand` (a preset request, not a config) is unchanged.
B. **Declare the fields once** in a new `Runtime.LaneVisualizationStyle.cpp` (internal JSON header,
   as `RUNTIME-327`): one row per field with a `ConfigFieldSpec` (type, enum tokens, finite floats,
   `ValueCount` bound 8), the scene path (`source`, ..., `scalar.map`, ...,
   `scalar.isolines.values`), and accessors. From the rows:
   - scene write and read (format unchanged, pinned by a golden test; reading keeps today's
     leniency: only `source` is required, a missing key keeps the default, a wrong type or token
     refuses the config);
   - a style check in `ApplyEditorVisualizationConfigCommand`, at the place of today's
     interpretation check (same precedence, also for a disabling command), run on the normalized
     config (`ToVisualizationConfig`: the highlight count clamped to 8, unused slots ignored):
     refuses an out-of-range enum or a non-finite float with `InvalidVisualizationProperty`
     (today only an unknown interpretation is refused; other values are stored);
   - the command's no-change equality and the panel model's cache signature, both from one
     fingerprint of the rows (the active highlight prefix only; an out-of-range enum keeps its
     number). The signature then also covers the baked-texture flag and the highlight values;
   - the panel's colormap combo through `DrawSpecEnumCombo`.
   Numeric widget ranges (bins 0..64, isolines 0..256, width 0.1..16) stay widget-only, as before
   (the scene and command never limited them; changing that would refuse existing scenes).
- Scene compatibility kept separate from the stricter command: the scene reader keeps its
  container checks (`scalar`, `isolines` present but not objects refuse), unsigned 32-bit
  integers, ignored unknown keys, optional fields, highlight arrays up to 8; numbers are checked
  finite as doubles and then stored as float, as today (a value beyond float range still reads as
  infinity).
- The two "keep the lane's styling" policies stay selective field assignments (presets take
  range/bins/count from the request; recipes keep bins/count); copying the whole `Scalar` would
  change behaviour.
- Kept, with its own semantics: the point-set comparator.
- Not in this slice: rendering consumers (`VisualizationSyncSystem`, extraction), the lane render
  hints (point size, edge width), and an agent surface for the style.

## Counting rule
- Representative change: one new scalar-style field (say an isoline dash length), authored and
  saved. Before: the component (`Graphics.Component.RenderGeometry.cppm`), model and command
  members (`Runtime.VisualizationEditingOperations.cppm`), the copies in `Actions.cpp`, the model
  fill (`Runtime.EditorWorkspaceSnapshots.Models.cpp`), scene write and read
  (`Runtime.SceneSerialization.cpp`), the panel widget (`Sandbox.PanelSupport.cpp`) = 6 files plus
  rendering and tests. Expected after: the component, the table row, the panel widget = 3, plus
  `Actions.cpp` only when a preset must keep the field. Before, also `SameVisualizationConfig`
  (Actions) and the signature (Models) listed it: 6 counted files included those two field lists in
  files already counted; after, they come from the rows. The point-set comparator still lists the
  fields (7th place before and after, `PointSetOperations.Processing.cpp`).

## Tests (predictions as assertions)
- `RuntimeSceneSerialization.VisualizationConfigJsonIsUnchanged` (golden, refactoring test: passes
  before and after): the exact JSON of a config differing from the defaults in every field, as
  entity config and as lane override, and a save of the loaded scene equal to the first.
- `RuntimeSceneSerialization.VisualizationConfigReaderKeepsItsLeniency` (characterization,
  refactoring test, written first): source only loads; unknown keys are ignored; every token of
  source, domain, interpretation and colormap round-trips; non-object `scalar`/`isolines`, wrong
  types, unknown tokens, a bin count of 2^32, a fractional bin count and 9 highlight values refuse;
  0 and 8 highlight values load; the same through a lane override.
- `SandboxEditorUi.VisualizationConfigCommandRefusesUndeclaredValues` (red before: the command
  stores them): an unsupported colormap, a non-finite isoline width and an unknown source are
  refused and the entity keeps its config; a highlight count of 20 still clamps to 8 and applies.
- Table size: a test asserts 17 rows (serialized leaf fields; values and count are one array).
- Existing editor, model, panel, scene and extraction suites pass with member paths changed only.

## Acceptance criteria
- [x] Model and command carry `VisualizationConfig`; the flat members and their copies are gone.
- [x] One field table drives scene write/read, the command's check and the panel's colormap names;
      the scene format is unchanged.
- [x] The count recorded here and in `ARCH-020`.
- [x] Codex plan and code review; focused suites and the full CPU gate pass.

## Verification
```bash
cmake --build --preset ci -j6
ctest --test-dir build/ci -R 'RuntimeSceneSerialization|SandboxEditorUi|SandboxDomainPanels|VertexChannelBindings|RuntimeRenderExtraction|SandboxProcessingPanels' -LE 'gpu|vulkan' --output-on-failure --timeout 60
# full CPU gate: the editor operations interface is imported widely
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' -j6 --output-on-failure --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-28: Plan counter-review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested gpt-6-astra/xhigh, read-only sandbox; thread `01a1818c-23d6-7031-948d-68c62ab5348b`):
  verdict "revise". B1 three more field lists (equality, model signature, point-set comparator);
  B2 characterize scene leniency beyond one golden; B3 check the normalized config, keep clamp and
  precedence, settle float overflow; B4 the focused regex named files, not suites. O1 keep the two
  selective styling policies; O2 the interpretation enum was already checked. All folded into the
  plan above. Recursion protection instruction-only.
- 2026-10-28: Implemented. A: model and command carry `VisualizationConfig` (flat members, both field
  copies gone; the two styling policies kept as selective assignments; every rewritten command
  initializer names its source, so the UniformColor command default never falls back to the
  component's Material). B: `Extrinsic.Runtime.LaneVisualizationStyle` (17 rows) drives scene write
  and read (≈280 hand-written scene lines and the source/domain token helpers deleted), the command
  check (at the old interpretation check, on the clamped config), the no-change equality and the
  model's cache signature (one fingerprint), and the panel's colormap combo.
  - Predictions held: golden and leniency tests passed on the old scene code (refactoring /
    characterization); the refusal test was red before (`Applied`, colormap stored as `Count`).
  - Mutations, one at a time, restored after: the old hand-written cache signature →
    `VisualizationModelCacheSeesEveryStyleField` red only (the old signature really missed the baked
    flag and the highlight values: a stale panel model, now fixed); no style check → the refusal test
    red only; one wrong scene key in a row → both scene tests red only.
  - Focused suites 446/446 (RuntimeSceneSerialization, SandboxEditorUi, SandboxDomainPanels,
    VertexChannelBindings, RuntimeRenderExtraction, SandboxProcessingPanels).
  - Count, representative change (a new scalar-style field, authored and saved). Before 7 places:
    the component, `VisualizationEditingOperations.cppm` (model and command members), `Actions.cpp`
    (copy to the component, equality, styling copies), `Models.cpp` (model fill, signature),
    `SceneSerialization.cpp` (write, read), `PanelSupport.cpp` (widget), point-set comparator.
    After: the component, the table row (`Runtime.LaneVisualizationStyle.cpp`), the panel widget = 3,
    plus `Actions.cpp` only when a preset must keep the field and the point-set comparator when it
    must compare it. Rendering consumers unchanged.
- 2026-10-28: Full CPU gate 5933/5933 (the editor operations interface is imported widely).
- 2026-10-28: Code review (Claude author → Codex reviewer, fresh session, `codex exec`, requested
  gpt-6-astra/xhigh, read-only sandbox; thread `01a1819b-5eb7-7402-9c3d-f5c0334faa9d`): verdict "revise
  required". P1 the JSON-text fingerprint changed equality (-0 vs 0 no longer NoChange) and merged
  NaN/±inf in the cache signature; N1 avoid serializing per lookup; N2 module inventory and the count
  in `ARCH-020`; N3 test holes (value-only highlight change, the whole stored config after a refusal,
  disabling command, precedence); N4 an out-of-range colormap previewed as Heat. Fixed:
  `SameLaneVisualizationStyle` (row by row, numeric) and `LaneVisualizationStyleHash` (FNV over exact
  float bits) replace the fingerprint; inventory regenerated; tracker updated; the tests cover a
  value-only change, ±inf, 0/-0, an inactive slot, the full stored config, a disabling command and
  -0 NoChange; the panel shows an out-of-range colormap as Viridis. Mutations: a text hash → the
  cache test red only; text equality → the refusal test red only (its -0 NoChange).
- 2026-10-28: After the fixes: full CPU gate 5933/5933; docs sync rules satisfied; module inventory
  fresh. Duplication (jscpd MCP, exact): one new 11-line clone, the rows-to-specs derivation shared
  with `Runtime.GeometryVectorFieldStyle.cpp`; kept (two tables; a third would justify a helper).
- 2026-10-28: Delta review 1 (same thread, `codex exec resume`): "revise required". P1 the hash XORed
  whole 64-bit words, so two sign flips cancelled ([0.25,0.5] → [-0.25,-0.5] kept the signature);
  N strings were hashed without their length. Fixed: byte-wise FNV-1a and a length prefix, as
  `MixSignature`; regressions for both collisions (mutation back to word mixing → red; length prefix
  removed → red). Delta review 2 (same thread): "approve", no remaining findings.
- 2026-10-28: Final full CPU gate 5933/5933 on the approved diff.
- 2026-10-28: Live check at `51d4dccf2` (same host and run as `RUNTIME-328`'s): `show_property`
  colored the Duck's surface lane by mean curvature; the saved scene's `visualization` and
  `visualizationLanes.surface` objects have the old keys and tokens; a hand-edited scene (Jet, manual
  range, 6 bins, 10 isolines width 2.5, one highlight value) loaded and drew binned Jet with isolines;
  saving it again gave the same entities up to float32 rounding; the same file with `"map": "jet"` was
  refused (`SceneLoadFailed`, InvalidFormat) and the scene stayed; validation errors 0. Not checked:
  the panel's colormap combo and the cache fix in the UI (no panel input through the agent socket).
