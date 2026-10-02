---
id: RUNTIME-316
theme: J
depends_on: [RUNTIME-315]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# RUNTIME-316 — Agent `bind_attribute` and attribute listing for UI parity

## Goal
- The agent lane can list and set the source property of every render attribute per element domain through the same runtime command as the unified Appearance panel (`UI-075`), so a capability a user has is also an agent tool (UI-parity rule, `docs/architecture/agent-control-lane.md`).

## Non-goals
- No raw ECS/property writes; no new transport (the agent-lane exclusions stand).
- No change to `show_property` semantics (scalar colormap / vector-as-color, the lane overlay). Amended 2026-10-02 (slice 3): it now delegates to the Color binding (`ApplyEditorAttributeBindingCommand`, which draws through the same visualization recipe path) instead of calling the recipe command itself; `normal_direction` keeps the recipe command.

## Context
- Operator decisions 2026-10-02 (model unification, picking/culling follow displayed positions, canonical normals, single Color mechanism, pixel sizes) are recorded in [RUNTIME-315](../../done/RUNTIME-315-per-domain-render-attribute-source-binding.md#operator-decisions-2026-10-02).
- Existing tools: `show_property`, `set_visibility` (`src/runtime/Agent/Runtime.AgentOperations.Editor.cpp:695,702`); mutating calls run under `ScopedEditorCommandLabelPrefix("Agent: ")` and are undoable. `domain` arguments share one enum generated from `GeometryElementDomain`.
- Tool schemas and descriptions are the agent's documentation; document each attribute, its type and domain rules in the schema description.

## Control surfaces
- Agent/CLI: new `attribute_bindings` (read-only: per attribute and domain the current source, candidates, incompatibility reasons) and `bind_attribute` (`entity`, `attribute`, `domain`, `property` or `default`), one undoable step.
- UI: `UI-075` calls the same `ApplyEditorAttributeBindingCommand`.
- Config: none.

## Slice plan
1. **`attribute_bindings` query (~250 lines).** Wraps `BuildEditorAttributeBindingModel`; schema, docs, `Test.AgentOperations` cases.
2. **`bind_attribute` mutation (~300 lines).** Wraps the command; typed refusal reasons; undo label "Agent: ..."; update `docs/architecture/agent-control-lane.md` (tool list and parity statement).
3. **`show_property` interplay (~150 lines).** Document and test that `show_property` and `bind_attribute` Color do not fight (which authority wins; a visible diagnostic when both are set).

## Acceptance criteria
- [ ] `attribute_bindings` lists every attribute on every domain the entity has, with compatibility reasons.
- [ ] `bind_attribute` rebinds position/normal/color/texcoord/size/width, refuses incompatible properties with a typed reason, and `default` restores the canonical source.
- [ ] Each call is one undoable step labeled `Agent: ...`.
- [ ] Tool schema conformance and agent-lane docs updated.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AgentOperations|SandboxAgentServer' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-02: Slice 1 (`attribute_bindings`). Read-only tool over the inspector's
  `PropertyCatalog.AttributeBindings` (the panel's `BuildEditorAttributeBindingModel`, through
  `PrepareSnapshot`), grouped by element domain, optional `domain`/`attribute` filters. The
  `attribute` enum and the per-row documentation in the description are generated from
  `RenderAttributeRules()`; `domain` shares the `GeometryElementDomain` enum. Unknown entities
  answer `stale_entity` (snake case of the command status, `ErrorCodeFor` in `Detail.hpp`), bad
  arguments `invalid_params`. Tests: `AgentOperations.AttributeBindingToolsFollowTheRuntimeTable`,
  `SandboxAgentServer.AttributeBindingsListThePanelsSourceTable`.
- 2026-10-02: Slice 2 (`bind_attribute`). `{entity, attribute, domain}` plus exactly one of
  `property` / `default: true` (schema `oneOf`), through `ApplyEditorAttributeBindingCommand`;
  one `Agent: ` undo step, reply with the resulting model row. Refusals carry the snake-case
  command status as error code and the model candidate's reason; an `UnsupportedRenderAttribute`
  for an existing row names the hidden points/edges lane. Not destructive (undoable). Tests:
  `AgentOperations.BindAttributeValidatesItsArgumentsBeforeTouchingTheScene`,
  `SandboxAgentServer.BindAttributeRebindsRefusesAndRestoresTheDefault` (bind, type/missing/domain/
  entity refusals, default, NoChange, undo, pixel size after showing the points lane, canonical
  `v:position` untouched).
- 2026-10-02: Slice 3 (`show_property` interplay). No parallel path: `show_property` without
  `normal_direction` delegates to the `bind_attribute` color path (`ApplyEditorAttributeBindingCommand`,
  whose Color branch already ran the same recipe command), so the semantics stay the overlay's (the
  task's non-goal holds) and there is no precedence to diagnose: the last call wins and both are
  visible as the row's Color source. Gains: the binding's validation, typed error codes and the
  reply's `row`. `normal_direction` stays on the recipe command (a display recipe, not a source).
  Test: `SandboxAgentServer.ShowPropertyIsTheColorBinding`.
  Coordinator review folds: the Color rows of `RenderAttributeRules()` are exactly the domains the
  overlay can draw (`ColorOverlayTargetFor`: mesh vertex/edge/face, graph node/edge, point-cloud
  point); the recipe command refused halfedge properties too (`UnsupportedGeometryDomain`, no
  overlay lane), so delegation refuses nothing that was shown before, only with a typed code
  (`unsupported_render_attribute`). Remaining difference: the recipe command shows a scalar that is
  only resident on the GPU (awaiting Accept) without CPU validation; the agent could not name such a
  property before either (`show_property` resolves names through the CPU property catalog).
  `attribute_bindings` answers `unsupported_geometry_domain` for an entity without geometry.
  Tests: `SandboxAgentServer.BindAttributeCoversEveryAttributeAndItsDefault` (table-driven over the
  six attributes with `default`, one `Agent: ` step each, halfedge refusal, entity without geometry).
