---
id: RUNTIME-249
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive structural cleanup verified by source, compiler metadata and existing import tests; no timing claim.
contract_schema: 1
contracts: [repo.source-documentation]
---
# RUNTIME-249 — Model materialization implementation locality

## Goal
Continue the user-directed duplicate-code and compilation cleanup with Claude.
Keep lease-owning model state and implementation-only helpers out of the module
interface, simplify stored records, and preserve import behavior.

## Plan and reuse
Source search finds no callers outside the matching implementation for
`AssetWorkflowModelMaterializationState`, `BuildEmbeddedTextureAssetPath`,
`LoadEmbeddedTextureAsset` or `MaterializeModelSceneAsset`. Put them in that
file's anonymous namespace. Store state directly instead of the one-field
`RuntimeModelSceneRecord`; preserve entity destruction before lease release.
The existing materializer pimpl and event subscription remain the lifetime owner.

Drop imports used only by those private details. The renderer's existing
globally attached `extern "C++"` declaration permits a matching pointer/reference
forward declaration; retain imports for types attached to named modules.
Compare duplicated model/texture error predicates before choosing their owner;
do not add a generic error framework or another helper file. New external
callers would justify a deliberate shared contract; there are none today.

Read-only Claude reviews use fixed source packets; one writer owns this checkout.
Baseline is `bca143297`. Historical Ninja times identify candidates but do not
establish speedup. BUILD-007 retains matched compile timing.

## Acceptance criteria
- [x] Private state/helpers no longer enlarge the materializer interface.
- [x] Redundant state wrapper removed; lifetime and material publication preserved.
- [x] Claude findings resolved and relevant CPU, sanitizer and Vulkan checks pass.
- [x] Record full production footprint and compiler dependencies; update docs and retire.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^(AssetWorkflowModule|AssetFormatCapabilities|RuntimeEnginePrivateGlue|RuntimeAsset)' --no-tests=error --timeout 60
tools/ci/run_clean_workshop_review.sh . --strict
```
Repeat the focused selector plus `AssetCompilationLocality` in existing
`ci-asan` and `ci-ubsan` preset trees, building `IntrinsicRuntimeContractTests`,
and run actual Vulkan imported-scene checks in
`IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`. Retain existing
BUG-178 cache, BUG-188 discovery and BUG-180 leak exclusions; no new build trees
given BUG-195 disk headroom. Local logs: `/tmp/intrinsic-model-locality-20260914/`.

## Implementation and review
Four production files remain; their total shrinks from 2,786 to 2,748 lines.
The model-interface compiler map drops from 96 to 54 module dependencies, while
its implementation and consumers retain their required renderer imports. This
is structural evidence, not a measured compile-time improvement. The new
`AssetCompilationLocality.ModelMaterialization` test reuses the compiler-metadata
checker; a deliberate negative control rejected a real required dependency.

Claude approved the production diff after checking namespace lookup, module
attachment, error-policy equivalence and lease-release ordering. Retained
`ModelTexturePayload` for its live invalid-index constant despite the suggested
removal. Kept shared predicates in the already-private texture owner, not Core;
failure counters remain payload-specific. The caller already records embedded
texture failures against the model asset, so no diagnostic behavior change was
needed. Moved helper bodies match the baseline modulo the unused argument.

Claude's test review caught replacement coverage displacing original-import
readbacks. Both now share one fixture helper through separate tests. A proposed
resident-generation warmup did not compile because frame stepping is private;
the unsupported call was removed. The new case replaces twice before rendering
and checks retired entity handles, final material/geometry output and picking;
it does not claim in-flight GPU retirement coverage. The accidental run against
the older binary is marked `premature-vulkan-old-binary` and is not final evidence.

Claude approved the final limited tests. The complete original assertion and
readback block matches the baseline verbatim. Earlier generations in the new
case retire before any frame, so retirement of rendered generations is outside
its evidence. The final wording states this explicitly.

## Verification and retirement
Completed 2026-09-14. PR/commit: enclosing retirement commit on baseline
`bca143297`. Endpoint: verified structural cleanup; no new backend or capability
promotion. BUILD-007 retains matched engine-source compile measurements.

- Final `IntrinsicTests` build and focused native selector: pass, 73/73.
- Full CPU selector: 4,624 selected, zero failures, six skips. Five native-window
  checks pass with host display access; only the expected unsanitized leak-control
  skip remains. Total distinct passing CPU cases: 4,623.
- Matching isolated ASan and UBSan focused selectors: 73/73 each, serial CTest.
- Corrected Vulkan build and selector
  `^RuntimeSandboxAcceptanceGpuSmoke.(Imported|DirectMeshEnrichmentPending)`:
  6/6 pass with `ASAN_OPTIONS=detect_leaks=0`, timeout 120, existing BUG-180 limit.
- Developer `ExtrinsicSandbox` rebuilt. Strict clean-workshop, source documentation,
  layering, task policy, links, test layout, root hygiene and skill checks pass.
  Module inventory refreshed; its 418-module count is unchanged.

Final sweep: one asset-materialization intent; no dependency-policy changes;
verified lookup/lifetime behavior; architecture and reuse routes synchronized.
Clean-workshop rows 1–3 pass (allowed imports/links and no downward exposure),
4–7 are not applicable (no renderer/pass/recipe changes or capability promotion),
8 passes with no temporary exceptions. No new ownership facade or compatibility path.
