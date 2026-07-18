# Kernel target state — the north star for runtime work

- **Status:** Living target document (not an ADR, not a task). Update the
  scorecard whenever a seam lands or a domain extracts; do not retire it.
- **Contract:** [ADR-0024](../adr/0024-kernel-module-architecture.md) (the
  *why* and the thirteen decisions; frozen). This doc is the *where we are
  vs. where we are going* (living).
- **Module-scope refinement:**
  [ADR-0026](../adr/0026-runtime-module-scope-by-consumer-contract.md) decides
  when demonstrated runtime responsibilities share one composed module or
  split; it defines no family taxonomy and does not ratify `IRuntimeModule`.
- **Tracked by:** [`ARCH-014`](../../tasks/active/ARCH-014-kernel-convergence-tracking.md)
  (umbrella; stays open until the scorecard is all-green).
- **Read this before** adding anything to `Runtime.Engine` or introducing a
  new `Runtime.*Module`. See the [Feature Module Playbook](feature-module-playbook.md)
  for the per-feature procedure.

> **Composition-mechanism decision hold (2026-07-18).** The reconciliation
> audit found that several literal rows below name zero-consumer mechanisms or
> conflate a domain-free Engine outcome with implementing `IRuntimeModule`.
> [`ARCH-016`](../../tasks/backlog/architecture/ARCH-016-right-size-runtime-composition-target.md)
> owns ADR-0027 and the evidence-backed scorecard correction. Until it retires,
> do not create extension registries, input-filter chains, `InlineModule`
> builders, or one-wrapper-per-service tasks merely to flip these boxes.

## The destination in one sentence

`Extrinsic.Runtime.Engine` becomes a **slim kernel** — frame loop, command
bus, event bus, JobService + worker pool, WorldRegistry, FrameGraph
scheduling, FrameRecipe activation + extension-pass slots, input capture
chain — and **everything with a domain noun in its name is a RuntimeModule**
composed by the app (ADR-0024 D9).

ADR-0024 D9 decides **kernel versus module**. ADR-0026 then decides **one
module versus several** from app lifecycle, durable-state scope,
dependency/cancellation/commit ownership, and consumer reactions. A family
name, matching result shape, extra service, or different execution mechanism
alone is never the boundary.

## The knob-decision guide — "where does my thing go?"

Before adding an `Engine` method or member, place the responsibility with
this table. If it does not fit a row, that is a signal to revisit ADR-0024,
**not** to add an `Engine` method.

| Your responsibility | Where it goes | Mechanism |
| --- | --- | --- |
| A user/UI action that *changes state* (import, save, bake, switch world, apply config, run an algorithm) | a **command** handled by a module | `Commands().Enqueue(XRequested{...})`; drained pre-sim (D5) |
| "When X happens, always do Y" (refresh a visualization after an attribute changes) | an **event reaction** in a module | `Events().Subscribe<XChanged>(...)` (D6); never a chain |
| Per-frame / per-substep data-dependent work (ECS query, extraction, a pump) | a **System** | `RegisterSimSystem(SimSystemDesc{.WaitForSignals, .EmitSignals, .Declare, ...})`; named signals set causal order, then `Declare` supplies Read/Write hazards (D1) |
| Background multi-frame compute (remesh, k-means, bake, readback) | a **job** | `Jobs().Submit(JobDesc{...})`; snapshot in, result committed at a pump (D8) |
| A new render technique (overlay, fullscreen analysis, compute fill) | an **extension pass** at a slot | `RegisterExtensionPass(...)` (D10) |
| Deciding whether a click belongs to UI or the viewport | an **input capture filter** | `RegisterInputCaptureFilter(...)` (D11) |
| A keybinding/chord that triggers an action | an **input action** | `RegisterInputAction(...)` (usually enqueues a command) |
| Always-present infrastructure another module needs (asset service, GPU cache) | a **Resolve-phase service** | `Services.Provide<T>()` / `Require<T>()` (D3) |
| A *domain-specific* extension point (post-import processors, camera controllers, visualization adapters, editor panels) | a **module-provided service**, **not** a kernel knob | the owning module `Provide`s its own registry; others `Find` it |
| Durable state other modules query (selection, undo history, camera set) | **module-owned state**, exposed as a service | `Services.Provide<TModule>()`; world-scope decided at ADR-0024 D2 |
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
record. After `RUNTIME-178`, the checked 2026-07-16 exact-policy snapshot is
42 / 21 with 31 distinct public `Engine::GetX()` names and two existing
re-exports. The fixed reference remains historical comparison evidence; the
current snapshot is below each reference budget and carries no temporary debt.

### Kernel seams exist (ADR-0024 D5–D11)

- [x] CommandBus + single pre-sim drain — `ARCH-007` (done 2026-07-08)
- [x] EventBus, queued-only, two pumps — `ARCH-008` (done 2026-07-09)
- [x] JobService, snapshot-in/result-out — `ARCH-009` (done 2026-07-09)
- [x] WorldRegistry, deferred two-phase world ops — `ARCH-010` (done 2026-07-09)
- [x] RuntimeModule contract + EngineSetup + ServiceRegistry — `ARCH-011`
      (done 2026-07-09)
- [ ] Extension-pass slot contract frozen (D10 validation items checked:
      splatting-lighting participation; order-dependent transparency)
- [x] Input capture filter chain — `UI-034` (done 2026-07-13 at
      `CPUContracted`; one end-of-editor-frame snapshot gates viewport input)

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


- [ ] `Runtime.Engine.cppm` import count ≤ 12 substrate modules
      (**baseline 45; 2026-07-13 reference 43; 2026-07-16 checked snapshot
      42**)
- [ ] Domain (non-substrate) imports in `Runtime.Engine.cppm` = 0
      (**baseline 27; 2026-07-13 reference 23; 2026-07-16 checked snapshot 21**).
      Measure by
      **allowlist**, not
      a blocklist of names:
      count every `import` that is *not* kernel substrate (see the metric
      above). A name blocklist silently undercounts as new domain imports
      appear (e.g. `AssetIngestStateMachine`, `Asset.Service`,
      `Geometry.HalfedgeMesh.IO`); the allowlist cannot.
- [ ] `Engine::GetX()` domain-facade accessors = 0 (kernel-service
      accessors only) (**baseline estimate 13 domain facades; 2026-07-16 guard
      snapshots all 31 public getter names**)
- [ ] No `entt::dispatcher::trigger` or direct dispatcher use in module code
- [ ] No `Engine&` passed through any handler/module/setup surface (D13)
- [ ] `IApplication::OnSimTick` / `OnVariableTick` removed (**baseline: present**)

### Domains are modules (D9) — one row per extracted domain

- [x] Clustering — `ARCH-012` (done 2026-07-08 at `Operational`; Sandbox
      composes `Runtime::ClusteringModule`, and `Runtime.Engine.cppm` / `.cpp`
      contain no `KMeans` or `Runtime.ClusteringModule` tokens)
- [ ] EditorUi (ImGui adapter, dockspace, panel registry, `Pass.ImGui`) —
      retired `UI-034` supplies the generic registration/capture/widget seam;
      retired `ARCH-006` moved Sandbox content and presentation into the app;
      remaining runtime ImGui adapter/bridge residue still prevents closure
- [ ] AssetImport pipeline — `RUNTIME-147`
- [ ] SceneDocument (save/load/new/close) — `RUNTIME-148`
- [ ] ConfigControl (recipe activation, hot-config apply) — `RUNTIME-149`
- [ ] WorldSwitch policy (preview worlds, staged switch) — new module (D2)
- [ ] Camera controllers — module
- [ ] Selection + gizmo + undo history — module(s); command-history hook (D5)
- [ ] Object-space normal bake — module (`RUNTIME-129` migrates onto
      JobService + a standing event reaction)

### The InlineModule research lane (D12)

- [ ] `InlineModule` builder ships (one-file, one-registration experiments)
- [ ] An experiment-app template exists that composes kernel + N modules

## Design pressure-point status

1. **GPU-job participant lifecycle — resolved 2026-07-09.** `RUNTIME-137`
   retired at `Operational`. `JobService` owns the `GpuQueue` participant
   registry, delegates frame-command recording, drains completed transfers
   during Maintenance, and shuts participants down after the device-idle wait.
2. **World-scoped module state — open per extraction.** `ARCH-010` introduced
   the kernel `WorldRegistry`; it did not choose policy for selection, camera
   sets, undo history, or other domain state. Each extraction task must decide
   whether its durable module state is world-scoped or global before exposing
   it through `RuntimeModule` services; `ARCH-014` tracks those decisions.

## How this doc is used

- **Adding a runtime feature?** Use the knob-decision table; if it needs an
  `Engine` method, stop — it is a module.
- **Reviewing a runtime PR?** Check it does not regress a scorecard
  invariant (new domain-noun import, new `Engine::GetX()`, new direct
  dispatcher use, new `Engine&` pass-through).
- **Picking work?** The additive seams `ARCH-007`..`ARCH-011`, the
  `ARCH-012` ClusteringModule proof, retired `UI-034`, the `ARCH-006`
  Sandbox-content relocation, and the `RUNTIME-178` budget restoration are
  complete. Remaining domain rows proceed through scoped extraction tasks.
