---
id: RUNTIME-327
theme: F
depends_on: [ARCH-020, RUNTIME-326]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: refactor of how the vector-field layer style is declared; scene keys, agent tool shape and rendering are unchanged (the agent's stride bound narrows to the command's); no catalog contract covers this recipe
---
# RUNTIME-327 — The vector-field style is declared once (moonshot M2, slice 2)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `d667f5d12`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator questions (verbatim, 2026-10-27)
3. "start improving the engine by implementing the Moonshot.md. Keep properly track of where you
   and your subagents are at so we can continue anytime properly."
4. "Do M2 slice 2"

## Goal
- Moonshot M2 ("declare once"), slice 2, tracked by `ARCH-020` row 3; the moonshot's Q1 item is
  "the vector-field style re-declared in M2: 29 files become 3 or fewer".
- Today one style field of `GeometryVectorFieldLayerRecipe` (13 fields: visible, depth test,
  length mode, length, line width, color, stride, max glyphs, length/color property, colormap,
  auto range, range) is restated in five source files, each with its own names, types and ranges:
  the recipe and its validator (`Runtime.GeometryPresentation.cpp(m)`), scene write and read
  (`Runtime.SceneSerialization.cpp`), the agent schema, parser and listing
  (`Runtime.AgentOperations.Editor.cpp`, one hand-written lambda per field), and the panel
  (`Sandbox.DomainPanels.cpp`). They have already drifted: the agent schema allows `stride` up to
  4294967295, the command refuses above 16777216; the colormap names are listed separately in the
  agent, the scene and the panel.
- After this slice each style field is one row of one table next to the recipe. Agent schema,
  agent read/list, scene write/read and the command's range checks come from the rows; the panel
  takes its widget ranges from them. Adding a style field then touches the recipe (member + row)
  and the panel widget only, plus rendering when the field changes what is drawn.
- Shortest path: reuse the RUNTIME-276 field tables (`ConfigFieldSpec`, `BuildSectionSchemaJson`,
  `AcceptsConfigFieldNumber`, `CollectDeclaredFieldErrors`), which already generate processing
  operation schemas. No schema compiler: C++23 tables reach every surface here (ARCH-020 Log,
  no reflection on clang 23).

## Plan
1. **One table** (new unit `GeometryIntegration/Runtime.GeometryVectorFieldStyle.cpp` in module
   `Extrinsic.Runtime.GeometryPresentation`, rows declared in the internal header
   `GeometryIntegration/internal/Runtime.GeometryVectorFieldStyleJson.hpp`, the
   `Runtime.ConfigFieldJson.hpp` pattern, since no module interface exports nlohmann). Each of the
   13 rows holds everything about its field: the `ConfigFieldSpec` (agent snake_case name, type,
   bounds, enum tokens/names), the scene key (camelCase, as written today), whether scenes require
   it (the pre-UI-083 keys), and `Get(layer) -> json` / `Set(json, layer)` in the agent encoding.
   The one encoding difference besides the key, the length-mode token case (`Normalized` in scenes,
   `normalized` for agents), is a per-row scene token list. The exported
   `GeometryVectorFieldStyleFields()` is the specs view, derived mechanically from the rows (the
   panel needs no JSON).
2. **JSON functions** (same header): `VectorFieldStyleToJson(layer, keys)` and
   `ReadVectorFieldStyleJson(object, layer, keys)` for `keys` = Agent or Scene; Scene mode
   requires the scene-required rows, Agent mode applies only the keys present. Type and range of
   each value are checked through `CollectDeclaredFieldErrors` on the row specs.
3. **Command**: `ValidateGeometryVectorFieldLayer` checks every field by iterating the rows:
   `VectorFieldStyleToJson(layer, Agent)` then `CollectDeclaredFieldErrors` against the specs
   (array components included: color is an Array of 4 numbers in 0..1). A new row is therefore
   validated without another validator edit. It keeps the cross-field rules (domain, vec3 kind,
   min < max) and its reason texts for those.
4. **Agent**: the `vector_field` schema is entity/action/domain/property plus the generated style
   properties; update is one `ReadVectorFieldStyleJson` into the current layer; the listing uses
   `VectorFieldStyleToJson`. The 13 field lambdas and `kVectorFieldColormaps` go.
5. **Scene**: `GeometryVectorFieldLayerToJson` / `TryReadGeometryVectorFieldLayer` use the rows in
   Scene mode; the file format is unchanged (golden test).
6. **Panel**: the Length mode combo and the Colormap combo take their names from the rows; the
   Width slider and stride clamp take their bounds from the rows. The Length drag keeps its
   widget-only range 1e-6..1e9 (a drag needs finite ends; the declared rule stays "positive and
   finite", which the agent and command accept beyond the drag's range); this is recorded as a
   panel choice, not a second declaration. Widgets stay hand-written (the moonshot's escape hatch).
- Out of scope (Codex B3): extraction's `VectorFieldRequest` copy
  (`Runtime.RenderExtraction.VectorFields.cpp:122`, `:748`), packets and shader. They are the
  rendering half of a style field and stay hand-written in this slice.

## Counting rule (done criterion for the count)
- Representative change: add one style field that changes what is drawn (as UI-083 slice 4 did).
  Count the source files it must edit, by category: authoring surfaces (recipe, validator, scene,
  agent, panel), rendering (extraction, packets, renderer, shader), tests. Before (from
  `7a999e199`): authoring 5 files (`GeometryPresentation.cppm`, `GeometryPresentation.cpp`,
  `SceneSerialization.cpp`, `AgentOperations.Editor.cpp`, `DomainPanels.cpp`). After: list the
  files the same change needs, with the reason for each. Expected: authoring 2 or 3 (recipe member,
  table row, panel widget); rendering unchanged. This slice reduces the authoring surface; the
  moonshot's Q1 "29 files become 3 or fewer" is not met while rendering and tests stay separate,
  and the note and `ARCH-020` say so.

## Tests (predictions as assertions)
- `VectorFieldStyle.AgentSchemaBoundsMatchTheCommand` (contract, written first, red before):
  inclusive bounds accepted at the bound and refused at the next representable float outside
  (`std::nextafter`) for line width and color; `length` exclusive 0 refused and the smallest
  positive float accepted; stride 1 and 16777216 accepted, 0 and 16777217 refused; max glyphs
  0 and 4294967295 accepted. Overflow beyond uint32 is a separate agent-only case (the recipe
  cannot hold it). Observed before the change: exactly one failure, agent `stride=16777217`
  accepted while the command refuses it.
- `VectorFieldStyle.EveryFieldRoundTripsOnEverySurface` (contract): a fully populated fixture
  that differs from a default layer in every row (asserted per row, and the row count is 13).
  Agent: `VectorFieldStyleToJson(fixture)` read into a default layer gives the fixture. Scene:
  write the fixture, read into a default layer, equal. Mutations: one row's setter emptied, and
  one whole row removed, each turn it red.
- `RuntimeSceneSerialization.VectorFieldLayerJsonIsUnchanged` (refactoring, golden captured from
  today's serializer, passes before and after), with the existing optional-key and malformed-input
  test `VectorFieldStyleKeysRoundTripAndStayOptional` unchanged.
- The existing suites (`AgentOperations`, `SandboxAgentServer`, `SandboxEditorUi`,
  `SandboxDomainPanels`, `RuntimeSceneSerialization`, `RuntimeVectorFieldExtraction`) pass; each
  changed expectation is recorded with its reason.

## Acceptance criteria
- [x] Each of the 13 style fields is declared by one table row; agent schema, agent read/list,
      scene write/read and the command's per-field checks come from the rows, and the panel's
      enum names and numeric bounds read them.
- [x] The stride drift is gone and the bounds test proves agent and command agree on every bound.
- [x] The scene format is unchanged (golden test) and the existing suites pass.
- [x] The representative change is counted by the rule above and recorded here and in `ARCH-020`,
      stating that Q1's "3 or fewer" is not met by this slice alone.

## Verification
```bash
cmake --build --preset ci -j6 --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(VectorFieldStyle|AgentArgumentSchema|AgentOperations|SandboxAgentServer|SandboxEditorUi|SandboxDomainPanels|RuntimeSceneSerialization|RuntimeVectorFieldExtraction)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-27: Opened from `ARCH-020` row 3.
- 2026-10-27: Plan counter-review (Claude author → Codex reviewer, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a17f91-08c5-7283-9eda-932ee1c907c1`;
  recursion protection instruction-only). Object: this note sha256 `45282531…02a7b`, base
  `7d967065e`. Verdict "revise". Blocking: two synchronized tables instead of one row per field,
  validator still enumerating members (B1); panel restates the length-mode and colormap lists, the
  Length drag's bounds unresolved (B2); the file count had no rule and extraction's request copy was
  not named (B3); test predictions too loose (exclusive bound, float neighbours, overflow,
  independent fixture, mutate a whole row) (B4). Optional: drop `requireSceneKeys` (O1), update
  `ARCH-020` (O2), focused targets first (O3). All folded in above; extraction stays out of scope.
- 2026-10-27: Implemented. `Runtime.GeometryVectorFieldStyle.cpp` holds the 13 rows (spec, scene
  key, scene-required, scene tokens, get/set) and derives the exported specs view; the JSON
  functions are declared in `internal/Runtime.GeometryVectorFieldStyleJson.hpp` and defined in
  `extern "C++"` (no nlohmann in a module interface). The validator checks the style through the
  rows plus min < max; scene, agent and panel use the rows. Deleted: the 13 agent field lambdas and
  its colormap list, the hand-written scene style reader, `ToString`/`TryParse` of the length mode
  (no other caller), `UnitInterval`.
  - Red before (observed): `AgentSchemaBoundsMatchTheCommand` failed only on agent
    `stride=16777217`. After: passes. The golden test passed before and after.
  - Mutations: stride setter emptied → `EveryFieldRoundTripsOnEverySurface`,
    `VectorFieldLayerJsonIsUnchanged`, `VectorFieldLayersRoundTripAcrossDomains` fail; the
    `color_range` row removed → `EveryFieldRoundTripsOnEverySurface`, `VectorFieldLayerJsonIsUnchanged`,
    `VectorFieldStyleKeysRoundTripAndStayOptional` fail.
  - Focused suites 397/397; no existing expectation changed. Validator reasons for the style now read
    "vector field line_width must be ..." (spec texts; no test or caller matched the old texts).
  - Count by the rule (a style field that changes drawing, like UI-083 slice 4). Authoring before
    5 files: `GeometryPresentation.cppm`, `GeometryPresentation.cpp`, `SceneSerialization.cpp`,
    `AgentOperations.Editor.cpp`, `DomainPanels.cpp`. After 3: `GeometryPresentation.cppm` (the
    member), `GeometryVectorFieldStyle.cpp` (the row), `DomainPanels.cpp` (the widget). Rendering
    unchanged at up to 7 (`RenderExtraction.VectorFields.cpp` request copy and use,
    `VisualizationRecipes.cpp(m)` packet inputs, `VisualizationPackets.cpp(m)`, overlay upload
    helper, `Renderer.cpp`, the shader), plus tests. So the authoring surface meets "3 or fewer";
    the moonshot's Q1 item (the whole option in 3 files) is not met while rendering stays separate.
- 2026-10-28: Full CPU gate 5927/5927 (ran because a widely imported module interface changed).
- 2026-10-28: Code review (Claude author → Codex reviewer, fresh session, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a17f9f-739c-7ee0-8d77-de90fece01fe`;
  recursion protection instruction-only) on patch sha256 `9e3d0df6…69b82`, base `7d967065e`.
  Verdict "revise required". Blocking P1: an unknown scene token passed through untranslated, so a
  scene with the agent's `"lengthMode":"raw"` loaded (the old reader refused it). Nonblocking: N1
  the panel's new enum combo duplicates `DrawSpecEnumCombo`; N2 the count missing from `ARCH-020`.
  - Fixes: an untranslatable token becomes "" (refused by every enum); the panel uses
    `DrawSpecEnumCombo` (the colormap row now has `EnumNames`); count and Q1 caveat in `ARCH-020`.
  - Regression test (in `EveryFieldRoundTripsOnEverySurface`): scene `lengthMode` "raw" and
    "normalized" refused, agent "Raw"/"Normalized" refused. Red with the old pass-through (both scene
    tokens accepted), green with the fix. Focused suites 397/397.
- 2026-10-28: Delta review (same Codex thread) on patch sha256 `e821aa8c…772c2`. Verdict verbatim
  "approve" ("No new blocking or nonblocking findings."). The full CPU gate predates these fixes;
  the focused suites (397/397) cover them.

