# ADR 0024: Kernel/module runtime architecture and communication contract

- **Status:** Accepted
- **Date:** 2026-07-08
- **Owners:** Runtime / Architecture
- **Amended by:** [ADR-0027](0027-right-sized-runtime-composition.md)
- **Related tasks:** `ARCH-007`..`ARCH-013` (kernel seams, proving extraction,
  post-seam collision re-review), `RUNTIME-146`..`RUNTIME-151` (Engine
  decomposition), `ARCH-006` (Sandbox editor content), `UI-034` (editor window
  contribution), `RUNTIME-137` (async GPU readback / `JobService` `GpuQueue`),
  `CORE-005`..`CORE-008` (task-system hardening)
- **Related docs:** [ADR-0003](0003-ideal-runtime-architecture.md) (staged
  frame products; this ADR refines it),
  [`docs/architecture/ground-up-redesign-vision.md`](../architecture/ground-up-redesign-vision.md),
  [`docs/architecture/feature-module-playbook.md`](../architecture/feature-module-playbook.md),
  [`docs/architecture/runtime-subsystem-boundaries.md`](../architecture/runtime-subsystem-boundaries.md)

## Context

`Extrinsic.Runtime.Engine` is a god object: ~7.4k lines across
`Runtime.Engine.cppm`/`.cpp`, with dozens of imports including domain machinery
(`ImGuiAdapter`, `GizmoInteraction`, `SelectionController`,
`ObjectSpaceNormalBakeQueue`, `KMeans*`). Domain features keep accreting
*inside* the runtime because there is no registration seam to put them behind.
ADR-0003 already names the target frame shape (immutable staged products
`W → R → F → G`); the vision doc prioritizes decomposition. What was missing
is the **composition and communication contract**: what the slim engine owns,
what a module is, and exactly how UI, commands, events, background jobs, and
worlds interact. This ADR records that contract as thirteen decisions (D1–D13)
resolved in a full design review on 2026-07-08.

ADR-0027 (accepted 2026-07-18) right-sizes the implementation mechanism after
the first proving extraction. It preserves the domain-free Engine, explicit
app composition, state-scope, queued communication, and no-`Engine&` outcomes,
but amends D1/D2/D3/D9/D10/D11/D12/D13 where this record prescribed an
unproven wrapper, registry, chain, schedule, or template. The amendment text
below is authoritative where it differs from the original decision record.

## Decision

### D1 — Vocabulary

**ADR-0027 amendment.** `RuntimeModule` is first a logical,
[ADR-0026](0026-runtime-module-scope-by-consumer-contract.md)-cohesive
app-composed responsibility, not a requirement to create one
`IRuntimeModule` wrapper per domain noun. The current interface remains the
lean type-erased app-to-runtime lifecycle boundary while concrete extractions
prove it. Registered frame hooks or simulation systems are accepted only when
a production responsibility uses them; a general schedule is not itself a
target outcome.

Two composition concepts, not three:

- **System** — a real per-frame/per-substep contribution whose data hazards
  must join frame scheduling. Stable names and causal signals are introduced
  only when interacting production systems need them; registration order is
  never a hidden dependency.
- **Runtime module** — the logical app-facing unit of composition selected by
  ADR-0026 cohesion. The current `IRuntimeModule` is one mechanism for this
  unit, not its definition.
- **Plugin** (dynamic code loading) remains explicitly rejected. Hot-reload
  data (recipes, shaders, command payloads), never code.

### D2 — Worlds: kernel mechanism, module policy

**ADR-0027 amendment.** `WorldRegistry` and `WorldHandle` remain kernel
substrate. A separate `WorldSwitchModule` is deferred because the repository
has no production preview/secondary-world workflow or
`RequestSetActiveWorld()` caller. The first real workflow that needs
preparation, readiness, or staged-switch policy may introduce a cohesive
behavior owner; the wrapper is not required beforehand.

The kernel owns a **WorldRegistry**: N `ECS::SceneRegistry` instances behind
`WorldHandle`s, world lifetimes, and the single *active world* as plain state.
World mutations (`RequestSetActiveWorld`, `RequestDestroyWorld`) are deferred
requests applied only at the frame boundary (Maintenance phase). World #0
exists from boot so frame 0 is never ambiguous. `Registry&`/`WorldHandle` is
always an explicit parameter — never a global.

No switch-policy owner is created until a production preview/secondary-world
workflow needs preparation, readiness, or staged activation. The registry
mechanism remains usable by that future behavior owner.

### D3 — Module communication

**ADR-0027 amendment.** Typed `Provide`/`Find`, duplicate/late-provision
rejection, and registry locking are the proven service-registry core.
Two-phase `Require`/`OnResolve` and their diagnostics remain subject to the
ADR-0027 deletion test because no production module currently consumes them:
the bounded extraction program must demonstrate a real cross-module
dependency or `RUNTIME-185` removes them. Built-in kernel capabilities are
passed through `EngineSetup`, not redundantly justified as services.

Default channel: **commands, events, and ECS components**. Typed
`ServiceRegistry::Provide`/`Find` is the escape hatch for optional or
order-independent synchronous discovery and stays locked after boot.
`Require`/`OnResolve` survives only when a production cross-owner dependency
proves the two-phase protocol. Hidden ownership, mutable peer backreferences,
and ordering by registration remain forbidden; the app may instead pass an
explicit narrow non-owning construction capability with a declared lifetime.
For real interacting Systems, named signals establish causal direction and
`Read`/`Write` preserves RAW/WAR/WAW safety.

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

**ADR-0027 amendment.** The litmus test is an ownership test, not a naming or
wrapper-count test. The kernel owns only the enumerated machinery required to
run a frame. A durable domain responsibility is explicitly composed by the app
outside `Engine`, with its global or world-qualified state scope recorded and
without a domain facade on `Engine`; its concrete C++ shape follows ADR-0026
cohesion and present lifecycle/communication needs. A domain noun does not by
itself justify a new interface, registry, or one-service module.

Kernel substrate is the frame loop plus the command/event/job/world/frame-graph
and recipe machinery every app needs. Clustering, asset workflow, scene
editing, camera, config control, and editor UI are the current app-composed
responsibility hypotheses; each implementation child must verify its actual
cohesion and state scope.

### D10 — Rendering extensibility: closed recipe-driven core

**ADR-0027 amendment.** The accepted present contract is a closed built-in
pass vocabulary selected by recipe data, with fail-closed schema, capability,
resource, and binding validation. There is no production extension-pass
registration API or consumer, so extension registration and insertion slots
are deferred. Reconsider them when the first production app/module pass cannot
be expressed by an existing built-in recipe gate and supplies a concrete
resource-ordering contract; splatting and order-dependent transparency are
validation cases for that real proposal, not blockers today.

`FrameRecipe` stays data. The current visibility → geometry → lighting → post
→ present vocabulary remains closed and kernel-validated. A future production
pass that cannot fit a built-in kind must bring its concrete resource,
ordering, capability, and backend-evidence contract before any insertion slot
is added.

### D11 — UI is app-composed; capture is a kernel primitive

**ADR-0027 amendment.** UI remains app-composed domain behavior outside the
kernel. The proven kernel primitive is one coherent
frame-loop-owned capture value that resets once at frame start. Every
ephemeral hook context borrows the same value by reference. The optional
EditorUi owner brackets the application tick through ordered `UiBegin`,
`UiBuild`, and `UiEndCapture` hooks, then writes the value only after the
adapter's `EndFrame`; later viewport behavior, any later hooks, and kernel
input-action dispatch consume it. The value clears to
unclaimed every frame, so omitting EditorUi remains deterministic. There is no
priority filter chain or ImGui import in Engine. Introduce arbitration only
after a second simultaneous, independent capture producer demonstrates a
conflict that explicit value composition cannot resolve. Panel content remains
app-owned; diagnostics stay in logs/telemetry.

### D12 — Application = parts list; seams-first migration

**ADR-0027 amendment; implemented by `RUNTIME-184`.** The app is now an
explicit parts list and the `IApplication` interface plus its unrestricted
simulation/variable-frame callbacks are removed. `InlineModule` and
an experiment-app template are deferred: the first real one-file experiment
with runtime lifecycle may justify a local convenience, and a shared template
requires a second production/research app that repeats the same bootstrap.
Ordinary explicit named composition is the default until then.

An app is `main()` + explicit composed responsibilities + initial world
content + initial `FrameRecipe`. The additive `ARCH-007`..`ARCH-011` seams and
`ARCH-012` Clustering extraction and the `RUNTIME-184` lifecycle cutover are
retired. Clustering proved app-owned
lifecycle, stable naming, command/job/event flow, service provision/discovery,
and reverse shutdown; it did **not** prove `Require`, non-no-op `OnResolve`,
sim-system registration, or frame-hook registration. `RUNTIME-179`..`187`
finish the behavior-owning extractions, mechanism deletion test, residual API
migration, and final representation/checker ratchet. `CommandSequence`, an
inline builder, and an experiment template land
only when their respective first consumers appear.

### D13 — No god-handles: contexts carry capabilities

**ADR-0027 amendment.** The no-`Engine&` outcome is unchanged.
`EngineSetup` remains the narrow composition capability context, but it must
not accumulate registrars without production consumers. The final convergence
ratchet removes or narrows unproven setup/service/schedule surface rather than
retaining it for hypothetical modules.

`Engine&` is never passed through the module/handler surface. Contexts
(`CommandContext`, frame-hook contexts) carry the narrow capability set
(`ActiveWorld`, `Commands`, `Events`, `Jobs`, `Worlds`, correlation ID).
Registration receives an `EngineSetup` (not "KernelSetup" — "kernel" is a
documentation role word, not code vocabulary; the class stays `Engine`).
Shutdown is a built-in `QuitRequested` command. The target contains no
`Engine&` in module, handler, setup, or app lifecycle surfaces. `RUNTIME-184`
removed the legacy application object; a concrete app may use
`Engine::BeginShutdown()` to announce/quiesce first, release app-owned state
through narrow world/service capabilities, and then call `Engine::Shutdown()`.

## Consequences

- Positive: the Engine interface drops from its measured domain-heavy baseline
  to substrate-only;
  every feature becomes removable; command streams give deterministic
  replay/repro; experiments compose a minimal parts list instead of paying the
  god object's build and coupling costs.
- Trade-offs: one frame of latency on discrete UI commands (accepted);
  snapshot copies for background jobs (accepted — versioned results are a
  feature); queued-only events mean "I fired it" never implies "it already
  happened" (accepted; two-phase teardown must be designed in).
- Risks: hook-vocabulary proliferation (mitigated by D9 and keeping the
  `EngineSetup` surface constrained to live production consumers).
- Follow-up tasks: `ARCH-007`..`ARCH-012` seeded with this ADR and retired
  through the first proving extraction; `ARCH-013` retired the post-seam
  collision re-review; ADR-0027 and `RUNTIME-179`..`187` own the right-sized
  convergence program.

## Open questions (parked deliberately, none blocks the seams)

1. **Determinism/replay policy** — command streams are replayable only under
   a stated fixed-timestep + seeded-RNG discipline; needs its own decision.

Scene persistence and the asset-system boundary are no longer open questions:
retired `RUNTIME-148`/`RUNTIME-147` established their behavior, and
ADR-0027's `RUNTIME-172`/`RUNTIME-183` children own their app-composed
placement.

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
  surface too large for the current production need; the closed built-in
  recipe vocabulary covers the demonstrated workload.
- **Renaming `Engine` to `Kernel`** — rejected: churn without behavior;
  "kernel" stays a prose role word (D13).

## Validation

- Each seam task (`ARCH-007`..`ARCH-011`) closed `CPUContracted` with
  headless contract tests under the default CPU gate.
- `ARCH-012` (ClusteringModule extraction) closed the Operational proof:
  Sandbox composes `Extrinsic.Runtime.ClusteringModule`, a `RunKMeans` command
  snapshots active-world geometry into `JobService`, completion commits labels
  through kernel event pump B, and `ClusterLabelsChanged` drives the standing
  visualization refresh reaction. `Runtime.Engine.cppm` and
  `Runtime.Engine.cpp` contain no `KMeans` or `Runtime.ClusteringModule`
  imports/surface tokens; the remaining Vulkan queue move is owned by
  `RUNTIME-137`. This proves the command/job/event and app-owned lifecycle
  path, not the then-unused `Require`/`OnResolve`, sim-system, frame-hook, or
  extension mechanisms; ADR-0027 owns their deletion tests.
- `ARCH-013` (post-seam collision re-review) retired 2026-07-08: every
  front-matter-gated row received a dated confirmation/re-scope note, every
  audit-only row received an unchanged/re-scoped/re-gated decision, `RUNTIME-129`
  was re-gated on `RUNTIME-137` for the `JobService` `GpuQueue` substrate, and
  the backlog sweep found no additional task prescribing rejected ADR-0024
  mechanisms without a recorded decision.
- The layering gate (`tools/repo/check_layering.py --root src --strict`) and
  module inventory regeneration verify the import-surface shrinkage claim as
  extractions land.
- Ongoing convergence toward this contract is tracked by the living
  [`docs/architecture/kernel-target-state.md`](../architecture/kernel-target-state.md)
  scorecard (greppable kernel-slimness invariants + per-domain module rows),
  owned by the umbrella task
  [`ARCH-014`](../../tasks/active/ARCH-014-kernel-convergence-tracking.md).
  `ARCH-007` retired 2026-07-08 (first seam; CommandBus).
