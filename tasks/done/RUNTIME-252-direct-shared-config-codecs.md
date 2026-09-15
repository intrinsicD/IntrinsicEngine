---
id: RUNTIME-252
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-14T23:23:04Z"
contract_schema: 1
contracts: [repo.source-documentation, runtime.processing-compilation-locality]
---
# RUNTIME-252 — Direct shared config codec definitions

## Goal
Remove the private config-codec forwarding layer while compiling shared JSON
parsing and validation once and preserving every config/UI/agent behavior.

## Non-goals
- No schema, defaults, diagnostics, validation or algorithm changes.
- No timing claim; BUILD-007 retains matched compile measurements.

## Context
The operator requested repeated cleanup with Claude until 2026-09-15 08:00
Europe/Berlin, overriding default task selection and the three-task limit.
Stop new implementation by 07:15; commit locally without pushing. Baseline
`29d75ebe7`. Standing Claude authorization applies; one writer owns the checkout.

Claude identified `Private.FeatureConfigCodecs` and five family forwarders.
All five public config interfaces already import Engine/EngineLoad; moving their
function definitions cannot introduce those dependencies. The shared codec TU
already owns all JSON parsing. Keep it at its current path as an ordinary C++ TU
importing the five public config interfaces. Globally attach only the forwarded
public function declarations with `extern "C++"`; define those functions directly
in `Extrinsic::Runtime` and remove the private `*Impl` API. Preserve the clustering
overload that supplies default properties. Keep genuine curvature conversion and
consolidation token functions in their existing module implementation units.

This removes redundant forwarding, not the five feature schema owners. Do not
split JSON code into each family, create new headers, or move unrelated helpers.
Reintroduce a private codec interface only for a demonstrated independent caller
that cannot use the public contract. Clang 23 is the available supported preset
toolchain; no additional build tree under existing BUG-195 disk constraints.

## Required changes
- [x] Direct definitions in the existing shared codec TU; global attachment of
  matching declarations in the five public config interfaces.
- [x] Delete the private codec interface and the three pure forwarding TUs;
  remove forwarding from the two TUs retaining genuine feature functions.
- [x] Update CMake and architecture assertions to retain the actual boundaries.

## Tests
- [x] Build canonical `IntrinsicTests` and run full CPU gate.
- [x] Rebuild and run focused config/editor tests in isolated ASan and UBSan;
  compare normalized moved bodies and retain diagnostics/round-trip coverage.
- [x] Independent Claude review of the frozen diff; fix confirmed findings.

## Docs
- [x] Update architecture/discovery owners; regenerate module inventory.
- [x] Record whole touched production footprint and compiler dependency counts;
  finalize overnight evidence, retire and seal the completed task.

## Acceptance criteria
- [x] One compiled JSON owner, no forwarding-only private codec module.
- [x] Public config behavior unchanged with successful native/sanitizer checks.
- [x] Reviewed final diff, synchronized docs and valid completion evidence.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
CCACHE_DISABLE=1 cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j 2
CCACHE_DISABLE=1 cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests -j 2
ctest --test-dir build/ci-asan --output-on-failure -R '^(SandboxConfigSections|PointCloudConsolidationConfig|RuntimeConfigControl|SandboxEditorUi|SandboxEditorPresentation|EditorCompilationLocality|ConfigCompilationLocality)' --no-tests=error --timeout 60 --parallel 1
ctest --test-dir build/ci-ubsan --output-on-failure -R '^(SandboxConfigSections|PointCloudConsolidationConfig|RuntimeConfigControl|SandboxEditorUi|SandboxEditorPresentation|EditorCompilationLocality|ConfigCompilationLocality)' --no-tests=error --timeout 60 --parallel 1
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/workflow_evidence.py validate --root .
```

## Forbidden changes
- Feature removal, compatibility wrappers, new abstractions, or weakened gates.
- Source changes after frozen review without renewing affected verification.

## Maturity
Endpoint is verified structural cleanup; no backend or capability promotion.
Existing BUG-178 cache and BUG-180 leak limitations remain; this task does not
change GPU execution and owes no new operational claim.

## Review and structural evidence
Claude implemented the declared slice as sole writer and approved the frozen
source/test diff after root's integration. Production and C++ test code stayed fixed after
that review; final review strengthened the two CMake compiler guards. The anonymous parser block is byte-identical; all 26 shared codec
bodies match after exact function-name substitution and extraction of the preserved
clustering default-properties overload. All 27 public definitions are present.
The retained curvature implementation directly includes `<cstddef>` for `size_t`.

Thirteen touched production/build files shrink from 4,150 to 3,750 lines: 400
lines and four production files removed. Module inventory falls from 418 to 417.
The retained curvature implementation's compiler dependencies fall from 39 to 17;
consolidation falls from 39 to 6, with no added dependency. Exact current
`Modules/...` compiler maps were used, excluding stale build-root maps. Two
compiler-boundary tests prohibit sibling codec imports. A negative control
requiring the real curvature module fails with the expected diagnostic.
These are structural counts; BUILD-007 retains matched compile timing.

Claude's optional test-tidiness notes require no correctness fix: the removed
private-module prohibition guards against reintroduction beside three live
sibling exclusions, and the one-element private-module assertion loop retains
the existing test structure. Compiler/linker and sanitizer checks address its
include/lookup caveats. No layer, schema, default, callback or error-policy change
is introduced. Shared codec functions have global language linkage; feature
structs and genuine feature functions remain attached to their owning modules.

Clean-workshop rows 1–3 pass (allowed imports, no new target edges or downward
public types), rows 4–7 are not applicable (no renderer/pass/recipe change or
capability promotion), and row 8 passes with no new exception. Architecture and
reuse-owner documentation point to the existing shared compiled owner. No new
production file, compatibility facade, template framework or public helper exists.

The first configure omitted `VCPKG_FORCE_SYSTEM_BINARIES=1`; the next configure
restored the same vcpkg packages and refreshed dependency timestamps, broadening
reconciliation. Subsequent commands consistently set it with `CCACHE_DISABLE=1`.
That build duration is neither a source regression nor timing evidence. Existing
Clang 23 preset trees were reused serially under BUG-195 disk pressure.

## Verification and retirement
Completed 2026-09-15. PR/commit: enclosing retirement commit on baseline
`29d75ebe7`. Endpoint: verified structural cleanup, no capability promotion.

- Canonical Clang 23 `ci` configure and `IntrinsicTests` build passed. Full CPU:
  4,629 selected, 4,628 passed, zero failures, one expected unsanitized
  `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` skip.
- Focused config/editor and compiler-boundary gates: 253/253 each on isolated
  ASan and UBSan after rebuilding both producers. Serial sanitizer CTest.
  This is focused sanitizer coverage, not the full sanitizer gate.
- Clean-workshop, task-state links, root hygiene, test layout, skill mirrors,
  source documentation and diff checks passed. Five changed module interfaces:
  zero source-documentation errors or review findings. Inventory regenerated.
- CPU JSON/config refactoring does not change GPU execution. No GPU capability
  promotion or matched compile-time claim; BUILD-007 remains open.

Command receipts and structural counts live in `tasks/evidence/RUNTIME-252/`.
The report, independent handoff/review records and post-commit seal bind the
reviewed source. Source and test code stayed fixed during native/sanitizer
verification; final metadata refresh renews downstream graph review only.
Claude's terminal final-surface verdict is recorded in the review ledger.
Temporary full CLI transcripts remain under `/tmp/intrinsic-overnight-20260915/`.

Final Claude review approved the source/evidence and noticed an optional gap:
the two retained config implementations did not forbid importing each other.
Both guards now exclude that sibling too, and a comment explains the retired
private-module reintroduction guard. Only CMake test registration changed;
all six strengthened checks were rerun on regenerated ci/ASan/UBSan registries.
No production/C++ test changes invalidate the existing full CPU or sanitizer runs.
The final narrow follow-up verdict is recorded against the refreshed digest.
