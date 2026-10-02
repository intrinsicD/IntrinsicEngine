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
- No change to `show_property` semantics (scalar colormap / vector-as-color); it keeps calling the visualization recipe path.

## Context
- Operator decisions 2026-10-02 (model unification, picking/culling follow displayed positions, canonical normals, single Color mechanism, pixel sizes) are recorded in [RUNTIME-315](../runtime/RUNTIME-315-per-domain-render-attribute-source-binding.md#operator-decisions-2026-10-02).
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
