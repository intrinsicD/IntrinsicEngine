---
id: RUNTIME-340
theme: F
depends_on: [ARCH-020, RUNTIME-339]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: a field type for a JSON object or a string (a payload passed as JSON text), with its schema and check; config_preview and config_apply register generated input schemas equal to their hand-written ones; no config section uses the type, so section schemas and validation are unchanged; no catalog contract covers the tool schemas
---
# RUNTIME-340 — Object-or-string tool arguments (moonshot M2, slice 16)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `d3cdf663a`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
19. (2026-10-28) "Ja bitte" (answer to: next, the payload type for `config_preview` and
    `config_apply`, the smallest step, two tools)

## Goal
- M2 target "100 % of MCP tools are generated" (`ARCH-020` row 3): 42 of 48 after `RUNTIME-339`.
  `config_preview` and `config_apply` take `section` (a string) and `payload`, a section payload
  as a JSON object or as its JSON text (`"type":["object","string"]`, read by `Payload()` in the
  agent helpers). No `ConfigFieldType` expresses that. After this slice: 44 of 48.

## Plan
1. **Type**: `ConfigFieldType::ObjectOrString`, appended last: any JSON object, or a string (an
   object passed as JSON text; what the string holds is the consumer's to check, as `Payload()`
   and the section validator do today).
2. **Generator and check**: `BuildFieldSchema` writes `"type":["object","string"]`, and
   `["object","string","null"]` when `Nullable` (B1: the shared nullable handling only extends a
   string-valued type, while `CheckField`'s shared early return accepts null), plus the shared
   description and default handling; `CheckField` accepts an object or a string and otherwise
   reports "<name> must be an object or a string.". `DescribeConfigFieldRange` needs nothing
   (unconstrained, empty text). Consumers (O1): only these two tool argument declarations use the
   type. `Param` and the contract test's `CasesFor` have defaults that exclude it; the point
   readers filter other types; panel helpers and the style and scene codecs read unchanged tables.
   `ParamsSchema` (operation params) reads `type` as a string and would need a change if an
   operation parameter ever used the type; none does.
3. **Tools**: both take `SectionArgument(true)` (`RUNTIME-337`) and `payload` (ObjectOrString,
   required, today's description). `kSectionProperty` and `kPayloadProperty` lose their last users
   and are deleted. Handlers unchanged.
4. **Baseline**: both hand-written schemas (registry dump at `36dd1fc81`; their registrations are
   unchanged since) join `tests/data/agent_declared_tool_schemas.json`; count pin 42 → 44.
- Not in this slice: `oneOf`/`const` (`bind_attribute`, `jobs_wait`), per-operation conditionals
  (`run_`/`preview_operation`).

## Tests (predictions as assertions)
- Order: before = `e53822a13` (CI green); the baseline entries and count are added and the
  baseline test observed red (two tools not flagged, count 42); then steps 1-3 and every suite
  reruns.
- `DeclaredFieldContracts.ObjectOrStringCheckAgreesWithItsSchema`: for one ObjectOrString field,
  not nullable and nullable, the config check (`CollectDeclaredFieldErrors`) and the agent
  validator on the generated schema (`BuildSectionSchemaJson`) each give the expected verdict:
  `{}`, `{"a":1}`, `"{}"`, `""` accepted; `null` accepted only when nullable; `1`, `1.5`, `true`,
  `[]` refused; the schema's `type` is exactly `["object","string"]` or
  `["object","string","null"]`.
- `AgentArgumentSchema.RegisteredDeclaredArgumentsKeepTheirRules` gains, on the registered
  schemas: `config_apply` accepts a payload object and a payload string, refuses a number payload
  and a missing payload; `config_preview` refuses a missing section.
- Refactoring: `AgentOperations.*`, `AgentArgumentSchema.*`, `DeclaredFieldContracts.*`,
  `SandboxAgentServer.*`, `EngineConfigSectionSchemas.*`, `CoreEngineConfig(Load|Sections).*`
  pass before and after.
- Mutations, each must turn a test red: the schema writes only `"object"`; the nullable schema
  drops `"null"`; the check accepts any value; neither schema nor check handles the type (the
  state before step 2); `payload` not required; `config_apply` registered as text (no flag).

## Acceptance criteria
- [x] `ObjectOrString` with its schema and check, agreeing.
- [x] `config_preview` and `config_apply` equal their baselines; 44 of 48 in `ARCH-020`.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure --timeout 120 -R '^(AgentOperations|AgentArgumentSchema|DeclaredFieldContracts|SandboxAgentServer|EngineConfigSectionSchemas|CoreEngineConfig(Load|Sections))\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-28: Codex plan review (`plan340.answer.md` in this session's scratchpad): revise. B1 a
  nullable field's schema would refuse null while the check accepts it: the nullable type array,
  its test case and mutation added (Plan 2, Tests). O1 the consumer boundary stated precisely,
  `ParamsSchema`'s string-typed read noted (Plan 2). The new enumerator judged preferable to a flag.
- 2026-10-29: Red before, observed with the enumerator only (no schema or check case): the
  baseline test (two tools not flagged, count 42) and the agreement test failed.
- 2026-10-29: Implemented: `ConfigFieldType::ObjectOrString` with its schema (nullable adds
  `null`) and check; `config_preview` and `config_apply` take `SectionArgument(true)` and the
  declared `payload`; `kSectionProperty` and `kPayloadProperty` deleted. The agreement test strips
  the section schema's `$schema` and `$id`, as `ArgumentsSchema` does (the validator does not take
  document keys). Mutations, each alone and restored, all red: schema only `"object"` (agreement,
  rules, baseline and `DocumentedArgumentFormsStillReachTheTool`); nullable schema without `null`
  (agreement); check accepts any value (agreement); neither schema nor check (agreement, rules,
  baseline, documented forms); `payload` not required (baseline, rules); `config_apply` as text
  (baseline). Focused CPU `AgentOperations|AgentArgumentSchema|DeclaredFieldContracts|
  SandboxAgentServer|EngineConfigSectionSchemas|CoreEngineConfig(Load|Sections)` 110/110;
  `SourceRules` 8/8; layering clean; no `-Wswitch` warning for the new enumerator; changed lines
  clang-format 20.1.8; jscpd: no new clone.
- 2026-10-29: Codex review of the revision and the change (`review340.diff` in this session's
  scratchpad): approve; B1 and O1 resolved. Optional, both done: the missing-section assertion
  named on `config_preview` (Tests); the tracker keeps the session's start date.
