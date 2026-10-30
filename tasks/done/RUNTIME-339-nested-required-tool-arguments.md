---
id: RUNTIME-339
theme: F
depends_on: [ARCH-020, RUNTIME-338]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: a field spec can mark a nested object's member as required in the generated schema; set_camera and property_compare register generated input schemas equal to their hand-written ones; config section schemas and validation unchanged; no catalog contract covers the tool schemas
---
# RUNTIME-339 — Required members of nested arguments (moonshot M2, slice 15)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `e53822a13`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
18. (2026-10-28) "Ja bitte" (answer to: next, nested objects with required members, which brings
    `set_camera` and `property_compare` to generated schemas)

## Goal
- M2 target "100 % of MCP tools are generated" (`ARCH-020` row 3): 40 of 48 after `RUNTIME-338`.
  Two of the remaining eight differ only by nested objects with required members: `set_camera`
  (`pose` requires `position` and `target`) and `property_compare` (`a` and `b` each require
  `domain` and `name`). `ConfigFieldSpec` already declares nested objects (`Object` with `Fields`,
  used by the clustering section's `properties` and parameterization's `lscm`, `harmonic`, `bff`,
  `view`, `atlas`) but cannot mark a member required. After this
  slice: 42 of 48.

## Plan
1. **Spec**: `ConfigFieldSpec` gains `bool Required{false}` as its last member (no existing
   designated initializer changes): an `Object` member the generated schema requires. Schema only,
   like `Object` itself (`CheckField` leaves nested objects to their owner, unchanged); a tool's
   top-level arguments keep `AgentArgument::Required`. The member's comment says so.
2. **Generator**: `BuildFieldSchema` for `Object` writes `required` (members in declaration order)
   when at least one member is required, so the six existing config Objects' schemas are unchanged.
3. **Tools**: `set_camera`: `controller` and `preset` (StringEnum, the handler's tokens, today's
   order), `pose` (Object: `position`, `target` Required, `up` with its description; each an Array
   of Float, 3 items), `focus` (Bool, its description "true: frame the entities or the selection.",
   O2), `entities` (Array of UInt 0..UINT32_MAX, as today, its
   description). `property_compare`: `entity`, and `a`, `b` (Object, required, from one local
   member array: `domain` with the shared domain tokens and description, `name` String NonEmpty,
   both Required). Handlers unchanged (`set_camera` keeps its exactly-one-mode check).
4. **Baseline**: both hand-written schemas (registry dump at `36dd1fc81`; their registrations are
   unchanged since) join `tests/data/agent_declared_tool_schemas.json`; count pin 40 → 42.
- Not in this slice: `oneOf`/`const` (`bind_attribute`, `jobs_wait`), payload unions
  (`config_preview`, `config_apply`), per-operation conditionals (`run_`/`preview_operation`).

## Tests (predictions as assertions)
- Order: before = `c00889f34` (CI green); the baseline entries and count are added and the
  baseline test observed red (two tools not flagged, count 40); then steps 1-3 and every suite
  reruns.
- `AgentArgumentSchema.RegisteredDeclaredArgumentsKeepTheirRules` gains, on the registered
  schemas: `set_camera` accepts a pose with position and target, refuses one without target, one
  with a 2-item position, one with an unknown pose key; `property_compare` refuses `a` without
  `name`, with an empty `name`, with an unknown domain.
- The generator has no direct test (its namespace is internal); its two cases are pinned through
  real schemas: `DeclaredFieldContracts.NestedObjectsRequireOnlyDeclaredMembers` asserts that the
  six config Objects (clustering's `properties`; parameterization's `lscm`, `harmonic`, `bff`,
  `view`, `atlas`, O1), which declare no Required member, have no `required` key, and the baseline
  test pins `pose` and `a`/`b` with theirs.
- Refactoring: `AgentOperations.*`, `AgentArgumentSchema.*`, `DeclaredFieldContracts.*`,
  `SandboxAgentServer.*`, `ClusteringModule.*`, `EngineConfigSectionSchemas.*` (nested defaults,
  schema/validator agreement) and `CoreEngineConfig(Load|Sections).*` pass before and after (B1).
- Mutations, each must turn a test red: the generator ignores `Required`; it emits `required` also
  when empty; `pose.target` not Required; a converted tool registered without the flag (run as
  `property_compare` registered as text).

## Acceptance criteria
- [x] `ConfigFieldSpec::Required` and the generator's nested `required`; section schemas unchanged.
- [x] `set_camera` and `property_compare` equal their baselines; 42 of 48 in `ARCH-020`.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure --timeout 120 -R '^(AgentOperations|AgentArgumentSchema|DeclaredFieldContracts|SandboxAgentServer|ClusteringModule|EngineConfigSectionSchemas|CoreEngineConfig(Load|Sections))\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-28: Codex plan review (`plan339.answer.md` in this session's scratchpad): revise. B1 the
  verification missed the config suites (pattern and target): fixed (Verification, Tests). O1
  parameterization's five Objects added to the absence check; O2 `focus` keeps its description.
  The per-member `Required` judged the smallest shape; its comment states the distinction.
- 2026-10-28: Implemented: `ConfigFieldSpec::Required` (last member), `BuildFieldSchema` writes an
  Object's `required` when a member is Required; `set_camera` (camera tokens beside the other
  token arrays, `pose` from a local member array) and `property_compare` (`a`, `b` from the
  `domain` and `name` arguments it already declares, made required members) generated. With the
  baselines and count added and before the conversion the baseline test was red exactly as
  predicted (two tools not flagged, count 40); after it, green and equal. Mutations, each alone
  and restored, all red: the generator ignores `Required` (baseline and rules tests); `required`
  written also when empty (`NestedObjectsRequireOnlyDeclaredMembers`); `pose.target` not Required
  (baseline and rules tests); `property_compare` registered as text (baseline test). Focused CPU
  `AgentOperations|AgentArgumentSchema|DeclaredFieldContracts|SandboxAgentServer|ClusteringModule|
  EngineConfigSectionSchemas|CoreEngineConfig(Load|Sections)` 125/125; `SourceRules` 8/8; layering
  clean; changed lines clang-format 20.1.8; jscpd: no new clone.
- 2026-10-28: Codex review of the revision and the change (`review339.diff` in this session's
  scratchpad): approve; B1, O1, O2 resolved. Optional, all done: the stale "nested required keys"
  in the registration comment replaced; "static member table" corrected to "local member array";
  the flag mutation named as run.
