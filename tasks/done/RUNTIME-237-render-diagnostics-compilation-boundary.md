---
id: RUNTIME-237
theme: J
depends_on: [RUNTIME-236]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive structural refactor; fixed source diff, review and verification provide evidence without a new timing claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.render-diagnostics-locality]
---
# RUNTIME-237 — Separate render diagnostics from runtime module compilation

## Completion — 2026-09-14
Completed locally and retired after acceptance/evidence review. Accumulated
implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
Historical dirty-source measurements retain their original eligibility limits;
this retirement is not a publication or whole-engine completion verdict.
BUILD-007 owns matched engine compile measurements; C92 remains a hypothesis.


## Goal
Remove renderer execution dependencies from the runtime's frame-pacing and
recipe-activation data contracts. Preserve every renderer feature, config
round trip, diagnostic field and runtime lifecycle behavior.

## Scope and owner decisions
The operator requests continued simplification with Claude, including plan,
implementation, review, tests and repair. Preserve the verified uncommitted
RUNTIME-233–236 baseline; no compatibility bridge is added; local integration is now recorded above.
Exact source snapshot and hashes: `/tmp/intrinsic-runtime237-20260913/before/`
and `before.json`.

Claude identified `Runtime.FramePacingDiagnostics` and
`Runtime.RenderRecipeActivation` importing `Graphics.Renderer` solely for
records, causing `Runtime.Module` to inherit renderer execution dependencies.
Engine's Pimpl does not remove those public module edges. Root narrowed the
proposal: importing upload helpers into a new stats module would retain device
managers and render systems, and recipe overrides already have a domain owner.

- One new declaration-only `Graphics.RenderDiagnostics` owns render-frame and
  upload diagnostic records. Upload implementations consume those records;
  diagnostics do not import implementation helpers or render systems.
- Reuse `Graphics.VisualizationPackets` for its existing property-buffer
  diagnostic record; do not move or copy it unnecessarily.
- `Graphics.RenderRecipeConfig` owns the plain override and diagnostic records;
  `Graphics.FrameRecipe` owns projection onto its `FrameRecipeFeatures` and the
  existing projection implementation. Move exclusive helpers with that body.
- Renderer imports the canonical owners; all callers use those same entities.
  No duplicate struct, wrapper, service, registry or per-pass module is added.
- Frame-pacing and recipe-activation interfaces use the new appropriate owners.
  The recipe editor also reads these same diagnostic records directly.
  Compiler metadata must reject `Graphics.Renderer` and concrete rendering
  systems in their transitive dependencies, including `Runtime.Module` and
  `Runtime.RenderRecipeEditingOperations`.

The new file is justified by distinct existing readers of copied diagnostics
and executing renderer components. Concrete rendering types are needed again
only by callers that execute rendering or access renderer-owned subsystems.
IRenderer's large inspection surface, Engine's direct renderer import and the
SpatialIndexCache GPU-record API remain separately bounded follow-up candidates.
No renderer algorithm, resource lifetime, shader, pass order or field is removed.

## Acceptance criteria
- [x] Establish exact baseline and record Claude/root owner review.
- [x] Migrate records and recipe projection to their canonical owners; remove duplicate old declarations/bodies.
- [x] Verify real compiler dependencies and preserve config/diagnostic/lifecycle behavior.
- [x] Review fixed source with Claude, repair valid findings and pass affected tests.
- [x] Synchronize architecture, reuse routes, inventory and exact full production footprint.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'CompilationLocality|FramePacing|RenderRecipeActivation|RendererFrameLifecycle|RenderExtractionContract' --no-tests=error --timeout 60
bash tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/sync_skills.py --check
```
Reconcile separate sanitizer CPU gates and the affected actual Vulkan frame,
recipe and readback tests after interface changes. Timed tests run after all
compilation; preserve BUG-188's host sanitizer workaround and BUG-180's
distinction between registered GPU tests and leak-freedom evidence. BUG-177's
exact optional metadata classification is repaired and tested in this batch.
No new wall-time benchmark is required for this declaration-ownership refactor;
compiler-metadata boundaries, exact source bodies and correctness are the gates.

## Review and measured dependency boundary

Claude planned and implemented the owner moves under one-writer coordination,
then reviewed a fixed snapshot independently. Root additionally migrated the
recipe editor's record dependency and removed unused renderer imports. The
23 moved record/function brace bodies match after excluding comments and
whitespace, with string contents retained. This comparison includes six
exclusive helpers and the projection function; it does not replace import,
signature or behavior review.

Clang/CMake module metadata records these transitive dependency counts:

| Interface | Before | After |
| --- | ---: | ---: |
| `Runtime.FramePacingDiagnostics` | 67 | 22 |
| `Runtime.RenderRecipeActivation` | 71 | 7 |
| `Runtime.Module` | 97 | 55 |
| `Runtime.RenderRecipeEditingOperations` | 105 | 62 |

These are source-graph observations, not compiler timings or an estimated
speedup. The negative baseline check reached `Graphics.Renderer`; the new
five-producer boundary test passes. The shared diagnostics owner intentionally
adds schema dependencies to the two existing pass interfaces that consume
its upload counters; it keeps one owner instead of adding per-counter files.

Review corrections expose the override types through `FrameRecipe` itself and
exercise empty-recipe rejection in the existing recipe contract producer,
which imports neither Renderer nor RenderRecipeConfig. Root-hygiene fixtures
now isolate each case and assert its actual classification section. The
touched upload helper's missing opening synopsis is fixed. No algorithm,
renderer virtual method, default, pipeline or resource lifetime changed.

The clean-workshop scorecard passes rows 1–4, 6 and 8; rows 5 and 7 do not
apply (no new pass or scaffold/parity closure). Strict layering, target-edge,
task-policy and documentation-link checks pass. The layering allowlist stays
empty. Final verification is recorded below.


## Final verification — 2026-09-13

Evidence archive: `build/analysis/runtime237-render-diagnostics-2026-09-13/`.
It contains the exact dirty baseline, fixed Claude review snapshot, correction
diff and follow-up review, before/after compiler metadata, source hashes,
verification logs and full scoped production footprint.

| Gate | Result |
| --- | --- |
| `ci` configure, `IntrinsicTests`, `ExtrinsicSandbox` | pass, Clang 23 |
| Focused contracts and compilation boundaries | 209 passed, including 20 compiler-metadata checks |
| Full CPU | 4,571 passed; one ASan-only case skipped |
| Full ASan CPU, grouped registration, serial execution | 2,950 passed |
| Full UBSan CPU, grouped registration, serial execution | 2,949 passed; one ASan-only case skipped |
| Affected actual Vulkan frame/overlay/readback cases | 17 passed, no skips |
| Root-hygiene regression and actual strict gate | 13 tests passed; strict gate passed |
| Strict layering, task policy, links, test layout, skill mirrors | pass |
| Source-documentation audit | zero objective errors; 65 advisory review findings retained |

The final production footprint is 16 touched files, +18 physical lines,
+14 nonblank lines and one additional file. The renderer interface shrinks
from 955 to 626 lines; the moved records remain present in their shared owner.
This is dependency isolation, not a net source-size reduction or a new
compile-time performance claim. No compatibility wrapper or new service was
introduced. Compiler-metadata counts stayed unchanged after the review fixes.
All 1,362 final source/test/build/tool hashes match the verification snapshot.

The missing source synopsis found during the first documentation check was
repaired; the original failing audit and passing rerun are retained. Both
root-checker entrypoints pass Claude's tightened classification assertions.
BUG-177 is resolved and locally integrated. BUG-188's host discovery workaround
and BUG-180's separate leak-enabled GPU evidence remain applicable; registered
Vulkan success here does not establish whole-process leak freedom.

An inactive ignored build tree's regenerable `.o`, `.pcm` and `.a` files were
reclaimed to make room for verification after confirming no active compiler
used that tree. Logs, metadata, binaries, source and evidence were preserved;
the exact cleanup manifest is in the archive.

This slice is implemented, reviewed, verified and locally integrated. Engine's direct renderer
exposure, the large renderer inspection API and SpatialIndexCache's GPU-facing
record dependencies remain candidates for the next scoped iteration.
