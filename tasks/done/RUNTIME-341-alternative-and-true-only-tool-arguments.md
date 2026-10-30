---
id: RUNTIME-341
theme: F
depends_on: [ARCH-020, RUNTIME-340]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: a tool's declared arguments can name alternative required sets (oneOf) and a Bool field can accept only true (const true); bind_attribute and jobs_wait register generated input schemas equal to their hand-written ones; no config section uses either, so section schemas and validation are unchanged; no catalog contract covers the tool schemas
---
# RUNTIME-341 — Alternative argument sets and true-only flags (moonshot M2, slice 17)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `a9cc0a6e1`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
20. (2026-10-29) "Ok" (answer to: next, `oneOf`/`const`, which brings `bind_attribute` and
    `jobs_wait` to generated schemas)

## Goal
- M2 target "100 % of MCP tools are generated" (`ARCH-020` row 3): 44 of 48 after `RUNTIME-340`.
  `bind_attribute` takes `property` or `default: true` (`oneOf` of two required sets; `default` is
  `const: true`), `jobs_wait` takes `token` or `entity` with `output` (`oneOf`). Neither is
  expressible by declared arguments today. After this slice: 46 of 48; left are
  `run_`/`preview_operation` (per-operation conditionals).

## Plan
1. **True-only flag**: `ConfigFieldSpec` gains `bool OnlyTrue{false}` (appended last): a Bool
   that accepts only `true` (a flag whose absence means false). `BuildFieldSchema`'s Bool writes
   `"const": true` for it; `CheckField`'s Bool refuses `false` with "<name> must be true.".
   Consumers: only `bind_attribute`'s `default`. The operation params' `Param` and the contract
   test's `CasesFor` read Bool without the flag; no table sets it (a later table use would need
   both to honour it).
2. **Alternative required sets**: `ArgumentsSchema` takes a third, optional parameter, the
   alternatives (`std::initializer_list<std::initializer_list<std::string_view>>`): when given,
   the schema gets `"oneOf": [{"required": [...]}, ...]` in the given order (exactly one set
   present, as JSON Schema's `oneOf` with `required`-only branches means; the validator already
   checks it). Top-level `required` stays as declared.
3. **Tools**: `bind_attribute`: `entity`, `lane` (`LaneArgument`, required, "Render lane of the
   row."), `attribute` (the attribute tokens, required, its description), `property` (String
   NonEmpty, "Source property."), `domain` (`DomainArgument`, its description), `default` (Bool
   OnlyTrue, "Restore the default source."); alternatives `{property}`, `{default}`. `jobs_wait`:
   `token` (String, the token description shared with `jobs_cancel`), `entity` (optional),
   `output` (String, its description), `timeout_ms` (UInt 0..60000, default 30000); alternatives
   `{token}`, `{entity, output}`; no required top-level argument: the generated schema writes
   `required: []`, the hand-written one omits it, so the baseline compares them normalized (O2).
   Handlers unchanged (they keep their own checks). The fragments that lose their last users
   (`LaneProperty`, `AttributeProperty`, `DomainProperty`, `kEntityProperty`, `tokenProperty`)
   are deleted; `DomainNames` stays (`UnknownDomainMessage` uses it, B1).
4. **Baseline**: both hand-written schemas (registry dump at `36dd1fc81`; their registrations are
   unchanged since) join `tests/data/agent_declared_tool_schemas.json`; count pin 44 → 46.
- Not in this slice: `run_`/`preview_operation`.

## Tests (predictions as assertions)
- Order: before = `d3cdf663a`; with the baseline entries and count added and before the
  conversion, the baseline test is observed red (two tools not flagged, count 44); then steps 1-3
  and every suite reruns.
- `DeclaredFieldContracts.OnlyTrueCheckAgreesWithItsSchema`: for one OnlyTrue Bool field, the
  config check and the agent validator on the generated schema each accept `true` and refuse
  `false`, `1`, `"true"`, `null`; the schema has `"const": true`. A plain Bool still accepts
  `false` in both.
- `AgentArgumentSchema.RegisteredDeclaredArgumentsKeepTheirRules` gains, on the registered
  schemas: `bind_attribute` accepts `property` alone and `default: true` alone, refuses both,
  neither, `default: false`, an empty `property`; `jobs_wait` accepts `token` alone and `entity`
  with `output`, refuses `entity` alone, `token` with `entity` and `output`, nothing, and a
  `timeout_ms` of 60001; it accepts `token` with only `entity` or only `output` (one branch
  matches), which the unchanged handler still refuses with `invalid_params` (O1).
- Refactoring: `AgentOperations.*`, `AgentArgumentSchema.*`, `DeclaredFieldContracts.*`,
  `SandboxAgentServer.*`, `EngineConfigSectionSchemas.*`, `CoreEngineConfig(Load|Sections).*`
  pass before and after.
- Mutations, each must turn a test red: the schema omits `const` for OnlyTrue; the check accepts
  `false`; the alternatives are dropped; the alternatives' sets are merged into one;
  `jobs_wait` registered as text.

## Acceptance criteria
- [x] `OnlyTrue` with schema and check agreeing; alternatives in `ArgumentsSchema`.
- [x] `bind_attribute` and `jobs_wait` equal their baselines; 46 of 48 in `ARCH-020`.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure --timeout 120 -R '^(AgentOperations|AgentArgumentSchema|DeclaredFieldContracts|SandboxAgentServer|EngineConfigSectionSchemas|CoreEngineConfig(Load|Sections))\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Codex plan review (`plan341.answer.md` in this session's scratchpad): revise. B1
  `DomainNames` keeps a user (`UnknownDomainMessage`): kept, only `DomainProperty` deleted (the
  implementation already did so; the plan text corrected). O1 the partial mixtures for `jobs_wait`
  asserted on schema and handler; O2 the `required` normalization stated. `OnlyTrue` and the
  alternatives parameter judged the smallest shapes.
- 2026-10-29: Red before, observed with `OnlyTrue` declared but not handled: the baseline test
  (two tools not flagged, count 44) and `OnlyTrueCheckAgreesWithItsSchema` failed.
- 2026-10-29: Implemented: `ConfigFieldSpec::OnlyTrue` (schema `const: true`, check refuses
  false); `ArgumentsSchema`'s `alternatives` (oneOf of required sets, in order); `bind_attribute`
  and `jobs_wait` generated (`attribute` and `token` declared once and shared with
  `attribute_bindings` and `jobs_cancel`); `LaneProperty`, `AttributeProperty`, `DomainProperty`,
  `kEntityProperty`, `tokenProperty` deleted. Every Editor.cpp tool's input schema is now generated. jscpd found
  the two field agreement tests (`RUNTIME-340`'s and this one) repeating their loop: one helper,
  `ExpectCheckAndSchemaVerdicts`, now serves both (no clone left in that file). Mutations, each
  alone and restored, all red: schema without `const` (agreement, rules, baseline); check accepts
  false (agreement, and again after the helper); alternatives dropped (baseline, rules);
  alternatives merged into one set (baseline, rules); `jobs_wait` as text (baseline); after the
  helper also `ObjectOrString`'s check accepting anything (agreement). Focused CPU
  `AgentOperations|AgentArgumentSchema|DeclaredFieldContracts|SandboxAgentServer|
  EngineConfigSectionSchemas|CoreEngineConfig(Load|Sections)` 111/111; `SourceRules` 8/8;
  layering clean; changed lines clang-format 20.1.8.
- 2026-10-29: Codex review of the revision and the change (`review341.diff` in this session's
  scratchpad): approve; B1, O1, O2 resolved. Optional, both done: `OnlyTrue` documented as not
  combined with `Nullable` (its const would refuse the null the check accepts; no consumer does);
  the Log says "input schema" generated.
