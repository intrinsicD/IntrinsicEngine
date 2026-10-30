---
id: RUNTIME-326
theme: F
depends_on: [ARCH-020]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: agent argument validation at the runtime trust boundary; no catalog contract covers the agent lane's argument schema (the lane's behavior is documented in docs/architecture/agent-control-lane.md, updated here)
---
# RUNTIME-326 — The advertised agent schema is enforced at dispatch (moonshot M2, slice 1)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `7d967065e`, `c452f339e`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator question (verbatim, 2026-10-27)
3. "start improving the engine by implementing the Moonshot.md. Keep properly track of where you
   and your subagents are at so we can continue anytime properly."

## Goal
- Moonshot M2 ("declare once"), first slice, tracked by `ARCH-020` row 3. Each agent tool already
  declares a JSON schema (`AgentOperationSpec::InputSchemaJson`, advertised by `tools/list`), but
  every tool also re-checks its arguments by hand, and the two drift apart:
  - UI-083 slice 4's review found that `vector_field` ignored unknown keys (`"lenght":2` answered
    NoChange); the fix was a hand-written 17-key allowlist in that one tool.
  - `vector_field` advertises `line_width` 0.5–32, `stride` ≥ 1 and colors in 0–1, but its parser
    accepts any number and leaves the range to the command or to nobody.
- After this slice the advertised schema is the one declaration of an agent tool's argument
  shape: `InvokeAgentOperation` validates every call against it before the tool runs, so a
  schema/parser mismatch cannot be written for unknown keys, types, enums, ranges and required
  fields. Hand-written duplicate checks are deleted. Shortest path: one generic check at the one
  dispatch function, instead of converting tools one by one.

## Plan
1. **Validator** (`Runtime.AgentOperations.ArgumentSchema.cpp`, inside module
   `Extrinsic.Runtime.AgentOperations`, about 150 lines): `ValidateAgentArguments(schema, args)`
   returns the first violation as `"<json pointer>: <reason>"`, or nothing. It supports the
   keywords the registry uses today: `type` (incl. type arrays), `enum`, `const`, `minimum`,
   `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `minLength`, `pattern` (ECMAScript),
   `minItems`, `maxItems`, `items`, `properties`, `required`, `additionalProperties` (bool),
   `oneOf`, `anyOf`, `allOf`, `if`/`then`. Unknown keys are reported first, then missing
   required ones, then values. Annotations are ignored (`description`, `default`, `title`,
   `x-*`). Any other keyword makes `SupportedAgentSchema(schema)` false (fail closed: no
   constraint is silently skipped). `integer` accepts JSON integers only; `number` any finite
   number.
2. **Dispatch**: `AgentOperationRegistry::Register` parses the schema once and refuses a spec
   whose schema does not parse or uses an unsupported keyword (returns false, as for a duplicate
   name). `InvokeAgentOperation`, after the read-only and presentable checks and before
   `spec->Invoke`, parses the arguments and refuses a violation with
   `{IsError, "Invalid arguments: <violation>; see the schema.", ErrorCode "invalid_params"}`.
   No history entry, no workspace preparation.
3. **Delete proven duplicates only**: the 17-key allowlist in `vector_field`. Checked conversions
   (uint32 extraction, typed field parsing) and cross-field rules a schema cannot state (e.g.
   `jobs_wait` refusing a token together with an entity) stay with the tools. Schemas that were
   wider than their handlers are narrowed instead: uint32 `maximum` on entity ids, `stride`,
   `max_glyphs` and camera entity lists; `set_camera.pose` closed (`additionalProperties:false`).
4. **Schema honesty**: where a tool documents accepting more than its schema says (e.g. the
   `run_operation` params note "enums take their name or integer code"), make the schema say
   what is accepted, so validation does not refuse documented input. Found by running the
   existing agent suites with the validator on (step 5).
5. **Docs**: `docs/architecture/agent-control-lane.md` states that arguments are validated
   against the advertised schema before a tool runs, with the supported keyword list.

## Tests (written first; predictions as assertions)
- `AgentArgumentSchema.*` (contract, new): one case per supported keyword, accepting and refusing;
  an unsupported keyword makes the schema unsupported. Red before: the file does not compile
  (no validator).
- `AgentOperations.RegistrySchemasAreSupported` (contract): the full registry (editor, processing,
  capture, diagnostics, property inspection) registers every spec, and every schema is supported
  and has `additionalProperties:false` at the top level. Prediction: passes once step 1 exists
  (no tool uses another keyword, per a source grep on 2026-10-27).
- `AgentOperations.UnknownArgumentRefusedForEveryTool` (contract): for every registered tool,
  `{"__unknown__":1}` answers `invalid_params` naming the key, and leaves the history unchanged.
  Prediction before the change: most tools do not answer `invalid_params` (they report a missing
  field, a missing workspace, or succeed), so the test fails; record the count.
- `AgentOperations.VectorFieldRangeRefusedBeforeTheCommand` (contract): `line_width: 100` and
  `stride: 0` answer `invalid_params` with no history change. Prediction before: not
  `invalid_params`.
- The existing suites (`AgentOperations`, `SandboxAgentServer`, and the agent cases in
  `SandboxEditorUi`, `RuntimeSceneSerialization`) pass unchanged; a failure there is a
  schema/parser divergence, handled by step 4 and recorded here.
- Mutation: skipping the validator call in `InvokeAgentOperation` turns the two refusal tests red.

## Acceptance criteria
- [x] Every registered agent tool's arguments are validated against its advertised schema before
      it runs; a violation answers `invalid_params`, never reaches the tool, creates no
      continuation and leaves the undo history unchanged (diagnostic attempt records are kept).
- [x] A schema with an unsupported keyword cannot be registered, and the registry test proves every
      current schema is supported.
- [x] The proven duplicate is deleted and the existing agent suites pass, each changed
      expectation recorded with its reason.
- [x] `agent-control-lane.md` documents the validation.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(AgentArgumentSchema|AgentOperations|SandboxAgentServer|SandboxEditorUi|RuntimeSceneSerialization)\.'
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-27: Opened from `ARCH-020` row 3. Plan above; counter-review pending.
- 2026-10-27: Plan counter-review (Claude author → Codex reviewer, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a17ee7-4391-7dd0-b6ff-1799823879b2`;
  recursion protection instruction-only). Object: this note sha256 `8a32641b…`, base `48e152f19`.
  Verdict "revise". Blocking: `anyOf` missing, so `run_operation`/`preview_operation` would not
  register (B1); schemas wider than handlers, keep checked conversions and cross-field rules (B2);
  tests must prove dispatch behavior, registration refusal and valid calls (B3); no unfiltered CPU
  run after a focused build (B4). Optional: name the next M2 slice (O5); record changed
  diagnostics (O6). All folded in above; O5 in `ARCH-020`.
- 2026-10-27: Implemented. `Runtime.AgentOperations.ArgumentSchema.cpp` (validator),
  `Register` refuses unsupported schemas, `InvokeAgentOperation` validates after the read-only and
  minimized checks. The build has no exceptions, so a pattern is compiled at use, not probed.
  - Red before `anyOf` support (observed, as B1 predicted): `run_operation` and
    `preview_operation` were missing from the registry and a test dereferenced the missing spec.
  - Mutation 1, dispatch validation bypassed: 6 tests fail (`ViolationsNeverReachTheHandler`,
    `DocumentedArgumentFormsStillReachTheTool`, `UnknownArgumentIsRefusedForEveryToolBeforeItRuns`
    with all 47 of 47 tools failing, `VectorFieldRangesAreRefusedBeforeTheCommand`, and the two
    changed expectations below). Mutation 2, registration check removed:
    `EveryRegisteredSchemaIsSupportedAndClosed` fails.
  - Changed expectations (O6): `EditorOperationsShareTheDomainEnum` expected "Unknown domain
    'Bogus'", now `"domain" must be one of [...]` with `invalid_params`;
    `ConfiguredOperationEntityRulesFollowTheSection` expected the tools' own "needs" / "Unknown
    operation" texts, now the schema's `missing argument "entity"` and `"operation" must be one
    of [...]` (the alias call passes an entity so the enum check is reached). The test fixture
    `echo` declared no properties but was called with `x`; it now declares `x`.
    `SandboxAgentServer.RunOperationTakesExplicitParamsAndUndoes` expected the owner's "1 to 10"
    for `mesh_subdivide` iterations 30; the dispatch now reports `"params/iterations" must be at
    most 10` (the violated bound of the same declared range).
  - Deleted: the 21-line key allowlist in `vector_field`.
- 2026-10-27: Code review (Claude author → Codex reviewer, fresh session, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox; recursion protection instruction-only) on the staged
  patch sha256 `57eff8962688d4080c7cce875180b80c85ebc7924e93313ccae2409447bc5141`, base `2f8060be8`.
  Verdict "revise required". Blocking: P1 `std::regex` can end the process (a malformed pattern
  throws without exceptions; a million-digit job token can exhaust the stack in libstdc++'s
  recursive matcher); P2 numeric bounds and `enum`/`const` equality lose exactness (`maximum` 2^53
  accepts 2^53+1 via `double`; nlohmann equates `18446744073709551615` with `-1`); P3 `integer`
  is narrower than JSON Schema. Nonblocking: no independent `minLength` test; diagnostics promise
  global precedence and JSON Pointers. Ponytail: per-call schema parsing (kept; no latency
  measured, caching is an optimization).
  - Fixes: `pattern` is unsupported (the job token keeps its checked `from_chars` parser, its
    schema describes the format); numbers compare exactly through `long double` (static_assert of
    64 mantissa bits; x86-64 and aarch64 Linux) and `enum`/`const` use exact recursive equality;
    P3 kept as a documented narrowing (integer literal only), because every integer reader of the
    tools refuses `1.0` too; docs state per-object precedence and slash-separated paths.
  - New tests: code-point `minLength`, 2^53 bounds, `const -1` vs `18446744073709551615`,
    `1 == 1.0`, `pattern` unsupported, `HugeJobTokenReturnsNormally` (1,000,000-digit token).
    Mutation 3 (`Exact` through `double`, `Equal` through nlohmann `==`): the four exactness
    assertions fail. Focused suites 340/340 (`AgentArgumentSchema` 7, `AgentOperations` 37,
    `SandboxAgentServer` 33, `SandboxEditorUi` 230, `RuntimeSceneSerialization` 33).
- 2026-10-27: Delta review (same Codex thread `01a17ef5-83c2-7582-96c3-be7b083348bc`) on the staged
  patch sha256 `5aec0fa1f22d1ca7705e4cf36c2e1def052e24ef16ede8455f445c40c387cdd6`. Verdict verbatim
  "approve" ("No blocking findings remain."; P3 "acceptable for this slice ... It remains a
  documented deviation from JSON Schema semantics"). Nonblocking, applied after the verdict and
  owed to the next M2 block's review: nested exact-equality test cases; the huge-token test comment
  now says `jobs_wait` stops at the missing job service. Portability limit recorded by the
  reviewer: targets whose `long double` has fewer than 64 mantissa bits (MSVC) fail the
  static_assert.
