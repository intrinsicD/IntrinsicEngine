---
id: RUNTIME-250
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive structural cleanup; existing behavior tests and compiler metadata verify the change without a timing claim.
contract_schema: 1
contracts: [repo.source-documentation]
---
# RUNTIME-250 — Direct render-extraction accessors

## Goal
Continue the user-directed cleanup with Claude. Remove duplicate public/private
accessor layers and unnecessary interface imports while retaining the cache's
existing private state and independently compiled rendering algorithms.

## Plan and reuse
Thirteen `RenderExtractionCache` methods only forward to matching `State`
methods, each with one caller. Put their bodies directly on the public class in
their current implementation units; delete duplicate declarations and forwarding
bodies. Preserve const reads, zero-id handling, missing lookups, recipe revision
rules and world-tagged snapshot ownership. Shared extraction, retirement and
shutdown algorithms remain on State.

Keep the complete private visualization record by value in State instead of
allocating it separately. No observer borrows that record. Drop Renderer and
MaterialSystem imports from the primary interface; retain GpuWorld because it
owns the exposed handle types. Use the renderer's existing globally attached
forward declaration, with complete imports in implementation units.

Baseline `ac932ab6a`; no new source files, service or ownership boundary. A real
second internal caller could justify a private shared accessor again. Historical
Ninja timings identify candidates only; BUILD-007 owns matched timing evidence.

Automatic approval review rejected a full-source Claude packet. It was not sent.
A source-free design question is the approved alternative for initial review;
record the final review scope truthfully.

## Implementation and review
Removed thirteen private method declarations and their public forwarding bodies;
the existing implementations now directly serve the public methods. A local
comparison confirms all thirteen bodies match baseline after normalizing only
qualification and state-member access. Const methods explicitly bind `const
State&`; public signatures and failure/revision rules are unchanged.

Claude reviewed two source-free design summaries, not the source diff. Local
source review resolved its constness, lookup, lifetime and module-attachment
questions. State already deletes copying and cannot implicitly move. The
visualization record was initialized in the constructor's member initializer
list, not its body; default construction owns empty vectors and borrows no later
member. Its definition was already complete in the private partition. There
are no pointer resets, null states or independent borrows of that record. Batch
spans continue to reference its owned vector storage. Complete renderer imports
remain at callers requiring member access; the interface only borrows references.
No new propagation wrapper, test seam or public API was needed.

The compiler-boundary regression reuses `compile_hotspots.py`; a negative control
for the required GpuWorld module fails as expected. No timing result is inferred
from the dependency reduction. Architecture docs and the reuse route explain the
direct accessor owner so future changes need not recreate the forwarding layer.

The complete five-file production footprint shrinks from 5,239 to 5,124 lines;
no production files were added. The primary interface's compiler map shrinks
from 85 module dependencies to 49, with no added dependency. Its private
implementation retains the complete owners it needs. These are structural
counts, not elapsed-time evidence.

## Acceptance criteria
- [x] Remove duplicate accessor declarations/bodies and the extra allocation.
- [x] Preserve rendering, recipe, material and snapshot behavior.
- [x] Complete review, relevant CPU/sanitizer/Vulkan checks and fixes.
- [x] Record complete footprint/dependency comparison; synchronize docs and retire.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
tools/ci/run_clean_workshop_review.sh . --strict
```
Use existing Clang 23 preset trees, focused rendering/recipe/interaction tests
in isolated ASan and UBSan, and relevant actual Vulkan extraction/selection
checks. Preserve BUG-178 cache, BUG-188 discovery and BUG-180 leak limitations;
no new build tree given BUG-195 disk headroom. Local review/build output lives
under `/tmp/intrinsic-render-locality-20260914/`.

## Verification and retirement
Completed 2026-09-14. PR/commit: enclosing retirement commit on baseline
`ac932ab6a`. Endpoint: verified structural cleanup; no new backend or capability
promotion. BUILD-007 retains matched full/incremental compile-time measurements.

- Canonical `ci` configured with Clang 23; final `IntrinsicTests` build passed.
- Full CPU selector: 4,625 selected, zero failures, six sandbox skips. The five
  window-dependent checks passed in the host-access focused run; only the
  expected unsanitized leak-control skip remains. Distinct passing CPU cases:
  4,624.
- Focused rendering, extraction, recipes, interaction and geometry checks:
  113/113 each on native, isolated ASan and isolated UBSan, serial CTest. Includes
  the new compiler-boundary test. Both sanitizer producers rebuilt successfully.
- Actual Vulkan geometry presentation, saliency/mask recovery, scalar/isolines,
  picking, imported-model replacement and selection outline: 7/7 passed after
  rebuilding `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`. Retains
  `ASAN_OPTIONS=detect_leaks=0` (existing BUG-180); no leak-cleanliness claim.
- Developer `ExtrinsicSandbox` rebuilt. Strict clean-workshop, task policy,
  layering, links, test layout, root hygiene and skill checks passed. Source
  documentation audit has zero objective errors; seven pre-existing declaration
  comment advisories remain outside this cleanup. Inventory refreshed, unchanged
  at 418 modules.

Final sweep: one render-extraction intent, no new layer or compatibility
exception; constness, construction, lifetime and missing-result semantics
reviewed; architecture and discovery route synchronized. Clean-workshop rows
1–3 pass (allowed imports/links, no downward type exposure); 4–7 are not applicable
(no new renderer/pass/recipe behavior or maturity promotion); 8 passes with no
new temporary exceptions. Claude collaboration stayed within the approved
source-free summaries after automatic approval rejected the full-source packet.
