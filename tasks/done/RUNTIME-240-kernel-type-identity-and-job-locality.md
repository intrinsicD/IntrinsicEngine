---
id: RUNTIME-240
theme: J
depends_on: [RUNTIME-239]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive kernel-interface refactor; exact dirty baseline, fixed Claude reviews and correctness/dependency checks, without a timing claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.kernel-interface-locality]
---
# RUNTIME-240 — Reuse type identity and narrow the kernel job interface

## Completion — 2026-09-14
Completed locally and retired after acceptance/evidence review. Accumulated
implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
Historical dirty-source measurements retain their original eligibility limits;
this retirement is not a publication or whole-engine completion verdict.
BUILD-007 owns matched engine compile measurements; C92 remains a hypothesis.


## Goal
Continue the operator-authorized simplification with Claude, preserving the
verified uncommitted RUNTIME-233–239 source. Remove frame-graph dependencies
from type identity and GPU command dependencies from CPU job declarations.
This is the next bounded architecture slice, not Framework24 product closure.

## Reuse and reviewed plan
- `Core.Hash` already owns hashing and is imported by both graph owners. Move
  `Core::TypeToken` and its existing `Core::Detail::TypeSig` there; remove the
  FrameGraph definitions. Share one constexpr 64-bit FNV-1a string hash with
  TaskGraph's existing type-token calculation. Preserve each signature source,
  unsigned-byte treatment and high-bit mask. Distinct graph signature sources
  are not merged. Keep existing 32-bit string hashing unchanged.
- Commands, events, service lookup and jobs import `Core.Hash` for identity.
  Real graph users keep their graph imports. Update direct callers to the
  actual owner; do not add compatibility re-exports, headers or wrappers.
- `JobService` names `RHI::ICommandContext` only by reference. Reuse the existing
  non-exported extern-C++ borrow pattern; its sole RHI class definition and all
  four out-of-line definitions receive matching attachment. Preserve the
  virtual surface, derived classes and GPU participant API. Actual consumers
  explicitly import the owner where required.
- Keep job/event envelope ownership separate: they have distinct access and
  publication contracts, and no present shared owner justifies a new wrapper.
  Keep the real scheduler import and all job/cancellation/publication logic.

Claude confirmed the plan. Root corrected its omitted out-of-line linkage
obligation before implementation. A baseline Clang23 assembly probe records
both type-token values with identical type spelling and preset flags; compare
after rebuilding. Do not turn incidental inequality between the graph tokens
into a new public contract or freeze compiler-specific constants in tests.

The exact baseline and reviews are under
`build/analysis/runtime240-kernel-type-identity-2026-09-13/`. No production file is added. Count the
complete source delta, including all import fixes and build declarations.
The borrow stays appropriate only while JobService needs a reference, not
command members or value storage; actual command use belongs in implementation.

## Acceptance criteria
- [x] Discover actual owners/consumers and review the bounded plan with Claude.
- [x] Implement one hash owner and narrow the four kernel interfaces.
- [x] Preserve type identity, erased dispatch, job lifecycle and GPU context behavior.
- [x] Add known hash vectors, retain cross-TU coverage and enforce compiler-metadata boundaries.
- [x] Review the fixed diff with Claude and fix confirmed findings before final gates.
- [x] Pass native CPU, separate sanitizer, relevant actual Vulkan and strict structural checks; synchronize docs/inventory and record remaining scope.

## Implementation notes
Implemented, reviewed and verified in the working tree.

- `Core.Hash.cppm` gains the exported `Hash::HashString64` (64-bit FNV-1a,
  unsigned bytes, embedded NULs included) plus `Core::Detail::TypeSig<T>()` and
  `Core::TypeToken<T>()`, moved verbatim from `Core.FrameGraph.cppm` with the
  same qualified names, signature spelling and high-bit mask, so
  `__PRETTY_FUNCTION__` and the resulting values are unchanged. It also gains the
  standard includes it already needed (`<cstddef>`, `<functional>`, `<limits>`)
  and a file synopsis. The 32-bit `HashString`/`StringID` lane is untouched.
- `Core.FrameGraph.cppm` drops both definitions and the now-unused `<cstddef>` /
  `<limits>`, keeps its `Extrinsic.Core.Hash` import for `Hash::StringID`, and
  no longer claims to own type identity. Its builder always routed typed
  declarations to the task graph's own tokens, so no behavior moved. No
  compatibility re-export or alias was added.
- `Core.Dag.TaskGraph.cppm` keeps its own `__PRETTY_FUNCTION__` signature source
  and mask in `Detail::TypeTokenValue<T>()`; only the duplicated FNV loop is
  replaced by `Hash::HashString64`. The two graphs' token sets are unchanged and
  unmerged.
- `Runtime.CommandBus`, `Runtime.KernelEvents`, `Runtime.ServiceRegistry` and
  `Runtime.JobService` import `Extrinsic.Core.Hash` instead of
  `Extrinsic.Core.FrameGraph`; JobService keeps its scheduler import. Stale
  FrameGraph-owner comments in CommandBus were corrected. No template body
  changed. The touched interfaces that carried their banner after the module
  declaration (`Core.FrameGraph`, `Core.Dag.TaskGraph`, `Runtime.CommandBus`,
  `Runtime.KernelEvents`) and `Runtime.ServiceRegistry`, which had none, gained
  the required short leading synopsis.
- `RHI.CommandContext.cppm` wraps the sole `ICommandContext` definition in
  `export extern "C++"`, and all four out-of-line definitions in
  `RHI.CommandContext.cpp` — destructor key function, `BindFrameSampledTexture`,
  `CopyTextureToBuffer`, `BindFrameSampledTextureAt` — respecify the same
  linkage in one block. Virtual surface, slot order, bodies, `NullCommandContext`
  and every derived class are unchanged.
- `Runtime.JobService.cppm` replaces its `Extrinsic.RHI.CommandContext` import
  with the non-exported global `extern "C++"` borrow; the reference-only GPU
  participant API is unchanged. No in-tree consumer needed a new import: every
  TU that names a command context or calls a member — `Runtime.Engine.cpp`,
  `Runtime.JobService.cpp`, `Runtime.SpatialIndexCache.cpp`, the TextureBake,
  Clustering and PointCloudConsolidation modules and their GPU units, and the
  job/readback/GPU-smoke tests — already imports the owner.
- Tests: `Test.CoreHash.cpp` adds the published FNV-1a 64 vectors
  (`""`, `"a"`, `"foobar"`), constexpr use, embedded-NUL and unsigned-high-byte
  checks, plus token stability/distinctness and the high-bit-clear mask; no
  compiler-specific token value is frozen and no graph-token inequality is
  asserted. `Test.CoreFrameGraphTypeTokenHelper.cpp` now imports
  `Extrinsic.Core.Hash`, so the retained cross-TU identity case in
  `Test.CoreFrameGraph.cpp` still crosses a FrameGraph-importing TU and a
  Hash-importing one.
- New CTest guards over the real `compile_hotspots.py` metadata:
  `KernelCompilationLocality.Commands`, `.Events`, `.Services` reject
  `Extrinsic.Core.FrameGraph`; `.Jobs` also rejects `Extrinsic.RHI.CommandContext`,
  `.Descriptors`, `.Types` and `.Handles`. Existing guards are unchanged.
- Docs: the canonical rule is in `docs/architecture/runtime.md` for
  `runtime.kernel-interface-locality`; `src/core/README.md`, `src/runtime/README.md`
  (JobService row), `src/graphics/rhi/README.md`, `tests/README.md`,
  `docs/architecture/patterns.md` and the reuse skill's `owner-routes.md` record
  the actual owners.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -R 'CoreHash|CoreFrameGraph|CoreTaskGraph|RuntimeCommandBus|RuntimeKernelEvents|RuntimeServiceRegistry|RuntimeJobService|KernelCompilationLocality|CompilationLocality' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
Reconcile `ci-asan` and `ci-ubsan` using the exact grouped serial CPU gates in
AGENTS.md. Verify actual Vulkan command dispatch, rendering/readback and queued
spatial work through existing smoke fixtures; keep their original deadlines
and leak settings. This changes declaration ownership, not numerical methods
or GPU continuations, so selection follows the affected interfaces. Preserve
BUG-188's host discovery workaround and BUG-180's separate leak evidence.
Run the strict clean-workshop, source-documentation, task/layout/root and skill
checks; refresh the module inventory and session brief. All builds finish
before timed tests. Dependency counts are not timing
benchmarks or a whole-engine performance result.

## Final review and verification — 2026-09-13
The evidence directory above retains the exact dirty baseline, fixed Claude
review source/diff, review correction and disposition, final source hashes,
compiler metadata, compiled token probes and complete gate logs. Earlier
RUNTIME-238/239 archives are unchanged. Those runs used dirty source and remain non-claim-eligible timing evidence
after the later local integration.

Claude found no blocking defect. Its valid comment finding was corrected:
with RTTI disabled, the linkage rationale concerns the vtable, not typeinfo.
Moving `TypeSig` into another namespace would change the preserved signature;
renaming the existing cross-TU test helper would add churn without another
identity boundary. Those suggestions were declined with reasons recorded.
Correct preset reconciliation, cache-disabled builds and actual dispatch tests
provide current-source evidence; no stale-object failure required a clobber.
The initial full CPU pass preceded the comment correction; a final native
rebuild and full CPU pass follow it, and all sanitizer/Vulkan runs use the final
source. Root also replaced a duplicate hash assertion with a fixed NUL vector.
The combined execution session ended with status 143 after both sanitizer
builds passed, while starting Vulkan compilation. No compiler error or orphan
build remained; that partial log is retained and the remaining gates resumed
as separate commands. This was an execution interruption, not a test verdict.

The before/after Clang23 probe returns the same two type-token values. Source
comparison preserves both signature sources/masks, all kernel namespace bodies,
RHI class layouts/virtual order/default bodies and 32-bit string hashing.
The runtime behavior checks and actual compiler graph guard those boundaries.

| Gate | Result |
| --- | --- |
| ci configure, IntrinsicTests and ExtrinsicSandbox; final rebuild | pass |
| Focused hash/kernel and dependency contracts | 164 passed; no skips |
| Full native CPU | 4,587 passed; 1 ASan-only case skipped |
| Full ASan CPU, grouped serial | 2,966 passed; no skips |
| Full UBSan CPU, grouped serial | 2,965 passed; 1 ASan-only case skipped |
| Affected actual Vulkan command/queue/rendering paths | 15 passed; no skips |
| Kernel-policy / compiler-analysis Python regressions | 24 / 22 passed |
| Strict layering, task policy, links, layout, root hygiene, skill mirrors | pass |
| Source documentation | zero objective errors; 76 advisory findings retained |

Actual transitive compiler-module dependencies:

| Interface | Before | After |
| --- | --- | --- |
| `JobService` | 22 | 11 |
| `WorldRegistry` | 25 | 14 |
| `Engine` | 59 | 52 |
| `KernelEvents` | 16 | 1 |
| `CommandBus` | 26 | 15 |
| `ServiceRegistry` | 16 | 2 |

Nine existing production files change, with 47 more physical lines and 44 more
nonblank lines, including synopses, includes and required linkage declarations.
No production file is added/deleted. This is a dependency/locality improvement,
not a source-size reduction or a measured compile-time improvement.

Clean-workshop rows 1–3 and 8 pass; 4–7 are not applicable (no renderer growth,
new pass/recipe edge or maturity closure). No target-link edge, layer exception,
compatibility wrapper, algorithm or backend choice is added. Existing Vulkan
test deadlines and leak settings remain; BUG-188's host discovery workaround
and BUG-180's separate leak-enabled evidence remain open.

The selected scope is complete. Remaining work includes integration of the
accumulated changes, matched compile-timing reconciliation and Framework24
product acceptance. Accumulated Ninja logs point to editor workspace
attachment/snapshots and visualization editing for the next locality review;
these are diagnostic candidates, not yet established savings or approved
architectural replacements. Claude's follow-on assessment proposed pruning
seven unused Snapshots imports. Root's retained-owner metadata calculation
still reaches all 135 dependencies after that prune (15 direct imports become
8), so this is deferred as a standalone compilation slice. The next review
must identify a real model/operation boundary before another broad rebuild;
see `next-plan-disposition.md` and `next-plan-dependency-check.json`.
