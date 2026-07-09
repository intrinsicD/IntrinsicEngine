# Kernel target state — the north star for runtime work

- **Status:** Living target document (not an ADR, not a task). Update the
  scorecard whenever a seam lands or a domain extracts; do not retire it.
- **Contract:** [ADR-0024](../adr/0024-kernel-module-architecture.md) (the
  *why* and the thirteen decisions; frozen). This doc is the *where we are
  vs. where we are going* (living).
- **Tracked by:** [`ARCH-014`](../../tasks/backlog/architecture/ARCH-014-kernel-convergence-tracking.md)
  (umbrella; stays open until the scorecard is all-green).
- **Read this before** adding anything to `Runtime.Engine` or introducing a
  new `Runtime.*Module`. See the [Feature Module Playbook](feature-module-playbook.md)
  for the per-feature procedure.

## The destination in one sentence

`Extrinsic.Runtime.Engine` becomes a **slim kernel** — frame loop, command
bus, event bus, JobService + worker pool, WorldRegistry, FrameGraph
scheduling, FrameRecipe activation + extension-pass slots, input capture
chain — and **everything with a domain noun in its name is a RuntimeModule**
composed by the app (ADR-0024 D9).

## The knob-decision guide — "where does my thing go?"

Before adding an `Engine` method or member, place the responsibility with
this table. If it does not fit a row, that is a signal to revisit ADR-0024,
**not** to add an `Engine` method.

| Your responsibility | Where it goes | Mechanism |
| --- | --- | --- |
| A user/UI action that *changes state* (import, save, bake, switch world, apply config, run an algorithm) | a **command** handled by a module | `Commands().Enqueue(XRequested{...})`; drained pre-sim (D5) |
| "When X happens, always do Y" (refresh a visualization after an attribute changes) | an **event reaction** in a module | `Events().Subscribe<XChanged>(...)` (D6); never a chain |
| Per-frame / per-substep data-dependent work (ECS query, extraction, a pump) | a **System** | `RegisterSimSystem(SimSystemDesc{Reads, Writes, ...})` (D1) |
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
2026-07-08 measurement (post-ARCH-007).

### Kernel seams exist (ADR-0024 D5–D11)

- [x] CommandBus + single pre-sim drain — `ARCH-007` (done 2026-07-08)
- [ ] EventBus, queued-only, two pumps — `ARCH-008`
- [ ] JobService, snapshot-in/result-out — `ARCH-009`
- [ ] WorldRegistry, deferred two-phase world ops — `ARCH-010`
- [ ] RuntimeModule contract + EngineSetup + ServiceRegistry — `ARCH-011`
- [ ] Extension-pass slot contract frozen (D10 validation items checked:
      splatting-lighting participation; order-dependent transparency)
- [ ] Input capture filter chain — `UI-034` / EditorUiModule extraction

### Kernel is slim (measurable)

> **Domain-import metric (allowlist).** The kernel's *permitted substrate*
> import prefixes are small and enumerable; everything else in
> `Runtime.Engine.cppm` is a domain import. Keep the allowlist in sync as
> seams land (add `Runtime.KernelEvents`/`JobService`/`WorldRegistry`/
> `Module`/`ServiceRegistry` as `ARCH-008`..`ARCH-011` create them):
>
> ```bash
> SUB='^import (Extrinsic\.Core\.|Extrinsic\.ECS\.Scene\.(Registry|Handle)|Extrinsic\.RHI\.|Extrinsic\.Platform\.|Extrinsic\.Graphics\.(Renderer|RenderFrameInput|FrameRecipe|RenderWorld)|Extrinsic\.Runtime\.(CommandBus|KernelEvents|JobService|WorldRegistry|Module|ServiceRegistry|RenderExtraction|RenderWorldPool))'
> grep -E '^import ' src/runtime/Runtime.Engine.cppm | grep -vcE "$SUB"   # domain imports (target 0; baseline 27)
> ```
>
> This inline grep is the **interim** metric. The authoritative, maintained
> counter is the `ARCH-014` ratchet checker
> (`tools/repo/check_kernel_convergence.py`, seeded there); wire the invariant
> flip and the `pr-fast` guard to that, not to the grep.


- [ ] `Runtime.Engine.cppm` import count ≤ 12 substrate modules
      (**baseline 45**)
- [ ] Domain (non-substrate) imports in `Runtime.Engine.cppm` = 0
      (**baseline 27**). Measure by **allowlist**, not a blocklist of names:
      count every `import` that is *not* kernel substrate (see the metric
      below). A name blocklist silently undercounts as new domain imports
      appear (e.g. `AssetIngestStateMachine`, `Asset.Service`,
      `Geometry.HalfedgeMesh.IO`); the allowlist cannot. Authoritative count
      is the `ARCH-014` ratchet checker once it lands; do not flip this row to
      0 on the interim grep alone.
- [ ] `Engine::GetX()` domain-facade accessors = 0 (kernel-service
      accessors only) (**baseline 13**)
- [ ] No `entt::dispatcher::trigger` or direct dispatcher use in module code
- [ ] No `Engine&` passed through any handler/module/setup surface (D13)
- [ ] `IApplication::OnSimTick` / `OnVariableTick` removed (**baseline: present**)

### Domains are modules (D9) — one row per extracted domain

- [ ] Clustering — `ARCH-012` (proving extraction; **not landed** —
      `Runtime.KMeans*` still live in `src/runtime/`, `ARCH-012` open/blocked;
      flipping this row also closes the seam `Operational` gate, so it must
      not be checked before `ARCH-012` retires)
- [ ] EditorUi (ImGui adapter, dockspace, panel registry, `Pass.ImGui`) —
      `ARCH-006` + `UI-034`
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

## Open design pressure points (decide before the owning seam freezes)

1. **GPU-job participant lifecycle** is richer than "submit a job": today's
   `RuntimeGpuJobParticipantDesc` has four callbacks (`RecordFrameCommands`
   every frame while in flight, `DrainCompletedTransfers`, `HasInFlightWork`,
   `ShutdownAfterDeviceIdle`). The JobService `GpuQueue` target (`RUNTIME-137`)
   must absorb that whole lifecycle, or it splits into an extension pass
   (records) + a job (computes). Pin the shape before `RUNTIME-137` lands.
2. **World-scoped module state:** selection, camera set, undo history are
   implicitly per-world today because there is one world. When `ARCH-010`
   introduces N worlds, decide per module whether its state is world-scoped
   or global. The single-world present is hiding this decision.

## How this doc is used

- **Adding a runtime feature?** Use the knob-decision table; if it needs an
  `Engine` method, stop — it is a module.
- **Reviewing a runtime PR?** Check it does not regress a scorecard
  invariant (new domain-noun import, new `Engine::GetX()`, new direct
  dispatcher use, new `Engine&` pass-through).
- **Picking work?** The unchecked seam rows are the ADR-0024 migration order
  (`ARCH-008` next); the unchecked domain rows are the extractions that
  follow each seam.
