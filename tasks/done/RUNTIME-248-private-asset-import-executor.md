---
id: RUNTIME-248
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive structural cleanup; source diff, compiler metadata and existing import tests provide verification. No timing claim.
contract_schema: 1
contracts: [repo.source-documentation]
---
# RUNTIME-248 — Private asset-import executor and canonical dependencies

## Goal
Continue the user's explicit simplification and compile-iteration work with
Claude. Remove an unnecessary module producer and duplicate dependency storage
from asset-import integration while preserving all import and lifecycle behavior.

## Plan and reuse
`AssetWorkflowModule` already owns the executor in its private implementation;
the executor interface has one production importer and no direct test consumers.
Its `BorrowedSubsystem` and `BorrowedBool` types add implicit conversions without
ownership or lifetime enforcement. The existing dependencies record can hold the
borrows directly instead of repeating its fields and copying them individually.

1. Review the fixed source and plan with Claude. Preserve the public workflow,
   target binding epoch, initialized pointee check, cancellation and shutdown order.
2. Replace the executor module interface with a private header shared only by
   the workflow's two implementation units. Keep import/decode bodies in their
   current compiled file; remove the obsolete CMake module registration.
3. Store the existing dependency record once, remove pointer wrappers and the
   unused dependency constructor, and make pointer/reference uses explicit.
4. Review the fixed combined diff, fix findings, build and run relevant import,
   lifecycle, full CPU, sanitizer and actual Vulkan checks.

Right-sizing: retain the class as a private implementation owner because queued
callbacks borrow its stable address across reinitialization. No new pimpl,
service, forwarding API or compatibility alias. A second independent owner
requiring this executor could justify a module surface again. Separate material
and geometry helpers retain their current owners and behavior.

The current mixed-run Ninja log identifies this interface as a dominant compile
producer, but supplies no matched timing evidence. Check removal using fresh
compiler metadata and count the complete production footprint. BUILD-007 retains
matched engine-source timing; no compile speedup is inferred from deleted lines.

## Acceptance criteria
- [x] Remove the private executor BMI and duplicate pointer/storage mechanisms.
- [x] Preserve initialization, rebinding, queue cancellation, imported data and visible output.
- [x] Address Claude's fixed-source review and pass the relevant final gates.
- [x] Synchronize private-owner documentation and module inventory; retire the task.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' -R '^(AssetWorkflowModule|AssetFormatCapabilities|RuntimeEnginePrivateGlue|RuntimeAsset)' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
```
Use existing Clang 23 preset trees and disabled ccache (BUG-178). Use host access
for sanitizer discovery (BUG-188) and actual Vulkan; retain BUG-180's GPU leak
policy. Keep one writer/build per tree and check disk headroom (BUG-195).
Baseline, bounded Claude reviews and command logs are under
`/tmp/intrinsic-asset-locality-20260914/`.

## Implementation and review
Claude's plan review confirmed the private-header approach and separate compiled
bodies. It highlighted the initialized-pointee check, explicit imports and the
two distinct binding epochs; those are preserved. `BindingValid` has live readers
and remains in the canonical dependency record. Existing public workflow tests
cover a non-null false initialization flag, reinitialization, world replacement,
cancellation and both owner shutdown orders.

Compilation exposed one missing explicit ECS handle import. Linking then exposed
the named-module rule that in-class definitions are not implicitly inline: both
accessors now have one definition in the existing executor implementation unit.
The subsequent `IntrinsicTests` build passes. The existing private-glue test now
checks the owning module attachment and three workflow units. No new synthetic
link test or runtime test seam was necessary.

Claude approved the final corrections. Explicit dependency imports, the private
header attachment guard, and a nothrow dependency-assignment assertion address
its findings. The required command-history service remains required: silently
skipping dirty-state publication for an unsupported null service would weaken
the existing resolve contract. The executor already includes `<type_traits>`.

The four affected production files shrink from 5,114 to 5,018 physical lines,
with four files retained and the module inventory reduced from 419 to 418.
The configured compiler command graph has no producer for the deleted executor
interface. These are structural observations, not a measured compile speedup.

Final verification on Clang 23, with ccache disabled:
- Full CPU selector: 4,623 selected, no failures, six skips. Five display-dependent
  checks passed on the host follow-up; the unsanitized leak-control check retains
  its expected skip. Total distinct passing CPU cases: 4,622.
- Focused import/lifecycle selector above: 72/72 native, 72/72 isolated ASan,
  72/72 isolated UBSan. Sanitizer builds use `IntrinsicRuntimeContractTests` and
  matching presets; CTest uses `--parallel 1`.
- Actual Vulkan: 5/5 import, progressive geometry, texture, visibility and picking
  checks pass. Build target `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`;
  selector `^RuntimeSandboxAcceptanceGpuSmoke.(Imported|DirectMeshEnrichmentPending)`,
  timeout 120, `ASAN_OPTIONS=detect_leaks=0` under the existing BUG-180 limitation.
- Strict clean-workshop, layering, task policy, documentation links, source
  documentation, root hygiene and test layout checks pass.
- Developer application rebuilt with
  `CCACHE_DISABLE=1 cmake --build --preset dev --target ExtrinsicSandbox -j 3`.

## Retirement
Completed 2026-09-14. PR/commit: enclosing retirement commit on baseline
`5319013a9`. Endpoint: verified structural cleanup, with existing
behavior preserved; this task introduces no backend or new capability claim.
BUILD-007 still owns matched compile-time evidence.

Scope, layer ownership, tests and docs pass the final sweep. Clean-workshop:
rows 1–3 pass (allowed imports, unchanged target links, no higher-layer public
types); rows 4–6 are not applicable (no renderer, pass or recipe changes);
row 7 is not applicable to capability promotion; row 8 passes with no temporary
exceptions. Existing epoch and callback lifetime rules remain unchanged.
