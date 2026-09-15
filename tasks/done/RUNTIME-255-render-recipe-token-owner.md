---
id: RUNTIME-255
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T01:13:12Z"
contract_schema: 1
contracts: [repo.source-documentation]
---
# RUNTIME-255 — Share render-recipe enum token ownership

## Goal
Give five existing render-recipe enum spellings one implementation at the enum
owner, reused by the runtime editor and graphics config parser.

## Non-goals
No schema, accepted-token, UI-label, validation, backend, numeric or lifetime
change. No generic enum helper/registry, new file/module, numeric enum iteration,
compatibility shim or performance claim. This is consolidation plus movement;
source size may be near-neutral, not a claimed net deletion or faster build.

## Context
User-authorized cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, commit locally without pushing. RUNTIME-254
is retired in `9a7c3f0c0`, sealed in `94862a63a`; claim clean main first.
Standing Claude source-sharing authorization and no-compatibility commitment apply.
High-risk profile covers additive public module declarations and fixed-source review.

Verified owners/callers:
- `Graphics.RenderingContract.cppm/.cpp` owns the enums and nine existing compiled
  `ToString` overloads. Extend this existing owner with five overloads only.
- `Runtime.RenderRecipeEditingOperations.cpp` has five local `DebugNameFor*`
  switches for BindingSourceDomain, BindingValueType, ViewKind, OutputTargetKind
  and InteractionMode. All return the matching token or `Unknown` for invalid values.
- `Graphics.RenderRecipeConfig.cpp` has five private `Parse*` functions spelling
  the same tokens. Root compared all 31 enum/token pairs mechanically: 9+10+5+5+2,
  byte-identical, including case and spacing. Repeat/save that baseline at claim.

Contract differences retained: BindingSourceDomain and BindingValueType explicitly
accept `Unknown`; the other three parsers reject it. Unrecognized strings return
nullopt; subsequent safety checks may reject parseable domains such as Runtime or
Generated. Invalid enum values still display `Unknown`. Claude initially suggested
rejecting Unknown everywhere, then explicitly withdrew that incorrect condition
after reviewing the exact source-derived token map. Preserve the existing sets.
All five parsers are ordinary non-constexpr functions, so compiled ToString calls
introduce no constant-evaluation requirement. No allocation in string_view comparisons.

Reuse/right-sizing decision: five compiled overloads at the existing enum owner
serve two present consumers. Keep each explicit parser candidate list and order;
replace only literal comparisons with `value == ToString(Enum::ExistingValue)`.
Do not add a generic parser/template/table, exported token registry or Parse API.
The separate prepared-frame template-helper candidate is rejected: ~30 lines of
repetition do not justify extra template glue beside the sealed RUNTIME-243 design.

Claude approved this corrected plan. Source comparison and review files are under
`/tmp/intrinsic-overnight-20260915/runtime254/next-token-baseline.json` and
`claude-next-token-correction.txt`; this task owns the scope and required evidence.

## Slice plan
1. Capture source identities, literal-pair equality and total affected production
   line/file counts before editing. Reuse existing tests and named enum owner.
2. Claude, as sole source writer, adds five ToString declarations/compiled switches,
   replaces the five runtime DebugNameFor helpers with string materialization at
   their callers, and routes only the five private parsers through ToString.
3. Add literal-driven contract checks to existing graphics test files for all
   spellings/invalid enum fallback and parser acceptance, Unknown treatment and
   unsafe-domain rejection. Keep existing editor-model assertions and strengthen
   only missing output coverage. No new test/source file.
4. Independent fixed-diff Claude review, full CPU/focused sanitizer verification,
   fix valid findings, refresh module inventory and existing renderer docs, retire
   with high-risk handoff/review/report/seal evidence.

## Required changes
- [x] Five token implementations live with their enums and serve both existing consumers.
- [x] All 49 spellings, parser candidate lists/order, Unknown handling and safety diagnostics preserved.
- [x] Remove superseded editor helpers directly; no new abstraction/module/file or compatibility path.

## Tests
- [x] Literal-driven expected spellings for all seven enums and invalid-enum fallbacks.
- [x] Per-enum accepted/unknown tokens and parseable-but-unsafe domain behavior remain unchanged.
- [x] Existing recipe editor model and full CPU/focused sanitizer gates pass.

## Docs
- [x] Document the shared token owner briefly in existing renderer docs and refresh module inventory.
- [x] Record exact net production counts without calling code movement deletion or speedup.
- [x] Complete high-risk independent review, handoff/report, retirement and exact source seal.

## Acceptance criteria
- [x] Canonical token ownership reused without changing any accepted config or displayed label.
- [x] Literal expectations and existing UI/config behavior verified with independent review.
- [x] Final source locally committed, retired and sealed with no new production file or performance claim.

## Verification
```bash
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --preset ci
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci --output-on-failure -R '^(RenderingContract|RenderRecipeConfig|SandboxEditorUi.RenderRecipe|EngineConfigControl|RenderRecipeActivation)' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-asan --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci-asan --output-on-failure -R '^(RenderingContract|RenderRecipeConfig|SandboxEditorUi.RenderRecipe|EngineConfigControl|RenderRecipeActivation)' --no-tests=error --timeout 60 --parallel 1
CCACHE_DISABLE=1 VCPKG_FORCE_SYSTEM_BINARIES=1 cmake --build --preset ci-ubsan --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests -j2
ctest --test-dir build/ci-ubsan --output-on-failure -R '^(RenderingContract|RenderRecipeConfig|SandboxEditorUi.RenderRecipe|EngineConfigControl|RenderRecipeActivation)' --no-tests=error --timeout 60 --parallel 1
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```
Existing Clang 23 preset trees only; keep cache environment consistent and run
variants sequentially. No GPU execution change or capability promotion; CPU and
focused sanitizers cover this string/config contract. BUILD-007/C92 remain open.

## Forbidden changes
- Changing spelling/case, normalizing input, adding aliases or accepting inferred enum values.
- Replacing explicit candidate lists with numeric ranges or broad schema rewrites.
- Adding exported parsers, generic token infrastructure, new files or unrelated prepared-frame helpers.
- Claiming runtime/compile speedup or reduced code size without exact matched evidence.

## In-scope planning refinement — existing token owners
Before parser/test edits or builds, source inspection found the same duplicated
spelling in adjacent ParseRendererCapability and ParseRenderOutputKind. Their
existing RenderingContract ToString overloads already provide all 11+7 identical
tokens. Reuse them in this same parser file, with unchanged candidate lists/order;
no additional declaration, file or mechanism. This completes the same token-owner
consolidation for all seven parsers with current ToString owners while avoiding a
second compiler rebuild for the identical two small follow-ups. The user-approved
cleanup scope is unchanged; the earlier five-overload API limit still applies.
Record 49 matched pairs total (31 moved-owner, 18 existing-owner); literal-driven
tests cover all seven. Other parser families, aliases and fallback policies stay
unchanged. The compiled owner/editor edits were already made by Claude before its
24-turn tool cap; resume only remaining parser/tests work, not those completed edits.

## Completion — 2026-09-15
- Endpoint: **Retired**, shared enum-token ownership with unchanged config/UI behavior.
- Commit: implementation and retirement are in the enclosing local commit;
  `tasks/evidence/RUNTIME-255/seal.yaml` names the exact sealed revision.
- Five compiled ToString overloads replace five runtime-local spelling switches.
  Seven parsers reuse 49 exact owner tokens, including two pre-existing overloads.
  Their candidate lists, order, Unknown handling and rejection diagnostics are
  preserved. Exact baseline substitution reproduces the entire config source.
- Four existing production files total 3,441 to 3,435 physical lines: six fewer,
  with zero new production files or modules. This is primarily consolidation,
  not large code deletion. No compile-time or runtime-speed claim.
- Four new tests pin literal spellings, invalid values and parser/ownership
  discrimination; existing config and editor-model tests cover the reused outputs.
  Full CPU: 4,636 passed and one expected unsanitized GLFW/LSan control skip.
  Focused tests: 27/27 on native, ASan and UBSan, with variants run sequentially.
  Full sanitizer and GPU execution suites were not repeated for this string-only
  change; no backend capability or method-parity claim is made.
- Claude approved the fixed source diff. Root checked every optional review note:
  fixed enum underlying types, direct algorithm include, intentional parse-versus-
  ownership assertions and untouched parser contracts. The source-documentation
  review flag is the existing mandatory slot-borrow lifetime comment; retained.
  Two earlier Claude implementation calls reached tool limits; root completed
  the remaining tests and removed duplicate fixtures. No failing C++ gate.
- Architecture/clean-workshop: rows 1–3 pass (existing allowed graphics owner,
  no new dependency, five additive declarations); 4–7 not applicable (no frame
  pass, recipe behavior, rendering protocol or maturity promotion); row 8 passes
  with no new exception. Lifetime, concurrency and failure paths are unchanged.
  Config/UI/agent spelling now shares the same owner; no generic enum machinery.
- Existing frame-graph and reuse-owner docs are synchronized; module inventory
  was regenerated and still contains 417 modules. Fixed-digest independent final
  review and command receipts bind the retirement. BUILD-007/C92 remain open.
