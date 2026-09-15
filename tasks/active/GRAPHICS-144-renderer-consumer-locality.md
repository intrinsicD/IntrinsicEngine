---
id: GRAPHICS-144
theme: B
depends_on: [BUILD-009]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive task; unattended workflow completion reports are exempt, but this task owns its benchmark manifests, results and source identities alongside review and test evidence.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# GRAPHICS-144 — Reduce remaining renderer interface and implementation dependencies

## Goal
Reduce the current renderer's remaining compile cost using narrower real
consumer contracts and less repeated work while preserving rendering capabilities.

## Scope and starting point
- Explicit operator-directed continuation. GRAPHICS-138–143 are complete:
  unused renderer accessors and registry getters are gone, device borrowing is
  established, scene handles have one small owner, and named buffer metadata
  has one map. These mechanisms must not be recreated or counted again.
- Start from `src/graphics/renderer/Graphics.Renderer.cppm` and `.cpp` plus their
  actual runtime/test consumers. BUILD-009 supplies the current hotspot ranking;
  the old roughly 15-second producer measurements are historical.
- Compare unused dependencies, narrower existing contracts and private state
  before splitting files or adding Pimpl. Any new boundary must remove actual
  dependency work without extra forwarding, allocation or repeated parsing.
- Reuse `Graphics.SceneHandles`, canonical RHI borrow declarations, snapshot
  types, and existing subsystem lifetime owners. Keep required texture AssetIds.
  Follow `docs/architecture/graphics.md`; preserve recipes, diagnostics, config,
  resource ordering, operational-device fallback and app → runtime ownership.
- The upload helpers are only a candidate if the measured renderer boundary
  selects them: compare ImGui UploadBytes with UploadPackedColorVertices using
  byte/vertex caps, empty-input behavior, usage flags, lease failure/reset,
  allocation counters and frame lifetime. Do not blindly route either through
  asynchronous GpuTransfer or add a broad helper import to share a short loop.
- GRAPHICS-105 owns material/visualization behavior; LEGACY-043 shader deletion;
  GRAPHICS-135 runtime scheduling; BUILD-006 backend/cache selection. This task
  does not absorb those independent changes.

## Acceptance criteria
- [ ] Audit current interface types, private state, imports and consumers;
      select a bounded cost-backed change and compare simpler alternatives with Claude.
- [ ] Implement without feature loss, new live-ECS/AssetService dependencies,
      compatibility wrappers or public subsystem forwarding. Record source/file
      and ownership deltas, including any justified new private boundary.
- [ ] Reuse the compiler-boundary test helper and retain existing guards;
      demonstrate the selected regression mechanism on the original source and
      its removal afterward. For an import cut use original/final compiler
      metadata; for serialization use paired phase/BMI diagnostics and matched
      producer/consumer timing including the new owner. Preserve real material,
      UV and extraction consumers.
- [ ] Pass focused and full CPU checks. Verify changed module attachment with
      fresh cache-off minimum-supported Clang; if GPU resource lifetime or upload
      behavior changes, run the affected promoted Vulkan tests under ci-vulkan.
- [ ] Compare matched renderer implementation/interface edit probes and the
      graphics-library target with the existing runner. Keep negative results;
      reject extra complexity without benefit or record an evidence-backed
      no-change verdict. Do not extrapolate target timings to the whole engine.
- [ ] Resolve Claude's fixed-diff review, synchronize graphics documentation and
      changed inventory, and retire with exact source/test/measurement evidence.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'CompilationLocality|Renderer|RenderWorld|GpuWorld|GeometryResidency|Visualization|ImGui|UvView' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Planning lead — 2026-09-15
Claude and a source-import audit identify nine direct Device importers within
the renderer's source graph: Renderer plus ColormapSystem, CullingSystem, HZB,
MaterialSystem, PostProcessSystem, ShadowSystem, UvView and VisualizationSyncSystem.
Removing only Renderer's direct Device/CommandContext imports cannot close that
boundary. Inspect complete-type uses in those existing owners before considering
a new UV contract split; a split that leaves the same transitive dependencies
does not establish a benefit. This is a planning lead, not a measured change.

## Refreshed baseline
BUILD-009 is complete. Use its [matched source comparison](../../ara/evidence/tables/build009_current_compile_measurement.md)
and retained producer/critical-path records; the old BUILD-007 costs are historical.
Freeze this task's immediate-before source before attributing its own changes.

## Snapshot serialization lead — 2026-09-16
RUNTIME-268's verified source `ca164c10c` keeps its records intact but imports
standard declarations from a small runtime-local module, avoiding expensive
merged-header serialization in the broad snapshot interface. Trace the current
renderer first: source splitting, Pimpl and a shared textual prelude need not
address that mechanism. Graphics must not import the runtime helper. Compare
existing lower-layer ownership and the cost of any proposed declaration owner,
including its producer and real consumers, before selecting an analogous change.
[RUNTIME-268 evidence](../../ara/evidence/tables/runtime268_snapshot_std_measurement.md)
is a diagnostic lead, not a measured renderer improvement.

## Selected slice — 2026-09-16
- Explicit operator continuation; baseline `dc606482a`, one root writer on
  `codex/editor-compile-locality`, Claude reviews fixed packets.
- Original renderer trace records serialization as its dominant phase. Test
  reusing the existing snapshot standard-declaration owner in Core for these two
  actual consumers. Add only function/span/unique_ptr needs; retain the existing
  standard type identities and all renderer state in its implementation.
- A graphics-local duplicate would add a second helper. Compare the shared owner's
  own compilation and real consumers; do not migrate unrelated modules or add a
  general prelude policy. Core gains only standard includes, not an engine edge.
- Existing forbidden-module guards remain. This slice targets serialization work,
  not elimination of renderer's required subsystem dependencies; acceptance proof
  follows that selected mechanism instead of inventing a forbidden import.
- RUNTIME-267 discovery found by-value config/result/session ownership. Its panel
  header split alone cannot remove an embedded frame definition without changing
  ownership. Keep that task open; avoid adding a per-session facade here.

- GLM reuse: only vec3 is named by the renderer interface, so test the existing
  `<glm/fwd.hpp>` rather than compiling its full umbrella header. Other modules
  already use that header; concrete consumers retain their GLM definitions.
- Claude's plan supports the mechanism but asks to charge the early Core producer
  and all consumers. All five renderer standard type spellings are routed,
  including existing string/optional names; no CTAD expressions are changed.

## Source review and verification — 2026-09-16
- Reused the declaration owner rather than duplicating it: one source/module moved
  from runtime to core; module count remains 419. No new allocation, forwarding,
  interface members or subsystem state; standard type identities remain unchanged.
- Claude raised the complete-element requirement for `span<const glm::vec3>`.
  The required SpatialDebugVisualizers interface exports a by-value vec3 record,
  making its definition reachable; the forward header supplies the name. A
  `sizeof(glm::vec3)` assertion checks this before the spans. Claude reviewed this
  resolution. Both compiler builds below include that final assertion.
- Canonical ci configured; full IntrinsicTests build passed. Focused CPU 444/444
  passed (35.19 s); full CPU 4,664 passed, zero failures, one ASan-only skip out of
  4,665 selected (141.22 s). Existing real hook execution, UV/material/extraction,
  snapshot records and compiler-boundary checks remain intact.
- Fresh cache-off Clang 20 Null/headless ExtrinsicSandboxEditor closure passed;
  final reconciliation also compiled renderer lifecycle and snapshot model test
  objects. This is minimum-compiler build evidence, not Clang 20 test execution.
- Strict layering/task/test-layout, root hygiene and documentation links passed.
  Source documentation audit found zero objective errors; 23 review flags were
  inspected, retaining existing lifecycle/ownership comments outside this slice.
- Matched graphics target and snapshot-owner measurements remain required before
  retirement. No speedup is claimed from the preliminary single traces.
