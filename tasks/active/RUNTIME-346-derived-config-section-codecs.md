---
id: RUNTIME-346
theme: F
depends_on: [ARCH-021]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: the kernel density and point spacing config sections are read, validated, written and registered by one codec derived from their bound field rows instead of hand-written code; payloads, canonical output, refusal messages, schemas and registrations are unchanged (pinned by golden round trips and the existing section tests), except that an integer literal above 2^53 in a float field now reaches the float through double (one more rounding); no catalog contract covers these codecs
---
# RUNTIME-346 — Config section codecs derived from bound fields (ADR 0031 phase 0, slice 1)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
27. (2026-10-29) "Do 1. - 3." (answer to: 1 accept ADR 0031; 2 lift the "no generic property-write
    API" limit for operations; 3 start phase 0 and then the phase 1 pilot)

## Goal
- ADR 0031 Decision 2: a config section's codec (parse, serialize, validation, get/set,
  registration) follows from its bound field rows, validating the payload before conversion. Today
  each section writes these by hand, e.g. kernel density (`Runtime.PointNeighborhoodConfigCodecs.cpp`
  ~89-119 rows, ~343-447 codec) and point spacing (~120-150, ~450-560), about 100 lines each, already
  on shared helpers (`ValidatePointConfigFields`, `InConfigFieldRange`, `IsConfigFieldToken`,
  `ValidatePointConfigNonnegativeFloats`, `ValidatePointConfigPropertyRefs`, `GetPointConfig`,
  `MakeSectionRegistration`). This slice derives both codecs; the phase 1 pilot operations then use
  them. Outlier analysis and density weights in the same file follow the same shape (next users).

## Plan
1. **Row kinds** (`Runtime.ConfigFieldSpec.cppm`): `Bind<&Cfg::Ref>(spec)` of a
   `GeometryPropertyRef` member fills the row's new `Ref` (a member pointer, not a number; the ADR's
   follow-up list called it `BindRef`); an enum bound to a `StringEnum` row needs no new kind (`Bind` already reads an enumerator as its
   index; enumerator i is token `EnumValues[i]`, checked against the owner's `ToString`, and an
   index past the tokens serializes as `"invalid"`, as `ToString` does today); the entity id stays a
   plain `Bind` of a `UInt` row. `ConfigFieldSpec` gains `Refusal`, the message of **one stage set by
   the row's type** (B1): a `UInt` row's range check, a `StringEnum` row's token check, a `Float`
   row's stored-float positivity check. Every other stage reports its generic text, so
   `bandwidth: -1` keeps "bandwidth must be a finite nonnegative float." while `bandwidth: 1e-50`
   reports the row's refusal, and a wrong unsigned type keeps its generic text.
2. **Derived codec** (`Config/internal`, an internal template header): a section declaration holds
   the name, schema id, version, title, the rows, the object and unknown-field messages and an
   optional cross-field check on the converted record. Validation runs on the payload first, in
   today's order: merge over the serialized defaults, reject unknown keys and wrong unsigned types
   (`ValidatePointConfigFields`), then per row in table order: unsigned ranges, tokens, non-negative
   finite floats, stored-float positivity (a positive value that would be zero as `float`), property
   references (`ValidatePointConfigPropertyRefs`); only then conversion and the cross-field check;
   accepted payloads are the canonical serialization of the converted record. Serialize, parse,
   get, set and the registration (with `BuildSectionSchemaJson`, the one schema builder) are
   generic functions over the declaration. The registration stores the **exported** validator
   (B2: `SandboxConfigSections` checks its function pointer); `Get` stays validated (through
   `GetPointConfig`) and `Set` stays unvalidated, as today.
3. **Kernel density and point spacing**: their rows become bound rows (members bound, refusal
   texts on the rows that have them), their codecs the derived functions; the exported functions
   (`Serialize*`, `Validate*Section`, `Get*`, `Set*`, `Make*Registration`, `*FieldSpecs`) stay as
   one-line forwards so no caller changes.
- Not in this slice: other sections; the operation runtime (`RUNTIME-347`).

## Tests (predictions as assertions)
- Before the change, a golden capture of each section's behaviour from the hand-written codec
  (`tests/data/config_section_codec_golden.json`, written by a temporary test and then removed,
  compared by `DeclaredFieldContracts.ConfigSectionCodecsMatchTheirGolden`): canonical serialization
  of the defaults, of a fully non-default record and of an out-of-range backend (`"invalid"`); 31
  payloads per section: every refusal path, valid payloads, normalization witnesses (B3:
  `{"bandwidth":0.1}` canonicalizes to the float's value, `-0.0`, an integer float, the largest
  unsigned) and paired refusals that pin the stage order; the registration's name, schema id,
  version, default payload and schema JSON compared as strings (O1). After the change the derived
  codec must reproduce every one exactly.
- The owners' `ToString` reads the row's tokens, so no test is needed for agreement between the two
  lists (the first version had a test `BoundTokensFollowTheOwnersNames` for a second list; removed
  after the complexity review).
- `IsDerivable` (a `static_assert` per section): every row has its accessor and matches its spec in
  `Fields` by name and type; kernel density rows with the point spacing fields do not compile.
- Refactoring: `SandboxConfigSections.*`, `KernelDensityOperations.*`, `PointSpacingOperations.*`,
  `AgentOperations.*`, `AgentArgumentSchema.*` (the tool schema baseline), `EngineConfigSectionSchemas.*`,
  `DeclaredFieldContracts.*` pass before and after.
- Mutations, each must turn a test red: the stored-float check dropped; refs validated after
  conversion; a token row mapped off by one; the canonical output written from the payload instead
  of the converted record; the out-of-range token serialized without its bounds check (red as a
  crash or a wrong string); the registration storing a lambda instead of the exported validator.

## Acceptance criteria
- [x] Row kinds and the derived codec; kernel density and point spacing on it, exports unchanged.
- [x] Golden behaviour reproduced exactly; suites green.
- [x] Codex plan and code review; structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 300 -R '^(SandboxConfigSections|KernelDensityOperations|PointSpacingOperations|AgentOperations|AgentArgumentSchema|EngineConfigSectionSchemas|DeclaredFieldContracts)\.|\.SourceRules$'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Codex plan review (`plan346.answer.md` in this session's scratchpad): revise. B1 the
  refusal text belongs to one stage per row type; B2 the registration keeps the exported validator,
  `Get` validated and `Set` not; B3 normalization, precedence and out-of-range enum witnesses; O1
  schema compared as a string; O2 the core target dropped. All taken (Plan, Tests). The golden was
  captured from the hand-written codecs before any change (31 cases per section).
- 2026-10-29: Implemented: `ConfigFieldSpec::Refusal`, `BoundField::Ref` (filled by `Bind` of a
  `GeometryPropertyRef` member), `Config/internal/Runtime.DerivedConfigSection.hpp` (declaration,
  `IsDerivable`, serialize, parse, validate, get, set, registration); kernel density and point
  spacing as bound rows, one declaration each and one-line forwards. The golden passes unchanged
  on the derived codecs. Mutations, each alone and restored, all red: stored-float check dropped
  (golden, `SandboxConfigSections.PointConfigFloatsPreserveRangesAndDiagnosticOrder`); refs
  validated after conversion (three `SandboxConfigSections` tests abort, so a witness written for it
  was dropped as redundant); token parse off by one (golden and four); canonical output from the
  payload (golden only); out-of-range token without its bounds check (golden only); registration
  storing a forwarding lambda (`PointConfigSectionsShareRegistrationSetAndGetContract`). jscpd over
  the five config codec files: no clone involves the new code (not run over the whole repository).
- 2026-10-29: Complexity review (ponytail): one finding, the backend tokens written twice (row and
  `ToString`): `ToString` now reads the row, the guard test removed. Codex code review
  (`review346.answer.md`): approve; B1-B3 resolved, no other differing payload found beyond the
  stated rounding. Optional, all taken: O1 the header states the contract the next users must
  check (every Float row refuses a positive value that stores as zero; `Check` after references;
  outlier analysis and density weights differ); O2 `IsDerivable` matches rows and fields by name
  and type (the mismatched table fails its `static_assert`, mutation shown); O3 the operation
  table's glyph_order row corrected (`ARCH-021`). These fixes go to the next block's review
  (`RUNTIME-347`).
  After the fixes: affected suites and `*.SourceRules` 129/129; layering and task policy clean;
  changed lines clang-format 20.1.8. Lines: `src` +102 (the header 180, the two sections' codecs
  -88, about 44 each); the header pays for itself after about four sections.
