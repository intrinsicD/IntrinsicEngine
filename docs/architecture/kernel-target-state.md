# Kernel target state — the north star for runtime work

- **Status:** Living target document (not an ADR, not a task). Update the
  scorecard whenever a seam lands or a domain extracts; do not retire it.
- **Contract:** [ADR-0024](../adr/0024-kernel-module-architecture.md), as
  right-sized by
  [ADR-0027](../adr/0027-right-sized-runtime-composition.md). This doc is the
  *where we are vs. where we are going* (living).
- **Module-scope refinement:**
  [ADR-0026](../adr/0026-runtime-module-scope-by-consumer-contract.md) decides
  when demonstrated runtime responsibilities share one composed module or
  split; it defines no family taxonomy or wrapper-count outcome.
- **Tracked by:** [`ARCH-014`](../../tasks/active/ARCH-014-kernel-convergence-tracking.md)
  (umbrella; stays open until the scorecard is all-green).
- **Read this before** adding anything to `Runtime.Engine` or introducing a
  new `Runtime.*Module`. See the [Feature Module Playbook](feature-module-playbook.md)
  for the per-feature procedure.

## The destination in one sentence

`Extrinsic.Runtime.Engine` becomes a **slim kernel** — frame loop, command
bus, event bus, JobService + worker pool, WorldRegistry, FrameGraph
scheduling, FrameRecipe activation, render-extraction orchestration, and one
coherent input-capture snapshot — while every durable domain responsibility
is explicitly composed by the app outside the Engine public surface
(ADR-0024 D9 as amended by ADR-0027).

ADR-0024/0027 decide **kernel versus app-composed responsibility**. ADR-0026
then decides **one composition unit versus several** from app lifecycle,
durable-state scope,
dependency/cancellation/commit ownership, and consumer reactions. A family
name, matching result shape, extra service, or different execution mechanism
alone is never the boundary. `IRuntimeModule` is the current lean type-erased
lifecycle boundary, not a mandatory wrapper shape.

## The knob-decision guide — "where does my thing go?"

Before adding an `Engine` method or member, place the responsibility with
this table. If it does not fit a row, that is a signal to revisit ADR-0024,
**not** to add an `Engine` method.

| Your responsibility | Where it goes | Mechanism |
| --- | --- | --- |
| A user/UI action that *changes state* (import, save, bake, switch world, apply config, run an algorithm) | a **command** handled by a module | `Commands().Enqueue(XRequested{...})`; drained pre-sim (D5) |
| "When X happens, always do Y" (refresh a visualization after an attribute changes) | an **event reaction** in a module | `Events().Subscribe<XChanged>(...)` (D6); never a chain |
| Per-frame / per-substep data-dependent work (ECS query, extraction, a pump) | the cohesive behavior owner | Register the smallest real phase hook/system the frame loop must iterate; add signal/DAG machinery only when interacting production systems require it (D1/ADR-0027) |
| Background multi-frame compute (remesh, k-means, bake, readback) | a **job** | `Jobs().Submit(JobDesc{...})`; snapshot in, result committed at a pump (D8) |
| A new render technique (overlay, fullscreen analysis, compute fill) | a built-in recipe pass while the vocabulary remains sufficient | Add validated recipe data and the graphics-owned implementation; propose extension registration only with the first real pass the built-in vocabulary cannot express (D10/ADR-0027) |
| Deciding whether a click belongs to UI or the viewport | one frame-loop-owned capture value borrowed by each ephemeral hook context | Reset it once at frame start; EditorUi brackets the application tick with `UiBegin`/`UiBuild`/`UiEndCapture` and writes the value after `EndFrame`; later viewport behavior, any later hooks, and kernel input-action dispatch read the same value. Add precedence only after a second independent simultaneous claimant proves a conflict (D11/ADR-0027) |
| A keybinding/chord that triggers an action | an **input action** | `RegisterInputAction(...)` (usually enqueues a command) |
| Always-present infrastructure another composed responsibility needs (asset service, GPU cache) | a typed service or explicit narrow non-owning construction capability with a declared lifetime | Use the current `Provide<T>()` / `Find<T>()` registry for optional or order-independent discovery; `Require`/`OnResolve` survives only if a production dependency proves it. Never create hidden ownership or a mutable peer backreference (D3/ADR-0027) |
| A *domain-specific* extension point (post-import processors, camera controllers, visualization adapters, editor panels) | a **module-provided service**, **not** a kernel knob | the owning module `Provide`s its own registry; others `Find` it |
| Durable state other composed responsibilities query (selection, undo history, camera set) | state owned by one app-composed responsibility, exposed narrowly if another real consumer exists | Record global versus world-qualified scope; do not add an Engine getter or one-service wrapper |
| Engine-wide shutdown | the built-in `QuitRequested` **command** | never a direct call; `Engine&` is not passed to modules (D13) |

**The rule that keeps the kernel slim:** a `Register*` knob goes on the
kernel *only if the frame loop itself iterates it*. Everything else is a
service a module provides. Most extension points are the latter — that is by
design, not a gap.

## Convergence scorecard

Each row is a greppable invariant. `ARCH-014` owns keeping this current;
flip a box when the invariant holds on `main`. Baseline column is the
2026-07-08 measurement (post-ARCH-007) and remains fixed for comparison;
dated current snapshots are refreshed separately. The fixed 2026-07-13
legacy-interim reference snapshot is 43 plain imports and 23 domain imports.
Its unanchored interim regex admitted the then-present
`Runtime.RenderExtractionService` through the `Runtime.RenderExtraction`
prefix; the exact v1 classifier intentionally does not rewrite that historical
record. After `RUNTIME-178`, the checked 2026-07-16 exact-policy snapshot was
42 / 21 with 31 distinct public `Engine::GetX()` names and two existing
re-exports. ADR-0027's 2026-07-18 classifier correction recognizes
`Extrinsic.Runtime.WorldHandle` as kernel substrate. The `RUNTIME-179`,
`RUNTIME-181`, and `RUNTIME-182` extractions then reduce the current exact
snapshot to 39 plain imports / 17 domain imports / 2 re-exports / 28 public
getter names. The fixed reference remains historical comparison evidence; the
current snapshot carries no temporary debt.

### Kernel seams exist (ADR-0024 D5–D11, amended by ADR-0027)

- [x] CommandBus + single pre-sim drain — `ARCH-007` (done 2026-07-08)
- [x] EventBus, queued-only, two pumps — `ARCH-008` (done 2026-07-09)
- [x] JobService, snapshot-in/result-out — `ARCH-009` (done 2026-07-09)
- [x] WorldRegistry, deferred two-phase world ops — `ARCH-010` (done 2026-07-09)
- [x] Lean app-to-runtime lifecycle boundary + no-`Engine&` `EngineSetup` +
      typed service discovery — `ARCH-011` (done 2026-07-09); unconsumed
      resolve/schedule portions remain under the final deletion test
- [x] Closed built-in recipe-pass vocabulary with fail-closed
      schema/capability/resource validation; extension registration is
      deliberately deferred until a non-expressible production pass exists
- [x] One coherent input-capture snapshot — `UI-034` (done 2026-07-13 at
      `CPUContracted`; the completed editor-frame value gates camera, gizmo,
      and registered actions; no priority chain exists)

### Kernel is slim (measurable)

> **Domain-import metric (allowlist complement).** The kernel's permitted
> substrate prefixes and exact modules are small and enumerable; every plain
> import outside that set is a domain import. The authoritative metric and
> snapshot live in `tools/repo/check_kernel_convergence.py` and
> `tools/repo/kernel_convergence_policy.json`:
>
> ```bash
> python3 tools/repo/check_kernel_convergence.py --root . --strict
> ```
>
> The checker treats plain imports and re-exports separately, uses exact
> runtime substrate names so `JobServiceGpuQueueBridge`/`ModuleSchedule` are
> intentional rather than regex accidents, strips comments before finding the
> public Engine getter set, and fails on both new and stale snapshot entries.
> A reduction therefore must lower the policy in the same change, preventing
> the old cap from becoming room for later regrowth.


- [ ] `Runtime.Engine.cppm` contains only the exact imports required by its
      accepted kernel public surface and no unused plain imports
      (**baseline 45; 2026-07-13 reference 43; current checked snapshot 39**).
      ADR-0027 records the present final-surface candidate of 12 exact imports;
      that is an auditable allowlist derived from the remaining API, not a
      numerical budget or room for unrelated imports.
- [ ] Domain (non-substrate) imports in `Runtime.Engine.cppm` = 0
      (**baseline 27; 2026-07-13 reference 23; current checked snapshot 17**).
      Measure by
      **allowlist**, not
      a blocklist of names:
      count every `import` that is *not* kernel substrate (see the metric
      above). A name blocklist silently undercounts as new domain imports
      appear (e.g. `AssetIngestStateMachine`, `Asset.Service`,
      `Geometry.HalfedgeMesh.IO`); the allowlist cannot.
- [ ] `Engine::GetX()` domain-facade accessors = 0. A domain facade is any
      public `GetX()` whose return exposes a responsibility outside the exact
      kernel-substrate allowlist; the final checker measures the complement of
      exact allowed kernel getter names rather than treating all `GetX()` names
      alike (**baseline estimate 13 domain facades; current guard snapshots all
      28 names pending that classifier**)
- [ ] Domain re-exports from `Runtime.Engine.cppm` = 0; a retained re-export
      must be explicitly classified as kernel public surface
- [x] No `entt::dispatcher::trigger` or direct dispatcher use in module code
- [x] No `Engine&` is passed through the `IRuntimeModule`/`EngineSetup`
      composition seam (`ARCH-011`)
- [ ] No `Engine&` is passed through any handler, module, app behavior, or
      setup surface (D13). Remaining EditorUiHost/Sandbox-default/facade and
      `IApplication` compatibility surfaces are removed by
      `RUNTIME-168`/`RUNTIME-184`
- [ ] `IApplication::OnSimTick` / `OnVariableTick` removed (**baseline: present**)

### Domain responsibilities are app-composed (D9/ADR-0027)

- [x] Clustering — `ARCH-012` (done 2026-07-08 at `Operational`; Sandbox
      composes `Runtime::ClusteringModule`, and `Runtime.Engine.cppm` / `.cpp`
      contain no `KMeans` or `Runtime.ClusteringModule` tokens; the module
      object is global and its jobs/commits are qualified by `WorldHandle`)
- [x] AsyncWork — `RUNTIME-179`; app-composed global `AsyncWorkModule` owns
      `StreamingExecutor` plus `DerivedJobRegistry`, publishes their narrow
      services and the existing Maintenance hook capability, and cancels
      generation-qualified queued/running/readback/apply work on
      `WorldWillBeDestroyed`. Engine never names the concrete module; omitted
      composition retains transfer collection followed by the asset tick.
- [ ] SceneEditing — re-scoped `RUNTIME-172`; one app-composed editor/document
      owner today, with document/history/selection/lookup/readback/gizmo state
      keyed or reset by active `WorldHandle` on switch and destruction; the
      implementation must split if any ADR-0026 cohesion axis diverges
- [ ] Camera — `RUNTIME-180`; global viewport/controller owner with
      world-qualified target/reset state; reference content stays app bootstrap
- [ ] ConfigControl — `RUNTIME-181`; global validated preview/commit owner for
      config and registered app sections
- [ ] AssetWorkflow — `RUNTIME-183`; global service/residency/import/bake owner
      whose borrowed scene/camera/selection handoffs are rebound or cleared by
      world identity; `RUNTIME-129` supplies the operational Vulkan bake proof
- [ ] EditorUi — `RUNTIME-182`; global optional adapter/host/contribution owner
      producing the single capture snapshot; Sandbox panel content stays
      app-owned
- [ ] Sandbox composition glue — re-scoped `RUNTIME-168`; app parts and default
      policies resolve the owners above without `Engine&`
- [ ] Explicit app lifecycle — `RUNTIME-184`; remove `IApplication`,
      `OnSimTick`/`OnVariableTick`, and every `Engine&` app behavior surface
      without adding a replacement framework
- [ ] Composition mechanism deletion test — `RUNTIME-185`; retain only
      behavior-backed lifecycle/setup/service/hook surface and remove
      test-only resolve/sim/DAG/provision branches after `RUNTIME-129` and
      explicit app lifecycle have both landed
- [ ] Residual Engine auxiliary surface — `RUNTIME-186`; remove the two
      re-exports and migrate remaining frame-pacing/render-extraction
      observation and input-action setup callers without absorbing a missed
      domain-owner correction or creating a generic facade
- [ ] Final exact Engine surface ratchet — `RUNTIME-187`; put implementation
      state behind PImpl and derive the exact allowed import/getter/type sets
      from the already-settled public API without semantic caller migration or
      a numerical budget

### Deferred mechanisms are not convergence blockers

- Extension-pass registration: reconsider on the first production pass that
  cannot be expressed by the closed built-in recipe vocabulary.
- Priority input-filter chain: reconsider on a second independent,
  simultaneous capture producer with conflicting claims.
- `InlineModule`/experiment template: reconsider after a real one-file
  lifecycle experiment, and share a template only after a second app repeats
  the bootstrap.
- `WorldSwitchModule`: reconsider on a real preview/secondary-world workflow
  with preparation/readiness/staged-switch policy.

## Design pressure-point status

1. **GPU-job participant lifecycle — resolved 2026-07-09.** `RUNTIME-137`
   retired at `Operational`. `JobService` owns the `GpuQueue` participant
   registry, delegates frame-command recording, drains completed transfers
   during Maintenance, and shuts participants down after the device-idle wait.
2. **World-scoped module state — open per extraction.** `ARCH-010` introduced
   the kernel `WorldRegistry`; it did not choose policy for selection, camera
   sets, undo history, or other domain state. Each extraction task must decide
   whether its durable composed state is world-scoped or global before exposing
   it through a narrow service; the rows above now record every decision.

## How this doc is used

- **Adding a runtime feature?** Use the knob-decision table; if it needs an
  `Engine` method, stop — it belongs to an app-composed responsibility.
- **Reviewing a runtime PR?** Check it does not regress a scorecard
  invariant (new domain-noun import, new `Engine::GetX()`, new direct
  dispatcher use, new `Engine&` pass-through).
- **Picking work?** The additive seams `ARCH-007`..`ARCH-011`, the
  `ARCH-012` ClusteringModule proof, retired `UI-034`, the `ARCH-006`
  Sandbox-content relocation, and the `RUNTIME-178` budget restoration are
  complete. Remaining domain rows proceed through the exact dependency graph
  recorded by `ARCH-014`.
