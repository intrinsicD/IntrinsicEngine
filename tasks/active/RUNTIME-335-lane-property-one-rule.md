---
id: RUNTIME-335
theme: F
depends_on: [ARCH-020, RUNTIME-329, RUNTIME-333]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: one rule decides which property a lane can show as a scalar field or as a color buffer, for the lane panel's candidates, show_property's check and the visualization encoder's CPU scalar and color recipes; the behaviour change is that the encoder refuses connectivity and deletion rows for those recipes; no catalog contract covers this rule
---
# RUNTIME-335 — One rule for a lane's scalar and color properties (moonshot M2, slice 11)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
15. (2026-10-28) "Bitte die drei Punkte nacheinander machen" (2: the same one-rule check for the
    lane style's color property)

## Goal
- M2's "candidates equal what the validator accepts", for the lane visualization style
  (`VisualizationConfig`): which property a lane can show as a scalar field (colormap, isolines) or
  as a color buffer (a vec2/vec3/vec4 color or a one-component label).
- Today the lane panel's candidates and `show_property`'s check share
  `AppendVisualizationPropertiesForDomain` (`Runtime.EditorFeatureContextAdapters.cpp`); the
  encoder that draws (`EncodeVisualizationRecipe`, reached by lane extraction) has its own type
  dispatch. Measured first (the new contract test against the unchanged code): kinds and the short
  property already agree; the only divergence is connectivity and deletion rows (`v:deleted`,
  `e:v0`, `e:v1`), which the encoder accepts as scalar and color recipes and the panel never offers.
- The shared rule is kind and topology eligibility. Encoding still validates values and options (a
  float label of 0.5, negative or non-finite labels, an unsupported interpretation). So the claim
  is shared kind/topology eligibility, and "offered equals encodes" for the test fixture's valid
  values and default options, not "offered equals draws": a short property is offered and encoded
  by both today, and surface extraction may still reject its remap (Codex O1, O2).

## Plan
1. **One rule**, exported from `Extrinsic.Runtime.VisualizationRecipes` (the encoder's owner; the
   editor imports it): `FindLaneVisualizationProperty(properties, domain, name)` returns the kind
   and whether it can be a scalar field and a color buffer, or nothing: a known kind, not a topology
   row; scalar = one component; color buffer = scalar or Vec2/Vec3/Vec4. The body is the editor's
   current logic moved.
2. **Editor**: `AppendVisualizationPropertiesForDomain` builds its presets from the rule (the
   vector-field candidate flag stays Vec3); `show_property` keeps using it.
3. **Encoder**: the CPU scalar recipe and the color/label recipe ask the rule for an existing
   property before encoding; a refused one reports `UnsupportedSourceType`. Missing names and
   domains keep their own statuses; the external GPU-preview scalar path is unchanged.
4. **Contract test** `SandboxEditorUi.LanePropertyCandidatesAreExactlyWhatTheEncoderAccepts`: for
   every vertex and edge property (all scalar kinds, Int64, Vec2, Vec4, a short property,
   `v:deleted`, the edges' own rows, `e:f`), offered as a scalar field ⇔ the scalar recipe encodes,
   offered as a color buffer ⇔ the color recipe encodes.
- Not in this slice: the raw config command, which takes any name (a scene or agent config keeps
  its name and now draws nothing for a refused one, as for a missing name), and the GPU-preview
  scalar path.

## Tests (predictions as assertions)
- The contract test fails before steps 1-3 on `v:deleted`, `e:v0`, `e:v1` (measured) and passes
  after; `SandboxEditorUi.VisualizationModelEnumeratesPromotedGeometryProperties`, the
  `VisualizationRecipes` and lane extraction suites pass before and after.
- Mutations, each must turn a test red: the encoder skips the rule (topology encoded again); the
  rule drops Vec2 from color buffers (`VisualizationRecipes.BooleanMasksAndUvsEncodeFiniteRgbaWithoutPublishingAliases`,
  which requires Vec2 colors; the new test's equality can agree on the loss); the rule treats Vec3 as
  a scalar (disagrees with the unchanged scalar encoder).

## Acceptance criteria
- [x] One exported rule used by the lane panel's candidates, show_property and the encoder.
- [x] The contract test red before, green after.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(SandboxEditorUi|VisualizationRecipes|RuntimeRenderExtraction|ViewCapture|AgentOperations)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-28: Codex plan review (`plan335.answer.md` in this session's scratchpad): approve, no
  blocking finding. Optional, all taken: O1/O2 the claim is kind/topology eligibility and "offered
  equals encodes"; O3 the Vec2 mutation's detector named; O4 the test asserts
  `UnsupportedSourceType` for a topology source.
- 2026-10-28: Implemented: `FindLaneVisualizationProperty` in `Runtime.VisualizationRecipes`
  (editor's logic moved; `IsScalarVisualizationKind` gone), used by `AppendVisualizationPropertiesForDomain`
  and by the encoder's CPU scalar and color/label recipes (`laneEligible`). The contract test was
  red before on exactly `v:deleted`, `e:v0`, `e:v1` and is green after. Mutations, each alone and
  restored, all red: the encoder skips the rule (the new test); Vec2 dropped (the new test's count
  floor and `BooleanMasksAndUvsEncodeFiniteRgbaWithoutPublishingAliases`); Vec3 as a scalar (the new
  test). Focused CPU `SandboxEditorUi|VisualizationRecipes|RuntimeRenderExtraction|ViewCapture|
  AgentOperations|RuntimeVectorFieldExtraction` 367/367.
- 2026-10-28: Codex code review: revise. B1 the rule ran before domain validation, so a halfedge
  property reported `UnsupportedSourceType` instead of `UnsupportedDomain`: the test now asserts
  `UnsupportedDomain` for `h:to_vertex` as scalar and color (red before the fix: 04 vs 02), and
  `laneEligible` skips domains the encoder does not visualize. O1 the claim worded as above (test
  comment too). Also recorded: an existing property of unknown kind (Int64) now reports
  `UnsupportedSourceType` instead of `MissingSource`.
- 2026-10-28: Codex delta review: approve; B1 and O1 resolved, no new issue.
