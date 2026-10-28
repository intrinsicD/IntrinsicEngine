---
id: RUNTIME-331
theme: F
depends_on: [ARCH-020, RUNTIME-327, RUNTIME-328, RUNTIME-330, GRAPHICS-165]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, the Vulkan smokes, the count experiment and CI
maturity_target: Operational
contract_schema: 1
contracts: []
contract_review: refactor of where a pass-through vector-field style field is declared; the layer, the agent schema, scene keys, the packet style, the record bytes and the drawn result are unchanged; no catalog contract covers this path
---
# RUNTIME-331 — A pass-through vector-field style field is one row (moonshot M2, slice 7)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
11. (2026-10-28) "Bitte slice 7" (answer to: generate the layer member, style row, glyph-style
    member and extraction copy from the same rows, so a pass-through field is one row plus its
    shader use, and a panel widget unless the panel is generic)

## Goal
- Moonshot M2, Q1 ("29 files become 3 or fewer", tests included; `ARCH-020` row 3). After
  `GRAPHICS-165` a new uniform float that the layer passes unchanged to the shader (say an arrow head
  size) still touches 7 production files: the layer member (`Runtime.GeometryPresentation.cppm`),
  the style row (`Runtime.GeometryVectorFieldStyle.cpp`), the panel widget
  (`Sandbox.DomainPanels.cpp`), the glyph-style member (`Graphics.VisualizationPackets.cppm`), the
  extraction copy (`Runtime.RenderExtraction.VectorFields.cpp`), the record row
  (`vector_field_draw_record.glsl`) and the shader use; plus the glyph-style check
  (`Graphics.VisualizationPackets.cpp`) when it is bounded. Today exactly one field is such a
  pass-through: `LineWidthPx`.
- After this slice: one row in a new row file plus the shader use, i.e. 2 production files. Tests,
  measured by experiment (see Log): generic tests cover the glue for every row; a new row needs the
  feature's own visual test and edits two deliberate pins (the record test's size and offset
  literals, the scene golden of `RuntimeSceneSerialization.VectorFieldLayerJsonIsUnchanged`).
  Measured 2 + 3 = 5 against Q1's 3 (predicted 4: the scene golden was missed).

## Plan
1. **Row file** `assets/shaders/common/vector_field_style_rows.glsl` (beside the record rows; the
   shader build already depends on every `*.glsl` there). One row per pass-through float:
   `VF_STYLE_ROW(Member, Default, agent_name, sceneKey, SceneRequired, "Panel label",
   "Description", Min, Max)`; inclusive finite bounds as float literals; floats only (a second type
   adds an argument when it is needed). First and only row: `LineWidthPx` (2.0f, `line_width`,
   `lineWidthPx`, required, "Width (px)", "Arrow width in pixels.", 0.5f, 32.0f). glslc ignores
   the discarded string and C++ arguments (checked with glslc 2023.8 in a scratch shader).
2. **Readers of the rows**, each defines `VF_STYLE_ROW` locally and undefines it after the include:
   - layer `GeometryVectorFieldLayerRecipe`: `float Member{Default};` at `LineWidthPx`'s place
     (member order and designated initializers unchanged);
   - glyph style `VectorFieldGlyphStyle`: the same, at its `LineWidthPx` place;
   - style table: one `Row` per line (spec name, `Float`, description, `Min`/`Max`, scene key,
     required flag, member get/set) at `line_width`'s place; `kRows` deduces its size
     (`std::to_array`);
   - glyph-style check (`IsValidVectorFieldGlyphStyle`): finite and inside `[Min, Max]` per row,
     in place of the hand-written width check;
   - extraction: `inputs.Style.Member = layer.Member;` per row, in place of `.LineWidthPx = ...`;
   - record rows (`vector_field_draw_record.glsl`): include the style rows as
     `VF_RECORD_STYLE(float, Member)` at `LineWidthPx`'s place (record bytes unchanged);
   - panel: a generic slider per row (label, spec bounds, the member) where the width slider was;
     runtime exports the list (`GeometryVectorFieldStyleSliders()`: label, spec name, member
     pointer) so the app does not include the row file. The slider keeps `%.1f` for every row
     (ImGui rounds the stored value to the format, so changing it would change values; a row
     that needs another precision adds a format argument then).
3. **Bounds once**: `kVectorFieldMinLineWidthPx`/`kVectorFieldMaxLineWidthPx` are deleted (their
   two users become generated). The shader's defensive `clamp(record.LineWidthPx, 0.5, 32.0)`
   stays as it is (recorded as a remaining duplicate; the record is written for valid packets only).
4. **Build**: `ExtrinsicRuntime` gets `assets/shaders` as a PRIVATE include directory, like
   `ExtrinsicGraphics` (`GRAPHICS-165`); so do the test object libraries
   `RuntimeGraphicsCpuTestObjs` and `GraphicsContractCpuTestObjs` for the generic tests (step 5).
5. **Generic glue tests** (include the row file, one assertion per row, at a value that is
   neither the row's default nor 0, since record members start at 0: a bound, else the middle): `RuntimeVectorFieldExtraction.EveryPassThroughRowReachesThePacket` (authored
   layer → extracted packet style) and `VisualizationOverlayPassContract.EveryPassThroughRowReachesTheRecord`
   (packet style → uploaded record). `VectorFieldStyle.EveryFieldRoundTripsOnEverySurface` sets
   every pass-through member still at its default to a spec bound that differs from it (else the
   middle) through the slider list (the
   hand value `LineWidthPx = 4.5` stays) and counts `12 + sliders` instead of 13; its whole-layer
   round trip and its every-field-differs check stay.
- Unchanged: the agent schema and messages (same spec), scene keys and strictness, packet and
  record bytes, the shader, every test's expectations.
- Not in this slice: the other style fields (their mapping to the GPU is not a pass-through:
  length mode → normalize flag, color → packed color, colormap → bindless id, auto range → resolved
  range, stride/glyph caps → row sampling), non-float pass-through types, the shader's clamp.

## Count experiment (the Q1 evidence)
After the slice, in the working tree (reverted afterwards from a saved patch): add one row
(`ColorGain`, 1.0f, `color_gain`, `colorGain`, not required, "Color gain", 0.0f, 4.0f) that the
shader applies to the arrow's RGB, so its effect is a deterministic readback. Add the feature's own
visual check to the existing color smoke (`RuntimeSandboxAcceptanceGpuSmoke.VectorFieldArrowsTakeLengthAndColorFromProperties`
or beside it): the same uniform arrow authored through the layer with gain 0.25 reads back darker
than with gain 1. Build the CPU targets, shaders and the Vulkan smoke targets; run the focused CPU
suites and the Vulkan vector-field smokes; record every file that had to change for green. Then two
trial mutations must fail: the shader stops reading `ColorGain` (the visual check red), and the
generated extraction copy is emptied (the generic packet test red). Prediction: production 2 (row
file, shader); tests 2 (the visual check, `Test.VisualizationOverlayPass.cpp` pins); no edit in
`DeclaredFieldContracts`, the generic glue tests or `VectorFieldStyle`.

## Tests (predictions as assertions)
- Refactoring tests, pass before and after: `DeclaredFieldContracts.*`, `VectorFieldStyle.*`,
  `RuntimeVectorFieldExtraction.*` (incl. `EveryStyleFieldReachesThePacket`),
  `GraphicsVisualizationPackets.*`, `VisualizationOverlayPassContract.*`, `RuntimeSceneSerialization.*`,
  `AgentOperations.*`, `SandboxEditorUi.*`; Vulkan `VisualizationOverlaySurfaceGpuSmoke.*` and
  `RuntimeSandboxAcceptanceGpuSmoke.VectorField*`.
- New, `VisualizationOverlayPassContract.VectorFieldPacketWidthStaysInItsRowBounds`: a packet with
  `LineWidthPx` 0.5 and 32 is renderable; the float neighbours outside, NaN and infinity are not.
  No graphics test covers the width bound today (the runtime refusal of 64 in
  `SandboxEditorUi` goes through the style table, not the glyph-style check). It passes before
  (hand check) and after (generated check).
- Generic glue tests (step 5) pass after the slice; before it they do not exist (they include the
  row file).
- Mutations, one at a time, each must fail: the extraction copy macro emptied
  (`EveryStyleFieldReachesThePacket` and `EveryPassThroughRowReachesThePacket` red); the
  glyph-style check row emptied (the new packet width test red); the style-table row macro
  emptied (`VectorFieldStyle`'s `12 + sliders` count red; `DeclaredFieldContracts` enumerates the
  surviving table and is not expected to fail); the record fill's style macro emptied
  (`EveryPassThroughRowReachesTheRecord` red).

## Acceptance criteria
- [x] One row declares `LineWidthPx` for the layer, the style table, the glyph style, its check,
      the extraction copy, the record and the panel slider.
- [x] Behaviour and bytes unchanged (SPIR-V of the vector-field shader byte-identical);
      refactoring suites, the generic glue tests and Vulkan smokes pass.
- [x] Count experiment run and recorded here and in `ARCH-020` (production and tests apart).
- [x] Codex plan and code review; focused suites, structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicGraphicsContractCpuTests IntrinsicGraphicsRendererCpuUnitTests IntrinsicSandboxEditorIntegrationTests IntrinsicShaderOutputs ExtrinsicSandboxEditor
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(DeclaredFieldContracts|VectorFieldStyle|RuntimeVectorFieldExtraction|GraphicsVisualizationPackets|VisualizationOverlayPassContract|RuntimeSceneSerialization|AgentOperations|SandboxEditorUi|RenderCompilationLocality)\.'
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
DISPLAY=:1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json ctest --test-dir build/ci-vulkan -L gpu -L vulkan --no-tests=error -R '^(VisualizationOverlaySurfaceGpuSmoke\.|RuntimeSandboxAcceptanceGpuSmoke\.VectorField)' --output-on-failure --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```
(`ExtrinsicSandboxEditor` is built because the panel slider lives there; the `ci` preset is headless, without the `ExtrinsicSandbox` executable.)

## Log
- 2026-10-28: Codex plan review (`plan331.answer.md` in this session's scratchpad, task sha256
  `cde316af…`): revise. B1 `%.3g` changes stored values (ImGui rounds to the format): kept `%.1f`.
  B2 remove the avoidable fixture pin: `VectorFieldStyle` driven by the slider list, `12 + sliders`.
  B3 the count trial must exercise the wiring: a nondefault value through the authored layer,
  generic packet and record tests, a visual readback for the trial field, trial mutations; chose
  `ColorGain` (RGB multiplier) over a head size for a deterministic readback. B4 build
  `IntrinsicGraphicsRendererCpuUnitTests`, which owns `GraphicsVisualizationPackets.*`. O1 (macro hazards,
  `std::to_array<Row>`) as implemented; O2 mutation attribution corrected.
- 2026-10-28: Codex delta answer (the first attempt failed on model capacity; retried; on-disk
  task sha256 `1957a14b…`, the suite-name fix `GraphicsVisualizationPackets` came after the
  packet): revise. B1-B3, O1, O2 resolved. B4 remains: `SandboxEditorUi.*` also selects tests in
  `IntrinsicSandboxEditorIntegrationTests`, now built. New B5: a test value of 0 could hide a
  missing record assignment (record members start at 0): every generic test now picks a value that
  is neither the default nor 0. O3 the fixture also picks a nondefault bound (else the middle); it
  may pick 0, which is fine there (the decode starts from a default layer, not a zeroed record).
- 2026-10-28: Implemented as planned (revision 3). Row file `assets/shaders/common/vector_field_style_rows.glsl`
  (one row, `LineWidthPx`), read by the layer, the glyph style and its check, the style table,
  the slider list, the extraction copy and the record rows; `kVectorFieldMin/MaxLineWidthPx`
  deleted; `ExtrinsicRuntime` and the two test object libraries include `assets/shaders`.
  Changed C++ lines formatted with clang-format 20.1.8; in `Runtime.GeometryVectorFieldStyle.cpp`
  that rewraps the whole `kRows` statement (its first line changed), accepted after the code
  review (B2) rather than keeping the old compact layout as an unapproved exception.
- Verification on the final tree: build of `IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests
  IntrinsicGraphicsContractCpuTests IntrinsicGraphicsRendererCpuUnitTests IntrinsicSandboxEditorIntegrationTests
  IntrinsicShaderOutputs ExtrinsicSandboxEditor`; CTest `DeclaredFieldContracts|VectorFieldStyle|
  RuntimeVectorFieldExtraction|GraphicsVisualizationPackets|VisualizationOverlayPassContract|
  RuntimeSceneSerialization|AgentOperations|SandboxEditorUi|RenderCompilationLocality` 384/384;
  `SourceRules` 8/8; the vector-field shader's SPIR-V is byte-identical to the one before the slice;
  task policy, layering, doc links pass; module inventory unchanged. Vulkan (RTX 4090, driver
  580.159.04): `VisualizationOverlaySurfaceGpuSmoke.*`, `RuntimeSandboxAcceptanceGpuSmoke.VectorField*`
  10/10, no skips. The log line `VulkanRequestedButNotOperational ... BarrierValidationFailed`
  appears in passing smokes too (harness cold start; not from this change, not investigated).
- Mutations, each alone and restored, all red: extraction copy emptied
  (`EveryPassThroughRowReachesThePacket`, `EveryStyleFieldReachesThePacket`); glyph-style check row
  emptied (`VectorFieldPacketWidthStaysInItsRowBounds`); style-table row emptied
  (`VectorFieldStyle.AgentSchemaBoundsMatchTheCommand`, `.EveryFieldRoundTripsOnEverySurface`);
  record fill's style macro emptied (`EveryPassThroughRowReachesTheRecord` and both record tests).
- Count experiment (working tree, reverted byte for byte from a saved patch): one row `ColorGain`
  (1.0, 0..4, not scene-required) plus `fragColor.rgb *= record.ColorGain;` in the shader, and the
  feature's own GPU check (`VectorFieldColorGainScalesTheArrowColor`: gain 0.25 vs 1 on two
  uniform magenta arrows authored through the vector-field command). Files that had to change for
  green: production 2 (row file, shader); tests 3 (`Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` the
  feature check; `Test.VisualizationOverlayPass.cpp` size 144→152 and offsets +4/+8;
  `Test.RuntimeSceneSerialization.cpp` the scene golden gains `"colorGain":1.0`). No edit in
  `DeclaredFieldContracts`, the generic glue tests, `VectorFieldStyle`, agent, panel, extraction,
  glyph style or the C++/GLSL record. CPU focused 384/384 and Vulkan 11/11 with the trial. The first
  visual threshold (40 levels) was too strict: lighting and tone mapping lift a 0.25 gain to
  212 vs 245, so the trial asserted 20. Trial mutations: the shader not reading `ColorGain` →
  both arrows 245, check red; the extraction copy emptied → check red and the generic packet test
  red. Result: 5 files against Q1's 3; the gap is the two deliberate pins (record layout, scene
  golden), not glue.
- 2026-10-28: Codex code review (diff sha256 `a6f153bc…`; the first attempt failed on model
  capacity, the retry answered): revise. B4, B5, O3 of the plan delta resolved; no correctness
  defect in the production readers. B1 the trial's visual check accepted a black "dim" arrow:
  added `IsMagenta(dark)`; rerun with the trial: passes (dark (212,0,212) vs (245,0,245)); the
  mutation `floor(record.ColorGain)` turns the dim arrow (0,0,0) and fails it; tree restored byte
  for byte. B2 the compact `kRows` layout was an unapproved formatting exception: the changed
  statement is now clang-format 20.1.8 formatted. O1 the fixture rule described as nondefault.
  Re-verified on the final tree: CPU focused 384/384, SPIR-V unchanged, Vulkan 10/10, task policy.
- 2026-10-28: Codex delta code review (format diff sha256 `4a9ad9a4…`): approve; B1, B2, O1
  resolved, the 13 rows and 26 getter/setter lambdas token-identical after the reformat. Author's
  own check: the formatted file equals the unformatted one with whitespace and line splices removed.
