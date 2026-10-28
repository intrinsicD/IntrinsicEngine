---
id: RUNTIME-336
theme: F
depends_on: [ARCH-020, RUNTIME-329, RUNTIME-330, RUNTIME-335]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: a new agent tool whose style arguments, reader and checks come from the lane visualization style table and whose effect goes through the existing lane config command (validated, one undo step); no existing behaviour, scene format or tool changes; no catalog contract covers it
---
# RUNTIME-336 — An agent tool generated from the lane style table (moonshot M2, slice 12)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
15. (2026-10-28) "Bitte die drei Punkte nacheinander machen" (3: the larger M2 goals: generated
    panels, generated MCP tools, a smaller `src/runtime/Editor`; this is the first step)

## Goal
- M2 targets "100 % of MCP tools are generated" and "one declaration ... generates the MCP tool
  schema". Baseline today (2026-10-28, HEAD `ed50d5ab2`): 47 agent tools; their arguments are
  hand-written JSON except partly generated pieces: the `vector_field` style properties (from its
  table), the operation families' `params` (from the operations' field tables) and the attribute
  and domain enums; no tool is generated whole. `src/runtime/Editor` has 52,077 lines in 133 files
  (target 25k). Generated panels: the vector-field panel's pass-through sliders (`RUNTIME-331`).
  The tool count (47 → 48) is a pin, not a progress measure toward generated tools.
- The lane color style (`VisualizationConfig`, 17 declared rows, `RUNTIME-329`) has no agent tool:
  an agent can only `show_property`/`bind_attribute`, not set a colormap, a manual range, bins or
  isolines. This slice adds `lane_style`, whose style arguments are generated from the table, read
  by the table's reader and checked by the table's rules, applied through the panel's lane config
  command. A new table row then reaches the agent without an agent-side edit.

## Plan
1. **Schema**: `LaneVisualizationStyleSchemaProperties()` in the lane style's internal header
   (like `GeometryVectorFieldStyleSchemaProperties`): the section schema's properties of
   `LaneVisualizationStyleFields()`. The tool schema is the shared envelope (`entity`, `lane`:
   surface/edges/points, both required) plus those properties; unknown keys are refused by the
   schema (RUNTIME-326).
2. **Tool** `lane_style` (mutating): builds the lane's domain window model (surface → Mesh,
   edges → Graph, points → PointCloud window, as the panel); the lane must exist for the entity
   (`VisualizationTargetAvailable`, as the panel checks; a point cloud has no surface) and the
   commands must run (`VisualizationControlsAvailable`), else `unsupported_geometry_domain`. A call
   without style fields is a read and runs no command (B1: writing back the shown defaults would
   create a lane config and an undo step). Otherwise it takes the lane's effective config (its own
   or the entity's it inherits), applies the passed fields with `ReadLaneVisualizationStyleFields`
   (all or nothing, float storage checked, `RUNTIME-330`), and runs
   `ApplyEditorVisualizationConfigCommand` for the window's target (one undo step; NoChange when the
   result equals what the lane draws). Inheritance (B3): a changed style becomes the lane's own
   complete config, as with the panel; fields not passed keep the values the lane showed, inherited
   ones included, and stop following the entity's config; undo removes a newly created lane config.
   Reply: status, entity, lane, `has_config` (the lane draws its own or an inherited config) and the
   style as the flat field object. Refusals: `invalid_params` for a value the table refuses,
   `stale_entity` for an unknown entity, the command's status otherwise.
3. **Generated contract tests**: `DeclaredFieldContracts.LaneVisualizationStyle` gains the
   registered `lane_style` schema and a valid envelope, so every lane row is also checked on the
   agent schema (the leg it lacked).
4. **Integration test** `SandboxAgentServer.LaneStyleSetsDeclaredFieldsRefusesAndUndoes` (the
   real agent server and scene, like the vector-field test): a read before any write creates no
   config and no undo step; a mesh's edges and points lanes and a cloud's points lane are editable,
   a cloud's surface is refused, an unknown entity is `stale_entity`; before any write, a valid
   field with `range_min: 1e39` is refused and nothing changes (B4: the reader's all-or-nothing on a
   lane without config), as are a schema-refused token and an unknown key; then colormap Jet, manual
   range, bins and isolines are set in one undo step, kept, and undone. Inheritance on a second grid
   with an entity config: the lane shows it; the same value writes nothing; a changed one becomes
   the lane's own config with the inherited rest, the entity's config and the other lanes untouched;
   undo restores inheritance.
5. **Tool count** pin in `AgentOperations` 47 → 48.
- Not in this slice: converting existing hand-written tools, generated panels, Editor size.

## Tests (predictions as assertions)
- The extended `DeclaredFieldContracts.LaneVisualizationStyle` passes (schema and reader agree,
  as for the vector field); the integration test fails without the tool (unknown tool) and passes
  with it; the unknown-argument sweep in `AgentArgumentSchema` covers the new tool automatically.
- Mutations, each must turn a test red: the tool ignores what it read; the schema drops one row's
  properties (the generated agent check); the tool applies each valid field before the rest is
  checked (the mixed refusal on a lane without config then changes it); the lane availability
  check is dropped (a cloud's surface becomes editable).

## Acceptance criteria
- [x] `lane_style` registered; its style schema, reading and checks from the lane table only.
- [x] Generated agent-schema checks for every lane row; integration test green.
- [x] Baseline and tool count recorded in `ARCH-020`.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(DeclaredFieldContracts|AgentOperations|AgentArgumentSchema|SandboxAgentServer|SandboxEditorUi)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-28: Codex plan review (`plan336.answer.md` in this session's scratchpad): revise. B1 a
  fieldless read must not run the command (the author's first integration run had found the same:
  status Applied and a new lane config); B2 require `VisualizationTargetAvailable`; B3 pin the
  inheritance contract; B4 a discriminating atomicity check; O1 entity in the reply, `stale_entity`;
  O2 baseline precise. All taken (Plan, Tests above).
- 2026-10-28: Implemented: `LaneVisualizationStyleSchemaProperties()` (lane internal header),
  `lane_style` in `Runtime.AgentOperations.Editor.cpp`; `DeclaredFieldContracts.LaneVisualizationStyle`
  checks the registered schema; the integration test as planned; tool count 48. Mutations, each
  alone and restored, all red: ignore what was read (integration); schema without `colormap`
  (generated contract test); apply valid fields before the check (integration, the mixed refusal);
  availability check dropped (integration, the cloud surface). Focused CPU `DeclaredFieldContracts|
  AgentOperations|AgentArgumentSchema|SandboxAgentServer|SandboxEditorUi|VectorFieldStyle` 318/318;
  `SourceRules` 8/8; layering clean; changed lines clang-format 20.1.8.
- 2026-10-28: Codex review of the revision and the change (`review336.diff` in this session's
  scratchpad): approve; B1-B4, O1, O2 resolved. Optional done: after the inheriting write a server
  read must equal the inherited style with only the colormap changed, and the entity's config is
  compared whole (`SameLaneVisualizationStyle`).
