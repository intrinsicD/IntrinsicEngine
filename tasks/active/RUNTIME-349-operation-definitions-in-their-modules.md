---
id: RUNTIME-349
theme: F
depends_on: [ARCH-021, RUNTIME-348]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: the operation runtime becomes an exported framework module for operation modules, and the kernel density and point spacing definitions (record, field rows, section, compute, result) move into their modules under src/runtime/Modules, which instantiate the runtime across the module boundary; entry points, messages, statuses and values are unchanged; a layering rule keeps operation modules out of editor internals; no catalog contract covers these operations
---
# RUNTIME-349 — Operation definitions in their modules (ADR 0031 phase 1, slice 4)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
27. (2026-10-29) "Do 1. - 3." (3: start phase 0 and then the phase 1 pilot)
28. (2026-10-29) "Please do 1. and 2." (1: tests that pin the per-operation structure may be
    rewritten, keeping every behavioural assertion)

## Goal
- ADR 0031 Decisions 7 (module surface) and 8 (placement), the first step of the old `RUNTIME-349`
  scope; generated exposure (agent row, section registration, default panel) and the phase 1 gate
  measurement follow in `RUNTIME-351`. Codex's `RUNTIME-347` review O1: start this with the exported
  framework module and the first instantiation across a module boundary, and drop the internal
  header route.
- Today a kernel density definition is spread over the config module (`Modules/KernelDensity`), the
  codec section (`Config/internal/Runtime.PointNeighborhoodConfigCodecs.cpp`), the editor's point
  field module (definition, result, entry points) and the runtime header.

## Plan
1. **Technical spike first** (asserted pass or fail; B2 approves it): an exported module
   `Extrinsic.Runtime.OperationRuntime` (`src/runtime/Editor/Operations/Runtime.OperationRuntime.cppm`)
   holds the runtime templates, with the internal helper declarations they call included in its
   purview inside `extern "C++"` (global-module attachment; reachability does not need them
   exported). Pass: the kernel density implementation unit under `src/runtime/Modules`, **without**
   any editor header include, instantiates `OperationRuntime<Def>`, compiles **and links**, and the
   suites pass. **Fallback** if the toolchain refuses, specified before it is needed (B2): the driver
   becomes non-template code in the framework's implementation unit working on one `OperationRun`:
   the typed result is owned as `std::shared_ptr<void>` next to a `ResultView` (function pointers to
   the common fields the concept lists: status, message, counts, backends, GPU counters, statistics
   fold) generated once per definition by a template from that list, so no operation writes a copy
   list; delivery adapters cast back to `Def::Result` (the key, not RTTI, justifies the cast). The
   fallback is reported to the operator before it is built.
2. **Placement** (per operation, the gate's "3 own files" structure, O1):
   - interface `Modules/<Op>/Runtime.<Op>Config.cppm` (kept, extended): the record, the result
     struct, an exported definition tag (`Params`, `Result`, names; no compute), typed access to the
     bound rows (`std::span<const BoundField<Params>>`, for the generated panel and agent of
     `RUNTIME-351`), the entry points;
   - implementation `Modules/<Op>/Runtime.<Op>Operation.cpp` (new): the rows and section declaration
     (moved from `Runtime.PointNeighborhoodConfigCodecs.cpp`), the runtime definition (the tag plus
     `Compute`, `Validate`, GPU parameters and statistics fold), the instantiation, the entry point
     forwards;
   - the test file.
   `Extrinsic.Runtime.PointFieldOperations` stays only as the pilot family's frame
   (`PrepareEditorPointFieldFrame`) and a re-export of the two modules; it is frozen: later
   operations do not add to it (O1).
3. **Keys by definition** (O2): the store keys by the exported definition tag and its payload is
   `Tag::Result` (`Store<Tag>(invocation, Tag::Result)`, `Last<Tag>()`), so key and payload cannot be
   chosen apart; a test with two definitions sharing one result type keeps them apart, and the
   ordering, dismissal and detached-delivery tests stay.
4. **Layering rule** (B3): a source rule that files under `src/runtime/Modules` include no
   `Editor/internal/` or `Editor/Operations/` header. Its allowlist names exact files and includes,
   with owner and expiry (repository rule on allowlists): today's offenders found by the rule
   itself (Clustering and PointCloudConsolidation include `Editor/Operations` headers; TextureBake
   only imports a module and is not exempt). No directory-wide exclusion.
- **Structural test rewrites** (operator decision 28, B1), listed now:
  `ProcessingCompilationLocality.ConfigPropertyTypes` (the two config interfaces now depend on the
  editor processing surface through their entry points: the rewrite drops the
  `GeometryAvailability` and `ECS.Components.GeometrySources` exclusions for these two files only
  and adds exclusions of unrelated operation modules and of the algorithm modules
  `Geometry.PointCloud*`, keeping the check meaningful); `ProcessingCompilationLocality.Family` and
  `.PointFieldResults` (producer paths updated to the new files); the 19 `--forbid-module
  Extrinsic.Runtime.PointFieldOperations` checks also forbid the two operation modules where they
  guard unrelated families; test imports where a test names a moved declaration.
- Not in this slice: agent row, section registration and default panel generation, the gate
  measurement (`RUNTIME-351`); other operations.

## Tests (predictions as assertions)
- Refactoring pin: the `RUNTIME-348` list (804 tests) and the Vulkan binary (57) pass; the
  structural rewrites above, each listed in the Log.
- Spike: the kernel density and point spacing implementation units contain no `Editor/` include
  (checked by the new rule) and build and link.
- Store keys: `OperationResults.DefinitionsSharingAResultTypeStaySeparate` (red before: keyed by
  result type they collide).
- Layering rule: red on a fixture file under `src/runtime/Modules` that includes an editor internal
  header, green on the tree; a mutation that weakens its pattern turns the fixture test green
  (shown red). (B2: the explicit-instantiation mutation is dropped; entry points can instantiate
  implicitly, so its removal need not fail.)

## Acceptance criteria
- [x] Technical test passed (or the fallback taken and reported); framework module exported.
- [x] Both definitions in their modules; codec and editor files lose them; suites green.
- [x] Layering rule with its test; Codex plan and code review; structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure --timeout 300 -j8 -R "<RUNTIME-348 list>"
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 600 -R "<every test of IntrinsicPointLBVHGpuTests>"
python3 tools/repo/check_source_rules.py --self-check
python3 tools/repo/generate_module_inventory.py --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Plan; the runtime template calls about twenty internal editor helpers, which makes the
  exported module the slice's risk (step 1).
- 2026-10-29: Codex plan review (`plan349.answer.md`): revise. B1 the config interfaces' boundary
  test conflicts with entry points in them: the structural rewrites are listed (Plan), keeping
  exclusions of unrelated operations and algorithms. B2 the spike is approved; the fallback is now
  specified (a generic result view, no per-operation copy lists) and the explicit-instantiation
  mutation dropped. B3 the allowlist names exact files with owner and expiry; TextureBake is not
  exempt. O1 typed access to the bound rows exported now; the point field facade frozen. O2 keys by
  the exported definition tag with its result type bound, tested with two definitions sharing a
  result type. All taken.
- 2026-10-29: Spike (Plan step 1) passed: `Runtime.OperationRuntime.hpp` became the exported module
  `Extrinsic.Runtime.OperationRuntime` (`Runtime.OperationRuntime.cppm`: the six editor helper
  headers in its purview inside `extern "C++"`, the runtime under `export extern "C++"`); the point
  field implementation unit imports it with **no** editor header include and instantiates both
  definitions; it compiles and links (clang 23); the `RUNTIME-348` list: 803 passed and 1 skipped by
  design (the density weights case of the GPU retention test); the Vulkan binary: 56 passed and 1
  skipped by design (the opt-in CPD scaling profile), address and undefined-behaviour sanitizers on.
  The fallback is not needed.
- 2026-10-29: Codex code review of the spike (`review349a.answer.md`): revise. The module form is
  correct and the runtime body unchanged byte for byte; two corrections taken: the module inventory
  regenerated (the new module), and the counts above restated with their skips (ctest's summary
  counts a skipped test as passed). The exported surface (the definition concept, the runtime and
  its nested `Work`, `PointSamplesInput`, `JoinOperationText`) hides the editor helpers' names but
  not their shapes; stated, not changed.
- 2026-10-29: Placement (Plan steps 2-4) implemented:
  - each operation's interface (`Modules/<Op>/Runtime.<Op>Config.cppm`) holds the record, the result,
    the tag `<Op>Operation`, `<Op>ConfigRows()` and the entry points; its implementation unit
    `Runtime.<Op>Operation.cpp` holds the rows and section (moved from the codec file, whose
    exported functions keep global-module attachment), the definition, the entry points and the
    config wrappers through the framework module's `ApplyOperationConfig`/`ActiveOperationConfig`;
  - `Runtime.PointFieldOperations.Processing.cpp` and `.cpp` are gone; the facade keeps the frame and
    re-exports both modules (frozen); the codec file keeps outlier analysis and density weights;
  - the store keys by the operation tag (`Store<Op>`, `Last<Op>`, payload `Op::Result`; the runtime
    concept requires `Def::Operation` with the same result);
  - rule `OperationModuleBoundary` (owners: the two grandfathered files; their include counts
    pinned) with removal task `RUNTIME-352` (expiry 2026-12-31) and its ctest suite;
  - structural test rewrites (operator decision 28): `ConfigPropertyTypes` without the two
    interfaces, which join `PointFieldResults` (no point cloud algorithm modules); `Family` checks the
    two implementation units instead of the deleted files; `RuntimeReuseDriftGuard`'s literal
    allowlist loses the two interfaces, which use `PropertyNames::kPosition` now; the key change in
    the tests (`Last<Op>`); the cross-table `IsDerivable` witness renames a field of its own table.
  Tests: `OperationResults.DefinitionsSharingAResultTypeStaySeparate` (red before as a compile
  failure: the old API keys by the result type; red by mutation: keying by `Operation::Result`
  again fails it and the dismiss test). The rule names a new offender (mutation: an editor include
  added to the kernel density unit); its self-check passes. Results: the list plus
  `OperationModuleBoundary` 806 (805 passed, 1 skipped by design); Vulkan 57 (56 passed, 1 skipped
  by design); config codec golden unchanged; changed lines clang-format 20.1.8; module inventory
  current. Lines: `src` +178 net (the two units 2x ~265 replace ~541 lines; interfaces, the
  framework helpers and each unit's module boilerplate add the rest); `src/runtime/Editor` 52,190.
  (The first version of this entry put each operation's own code at about 330 lines; ADR 0031 counts
  every line of the operation's own files: see the next entry.)
- 2026-10-29: Codex code review (`review349b.answer.md`): revise. Behaviour preserved (rows, tokens,
  sections, codec and registration bodies, results and entry points identical by token comparison;
  global-module attachment kept; `kPosition` equals the literal). B1 the 19 family checks also
  forbid `Extrinsic.Runtime.KernelDensityConfig` and `Extrinsic.Runtime.PointSpacingConfig` now (all
  35 locality checks pass). B2 the grandfathered files' exact include strings are pinned besides
  their count (swapping one for another editor header fails, shown). O1 the accounting: own files
  382 lines for kernel density (interface 101, implementation 281) and 367 for point spacing (103,
  264); `RUNTIME-351` created with a removal budget and the early warning that the phase 1 gate
  (150) looks out of reach without deeper cuts. These fixes go to the next block's review
  (`RUNTIME-351`). After them: 806 (805 passed, 1 skipped by design).
- 2026-10-29: Codex (in `RUNTIME-351`'s plan review, `plan351.answer.md`, Object B): B1 and B2 of the
  code review confirmed resolved in `a3dd0663a`.
