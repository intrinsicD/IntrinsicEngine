---
id: RUNTIME-343
theme: F
depends_on: [ARCH-020, RUNTIME-342]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: the explicit-parameter commands (mesh denoise, remesh, subdivide, simplify, scalar ridge) declare each field once with the command member it sets; the editor's range checks and the agent's params mapping read that declaration instead of repeating it; refusals, defaults and the agent schemas are unchanged, and accepted values too except remesh's approximation_error, now held to its declared minimum of 0 under every sizing law (deliberate, no production path sends a negative value); no catalog contract covers these commands' parameter checks
---
# RUNTIME-343 — Command fields bound to their members once (moonshot M2, slice 19)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
22. (2026-10-29) "Do both" (answer to: next M2 goals, generated editor panels and a smaller
    `src/runtime/Editor`)

## Goal
- M2: "one declaration ... generates the ImGui panel, the validated apply path, the MCP tool"; and
  `src/runtime/Editor` ≤ 25k lines. Surveys this session (two read-only agents, findings verified
  on the code): the explicit-parameter commands state each field's link to its command member three
  times beside their field tables: the agent's `Param(fields, "name", &Cmd::Member)` lists
  (`Runtime.AgentOperations.Operations.cpp` ~472-560), the editor's per-field range chains
  (`InFieldRange(..., "name", command.Member)`, `Runtime.MeshTopologyOperations.Topology.cpp`
  ~2494-2702; `InRange` in `Runtime.ScalarRidgeOperations.cpp` ~68-82), and the panels' per-field
  `DrawSpec*` calls (`Sandbox.MeshProcessingPanels.cpp`, e.g. 972-989, which also omits
  `degenerate_normal_length_epsilon`).
- This slice declares the link once, in the field table, and makes the editor checks and the agent
  read it. The panels follow in the next slice from the same rows (generated controls, starting
  with the mesh topology panels). Editor size: the surveyed candidates total about 2.6-2.8k lines
  against the ~27k the 25k target needs from the 52,077-line baseline (O3: insufficient identified
  savings, not an exhaustive limit); the target stays open and this is reported to the operator as
  a goal question, not decided here.

## Plan
1. **Bound field** (in `Extrinsic.Runtime.ConfigFieldSpec`, exported templates): `BoundField<Cmd>`
   = a `ConfigFieldSpec` plus `Get`/`Set` function pointers that read and write the member as a
   double (Bool 0/1, an enum its enumerator value), made by `Bind<&Cmd::Member>(spec)` (captureless
   lambdas, so tables stay `constexpr`); `FieldNumber`/`SetFieldNumber` add and remove an Enum's
   `Min` with the row's spec (O1). Lossless for the bound members (32-bit integers, `uint8_t`,
   float, double, bool, small enums, `size_t` within its declared 0..1e9); `Set` takes a value its
   member can hold, so callers check the range first (documented). A row with no
   member (scalar ridge's `property`, a `GeometryPropertyRef` the owner reads) has null accessors.
   `SpecsOf(rows)` projects a `constexpr` array of plain specs, so `...FieldSpecs()` keep their
   span and every current caller (schemas, contract tests, panels) is unchanged.
2. **Generic check**: `FieldsInDeclaredRange(rows, command)`: every bound numeric row's value is
   accepted by its spec (`AcceptsConfigFieldNumber`; an Enum's code within its names); Bool rows
   always pass. It replaces the `InFieldRange` chains of denoise, remesh, subdivide, simplify and
   scalar ridge's `InRange` chain. The enum validators (`ValidMesh*`) go: every declared enum's
   names are exactly its valid values, in enumerator order (checked). Denoise's stage has no row
   and keeps its check. Messages, statuses and cross-field rules are unchanged.
   One accepted value changes, deliberately (B1): remesh's `approximation_error` was only required
   finite under the mean-curvature sizing law, while its declaration (and so the agent's schema and
   params) requires at least 0; the generic check now holds the command to it. Checked first that
   no production path can send a negative value: the panel does not draw the field (it sends the
   default 0.01), the agent refuses it by schema, and the kernel reads it only under the
   error-bounded law. Codex preferred keeping the old rule in this refactoring slice; the check
   above is the reason for keeping the tightening here, stated for the code review to judge.
3. **Agent**: `Param` takes a bound row instead of a member pointer; each command's params list is
   built from its rows (`ParamsOf(rows)`), the ridge's `property` keeps `NameParam` in its first
   place. Defaults keep coming from `Cmd{}` (through `Get`), as type-specific JSON (O2).
4. **Tables**: denoise, remesh, subdivide, simplify (`Runtime.MeshTopologyOperations.Topology.cpp`)
   and scalar ridge (`Runtime.ScalarRidgeOperations.cpp`) become bound rows; new exports
   `EditorMesh{Denoise,Remesh,Subdivide,Simplify}Fields()` and `EditorScalarRidgeFields()` return
   the rows.
- Not in this slice: the panels (next), the config-section families (their sections are read by
  owner codecs), the other repeated glue the survey found (config wrappers, topology job template,
  undo snapshot helpers).

## Tests (predictions as assertions)
- Refactoring, unchanged behaviour: the agent schema baseline (`AgentOperations.DeclaredSchemas
  EqualTheHandWrittenBaseline`: params bodies and their defaults identical), the mesh methods
  suite (`Test.SandboxEditorMeshMethods.cpp`, suite `SandboxEditorUi`), `ScalarRidgeOperations.*`, `AgentOperations.*`,
  `AgentArgumentSchema.*`, `DeclaredFieldContracts.*` pass before and after.
- Before and after the change, a dump of every agent tool's schema and description must be equal
  (one-off, the temporary dump test).
- New, with independent expectations (B2): `DeclaredFieldContracts.BoundFieldsReachTheirNamedMembers`
  sets each bound field of the five commands by name to a distinct value and checks the member of
  that name (and that no other bound field changed); every bound row must have an expectation, and
  the ridge's `property` is the only unbound row (O2: the exact field set).
  `BoundFieldsRefuseValuesOutsideTheirRanges` sets members directly to a value each can hold just
  outside its declared range (and an enum past its names) and expects the generic check to refuse
  it, the defaults to pass (every numeric field except subdivide's `max_output_faces`, whose range
  is its whole `uint32_t`). `BoundEnumFieldsOffsetTheirWireCode`: a local enum with `Min` 1 maps
  both ways.
- `SandboxEditorUi.MeshTopologyOptionAdmissionSharesValidationAndPermitsRecovery` gains the remesh
  matrix (B1): negative and NaN `approximation_error` refused under both sizing laws (status and the
  preview's reason, no undo step), 0 accepted under mean curvature and refused under error-bounded.
- Mutations, each must turn a test red: a row bound to another field's member; `FieldsInDeclaredRange`
  skipping Float rows; an Enum's wire number without its `Min`; the agent's params dropping the
  enum rows (the operation description pins `mesh_simplify`'s `metric`); the old remesh rule for
  `approximation_error` (the new matrix case, i.e. red before).

## Acceptance criteria
- [x] Bound rows for the five commands; specs projected from them.
- [x] Editor range chains and agent `Param` lists replaced; schemas unchanged, behaviour unchanged except
      remesh's `approximation_error` minimum (deliberate, Plan 2).
- [x] Codex plan and code review; focused suites and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 180 -R '^(AgentOperations|AgentArgumentSchema|DeclaredFieldContracts|SandboxEditorUi|ScalarRidgeOperations|SandboxAgentServer)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Codex plan review (`plan343.answer.md` in this session's scratchpad): revise. B1 the
  generic check tightens remesh's `approximation_error`: kept deliberately after checking no
  production path sends a negative value (Plan 2), with the requested matrix of cases (Tests). B2
  the tests could not catch swapped bindings or the Enum offset, and casting out-of-range doubles
  into unsigned members is undefined: named-member expectations with distinct values, a local
  enum with `Min` 1, and members set directly to holdable values instead. O1 the double contract
  and `Set`'s precondition documented; O2 the exact field set and the property row asserted; O3
  the size statement qualified. (The verification command's suite names were corrected after the
  review was sent: `SandboxEditorUi`, `ScalarRidgeOperations`.)
- 2026-10-29: Implemented: `BoundField`, `Bind`, `SpecsOf`, `FieldNumber`/`SetFieldNumber`,
  `FieldsInDeclaredRange` (exported templates in `Runtime.ConfigFieldSpec.cppm`); the five tables
  as bound rows with `...Fields()` exports and their spec spans projected; the `InFieldRange` and
  ridge `InRange` chains, `InFieldRange` itself and the four `ValidMesh*` validators with their
  arrays removed; the agent's `Param` reads a row and `ParamsOf` replaces the five lists. A dump of
  all 48 tool schemas and descriptions before (HEAD) and after: equal; the dump test removed.
  Mutations, each alone and restored, all red: a row bound to another field's member (named-member
  and range tests); the range check skipping Float rows (range test and two mesh admission tests);
  the Enum wire number without `Min` (offset test); the agent's params without the enum rows
  (`ConfiguredOperationEnumMatchesTable`, baseline); the old remesh `approximation_error` rule (the
  new matrix case, so red before). Focused CPU `AgentOperations|AgentArgumentSchema|
  DeclaredFieldContracts|SandboxEditorUi|ScalarRidgeOperations|SandboxAgentServer` 330/330;
  `SourceRules` 8/8; layering clean; changed lines clang-format 20.1.8 (it also re-wrapped the
  lambdas of the touched `ExplicitOperation` statements); jscpd: no clone in the changed code.
  Lines: `src/runtime/Editor` 52,077 -> 52,088 (+11: chains and validators out, rows longer); the
  agent file +40 (formatting). This slice is the shared declaration; the panels follow.
- 2026-10-29: Codex review of the revision and the change (`review343.diff` in this session's
  scratchpad): approve; B1 (the tightening accepted on the producer check), B2, O1-O3 resolved.
  Optional, all done: the unchanged-behaviour claims qualified with the `approximation_error`
  exception (front matter, acceptance); the range test's comment names the `max_output_faces`
  exception; the tracker records this slice.
