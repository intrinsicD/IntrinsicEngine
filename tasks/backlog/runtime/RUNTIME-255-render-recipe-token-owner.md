---
id: RUNTIME-255
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
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
- [ ] Five token implementations live with their enums and serve both existing consumers.
- [ ] All 31 spellings, parser candidate lists/order, Unknown handling and safety diagnostics preserved.
- [ ] Remove superseded editor helpers directly; no new abstraction/module/file or compatibility path.

## Tests
- [ ] Literal-driven expected spellings for all five enums and invalid-enum fallbacks.
- [ ] Per-enum accepted/unknown tokens and parseable-but-unsafe domain behavior remain unchanged.
- [ ] Existing recipe editor model and full CPU/focused sanitizer gates pass.

## Docs
- [ ] Document the shared token owner briefly in existing renderer docs and refresh module inventory.
- [ ] Record exact net production counts without calling code movement deletion or speedup.
- [ ] Complete high-risk independent review, handoff/report, retirement and exact source seal.

## Acceptance criteria
- [ ] Canonical token ownership reused without changing any accepted config or displayed label.
- [ ] Literal expectations and existing UI/config behavior verified with independent review.
- [ ] Final source locally committed, retired and sealed with no new production file or performance claim.

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
