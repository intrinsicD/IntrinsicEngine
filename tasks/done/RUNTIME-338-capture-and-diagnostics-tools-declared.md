---
id: RUNTIME-338
theme: F
depends_on: [ARCH-020, RUNTIME-337]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: the capture, diagnostics and device-status tool input schemas generated from declared arguments instead of hand-written JSON; accepted arguments, refusals and tool behaviour unchanged (the committed baseline comparison); no catalog contract covers the tool schemas
---
# RUNTIME-338 — Capture and diagnostics tools declared once (moonshot M2, slice 14)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `c00889f34`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
17. (2026-10-28) "Ja bitte" (answer to: continue with the four tools `RUNTIME-337` deferred for
    scope: `view_screenshot`, `view_capture`, `diagnostics_read`, `device_status`)

## Goal
- M2 target "100 % of MCP tools are generated" (`ARCH-020` row 3). After `RUNTIME-337`: 36 of 48
  tools have a generated input schema; these four are expressible today but register in their own
  units (`Runtime.AgentOperations.Capture.cpp`, `Runtime.AgentOperations.Diagnostics.cpp`) without
  the agent helper header, and omit `required`. After this slice: 40 of 48; the remaining eight
  need constraints `ConfigFieldSpec` does not express (listed in `RUNTIME-337`).

## Plan
1. **A small header for the declarations**: `AgentArgument`, `ToolSchema`, the `ArgumentsSchema`
   declaration and `EntityArgument` move from `Runtime.AgentOperations.Detail.hpp` to a new
   `Agent/internal/Runtime.AgentArguments.hpp` (needs only `Extrinsic.Runtime.ConfigFieldSpec`,
   nlohmann json and std headers); `Detail.hpp` includes it, so the three existing units are
   unchanged. Capture and Diagnostics include only the new header (not the editor-heavy one).
2. **Capture**: `region` (StringEnum viewport/window), `preset` (StringEnum, tokens from the
   handler's own `kPresets` through `ToString`, in its order), `fit_entity` and `legend_entity`
   (the qualified `AgentDetail::EntityArgument`, optional, renamed to their own names, their
   descriptions; Capture's own `EntityArgument` parser stays, O1), and for `view_capture` also `path` (String),
   `path_only`, `overwrite` (Bool), descriptions as today, no defaults (none today). Registered
   with `.InputSchemaJson = schema.Text, .DeclaredArguments = schema.Declared`; flags and handlers
   unchanged (the handler's own parsing stays).
3. **Diagnostics**: `since_cursor` (Int, Min 0, default 0: no maximum, as today), `levels` (Array
   of Enum with names Info/Warning/Error/Debug, values 0..3), `categories` (Array of String,
   MaxItems 1000), `limit` (UInt 1..1000, default 1000); `device_status` from `ArgumentsSchema({})`
   (its handler's own empty-object check stays).
4. **Baseline**: the four hand-written schemas from the registry dump taken for `RUNTIME-337` at
   `36dd1fc81` before any source edit (Capture.cpp, Diagnostics.cpp and the spec's default schema
   are unchanged since) join `tests/data/agent_declared_tool_schemas.json`. They omit `required`, the
   generator always emits it (as for the 36 others, including the argument-less ones); the test
   treats an absent `required` in a baseline as `[]`, which JSON Schema defines as the same, and
   says so in a comment. Every other key must be equal, descriptions and `x-enum-names` included.
   Count pin 36 → 40.
- Not in this slice: the eight tools with richer constraints; generated handlers.

## Tests (predictions as assertions)
- `AgentOperations.DeclaredSchemasEqualTheHandWrittenBaseline` fails after step 4's baseline
  entries are added and before steps 1-3 (four tools in the baseline, not flagged), passes after.
- `AgentArgumentSchema.RegisteredDeclaredArgumentsKeepTheirRules` gains, on the registered schemas:
  `diagnostics_read` accepts `since_cursor` 4294967296 and `levels` [0,3], refuses `since_cursor`
  -1, `levels` [4], 1001 categories, `limit` 0; `view_capture` refuses `fit_entity` 0 and region
  `screen`; `view_screenshot` refuses `path` (unknown argument); `device_status` refuses any key.
- Order (O2): before = `d87a49012` (CI green, every suite); then the baseline entries, count and
  normalization are added and the baseline test is observed red as predicted; then steps 1-3 and
  everything reruns. Refactoring: `AgentOperations.*` (except that intermediate),
  `AgentArgumentSchema.*`, `ViewCapture.*`, `DeclaredFieldContracts.*`, `SandboxAgentServer.*` pass
  before and after.
- Mutations, each must turn a test red: the `levels` item declared UInt instead of Enum; the
  `categories` MaxItems dropped; Capture registers without the flag; the test's `required`
  normalization removed (the four baselines then differ).

## Acceptance criteria
- [x] The declarations in their own header; Capture and Diagnostics register generated schemas.
- [x] The four schemas equal their baselines (absent `required` as `[]`); 40 of 48 in `ARCH-020`.
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 120 -R '^(AgentOperations|AgentArgumentSchema|ViewCapture|DeclaredFieldContracts|SandboxAgentServer)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-28: Codex plan review (`plan338.answer.md` in this session's scratchpad): approve, no
  blocking finding. Optional, both taken: O1 the entity-id helper named explicitly (Plan 2); O2 the
  verification order (Tests).
- 2026-10-28: Implemented: `Agent/internal/Runtime.AgentArguments.hpp` (the declarations moved
  out of `Detail.hpp`, which includes it); Capture declares region, preset (tokens from
  `kPresets`), `fit_entity`, `legend_entity` and the three `view_capture` file arguments;
  Diagnostics declares `since_cursor`, `levels`, `categories`, `limit` and `device_status`'s empty
  list; `kRegionSchema` deleted. With the four baselines, count and normalization added and before
  the conversion, the baseline test was red exactly as predicted (four tools not flagged, count 36);
  after it, green. Mutations, each alone and restored, all red: `levels` item UInt (both new
  tests); `categories` MaxItems dropped (both); `view_screenshot` not flagged (baseline test);
  normalization removed (baseline test). Focused CPU `AgentOperations|AgentArgumentSchema|
  ViewCapture|DeclaredFieldContracts|SandboxAgentServer` 92/92; `SourceRules` 8/8; layering clean;
  changed lines clang-format 20.1.8 (it also rewrapped the two capture handlers' one-line `if`s in
  the touched statements); jscpd: no new clone (the two capture handlers' existing clone remains).
- 2026-10-28: Codex code review (`review338.diff` in this session's scratchpad): approve, no
  blocking or optional finding.
