---
id: RUNTIME-351
theme: F
depends_on: [ARCH-021, RUNTIME-349]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, CI and the gate measurement
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: kernel density and point spacing get their agent row, section registration and panel generated from their definitions, their entry point and section forwards become generic, and the phase 1 gate of ADR 0031 is measured; behaviour unchanged; no catalog contract covers the editor's generated exposure
---
# RUNTIME-351 — Generated exposure and the phase 1 gate (ADR 0031 phase 1, slice 5)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
27. (2026-10-29) "Do 1. - 3." (3: start phase 0 and then the phase 1 pilot)
28. (2026-10-29) "Please do 1. and 2." (1: tests that pin the per-operation structure may be
    rewritten, keeping every behavioural assertion)
29. (2026-10-29) "ok, then do 1." (answer to: 1 build `RUNTIME-351` as planned and measure the phase 1
    gate, then bring options if it is missed; 2 deeper cuts first; 3 pause and decide the gate now)

## Goal
- ADR 0031 Decision 7 (exposure: `OperationRow<Def>`, `RegisterOperationSection<Def>`, a default
  `DrawOperation<Def>` panel) and the phase 1 gate: each pilot operation fits "adding an operation"
  (at most 9 files, 3 its own) with at most 150 operation-specific lines; build time and memory
  within the ADR's limits; existing behaviour unchanged.
- **Starting point (measured 2026-10-29, after `RUNTIME-349`):** own files: kernel density 382 lines
  (interface 101, implementation 281), point spacing 367 (103, 264); plus their panels (about 130
  each in `Sandbox.MeshProcessingPanels.cpp`), agent rows and registrations in shared files.
- **Removal budget for kernel density** (to be refined by the plan):

  | Part | Lines now | Can it go? |
  |---|---|---|
  | entry point declarations and forwards (9 functions) | about 75 | to generic templates on the operation tag; a few lines remain |
  | section functions and their declarations (serialize, validate, get, set, specs, registration, ToString) | about 50 | to generic functions of the section declaration; a few lines remain |
  | `IsDerivable` witnesses | about 22 | to a test |
  | config wrappers | about 18 | to the runtime |
  | panel (shared file) | about 130 | to `DrawOperation<Def>` plus a one-line listing |
  | record, enum, result struct | about 45 | stays |
  | rows and section declaration | about 63 | stays |
  | definition (names, compute, GPU parameters, statistics fold) | about 55 | stays |
  | module boilerplate and includes | about 50 | partly |

  **Early warning:** what stays adds up to about 210-230 lines per operation before any shared-file
  lines, above the gate's 150. Unless the plan finds deeper cuts (a generic result header instead
  of each result's common fields, fewer includes), the gate will be missed; per ADR 0031 that is an
  implementation failure to fix within the phase, and the operator decides how to proceed.

## Plan
Blocks, each built, tested and reviewed before the next.
- **Dropped: generic entry points** (Codex plan review B1). Calling `OperationRuntime<Op>` from
  consumers would cross ADR 0031 Decision 7's boundary (consumers use non-template entry points; the
  runtime template is for operation modules). The accepted boundary stays; changing it would need
  an ADR amendment by the operator. Consequence: the nine entry points per operation (about 75 lines)
  stay, so the size criterion moves further out of reach.
1. **Generic section functions** (O1). The derived codec becomes an exported module
   (`Extrinsic.Runtime.DerivedConfigSections`: its templates and declaration types exported, the
   config helper headers in its purview with their existing linkage). An operation's interface
   exports its section declaration (rows and backing arrays with static lifetime); the named
   functions (`Serialize<Op>Config`, `Validate<Op>ConfigSection`, `Get/Set<Op>Config`,
   `<Op>ConfigFieldSpecs`, `Make<Op>ConfigSectionRegistration`) give way to generic ones over the
   declaration. The registration stores `&ValidateSection<Section>`, a function with the exact
   three-`string_view` validator type (no closure), and the test that compares the registered
   validator compares that same specialization. Golden payloads, schemas and diagnostic order stay.
   Callers' signatures and defaults are written out before migrating (O2). If the helpers cannot
   be reached, the block stops and reports.
2. **Generated exposure.** `OperationRow<Op>()` in the agent (today's `ConfigSourced` row from the
   definition's name, family and section and the existing entry points); `RegisterOperationSection`
   in the Sandbox's section list; `DrawOperation<Op>` in `Sandbox.PanelSupport`, under a **panel
   contract** fixed in the block's own plan before the windows are replaced (B2): window title and
   menu identity, field order, exact labels and widget IDs (today's `InputScalar`/`InputFloat`/
   `Combo`, not the clamping drag controls of `DrawBoundFields`), the GPU-only batch size field,
   positions-to-output domain synchronization, draft synchronization with the active section,
   session-owned state, the GPU transaction row, execution diagnostics, progress, show, dismiss and
   the result lines (`Describe(result)`); display labels kept apart from the schema descriptions, no
   added ID scope. The two hand-written windows go only when the panel tests pass unchanged.
3. **The gate, measured.**
   - **Size (static, deterministic):** per operation, its "adding an operation" footprint (files and
     lines: own files, and each line it adds to shared files: exports, menus, state, build lists,
     generated files), counted separately from migration edits; framework additions reported
     separately; against ADR 0031's limits (9 files, 3 own; 150 lines).
   - **Build time and memory:** these decide gate criteria, so they are results-bearing runs (B4):
     full experiment pre-flight before the first measurement (frozen packet: pre-pilot baseline
     `f96a13e2d` and `HEAD`, exact targets, preset and compiler, parallelism, cache policy, the
     definition edit, clean-build procedure, repetition order, uncertainty and detection limit;
     memory: executable, filter, input, backend, workers, RSS aggregation; two fresh reviews, Claude
     and Codex; an HTML report from raw outputs). Measured whatever the size result (question 29:
     build and measure).
- Not in this slice: other operations; the common result header and the shared point-scalar field
  declarations (O3: a plausible deeper cut, an option for the operator after the measurement).

## Tests (predictions as assertions)
- Every existing behavioural assertion stays (decision 28); structural pins of removed functions and
  windows are listed per block.
- New: the generic section functions reproduce the golden (`config_section_codec_golden.json`
  unchanged) and the registration stores the generic validator; `OperationRow` equals today's rows
  (the agent tool schema baseline unchanged); `DrawOperation` meets the panel contract (the panel
  tests unchanged, plus a test per contract item the existing tests do not cover).
- Mutations per block, named in the block's plan.

## Acceptance criteria
- [ ] Generic section functions; generated agent row, section registration and default panel for
      both operations.
- [ ] Phase 1 gate measured and reported: size, and build time and memory under pre-flight.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure --timeout 300 -j8 -R "<RUNTIME-349 list>"
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 600 -R "<every test of IntrinsicPointLBVHGpuTests>"
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/repo/generate_module_inventory.py --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Created from `RUNTIME-349`'s code review (O1), with the measured starting point and the
  removal budget.
- 2026-10-29: Operator (question 29): build as planned and measure. Plan written: four blocks
  (public definitions without entry point forwards; generic section functions; generated agent
  row, registration and panel; the gate measured by a method fixed in advance).
- 2026-10-29: Codex plan review (`plan351.answer.md`): revise. B1 generic entry points would cross
  ADR 0031's module boundary: dropped, the boundary stays (an amendment would be the operator's).
  B2 the panel needs an exact contract before the windows are replaced: fixed in block 2's own plan.
  B3 the measurement method was not frozen and the baseline should be pre-pilot; B4 build time and
  memory decide gate criteria, so they need the full experiment pre-flight: both in block 3. O1 the validator pointer: a generic
  function of the exact type. O2 signatures before migration. O3 a deeper cut (shared point-scalar
  fields, backend tokens, the same-domain check, common result fields) kept as an option after the
  measurement. Object B: `RUNTIME-349`'s two post-review fixes confirmed resolved.
- 2026-10-29: Block 1 (generic section functions) implemented: `Extrinsic.Runtime.DerivedConfigSections`
  (`Runtime.DerivedConfigSections.cppm`, from the internal header: `DerivedConfigSection`,
  `IsDerivable`, the codec internals reachable, and `SerializeDerivedSection`, `ValidateDerivedSection`,
  `GetDerivedSection`, `SetDerivedSection`, `MakeDerivedSectionRegistration` over a section
  declaration); each operation's interface exports its rows, fields and `k<Op>Section`; the named
  section functions are gone (callers renamed: 85 lines in 9 files; `ToString(<Op>Backend)` stays);
  the registration stores `&ValidateDerivedSection<Section>` and the configuration test compares it.
  Finding: instantiating the codec templates in consumer units failed where a consumer lacks a
  standard header the module's global fragment discarded (`std::span` iterator comparison); fixed
  by `extern template` declarations in the interface and one explicit instantiation per section in
  the operation's unit, so consumers never instantiate the codec. The `IsDerivable` witnesses moved
  to `Test.DeclaredFieldContracts.cpp` (dropping the name check fails its `static_assert`, shown).
  Results: 806 (805 passed, 1 skipped by design), Vulkan 57 (56, 1 opt-in skip); golden unchanged.
  Own files: kernel density 332 lines (159 + 173; was 382), point spacing 339 (161 + 178; was 367);
  `src` net about -23 lines with the new module (247).
- 2026-10-29: Codex code review of block 1 (`review351a.answer.md`): revise for the note only, no
  C++ defect: behaviour preserved, the extern template arrangement sound, no ODR or lifetime hazard,
  renames name-only. B1 the plan had made build time and memory conditional on the size result,
  which question 29 did not authorize (and the plan review did not propose): the condition is
  removed; all three are measured. O1 `<limits>` included in both interfaces.
