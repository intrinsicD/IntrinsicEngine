---
id: ARCH-006
theme: F
depends_on:
  - ARCH-012
maturity_target: Retired
completed: 2026-07-14
---
# ARCH-006 — Move Sandbox application editor content out of runtime

## Status
- Retired on 2026-07-14 at the structural `Retired` endpoint.
- Owner: Codex.
- Completion branch: `codex/arch-006-completion`.
- Commit/PR: source retirement commit `5b426580`; no PR (direct aggregate-main
  workflow).
- Slices 0-5 are complete. `Runtime.EditorUiHost` now owns generic
  callback, registry, and visibility lifecycle, while `Sandbox.cppm` exports
  only the app factory/registration surface and the concrete app lives in
  `Sandbox.cpp`. App-owned registered windows now own K-Means and Progressive
  Poisson presentation, and their runtime command/config/result facade bodies
  compile separately from the editor shell.
- Slice 3 moved ICP plus denoise, curvature, remesh, subdivision,
  simplification, and mesh/graph/point-cloud normals presentation/controller
  state into app-owned registered windows. The existing runtime model,
  command, undo, derived-job, and result-sink facades remain in runtime.
- Slice 4 moved point-cloud outlier removal plus the Mesh/Graph/PointCloud
  Appearance, Properties, and Selection windows into one app-owned registered
  panel family. Runtime retains the command/model/job/result facades.
- Slice 5 moved the remaining hierarchy/inspector/file/import,
  frame-graph/render-recipe/artifact, camera, and visualization presentation
  into app-owned `EditorShell`, retired the Sandbox-specific runtime UI module,
  and split the runtime facade contracts and tests by independently compiled
  subject families.

Slice 4 implementation boundary (2026-07-14):

- Add one concrete app-owned domain-panel family that registers the existing
  Mesh/Graph/PointCloud Appearance, Properties, and Selection windows plus
  PointCloud Remove Outliers through `Runtime.EditorWindowRegistry`. Preserve
  stable ids, menu paths, titles, defaults, first-use sizes, widget state, and
  the shared per-frame domain-model cache.
- Remove the legacy fixed domain-window table, its menu branches, and the
  runtime-owned Mesh Appearance exemplar from `Runtime.SandboxEditorUi`.
  Runtime retains domain-model construction, command/history execution,
  derived-job scheduling, config validation, and result publication.
- Keep the remaining Inspector's runtime-local visualization, bound-state, and
  texture-bake presentation helpers until Slice 5 moves that shell window.
  This is bounded transitional duplication; do not create a cross-layer shared
  presentation seam for a single remaining caller.
- Right-sizing: the concrete panel-family module is an independent compilation
  unit, not a new interface/service/registry seam. Deleting it would fold the
  same ImGui controllers and state back into the Sandbox composition unit; the
  existing UI-034 registry remains the only contribution mechanism. Its blast
  radius is `ExtrinsicSandbox`, runtime structural contracts, and the app/runtime
  ownership docs; no lower-layer import or CMake link edge is added. A new
  abstraction is justified only if a second application needs a shared domain-
  panel contract.
- Defer hierarchy/inspector/file/import, frame-graph/render-recipe/artifact,
  camera, visualization shell presentation, runtime facade relocation, and
  final test split/retirement to Slice 5. This was the recorded boundary before
  Slice 5 executed.

## Goal
- Restore the documented layering intent that application specifics live in
  `app/`: define and execute a sliced migration of
  `Extrinsic.Runtime.SandboxEditorUi` (~18.5k lines of method panels and
  sandbox workflows) so `runtime` retains only generic editor infrastructure
  (panel host, command surfaces, docking/window plumbing) and the Sandbox
  application content (K-Means, Progressive Poisson, registration, denoise/
  remesh/simplify, figure export panels) moves to `app/Sandbox`.

## Non-goals
- No behavior or panel-content changes during moves (mechanical relocation
  discipline: no mixing moves with semantic refactors — separate slices).
- No new editor features.
- Runtime keeps ownership of engine-facing command facades, extraction, and
  lifecycle; panels move, engine seams do not.
- Not gated on `RUNTIME-138`, but implementation slices that touch the same
  editor files should sequence around it to avoid churn collisions; coordinate
  slice timing in `tasks/active/README.md` when promoting. `RUNTIME-141` has
  retired and no longer owns overlapping work.

## Context
- Owner/layer: boundary decision between `runtime` and `app`.
- At task intake, `app/Sandbox` was a thin composition shell: it registered the
  Sandbox default runtime policies and `Runtime::ClusteringModule`, attached
  `Runtime::SandboxEditorUi`, and otherwise retains no-op application ticks.
  The editor's application-specific content still lives in
  `src/runtime/Editor/Runtime.SandboxEditorUi.cpp`, so the remaining ownership
  still inverts "app depends only on runtime; application specifics belong in
  app/" (`AGENTS.md`, `docs/architecture/runtime.md`).
- The generic seams the panels use (`SetImGuiEditorCallback`, command
  surfaces, `DerivedJobRegistry`, config-control facade) already exist and
  stay in runtime; the move is about *content* ownership.
- Retired `UI-031` already normalized the domain-window information
  architecture in the same file. Open `RUNTIME-138` (nonblocking selected-
  entity pipeline) still touches the same file; this task must slice around
  it, not duplicate it.
- Ownership split with retired `UI-034`: `UI-034` owns the generic domain-window
  registration API, lazy lifecycle, input-capture snapshot, global visibility
  toggle, and generic property widgets. This task owns the full inventory,
  panel-family split, and relocation of Sandbox content into `app/Sandbox`.
  Slice 0 may proceed independently; implementation slices consume the
  `UI-034` registration seam and must not create a second panel registry.
- ARCH-013 re-review (2026-07-08): Decision re-scoped onto the retired kernel
  seams. `ARCH-012` proved the first application-specific extraction:
  Sandbox now registers `ClusteringModule` from app startup and the editor
  submits `RunKMeans` through `CommandBus`/`JobService`/`KernelEvents` instead
  of `Engine` internals. Slice 0 must inventory generic editor-shell/panel-host
  code versus Sandbox content with that module-composition shape as the target;
  moved app panels may register modules and panel callbacks, but runtime must
  not import app code or accept `Engine&` pass-through.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R5.
- Compile-cost evidence from the 2026-07-09 `CI-003` audit:
  `Runtime.SandboxEditorUi.cppm` was the largest compile hotspot at 159.174s
  (2,899 lines, 44 imports), `Runtime.SandboxEditorUi.cpp` took 80.264s at
  24,807 lines, `Test.SandboxEditorUi.cpp` took 96.017s, and the 54-line
  `Sandbox.cppm` importer took 92.724s. These measurements are retained in
  `CI-003`; do not repeat exploratory benchmarking before slicing this task.
- The 2026-07-10 local triage ranked the same hot path as the strongest
  `.cppm` privatization candidate: `Runtime.SandboxEditorUi.cppm` still has
  one production consumer, roughly 51 imports, and very high churn, while
  `Sandbox.cppm` remains a one-consumer app module whose compile cost is
  dominated by importing the editor shell. This task is the owner for those two
  candidates; do not open a duplicate header-conversion task for them.

## Slice 0 inventory (2026-07-13)

Current measured source shape:

| Surface | Lines | Direct module imports | Current owner |
|---|---:|---:|---|
| `Runtime.SandboxEditorUi.cppm` | 2,970 | 54 | `runtime/Editor` |
| `Runtime.SandboxEditorUi.cpp` | 25,539 | 86 | `runtime/Editor` |
| `Test.SandboxEditorUi.cpp` | 12,254 | n/a | runtime contract tests |
| `Sandbox.cppm` | 64 | 4 | `app/Sandbox` |

Ownership inventory and destination:

| Inventory | Current shape | Destination |
|---|---|---|
| Generic editor host | Engine editor-callback attachment, one global visibility input action, registry draw lifecycle, and detach cleanup are mixed into `SandboxEditorUi::Attach/Detach` near the end of the implementation. | A small `Runtime.EditorUiHost` in `runtime/Editor`, built on the retired UI-034 registry/capture contracts. Contributor callbacks receive no `Engine&`. |
| Generic reusable widgets | `Runtime.EditorWindowRegistry` and `Runtime.EditorPropertyWidgets` are already independent runtime modules. | Stay in `runtime/Editor`; no competing registry or property-widget layer. |
| Sandbox presentation and UI state | Fixed menu/window enums, menu rendering, ImGui draw functions, method-control state, result display state, and the application editor controller occupy the anonymous-namespace UI blocks and the current `SandboxEditorUi` class. | `app/Sandbox/Editor`, split into shell, method-panel, and domain-panel units. App code imports runtime modules only. |
| Runtime panel models and command facades | Exported panel/model/command records plus model builders and `ApplySandboxEditor*` implementations contain ECS, geometry, assets, graphics, scheduling, and undo/redo work. | Stay in runtime, but move out of `runtime/Editor` and split by result-consumer contract. These are the runtime seams app panels call; live subsystem ownership does not move. |
| Tests | One 12,254-line runtime contract file mixes facade correctness, presentation behavior, and app composition. | Split with the subject: runtime facade contracts remain under runtime tests; app editor composition/presentation coverage moves to app-linked integration units. |
| Figure/export controls | No standalone figure-export panel exists in the current source. Render-artifact publication/export controls are part of the render-recipe editor. | Move with the shell/render-recipe presentation slice; do not invent a new feature. |

Implementation slice boundaries:

1. **Generic host and private app shell.** Add `Runtime.EditorUiHost`, route the
   current callback/visibility lifecycle through it, and move `App` lifecycle
   bodies out of `Sandbox.cppm`. No panel behavior moves in this slice.
2. **K-Means and Progressive Poisson.** Move their ImGui controls/state into
   app-owned registered windows; split their runtime models/commands from the
   editor shell without moving kernels or job ownership.
3. **Registration and mesh processing.** Move registration, denoise,
   curvature, remesh, subdivision, simplification, and normals presentation;
   keep execution/undo/job facades in runtime family units.
4. **Point-cloud and generic domain panels.** Move outlier-removal plus domain
   appearance/properties/selection presentation and their app state. The
   UI-034 registry is the only contribution seam.
5. **Shell, tests, and retirement.** Move hierarchy/inspector/file/import,
   frame-graph/render-recipe/artifact, camera, and visualization presentation;
   split tests, remove the Sandbox-specific class/module from `runtime/Editor`,
   update docs/inventory, and record final interface/import/build metrics.

Slice 3 implementation boundary (2026-07-13):

- Add one app-owned mesh-processing panel module that registers nine windows
  through the existing UI-034 registry seam: ICP; mesh denoise, curvature,
  remesh, subdivision, and simplification; plus mesh, graph, and point-cloud
  normals. Preserve the current menu paths, titles, defaults, controls, and
  per-window lazy domain-model cache.
- Remove the corresponding fixed window slots, ImGui draw routines, and input
  state from `Runtime.SandboxEditorUi`. Runtime retains the exported domain
  models and `ApplySandboxEditor*` facades, including undo/redo, derived-job
  scheduling, stale-result rejection, and result-sink delivery.
- Add structural proof that the app module imports runtime only, owns the nine
  registrations and ImGui controllers, and that the runtime editor shell no
  longer owns those presentation symbols. Existing runtime facade contracts
  remain the behavioral proof.
- Defer point-cloud outlier removal and generic appearance/properties/
  selection panels to Slice 4; defer hierarchy/inspector/file/render shell
  presentation and final runtime Sandbox editor retirement to Slice 5.

Slice 5 implementation boundary (2026-07-14):

- Add one concrete app-owned `Extrinsic.Sandbox.Editor.Shell` compilation unit
  for the remaining ten core windows, menu rendering, and ImGui-local state;
  compose it with the existing method, mesh-processing, and domain panel
  families through a small app-owned controller. Preserve titles, menu paths,
  defaults, first-use sizes, commands, and the one prepared runtime context per
  editor frame.
- Separate the presentation-free attachment/context lifetime from the old UI
  class as `Runtime::SandboxEditorSession`. It retains runtime-owned Engine,
  job/GPU queue, result-sink, cache, and render-recipe state; the app shell
  receives only a callback-scoped prepared view and never receives or owns live
  lower-layer subsystem state.
- Retire `Extrinsic.Runtime.SandboxEditorUi`; move its surviving data-only
  models, command facades, and session contract to the runtime-root
  `Extrinsic.Runtime.SandboxEditorFacades` module. Keep the active public DTO
  surface together for this mechanical ownership slice; split the already
  independent method and render-recipe implementation bodies, but do not turn
  a module rename into a semantic API redesign.
- Split `Test.SandboxEditorUi.cpp` by runtime subject family and move ImGui/app
  composition contracts to an app-linked integration unit. Preserve original
  runtime suite/name pairs wherever ownership did not change, then update
  ownership docs and the generated module inventory before retirement.
- Right-sizing: `EditorShell` and the app controller are concrete compilation
  units, not new interfaces or registries. `SandboxEditorSession` is retained
  because it is load-bearing for attachment epochs, asynchronous result-sink
  lifetime, GPU queue shutdown, and the `app -> runtime` boundary; moving those
  live owners into app would violate the repository contract. The blast radius
  is the Sandbox app target, runtime facade importers, editor tests, and
  ownership docs. A further public-facade split is justified only by a semantic
  result-consumer task with independently evolving callers, not by this move.

## Required changes
- [x] Slice 0 (planning, this file): inventory `Runtime.SandboxEditorUi`
      content into (a) generic editor infrastructure that stays in runtime,
      (b) sandbox/method panels that move to app, (c) engine command
      facades the panels call (stay). Record the inventory and the slice
      boundaries in this task file before any move.
- [x] Slice 1: after the `UI-034` registration seam lands, extract/slim the
      generic panel-host/editor-shell module in runtime and attach Sandbox
      through that seam. Do not define a second registration API or move
      additional panel content in this slice.
- [x] Slices 2..N: move panel families to `app/Sandbox` one reviewable slice
      at a time (method panels first: K-Means, Poisson, registration,
      mesh-processing, figure export), each slice green on the CPU gate and
      layering check.
- [x] Split the monolithic implementation and matching
      `Test.SandboxEditorUi.cpp` coverage along the same panel-family boundaries
      so moves create independently compilable units rather than one equally
      large app-side translation unit.
- [x] As part of the shell/content split, reduce
      `Runtime.SandboxEditorUi.cppm` to a tiny generic editor-shell contract and
      decide whether `src/app/Sandbox/Sandbox.cppm` should remain a module or
      become app-private header/source glue; record the decision and metrics in
      this task before the final slice.
- [x] Final slice: `src/runtime/Editor` contains no method/sandbox-specific
      panel code; update module inventories.

## Tests
- [x] Per slice: default CPU gate + `check_layering.py --strict` green.
- [x] Existing editor command/contract tests keep passing unmoved or move
      with their subject per `check_test_layout.py`.
- [x] Sandbox smoke (headless null-backend `Engine::Run()` coverage) stays
      green after each slice.

## Docs
- [x] Update `docs/architecture/runtime.md` and `src/app/README.md` /
      `src/runtime/README.md` ownership text as slices land.
- [x] Regenerate `docs/api/generated/module_inventory.md` per moved module.

## Acceptance criteria
- [x] `app/Sandbox` owns its panels; `runtime/Editor` owns only generic editor
      infrastructure, while presentation-free engine-facing facade contracts
      remain at the runtime root; `app → runtime` remains the only dependency
      direction.
- [x] Normal attached-frame panel content and commands are unchanged by the
      mechanical moves. Review found and corrected one lifecycle defect:
      detach now discards attachment-scoped local results and pending Poisson
      autorun state, matching `SandboxEditorSession` before cross-engine
      reattach.
- [x] Layering gate green at every slice boundary.
- [x] Record before/after interface lines/imports, top translation-unit compile
      durations, and clean build edges against the `CI-003` baseline; no
      compile-time claim is made from one run.
- [x] The `Runtime.SandboxEditorUi` and `Extrinsic.Sandbox` module surfaces are
      either measurably slimmed or explicitly retired to private header/source
      glue without changing app-to-runtime dependency direction.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice 1 evidence (2026-07-13):
- Added the 51-line / 2-import `Runtime.EditorUiHost`; its frame callback is
  parameterless and hidden editors invoke no application callback.
- `Sandbox.cppm` moved from 64 lines / 4 imports to 12 lines / 1 import; the
  concrete app lifecycle and editor imports are private to `Sandbox.cpp`.
- `IntrinsicRuntimeContractTests` and `ExtrinsicSandbox` built; focused host
  plus Sandbox editor coverage passed 145/145.
- `IntrinsicTests` built and the default CPU-supported gate passed 3677/3677;
  strict layering, test-layout, task-policy, docs-link, and diff checks passed.

Slice 2A evidence (2026-07-13):
- Added the 29-line / 1-import `Sandbox.Editor.MethodPanels` interface and its
  independently compiled app implementation. It registers PointCloud, Graph,
  and Mesh K-Means plus PointCloud and Mesh Progressive Poisson windows through
  the existing registry; callbacks receive `SandboxEditorContext`, not
  `Engine&`.
- Removed the five fixed domain-window slots and their K-Means/Progressive
  Poisson ImGui controls/state from `Runtime.SandboxEditorUi`. App panels retain
  one lazy per-domain model per frame and call runtime-owned model, command,
  config, job, and result-publication facades.
- The runtime implementation fell from 25,539 to 24,520 lines and the fixed
  domain-window count fell from 42 to 36. The new app implementation is 1,193
  lines after the cache-preserving review fix; no compile-time claim is made
  from these source metrics.
- `ExtrinsicSandbox` and `IntrinsicTests` build; focused `SandboxEditorUi.*`
  coverage passed 144/144 and the default CPU-supported gate passed 3678/3678.
  Strict layering, test-layout, task-policy, docs-link, and diff checks passed.

Slice 2B evidence (2026-07-13):
- Split the K-Means and Progressive Poisson command/config/result bodies plus
  K-Means GPU queue lifecycle into the independently compiled private
  `Runtime.SandboxMethodFacade.cpp` implementation unit. The public
  `Extrinsic.Runtime.SandboxEditorUi` module surface and Sandbox imports did
  not change; app-to-runtime remains the only dependency direction.
- The editor-shell implementation fell from 24,520 to 21,943 lines. The new
  method facade is 2,911 lines and its private cross-unit declarations are 58
  lines; the public interface remains 2,959 lines / 53 imports. These are
  source-shape metrics only, not a compile-time claim.
- Added a runtime contract assertion that both facade definitions and their
  CMake source edge live outside the editor-shell implementation. Existing
  behavioral K-Means and Progressive Poisson contracts remain the behavior
  gate.
- `ExtrinsicRuntime`, `IntrinsicRuntimeContractTests`,
  `IntrinsicRuntimeGraphicsCpuTests`, and `IntrinsicTests` built with the `ci`
  preset. Focused K-Means/Poisson/app-boundary coverage passed 17/17, headless
  Sandbox acceptance passed 9/9, and the default CPU-supported gate passed
  3638/3638. Strict layering, test-layout, task-policy, docs-link,
  clean-workshop-validator, and diff checks passed. The root-hygiene check
  completed in warning mode with the existing `ara/` allowlist finding.
- Regenerated the 386-module API inventory and the session brief; neither
  generated artifact changed because this slice adds only a private
  implementation unit and does not change task front matter.

Slice 3 evidence (2026-07-13):
- Added the 29-line / 1-import `Sandbox.Editor.MeshProcessingPanels` interface
  and its 1,763-line / 3-import app implementation. It owns nine registered
  windows: ICP; mesh denoise, curvature, remesh, subdivision, and
  simplification; plus mesh, graph, and point-cloud normals. Destruction and
  normal Sandbox shutdown both unregister callbacks idempotently.
- Removed the corresponding fixed window slots, menu branches, ImGui draw
  controllers, and input state from `Runtime.SandboxEditorUi`. Runtime retains
  all model builders, command/history execution, derived-job scheduling,
  stale-result rejection, and asynchronous result sinks. Point-cloud outlier
  removal and the generic domain panels remain explicitly deferred to Slice 4.
- The runtime implementation fell from 21,943 to 19,895 lines, its interface
  fell from 2,959 to 2,915 lines, the fixed domain-window count fell from 36
  to 12, and the runtime exemplar count fell from two to one. These are
  source-shape metrics only; no compile-time claim is made.
- `ExtrinsicSandbox`, `IntrinsicRuntimeContractTests`, and `IntrinsicTests`
  built with the configured presets. Focused `SandboxEditorUi.*` coverage
  passed 146/146 after the final review fixes; the new structural contract
  pins callback unregistration, immediate/pending result-sink forwarding, and
  the preserved ICP first-use dimensions, while the existing runtime behavior
  tests exercise asynchronous result publication. After those review fixes
  and all ten selected task slices were integrated at `11f56bf5`, the
  exact-head default CPU-supported gate passed 3692/3692. Strict layering,
  test-layout, task-policy, docs-link, clean-workshop-validator, and diff
  checks passed.
- Regenerated the 387-module API inventory. Task front matter did not change,
  so the generated session brief did not require regeneration.

Slice 4 evidence (2026-07-14):
- Added the 27-line / 1-import `Sandbox.Editor.DomainPanels` interface and its
  independently compiled 1,691-line / 9-import app implementation. It owns ten
  registered windows: Appearance, Properties, and Selection for Mesh, Graph,
  and PointCloud plus PointCloud Remove Outliers. Destruction and normal
  Sandbox shutdown both unregister callbacks idempotently.
- Removed the final 12 fixed domain-window slots, legacy menu branches,
  runtime-owned Mesh Appearance exemplar, ImGui controllers, and app-specific
  widget/input state from `Runtime.SandboxEditorUi`. Runtime retains selected-
  domain model construction, the callback-scoped mesh property view,
  command/history execution, validation, derived jobs, stale-result checks,
  and result sinks.
- Inspector still uses runtime-local copies of its bound-state,
  visualization, and texture-bake presentation helpers. Slice 5 owns their
  removal with the Inspector move; this slice deliberately adds no shared
  cross-layer widget seam.
- The runtime implementation fell from 19,895 to 18,707 lines and its
  interface is 2,912 lines / 53 imports. The new Geometry property import
  supports the borrowed callback-scoped property view, while the now-unused
  public property-widget import was removed; neither change adds an app
  dependency edge. These are source-shape metrics only; no compile-time claim
  is made.
- `ExtrinsicSandbox`, `IntrinsicRuntimeContractTests`, and `IntrinsicTests`
  built. Focused `SandboxEditorUi.*` coverage passed 147/147; the default
  CPU-supported gate passed 3,698/3,698, including all nine headless
  `RuntimeSandboxAcceptance` tests. The gate first exposed the unrelated
  baseline test compile defect separately tracked and retired as `BUG-084` in
  commit `2c8e8215`.
- Strict layering, test-layout, task-policy, task-link, docs-link,
  clean-workshop-validator, root-hygiene warning-mode, session-brief, and diff
  checks passed. Root hygiene retained only the pre-existing non-fatal `ara/`
  allowlist warning. Regenerated the 388-module API inventory; task front
  matter did not change, so the generated session brief remains current.

Slice 5 evidence (2026-07-14):
- Added the 64-line / 3-import `Extrinsic.Sandbox.Editor.Shell` interface and
  its 2,730-line / 9-import app-owned implementation. It owns the ten core
  Sandbox windows, menu and ImGui state, frame-scoped presentation view, and the generic
  `Runtime.EditorUiHost` / `SandboxEditorSession` attachment lifecycle. The app
  controller composes this shell with the method, mesh-processing, and domain
  panel families. The retained lean domain-panel implementation is 1,696 lines /
  10 imports after adding the shell contribution import and final attachment-
  reset handling; its Slice 4 boundary was 1,691 lines / 9 imports.
- The small app controller is retained because the Sandbox application, the
  app-linked reattachment contract, and the GPU acceptance fixtures all
  instantiate the same four-family composition. It concentrates the required
  attach/detach order; deleting it would duplicate that ordering across those
  callers. Inspector/visualization stays a core Shell window while the domain
  panels own their distinct Appearance controls; their similar local drawing
  code does not justify a new shared widget interface in this mechanical slice.
- Deleted `Extrinsic.Runtime.SandboxEditorUi` and moved the surviving
  presentation-free engine contracts out of `src/runtime/Editor` as the
  accurately named `Extrinsic.Runtime.SandboxEditorFacades` module. Its public
  interface is 2,903 lines / 52 imports because active app panels consume
  its runtime-owned contexts, models, commands, jobs, and result records;
  splitting that public API would be a semantic API redesign rather than this
  ownership move. K-Means/Progressive Poisson bodies compile in the independent
  2,912-line method unit and render-recipe/artifact bodies in a 620-line /
  4-import unit; the remaining implementation is 15,719 lines. The lean facade
  preserves the Slice 4 callback-scoped selected-mesh property resolver and
  does not restore the rejected public property-widget model facade.
- `src/runtime/Editor` now contains only the generic `EditorUiHost`,
  `EditorWindowRegistry`, and `EditorPropertyWidgets` interface/implementation
  pairs. Runtime sources contain no ImGui Sandbox drawing, fixed Sandbox window
  inventory, or app menu state.
- Review-driven lifecycle correction: each app panel family now clears
  attachment-scoped results, selected-property plot cache, and pending Poisson
  autorun identity/timing during unregister. A controller cross-engine
  reattach contract plus structural reset-policy assertions prevent the new
  session from inheriting prior-engine presentation state.
- Replaced the 12,254-line monolithic runtime contract file with six
  independently compiled subject files: models (3,226 lines / 34 tests), scene
  commands (1,926 / 22), clustering methods (2,117 / 20), mesh methods
  (3,912 / 41), visualization (1,752 / 14), and session lifecycle (400 / 4).
  The six runtime files contain 135 tests; the app-linked domain-panel and
  presentation files contain 4 and 16 tests respectively. Across the endpoint,
  132 original suite/name pairs remain exact, 15 presentation tests moved or
  were renamed with their app-owned subject, and 8 endpoint contracts cover
  session lifetime and domain-panel registration/cache behavior. The obsolete
  selected-mesh scalar-plot helper regression is intentionally absent, for a
  conservative focused endpoint inventory of 155 tests. The source retirement
  run's app-owned subset passed 20/20.
- `Sandbox.cppm` remains a module: at 12 lines / 1 import it is the narrow
  app factory/registration boundary between the CLI entry point and private
  composition, so converting it to a private header would remove no meaningful
  import fan-out. This is a source-shape decision, not a compile-time claim.
- Manual clean-workshop scorecard: rows 1-3 pass (ownership, layering, and
  reviewable decomposition); rows 4-6 are not applicable because no new
  subsystem, pass, or ownership seam was introduced; row 7 is not applicable
  at the `Retired` endpoint; row 8 passes with no temporary exception or
  allowlist entry.
- A controlled `CCACHE_DISABLE=1` clean build of `IntrinsicTests` completed all
  2,326 Ninja edges in 1,733.14 seconds. The clean sample measured the final
  source shape before one review-driven selected-property reset was narrowed;
  the exact head then rebuilt its 1,085 affected dependency/build edges in
  19.06 seconds. The clean log contains 1,067 scan edges, 1,067 compile edges,
  and 47 link edges.
- The clean sample's relevant top compile edges were the then-2,916-line /
  52-import `Runtime.SandboxEditorFacades.cppm` at 133.298 seconds and the
  64-line / 3-import `Sandbox.EditorShell.cppm` at 89.962 seconds. The Ninja log
  also retained a deleted-output tombstone for the old editor module, so that
  stale entry was excluded. For comparison only, the task's `CI-003` baseline
  recorded the old `Runtime.SandboxEditorUi.cppm` at 159.174 seconds, its
  implementation at 80.264 seconds, the monolithic test at 96.017 seconds, and
  `Sandbox.cppm` at 92.724 seconds. These are single-host diagnostic samples;
  no compile-time improvement is claimed. The post-replay facade source metrics
  above supersede the sampled line count without implying a new timing result.
- The replayed exact head configured with the `ci` preset, completed the
  1,290-edge `IntrinsicTests` build, then completed the 1,085 affected-edge
  rebuild after removing the obsolete test's dead helper with no compiler
  diagnostics. The focused editor inventory passed 155/155 in 15.91 seconds:
  135 runtime facade/session contracts plus four domain-panel and sixteen app-
  presentation contracts. The full default CPU-supported gate passed
  3,706/3,706 in 379.47 seconds.
- Strict layering, test-layout, task validation/policy, clean-workshop
  automation, documentation links, PR-contract mapping, module-inventory
  generation, and diff checks passed. Replay root hygiene remained non-fatal
  warning mode for the existing `.ruff_cache/`, `ara/`, and `imgui.ini` root
  entries; it introduced no task finding.

## Forbidden changes
- Mixing mechanical moves with semantic refactors in one slice.
- Moving live ECS/renderer/asset ownership into `app`.
- Starting content moves before the Slice-0 inventory is recorded here.
- Moving the monolith intact and declaring the source-ownership change a
  compile-time improvement without comparable measurements.

## Maturity
- Target: `Retired`-style structural endpoint (content relocated, layering
  clean); each slice closes at `Operational` for the moved panels (sandbox
  still runs). Slice 0 alone closes `Scaffolded` with the slice plan as the
  artifact; follow-up slices are owned by this same task until split.
