---
id: RUNTIME-166
theme: F
depends_on:
  - CI-003
  - BUILD-004
maturity_target: Operational
---
# RUNTIME-166 — Slim and partition the RenderExtraction module

## Goal
- Reduce the same-host primary interface-edge compile time for
  `Extrinsic.Runtime.RenderExtraction` and make its implementation domains
  independently compilable by hiding `RenderExtractionCache` private state,
  without changing extraction behavior or public data contracts.

## Non-goals
- No renderer/RHI/ECS ownership change and no new extraction feature.
- No semantic change to residency, deferred retirement, adapter application,
  statistics, selection, or render-world submission.
- No compatibility re-export or duplicate extraction path.
- No edit to `Runtime.Engine` decomposition owned by `RUNTIME-146..151`.
- No overall cold-build, full clean-gate wall-time, or whole-tree parallelism
  claim from primary-edge samples.

## Context
- Owner/layer: `runtime`; the module may import ECS/graphics lower layers, while
  graphics remains unaware of live ECS ownership.
- The 2026-07-09 `CI-003` audit measured
  `Runtime.RenderExtraction.cppm` at 106.935s (867 lines, 36 imports/exports),
  the third-largest module-interface compile hotspot. The implementation is
  3,474 lines.
- The interface exposes public snapshot/stat/availability contracts but also
  embeds extensive private residency sidecars, pack buffers, retire queues,
  adapter registries, and heavy graphics/ECS types in
  `RenderExtractionCache`'s layout. Those private declarations force importers
  to parse dependencies that only the implementation needs.
- A private implementation object and non-exported module partitions are
  established repository patterns. No ADR is needed unless implementation
  discovers a public ownership/API decision.
- Historical evidence in
  `tools/analysis/build_time_baseline_2026-04-05.md` shows implementation-only
  touches rebuilding in 16s while module-interface touches cascaded for
  3m23s–4m19s. That different-generation evidence motivates the task but is not
  a comparable before/after population for this change.
- `BUILD-004` owns normalization of multi-output module compile edges and
  source-complete hotspot reporting. This task consumes that repaired evidence
  for hotspot selection context; its performance result uses only paired
  same-host primary interface-edge samples and physical rebuild-edge evidence.

## Status
- Completed and retired at `Operational` on 2026-07-19; owner: Codex team;
  implementation branch: `codex/runtime-166-slim-render-extraction`.
- Implementation commit: `c02eb142`; merged to `main` as `66f5f7b9`.

## Before inventory (2026-07-18)
- Source size at `c0c2c376`: `Runtime.RenderExtraction.cppm` is 908 lines and
  `Runtime.RenderExtraction.cpp` is 3,500 lines.
- The interface has 38 module dependency statements (36 ordinary imports and
  2 re-exports), plus 14 global-module-fragment includes. It has 38 direct
  importers in the configured source tree: 12 under `src/` and 26 under
  `tests/`.
- The public contract consists of the asset observation/acknowledgment enums
  and records, `RuntimeRenderExtractionStats`, the three free observation/pool
  helpers, and `RenderExtractionCache`'s extraction, maintenance, settings,
  availability, adapter-registration, and test-observation API. Its nested
  value contracts are `RenderableSidecarView`, `GpuRenderLaneAvailability`,
  `GpuRenderableAvailabilityView`, `VisualizationAdapterBindingKind`, and
  `VisualizationAdapterBinding`.
- Seventeen module dependencies are used by those public declarations and remain
  candidates for the slim interface:
  `ECS.Scene.Registry`, `Asset.Registry`, `Graphics.GpuAssetCache`,
  `Graphics.Renderer`, `Graphics.GpuWorld`, `Graphics.Material`,
  `Graphics.MaterialSystem`, `Graphics.RenderWorld`,
  `Graphics.Component.GpuSceneSlot`,
  `Runtime.GeometryAvailability` (re-export),
  `Runtime.MeshPrimitiveViewPacker`, `Runtime.ProceduralGeometry`,
  `Runtime.RenderWorldPool`, `Runtime.SelectionController`,
  `Runtime.SpatialDebugAdapters`, `Runtime.WorldHandle`, and
  `Runtime.VisualizationAdapters` (re-export). The exact retained set will be
  compiler-checked because C++ module reachability may require a direct import
  beyond a type's apparent declaration owner.
- Twenty-one module dependencies are private implementation-only candidates:
  `ECS.Components.AssetInstance`, `ECS.Components.GeometrySources`,
  `ECS.Component.DirtyTags`, `ECS.Component.ProceduralGeometryRef`,
  `ECS.Component.SpatialDebugBinding`, `ECS.Component.Transform`,
  `ECS.Component.Transform.WorldMatrix`, `ECS.Component.Culling.World`,
  `ECS.Component.Light`, `Graphics.TransformSyncSystem`, `Graphics.LightSystem`,
  `Graphics.VisualizationSyncSystem`, `Graphics.Component.Material`,
  `Graphics.Component.RenderGeometry`,
  `Graphics.Component.VisualizationConfig`, `RHI.Types`,
  `Runtime.GraphGeometryPacker`, `Runtime.MeshGeometryPacker`,
  `Runtime.PointCloudGeometryPacker`,
  `Runtime.ProgressivePresentationExtraction`, and
  `Runtime.ProceduralGeometryPacker`.
- Private declarations embedded in the exported class are three concrete
  records/enums (`RenderableSidecar`, `MeshPrimitiveViewKind`, and
  `GeometryRetireRecord`), one incomplete visualization state record, and 21
  helper methods. Private storage includes the renderable map and live-key
  scratch set; transform, visualization, light, and spatial-debug batches;
  procedural/mesh/graph/point-cloud/primitive-view pack buffers and residency
  state; four deferred-retire queues and their counters; settings/material
  binding maps; owned spatial/visualization adapter registries; and last-frame
  statistics.
- Right-sizing decision: retain the one existing public cache type and put its
  existing private state behind one implementation object. The three requested
  implementation domains will share that object through one non-exported
  implementation partition; no new public interface, registry, service,
  factory, or ownership layer is introduced.
- Timing comparability boundary: this local host selected
  `/bin/clang++-23` with matching Clang/clang-scan-deps 23.0.0, CMake 3.28,
  Ninja 1.11, and the repository dependency cache. The hosted `BUILD-004`
  baseline uses Clang 20, so timing from this configured Clang 23 tree is
  diagnostic only and cannot be compared directly for a gain claim. Matching
  Clang 20 is also installed. Five paired before/after direct primary-edge
  samples from separate pinned build trees on this host, with alternating
  order, ccache disabled, and one pinned CPU form the comparable local
  population. Hosted `BUILD-004`/`CI-003` measurements remain context only; no
  cross-host percentage claim is valid.

## After implementation evidence (2026-07-18)
- `Runtime.RenderExtraction.cppm` is 599 lines with 17 module dependency
  statements and 5 global-module-fragment includes, down structurally from
  908/38/14. The exported cache now stores only an incomplete `State` and
  `std::unique_ptr<State>`; this count reduction is not itself a compile-time
  improvement claim.
- Non-exported implementation partition
  `Runtime.RenderExtraction.Internal.cpp` is 406 lines and provides the single
  named-module definition of the private state and cross-unit declarations,
  without non-trivial inline control flow. The ordinary primary-module
  implementation units import `:Internal` and are 2,399 lines for base
  extraction/submission, 1,374 for geometry residency and retirement, and 301
  for visualization/spatial adapters.
- Independent review rejected an earlier include-only header shape because
  textually defining `RenderExtractionCache::State` after the named-module
  declaration in three translation units would be ill-formed, no diagnostic
  required under the C++ ODR. The implementation partition is compiled once in
  a private CMake `CXX_MODULES` file set and removes those duplicate
  named-module definitions.
- A forced touch of `Runtime.RenderExtraction.Geometry.cpp` followed by an
  `ExtrinsicRuntime` build compiled only
  `Runtime.RenderExtraction.Geometry.cpp.o` and relinked
  `libExtrinsicRuntime.a`; no module interface or importer object/BMI compiled.
  The repository's glob recheck did rerun configured-graph dependency scans,
  which are recorded separately from physical compile edges.
- An earlier configured local Clang 23 hotspot report is excluded from
  RUNTIME-166 evidence because it preceded the independent-review correction
  from duplicate include-only state definitions to one implementation
  partition. The final bounded claim uses only the corrected paired Clang 20
  population.
- Focused extraction/runtime acceptance selection: 49/49 passed. Direct mesh,
  graph, point-cloud, primitive-view, procedural, and RenderExtraction
  selection: 145/145 passed, including the pending-retire PImpl destruction
  case. `IntrinsicTests` built successfully, and the final full default CPU
  selector completed with zero failures across 4,104 tests (one expected GLFW
  lifecycle skip) in 68.99s. That duration is correctness-gate context, not
  compile-performance evidence.
- Strict layering, task policy, test layout, root hygiene, and documentation
  link checks passed. The module inventory generator reported 388 modules and
  produced no tracked diff.
- Corrected direct-edge timing compared baseline source `c0c2c376` with timed
  candidate source revision `eaca6571`; the subsequent amendment is
  evidence-only. The population used Clang 20.1.2 and Ninja 1.11.1. For each
  tree, the exact final compiler command was extracted with
  `ninja -t commands <primary-object> | tail -1` and run directly under
  `/usr/bin/time` with `CCACHE_DISABLE=1` and CPU affinity pinned to CPU 1.
  CMake, dependency scanning, dyndep generation, linking, and downstream
  compilation were therefore outside the timed region.
- Pair order was baseline/candidate, candidate/baseline, baseline/candidate,
  candidate/baseline, baseline/candidate. Baseline elapsed samples were
  41.05s, 37.92s, 44.25s, 40.87s, and 37.84s (median 40.87s;
  nearest-rank p95/max 44.25s). Candidate elapsed samples were 20.94s, 20.72s,
  19.84s, 19.72s, and 22.68s (median 20.72s; nearest-rank p95/max 22.68s).
  The paired-population median primary-edge elapsed reduction is 49.3%.
- Baseline user-CPU samples were 39.66s, 36.52s, 40.46s, 39.54s, and 35.85s
  (median 39.54s); candidate samples were 19.74s, 19.76s, 18.82s, 18.73s, and
  20.80s (median 19.74s), a 50.1% median reduction. Host load was
  9.57/11.23/11.69 at population start and 8.86/9.92/11.00 at population end.
  The session-local raw log is
  `/tmp/runtime166-direct-paired-20260719.log`.
- This establishes only the same-host direct primary-interface edge result.
  It does not establish an overall cold-build, full clean-gate, or whole-tree
  parallelism improvement.

## Required changes
- [x] Inventory interface declarations/imports into public contract, required
      complete public types, and private implementation-only types; record the
      inventory in this task before editing.
- [x] Hide `RenderExtractionCache` private sidecars, scratch buffers, retire
      queues, and adapter state behind implementation storage so their headers/
      modules leave the exported interface.
- [x] Move non-trivial control flow and all private helper declarations that do
      not need public visibility into implementation units or non-exported
      partitions.
- [x] Split the implementation into independently compilable domains at natural
      boundaries (base extraction/submission, geometry residency/retirement,
      visualization/spatial adapters) without duplicating state ownership.
- [x] Preserve public API and value/lifetime semantics; if PImpl changes
      special-member requirements, define them explicitly in the implementation
      and update direct construction tests.
- [x] Re-audit every interface import and global-module-fragment include,
      retaining only declarations required by the public surface.
- [x] Record before/after interface lines/imports, five paired same-host primary
      interface-edge samples, and physical rebuild edges after an
      implementation-only touch. Do not infer whole-tree or full clean-gate
      wall time from those measurements.

## Tests
- [x] Existing RenderExtraction unit/contract/integration tests pass unchanged
      except for mechanical import updates.
- [x] Add or retain lifetime tests covering construct, move if supported,
      shutdown/drain, and destruction with pending deferred-retire state.
- [x] Run the full default CPU gate and layering check.
- [x] Compare the primary interface edge with at least five paired same-host
      samples, and audit implementation-touch physical compile edges, before
      claiming a bounded gain.

## Docs
- [x] Update runtime architecture/module documentation if implementation
      partitions or the public import surface change.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Update `tasks/backlog/runtime/README.md` and regenerate
      `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [x] `Runtime.RenderExtraction.cppm` contains no private residency/retire/
      adapter storage declarations that can live behind implementation state.
- [x] Interface import count and paired same-host primary interface-edge median
      decrease, with no importer object/BMI physical compile edges after an
      implementation-only edit.
- [x] Extraction behavior, diagnostics, residency lifetimes, and public API are
      unchanged under focused and full CPU tests.
- [x] The module remains `Operational` through the existing Engine/runtime
      composition path.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RenderExtraction|RenderWorldPool|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Moving live ECS ownership into graphics or exposing `Vk*` through runtime/RHI
  surfaces.
- Mixing behavior changes with the mechanical implementation/interface split.
- Leaving exported forwarding shims for private helper types.
- Claiming compile-time improvement without comparable baseline results.
- Claiming overall cold-build, full clean-gate, or whole-tree parallelism
  improvement from the bounded primary-edge experiment.

## Maturity
- Target: `Operational`; this is a behavior-preserving decomposition of the
  active runtime extraction path, so no new capability follow-up is owed.
