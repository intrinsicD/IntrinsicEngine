---
id: RUNTIME-238
theme: J
depends_on: [RUNTIME-237]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive refactor; exact dirty baseline, fixed reviews and correctness/dependency checks provide evidence without a new performance claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.render-diagnostics-locality, runtime.engine-compilation-locality]
---
# RUNTIME-238 — Consolidate renderer queries and isolate Engine compilation

## Goal
Replace repeated renderer query methods with one canonical typed mechanism and
remove implementation dependencies from Engine's public interface. Preserve
rendering features, configuration, diagnostics and resource lifetimes.

## Scope and owner plan
The operator requests continued simplification with Claude, including planning,
implementation, review, tests and fixes. Preserve the verified uncommitted
RUNTIME-237 baseline. No compatibility bridge or commit/push is requested.
Exact baseline: `/tmp/intrinsic-runtime238-20260913/before/` and `before.json`.

Discovery found 48 pipeline getters in `Graphics.Renderer`: 24 handle/descriptor
pairs with the same optional lease/manager checks. Their only external caller
is `Test.RendererFrameLifecycle.cpp`; three renderer hot paths use handle-only
queries. No existing pipeline selector was found in graphics/renderer or RHI.
Existing descriptor builders and pipeline leases remain the canonical owners.

- Reuse those builders and leases through one typed selector and separate
  handle/descriptor queries. Descriptor construction must stay off handle-only
  hot paths. Remove the 48 old methods and update every caller directly.
  Keep failure explicit for unknown identifiers and missing/unready leases.
- Engine already has Pimpl. Its interface imports renderer execution and the
  full runtime module composition for types it can obtain through narrow
  declarations. Reuse `Runtime.ModuleLifecycle`, `Runtime.RenderRecipeActivation`
  and the established `extern "C++"` declaration/definition pattern for the
  borrowed `IRenderer` type. Keep the class definition in graphics and full
  imports at actual implementation/caller sites. Add no forward-header file.
- Check the exact repeated recipe-slot lookup in config, frame projection and
  recipe editing. If consolidated, use the existing config owner and preserve
  const/mutable borrowing, first-match ordering and missing-slot behavior.

The existing renderer lifecycle, RHI/window interfaces, resource managers,
leases and pass implementations carry correctness and remain. This slice
changes query shape and compilation reachability, not algorithms, descriptor
contents, config formats, backend choice or ownership. A distinct pipeline
query is warranted again only if it has different semantics the typed selector
cannot express. No registry, factory, wrapper or generalized framework is added.
SpatialIndexCache's GPU-facing records remain a separate next candidate.

## Reviewed plan
Claude confirmed there is no existing pipeline identifier and recommended
`RendererPipelineId`, `GetPipeline(id)` and an optional `GetPipelineDesc(id)`.
An empty `RHI::PipelineDesc` is not an explicit invalid state, so unknown IDs
return `nullopt`; unknown/unready handles remain invalid. Root keeps lease
selection and its shared validity check in the handle function itself, avoiding
an extra forwarding helper. The three hot callers use that allocation-free
path. All 24 lease fields and exact builder expressions are captured in
`pipeline-contract-map.json` for review.

Engine also names `FramePhase` and `EditorInputCaptureSnapshot` in a private
helper signature. Their existing definitions stay in Runtime.Module with the
same shared-declaration pattern; Engine needs only forward declarations.
No public record fields, enum values or lifecycle behavior change.

Recipe-slot consolidation is deferred from this slice. Its lookup traverses
a container, so it must remain compiled rather than adopting the plan's inline
proposal in the declaration-only RenderingContract module. RUNTIME-239 owns the compiled helper consolidation in the existing
RenderingContract owner, together with the spatial-cache boundary follow-up.

## Acceptance criteria
- [x] Capture exact baseline and canonical owners/consumers; reproduce the old Engine dependency edges.
- [x] Reconcile the bounded plan with Claude and implement the smaller API and Engine boundary.
- [x] Preserve every pipeline mapping and test unknown/unready/rebuild/shutdown states through the canonical API.
- [x] Enforce Engine's compiler-metadata boundary and update actual callers.
- [x] Review the fixed diff with Claude, fix valid findings and pass final CPU/sanitizer/affected Vulkan checks.
- [x] Update architecture, discovery routes, inventory and exact full production footprint.

## Implementation notes
`RendererPipelineId` (24 variants plus a `Count` bound) lives in
`Graphics.Renderer.cppm` beside `IRenderer`; the 48 getters are replaced by
`GetPipeline(id)` and `GetPipelineDesc(id)`. In `Graphics.Renderer.cpp` the
handle query holds the lease-selection switch and the one shared
manager/lease/validity guard inline, and the three HZB/cluster call sites use it
directly. The descriptor query is a separate exhaustive switch over the existing
builder expressions. Lease fields, builders, descriptor arguments and
init/rebuild/shutdown ordering are unchanged.

`IRenderer` is now declared inside `export extern "C++"`; `FramePhase` and
`EditorInputCaptureSnapshot` got the same treatment in `Runtime.Module.cppm`.
`Runtime.Engine.cppm` drops `Graphics.Renderer` and `Runtime.Module`, adds
`Runtime.ModuleLifecycle` and `Runtime.RenderRecipeActivation` (still 12 plain
imports), and carries matching non-exported forward declarations plus a direct
`<cstdint>` include. `Test.GizmoInteractionEngineWiring.cpp`,
`Test.ProceduralGeometryExtraction.cpp` and
`Test.PointCloudConsolidationGpuParity.cpp` needed explicit renderer imports;
`Test.RuntimeReferenceScene.cpp` already has one through
`EditorFeatureTestContext.hpp`.

Extra scope this required: `tools/repo/check_kernel_convergence.py` validated
that every allowed getter's `owning_import` is an exact plain import, which
`GetRenderer` can no longer satisfy. The policy schema gained
`current_snapshot.borrowed_imports` — declared owning modules the interface must
*not* import — with matching validation, a fail-closed check that a borrowed
owner is never imported again, and regression cases in
`Test.CheckKernelConvergence.py`. No compatibility layer was added.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RenderCompilationLocality|RuntimeEngine|RenderRecipe|FrameRecipe' --no-tests=error --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tests/regression/tooling/Test.CheckKernelConvergence.py
bash tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/sync_skills.py --check
```
Separate `ci-asan`/`ci-ubsan` full CPU gates and actual `ci-vulkan` rendering
checks pass. BUG-188's host discovery workaround remains; registered Vulkan
tests do not close BUG-180's separate leak-enabled evidence. No timing benchmark
or speedup claim was made. Changes stay uncommitted pending integration.


## Final review and verification — 2026-09-13
Evidence: `build/analysis/runtime238-renderer-query-api-2026-09-13/` contains the
exact dirty baseline, fixed Claude review, correction diff/disposition,
compiler metadata, isolated linkage probe, source hashes and complete logs.

Claude found no correctness defects. Root's early compiler probe identified
that IRenderer's two out-of-line default methods also need matching extern-C++
attachment; both were fixed before the repository build and fixed review.
The 24 lease/descriptor expressions match the baseline exactly. After excluding
the two query replacements, three hot call sites and two linkage prefixes,
the remaining renderer implementation tokens match. Engine's class body and
the two shared runtime declaration bodies also match, including field defaults.

Two wording nits were corrected. The optional suggestion to duplicate the
borrowed-import list's length in another policy count was declined: the unique
validated list is already authoritative. The only post-review C++ change is an
illustrative comment; its tokens are identical to the native CPU build source.
ASan, UBSan and Vulkan built that final comment source. Both source identities
and the comparison are retained; all 1,362 final verification hashes match.

| Gate | Result |
| --- | --- |
| ci configure, IntrinsicTests and ExtrinsicSandbox (Clang23) | pass |
| Focused contracts and dependency checks | 296 passed, including 21 compiler-metadata checks |
| Full native CPU on host | 4,575 passed; one ASan-only case skipped |
| Full ASan CPU, grouped registration, serial | 2,954 passed |
| Full UBSan CPU, grouped registration, serial | 2,953 passed; one ASan-only case skipped |
| Affected Vulkan frame, overlay, HZB and bootstrap checks | 18 passed; no skips |
| Kernel-policy / compiler-analysis Python regressions | 24 / 22 passed |
| Strict layering, task policy, links, layout, root hygiene and skill mirrors | pass |
| Source documentation | zero objective errors; 32 advisory findings retained |

The initial sandbox CPU run skipped five GUI cases because it could not access
the display. The subsequent full host run executed those cases successfully.
All builds completed before the sanitizer/Vulkan timed gates. Only regenerable
objects, archives and module files from the inactive ignored ci-clang20 tree
were reclaimed for disk space; logs, metadata, binaries and evidence remain.

The complete engine C++ footprint is four changed production files, no new or
deleted source files, -376 physical lines and -319 nonblank lines. Renderer.cppm
shrinks from 626 to 416 lines. Its 48 pipeline virtual getters become two typed
queries, with all 24 lease owners retained. Engine's actual compiler-metadata
closure drops from 98 to 59 modules and reaches neither Renderer nor Module.
These are source-structure observations, not compiler timings.

Clean-workshop rows 1–4, 6 and 8 pass; 5 and 7 are not applicable (no new pass or
scaffold/parity closure). No layer-policy exception or renderer resource owner
was introduced. RUNTIME-239 owns the remaining recipe lookup and spatial-cache
items; this task does not claim the whole engine migration complete.
