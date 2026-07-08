# ADR 0024: Kernel/module runtime architecture and communication contract

- **Status:** Accepted
- **Date:** 2026-07-08
- **Owners:** Runtime / Architecture
- **Related tasks:** `ARCH-007`..`ARCH-012` (kernel seams + proving extraction),
  `RUNTIME-146`..`RUNTIME-151` (Engine decomposition), `ARCH-006` (Sandbox
  editor content), `UI-034` (editor window contribution), `RUNTIME-137`
  (async GPU readback), `CORE-005`..`CORE-008` (task-system hardening)
- **Related docs:** [ADR-0003](0003-ideal-runtime-architecture.md) (staged
  frame products; this ADR refines it), 
  [`docs/architecture/ground-up-redesign-vision.md`](../architecture/ground-up-redesign-vision.md),
  [`docs/architecture/feature-module-playbook.md`](../architecture/feature-module-playbook.md),
  [`docs/architecture/runtime-subsystem-boundaries.md`](../architecture/runtime-subsystem-boundaries.md)

## Context

`Extrinsic.Runtime.Engine` is a god object: ~7.4k lines across
`Runtime.Engine.cppm`/`.cpp`, 36 module imports including domain machinery
(`ImGuiAdapter`, `GizmoInteraction`, `SelectionController`,
`ObjectSpaceNormalBakeQueue`, `KMeans*`). Domain features keep accreting
*inside* the runtime because there is no registration seam to put them behind.
ADR-0003 already names the target frame shape (immutable staged products
`W → R → F → G`); the vision doc prioritizes decomposition. What was missing
is the **composition and communication contract**: what the slim engine owns,
what a module is, and exactly how UI, commands, events, background jobs, and
worlds interact. This ADR records that contract as thirteen decisions (D1–D13)
resolved in a full design review on 2026-07-08.

## Decision

### D1 — Vocabulary

Two composition concepts, not three:

- **System** — a per-frame/per-substep schedulable unit with declared data
  dependencies (`Read`/`Write` TypeTokens + named signals), exactly as
  `Runtime.EcsSystemBundle` already registers FrameGraph passes. ECS-query
  systems are the common flavor; extraction, readback pumps, and bake pumps
  are Systems too.
- **RuntimeModule** — the app-facing unit of composition: binds command
  handlers, UI contributions, the Systems it registers, and its owned state.
  Named `RuntimeModule` to avoid collision with C++23 modules.
- **Plugin** (dynamic code loading) is explicitly rejected. Hot-reload data
  (recipes, shaders, command payloads), never code.

### D2 — Worlds: kernel mechanism, module policy

The kernel owns a **WorldRegistry**: N `ECS::SceneRegistry` instances behind
`WorldHandle`s, world lifetimes, and the single *active world* as plain state.
World mutations (`RequestSetActiveWorld`, `RequestDestroyWorld`) are deferred
requests applied only at the frame boundary (Maintenance phase). World #0
exists from boot so frame 0 is never ambiguous. `Registry&`/`WorldHandle` is
always an explicit parameter — never a global.

A **WorldSwitchModule** owns the *policy*: creating/preparing preview worlds,
readiness tracking, and issuing the switch command. There is no `ISceneModule`
interface: one implementation means no interface, and an app-side scene
provider would let policy swap the kernel's substrate mid-frame.

### D3 — Module communication

Default channel: **commands, events, and ECS components**. Escape hatch for
always-present synchronous infrastructure: a two-phase **ServiceRegistry** —
`Provide<T>()` during `OnRegister`, `Require<T>()`/`Find<T>()` during
`OnResolve`; a missing `Require` is a **boot error naming both modules**,
never a null-deref at frame 400. Direct module-to-module pointers are
forbidden. Ordering between modules comes from declared data dependencies and
the two-phase startup, never from registration order.

### D4 — World rendering topology

Hard switch: exclusive rendering, scalar active world, non-active worlds fully
frozen (no ticking). Cheap future-proofing retained: render-world extraction
and pass data-feeding take an explicit world handle, so simultaneous preview
panes remain a refactor, not a rewrite.

### D5 — Commands

Plain-data payloads (no captured references into UI state), correlation IDs,
thread-safe enqueue from any thread/phase, execution **main-thread-only at a
single drain point between platform input and simulation**. Everything sim and
render observe in a frame is post-command, pre-tick: one mutation window,
deterministic, replayable. Discrete UI actions accept one frame of latency.
Continuous edits (gizmo drags, slider scrubs) bypass the bus: direct component
mutation during the variable-tick/UI phase, one undo-granularity command
emitted on release (feeds `EditorCommandHistory`). A command with no
registered handler fails loudly (fail-closed). Undoability is module policy:
commands may declare an inverse payload; the bus notifies a history hook after
successful execution.

### D6 — Standing reactions are events; call-site scripts are CommandSequences

Fixed follow-through ("attribute changed → refresh visualization") is wired as
an **event reaction**, never a chain — so every mutation path triggers it.
**CommandSequence** (not "recipe" — `FrameRecipe` owns that word) is a
call-site-composed list of command payloads with a per-sequence blackboard;
links execute strictly sequentially but may park on a `JobToken` and resume at
a later drain point. Failure = abort remainder + `ChainFailed` diagnostic with
correlation ID and link index. No rollback, no continue-on-failure flag until
a real batch case exists. Deferred until its first customer (repro scripts /
experiment automation); a serialized sequence is a reproducible experiment.

### D7 — Events: queued-only, two pumps, no `trigger`

Synchronous dispatch is not part of the API. Two main-thread pump points per
frame: post-drain (command effects visible before sim) and post-sim
(completions reach modules before extraction/UI). Events fired during a pump
deliver at the next pump — cascades are bounded, never stack-shaped. Worker
threads publish into a thread-safe inbox merged at the next pump; workers
never touch the dispatcher. Consequence, by construction: **all teardown is
two-phase** — announce at a pump (`WorldWillBeDestroyed`, module shutdown,
engine quit), destroy at a later defined boundary.

### D8 — Two-tier concurrency; jobs never touch live worlds

One shared worker pool, two tiers:

- **FrameGraph** — within-substep, deterministic, touches the live world under
  declared Read/Write.
- **JobService** (kernel) — multi-frame background work under an iron rule:
  *snapshot in, self-contained result out, main-thread commit at a pump*.
  Jobs never hold references into a live world. Completion publication is
  owned by the JobService: workers deposit results into the service and never
  publish completion events themselves; the service drains results on the
  main thread, drops those whose token is cancelled or whose scope world is
  being torn down, and publishes completion events only for survivors. Jobs
  carry cancellation scoped to a `WorldHandle`; two-phase world teardown
  cancels them, and a cancelled result — even one already finished on a
  worker — is dropped whole at this gate, never half-applied. One submission API
  with execution targets `CpuPool | GpuQueue`; the GPU target rides the
  GPU-job-participant frame contract internally. Long computations produce new
  versioned results (e.g. new geometry versions) — enabling A/B compare, undo,
  and reference-backend parity — rather than mutating inputs in place.
  Live-world locking/checkout semantics are rejected.

### D9 — The kernel/module litmus test

> Needed to run a frame → kernel. Has a domain noun in its name → module.

Kernel: frame loop, CommandBus, EventBus, JobService + worker pool,
WorldRegistry, FrameGraph seam, FrameRecipe activation + extension slots,
input capture chain. Modules: clustering, texture baking, asset import
pipeline, editor UI, world-switch policy, selection/gizmos — everything with a
domain noun, including today's kernel residents `KMeans*`,
`ObjectSpaceNormalBakeQueue`, `SelectionController`, `GizmoInteraction`.

### D10 — Rendering extensibility: closed core, extension slots

`FrameRecipe` stays data. The core skeleton (visibility → geometry → lighting
→ post → present) stays a closed, kernel-verified pass set. Modules register
**extension passes** at declared insertion slots (`AfterGBuffers`,
`AfterLighting`, `OverlayPreImGui`, off-critical-path `Compute`), implementing
the same internal pass interface core passes use: declared reads/writes on
named frame resources, RenderWorld + RHI command interface only — no `Vk*`
types, no live ECS. Recipes reference extension passes by registered ID next
to core kinds. Promoting later to a fully open graph is unsealing a
restriction, not a rewrite.

**Open validation items (must be checked before the slot contract freezes):**
point splatting that needs lighting participation; order-dependent
transparent/volumetric debug rendering.

### D11 — UI is a module; capture is a kernel primitive

The kernel mentions UI zero times. An **EditorUiModule** owns the ImGui
adapter/lifecycle, dockspace/layout, the panel-contribution registry, and
`Pass.ImGui` as an ordinary extension pass. Modules register panel descriptors
+ draw callbacks (optional dependency: contribute-if-present via `Find`);
panels emit commands only. The kernel keeps one primitive: the **input capture
filter chain** (priority-ordered claim filters; the EditorUiModule claims
events when ImGui wants capture; unclaimed events flow to input actions).
Recorded default: no always-on engine UI; diagnostics stay in logs/telemetry.

### D12 — Application = parts list; seams-first migration

An app is `main()` + a module manifest + initial world content + initial
FrameRecipe. All behavior — including Sandbox editor defaults — is modules.
`IApplication::OnSimTick`/`OnVariableTick` are deprecated and die at the end
of migration; an **InlineModule builder** keeps experiments at
one-file-one-registration (a real module underneath: scheduled, removable,
headless-testable).

Migration is strangler-style, seams before extractions, Sandbox green
throughout: ① CommandBus (`ARCH-007`) ② EventBus wrapper (`ARCH-008`)
③ JobService (`ARCH-009`) ④ WorldRegistry (`ARCH-010`) ⑤ RuntimeModule
contract + ServiceRegistry (`ARCH-011`) — all additive inside the current
Engine — then ⑥ **ClusteringModule as the proving extraction** (`ARCH-012`,
exercises every seam), ⑦ EditorUiModule (`ARCH-006`, `UI-034`), then
`RUNTIME-146`..`151` as further extractions onto the same seams, ⑧ tick
removal + experiment template. `CommandSequence` lands when its first real
customer appears.

### D13 — No god-handles: contexts carry capabilities

`Engine&` is never passed through the module/handler surface. Contexts
(`CommandContext`, frame-hook contexts) carry the narrow capability set
(`ActiveWorld`, `Commands`, `Events`, `Jobs`, `Worlds`, correlation ID).
Registration receives an `EngineSetup` (not "KernelSetup" — "kernel" is a
documentation role word, not code vocabulary; the class stays `Engine`).
Shutdown is a built-in `QuitRequested` command. `Engine&` appears in exactly
one place: `main()`.

## Consequences

- Positive: the Engine interface drops from 36 imports to substrate-only;
  every feature becomes removable; command streams give deterministic
  replay/repro; experiments compose a minimal parts list instead of paying the
  god object's build and coupling costs; the `RUNTIME-146`..`151` backlog gets
  the seams it currently lacks.
- Trade-offs: one frame of latency on discrete UI commands (accepted);
  snapshot copies for background jobs (accepted — versioned results are a
  feature); queued-only events mean "I fired it" never implies "it already
  happened" (accepted; two-phase teardown must be designed in).
- Risks: hook-vocabulary proliferation (mitigated by D9 and keeping the
  `EngineSetup` surface small); extension-slot contract freezing before the
  two D10 validation items are checked (tracked in the slot-contract task).
- Follow-up tasks: `ARCH-007`..`ARCH-012` seeded with this ADR.

## Open questions (parked deliberately, none blocks the seams)

1. **Scene persistence** — what serializes a world (interacts with D8's
   versioned geometry); owner: `RUNTIME-148` follow-up work.
2. **Determinism/replay policy** — command streams are replayable only under
   a stated fixed-timestep + seeded-RNG discipline; needs its own decision.
3. **Asset-system boundary** — `Asset.Service` traffic in Engine likely
   becomes a Resolve-phase service + import module; decided during
   `RUNTIME-147`.

## Alternatives Considered

- **`ISceneModule` / app-owned worlds** — rejected: interface with one
  implementation; per-frame virtual "current scene" call; mid-frame swap
  hazard; kernel would depend on a module for its own substrate.
- **JobsModule owning the worker pool** — rejected: the kernel is itself a
  customer; the FrameGraph shares the pool; world-teardown cancellation would
  invert the dependency.
- **Immediate (`trigger`) event dispatch** — rejected: unbounded mid-phase
  recursion, thread-unsafe from workers, two delivery semantics to reason
  about.
- **Second command drain after the variable tick** — rejected: mutates state
  the sim never saw that frame; two mutation windows with different
  visibility rules destroy replayability.
- **Chain rollback/compensation** — rejected: saga machinery; undo history
  covers user-visible reversal; atomic staging covers partial physical state.
- **Live-world locking for background jobs** — rejected: deadlock/priority
  tar pit; ends the determinism story.
- **Fully open render graph now** — rejected: barrier/lifetime validation
  surface too large for one developer; slots cover the research workload.
- **Renaming `Engine` to `Kernel`** — rejected: churn without behavior;
  "kernel" stays a prose role word (D13).

## Validation

- Each seam task (`ARCH-007`..`ARCH-011`) closes `CPUContracted` with
  headless contract tests under the default CPU gate.
- `ARCH-012` (ClusteringModule extraction) is the Operational proof: every
  seam exercised end-to-end in Sandbox composition, `KMeans*` modules gone
  from `src/runtime/` kernel surface.
- The layering gate (`tools/repo/check_layering.py --root src --strict`) and
  module inventory regeneration verify the import-surface shrinkage claim as
  extractions land.
