---
id: GRAPHICS-138
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive measured refactor; reviewed diff and CPU/backend tests appropriate to the selected change.
contract_schema: 1
contracts: [repo.source-documentation, runtime.render-diagnostics-locality]
---
# GRAPHICS-138 — Reduce renderer interface compilation cost

## Goal
- Narrow the remaining expensive renderer interface/implementation dependency
  surface while preserving the same rendering capabilities and ownership.

## Context and decision boundaries
- BUILD-007's 2026-09-15 report identifies `Graphics.Renderer.cppm` and
  `Graphics.Renderer.cpp` as expensive producers. Reproduce the current cost
  before selecting a change; parallel-build durations alone do not prescribe Pimpl.
- Inventory actual public types, private state, includes/imports and consumers.
  Compare unused-import removal and narrowing existing owners before adding
  private implementation storage or another file. A split must remove dependency
  work, not duplicate parsing or introduce forwarding-only wrappers.
- Preserve RHI/backend separation, recipe composition, device capability gates,
  resource lifetime and diagnostics locality. This is not GRAPHICS-135's runtime
  scheduling experiment, GRAPHICS-105's material-authority refactor, or the
  trigger-gated GRAPHICS-137/136 backend/rename work.

## Acceptance criteria
- [x] Freeze the current compiler graph/probe baseline and select one bounded
      interface/private-state change with actual consumer justification.
- [x] Preserve rendering behavior and public diagnostics; no compatibility
      wrappers, new backend, or new frame/lifetime policy.
- [x] Keep one implementation per mechanism and document file/line changes,
      including any new private header or implementation unit.
- [x] Compiler-boundary checks, affected CPU tests and any GPU/lifetime tests
      required by the chosen change pass; generated inventory/docs are current.
- [x] Reuse BUILD-007 tooling for matched timing evidence before a performance
      claim; reject the change if complexity or compilation cost increases.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'Render|Graphics|CompilationLocality' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Read-only preparation (2026-09-15)
- Claude identified four forwarding accessors with no callers beyond declaration
  and override: `GetForwardSystem`, `GetDeferredSystem`, `GetTransformSyncSystem`
  and `GetLightSystem`. Confirmed with repository-wide source/test/tool/doc search.
- `Graphics.Renderer.cppm` imports ForwardSystem and DeferredSystem only for those
  accessor types. The implementation already directly imports both owners.
- The compiler graph also reaches both through the renderer's re-export of
  `Graphics.RenderSubsystemRegistry`. No declaration in the renderer interface
  names a registry type. Audit consumers of that unused re-export and replace any
  reliance with direct owner imports; merely removing the two direct system imports
  leaves the transitive path intact.
- Light and transform imports remain necessary for snapshot value records. Keep
  getters used by runtime workflows and explicit tests. No implementation or
  timing result is claimed by this preparation; the acceptance criteria remain open.

## Selected bounded plan
- Operator explicitly requested continued compilation/reuse work with Claude.
  Baseline `7ec967379`; one writer, Claude read-only source review.
- Remove four unused IRenderer accessors and their only concrete forwarding
  overrides. Exact token search across source/tests/tools/docs finds four
  declarations and four NullRenderer definitions, with no caller or other
  implementation. Reintroduction requires a concrete caller and a reviewed need
  for public subsystem access; existing registry ownership remains load-bearing.
- Remove six unused interface imports: ForwardSystem, DeferredSystem,
  RenderSubsystemRegistry, RenderPrepPipeline, ImGuiUploadHelper and
  Pass.VisualizationOverlay. No interface declaration names the last four
  owners' types. Concrete consumers already import those owners directly;
  Light/Transform imports remain for snapshot value records.
- Keep RenderingContract and RenderCommandRouter visibility used by diagnostic
  consumers. No Pimpl, new file, wrapper, service or implementation. Runtime
  configuration, recipe composition, diagnostics and GPU behavior are unchanged.
- Captured compiler metadata before editing: renderer closure 66 modules; the
  proposed guard rejects all six owners on this baseline (expected exit 2).
  Rebuild all in-tree users after changing virtual slots; no ABI compatibility.
- Reuse the corrected BUILD-007 runner for a bounded ExtrinsicGraphics target
  comparison: clean/no-op/ForwardSystem interface touch, two ABBA samples per
  exact source revision, cache off and immutable preinstalled dependencies.
  Timings describe this graphics-library target, not the full engine or
  Sandbox/test build. Preserve negative results and avoid general speed claims.

## Registry forwarding consolidation
- The existing registry has 34 getter definitions (mutable and const) over 17
  optional fields. Every body only returns its field; mutable references already
  permit reset/emplace/assignment, so these wrappers enforce no mutation guard.
- Expose the same storage under existing accessor names and delete duplicate
  declarations/bodies. Preserve exact field types/order; keep private lifecycle
  metadata and all initialization/reset/shutdown/diagnostic algorithms. Update
  field access in Renderer.cpp and RenderPrepPipeline tests. Registry contract
  tests use lifecycle APIs and already need no getter rewrite.
- Claude accepted this extension. RHI manager types stay qualified to avoid field
  name lookup conflicts; private metadata keeps the class non-aggregate. Class
  symbol and member-reference searches find no accessor address/callable use.
  A future accessor needs a concrete invariant or computed result.
- All 319 renderer calls and 16 prep-test calls become direct field access. The
  registry's own imports remain necessary for owned values. Getter removal is
  code consolidation, not a claim that its module dependency closure shrinks.

## Source review and verification
- Claude approved both slices after correction of an initial duplicate-implementer
  inference; exact token search and the single NullRenderer declaration settle it.
  Existing mutable getters and new fields both permit mutation; no newly enforced
  access guard is claimed. Current clients do not replace/reset/emplace storage.
- The registry lifecycle token sequence is identical after removing trivial
  getters and substituting field names. Renderer/prep-test bytes match the exact
  call-to-field rewrite. Types/order of all 17 owned optionals and private
  metadata are preserved; qualification prevents RHI type-name hiding.
- Compiler-derived renderer closure: 66 → 59, seven modules removed and none
  added. Six direct roots leave; VisualizationOverlayUploadHelper also leaves
  through the removed pass import. The new guard passes on current metadata and
  rejected all six forbidden roots on the original metadata.
- Four production files: 12,085 → 11,863 lines, net −222. No new production file,
  module, service, allocation or implementation. Tests only adapt existing reads.
- Canonical Clang 23 ci configure and complete IntrinsicTests rebuild pass.
  Focused CTest: 151 passed. Full CPU: 4,641 passed, zero failures, one expected
  ASan-only GLFW skip (4,642 selected, 133.31 seconds). GPU/sanitizer runtime
  behavior is not claimed; the changed access surface has no new GPU logic.
- Strict layering, test layout, task policy, documentation links and automated
  clean-workshop checks pass; 417-module inventory refreshed. Manual scorecard:
  rows 1–3 pass (ownership/targets/dependencies preserved); 4–6 n/a for behavior
  changes (recipe, pass order and RHI execution unchanged); 7 n/a for capability
  promotion; 8 pass (no temporary exceptions).
- Two benchmark samples per arm provide descriptive corroboration only, with raw
  values/ranges retained and no statistical or cross-host claim. Three or more
  samples were suggested during review; no general timing claim is sought here.

## Completion — 2026-09-15
- Commit reference: `1f84ec611120b8a0c5f9428be015284a40c68a03`; the enclosing
  evidence commit records retirement. All acceptance criteria for this bounded
  renderer surface change are met; this is not whole-engine completion.
- Four exact-source ABBA results validate. ForwardSystem interface touch omits
  exactly one critical-path producer, Graphics.Renderer.cppm (12 → 11); observed
  times are 29.734–29.749 → 16.174–16.279 seconds. The renderer implementation
  remains in the rebuild. Clean graphics builds still compile 266 producers,
  with observed times 89.402–89.877 → 85.373–86.213 seconds. Two observations per
  arm establish no general/statistical or clean-build speedup.
- C96 and the [measurement report](../../ara/evidence/tables/graphics138_renderer_compile_measurement.md)
  retain raw values, ranges, commands, immutable dependency identity, source
  checks and all review/test logs. Every result is claim_eligible false.
- Claude's final reporting findings are resolved: seven removed renderer-import
  dependencies do not mean seven removed target producers; timings are
  descriptive, combined-patch attribution is limited, and no RSS gain is claimed.
- Further renderer splitting needs a concrete dependency and cost justification;
  no additional wrapper or upload abstraction was introduced by this task.
