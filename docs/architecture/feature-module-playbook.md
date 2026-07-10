# Feature Module Playbook (Refactor + New Development)

This playbook defines the **default way** to add and refactor runtime features without growing accidental coupling.

Use it when introducing anything like `Runtime.SomethingModule.cppm`, geometry tooling, streaming jobs, or render integrations.

> **Target state first.** Before adding anything to `Runtime.Engine` or a new
> `Runtime.*Module`, read [`kernel-target-state.md`](kernel-target-state.md)
> and use its knob-decision table to place the responsibility. The kernel is
> converging to a slim seam-only core ([ADR-0024](../adr/0024-kernel-module-architecture.md),
> tracked by [`ARCH-014`](../../tasks/backlog/architecture/ARCH-014-kernel-convergence-tracking.md)):
> if your feature wants a new `Engine` method, that is the signal it is a
> module, a command, an event, or a service — not kernel surface.

---

## 0) Minimal-feature floor

Not every research probe needs a full vertical slice on day one. A one-caller,
data-driven feature with no backend split, no async/off-main-thread execution,
and no durable runtime state may start as a plain parameter struct plus a free
function in the owning lower layer.

Grow it into the full feature contract when any of these appears:

- A second caller needs the behavior.
- The feature gains a CPU/GPU/backend variant split.
- Work moves off the main thread or into a scheduled runtime lane.
- The feature needs persisted config, command routing, UI control, or
  telemetry-backed diagnostics.

The floor is an escape hatch for tiny probes, not a reason to hide shared
behavior inside ad-hoc runtime or ImGui code.

---

## 1) Core rule: a feature is a vertical slice with explicit seams

When a feature grows past the floor, it is a small contract-driven slice:

1. **RuntimeModule** — implement `IRuntimeModule` (`Extrinsic.Runtime.Module`) and compose it with `Engine::AddModule`/`EmplaceModule` before boot. Register command handlers, event subscriptions, sim systems, and frame hooks through the narrow `EngineSetup` surface (never an `Engine&`); publish or consume cross-module infrastructure through the two-phase `ServiceRegistry` (`Provide` in `OnRegister`; `Require`/`Find` in `OnResolve`, fail-closed at boot). Module systems use unique stable pass names and explicit `WaitForSignals`/`SignalLabels` for causal order (declared once on `SimSystemDesc`; the schedule derives the per-tick FrameGraph edges from them); Read/Write declarations protect hazards after that order is canonicalized. This is the `ARCH-011` registration seam per [ADR-0024](../adr/0024-kernel-module-architecture.md) D1/D3/D12/D13; module add order is never load-bearing. See [`kernel-target-state.md`](kernel-target-state.md).
2. **Domain/service layer** (pure logic, testable without GPU/UI).
3. **Data contract** (typed IDs/handles, immutable inputs, explicit outputs).
4. **Adapters** (ECS, GPU, IO, UI hooks).

If a grown feature is missing one of these seams, it will drift into "works
now" code instead of reusable architecture.

---

## 2) Where to put `Runtime.SomethingModule.cppm`

Your proposal is directionally correct, with one refinement:

- Put **stitching/orchestration** in `Runtime.*`.
- Keep **algorithmic kernels** outside `Runtime` (e.g., `Geometry:*`, `Core:*`, `Graphics:*`).

`Runtime` should wire systems together, not own heavy math kernels.

### Recommended split

```text
src/Runtime/
  Runtime.SomethingModule.cppm      # exported runtime-facing contract
  Runtime.SomethingModule.cpp       # orchestrator impl

src/Geometry/
  Geometry.SomethingAlgo.cppm       # algorithm contract
  Geometry.SomethingAlgo.cpp        # algorithm implementation
```

This lets you reuse the algorithm in batch tools, editor workflows, tests, and async workers without dragging runtime dependencies.

---

## 3) Dependency direction (must stay acyclic)

Follow the existing subsystem intent in `runtime-subsystem-boundaries.md`:

- `Core` <- `Geometry` / `ECS` / `RHI`
- `Graphics` depends on lower layers, not on `Runtime`
- `Runtime` is composition root

A runtime feature module may import lower layers, but lower layers must never import that runtime module.

---

## 4) Full feature contract

Once the floor no longer fits, the feature module should export the same
minimum shape:

- `Config` (validated user tuning / policy)
- `InputSnapshot` (immutable per-dispatch data)
- `Result` (diagnostics + payload)
- `Execute(...) -> std::expected<Result, Error>`
- `EnqueueAsync(...)` for streaming/off-main-thread heavy work (if applicable)

Runtime integration for a grown feature goes through
`Extrinsic.Runtime.Module`: implement `IRuntimeModule`, register commands,
events, jobs, worlds, services, fixed-step sim systems, and frame-phase hooks
through `EngineSetup`, and add the module to the engine before
`Engine::Initialize()`. Per ADR-0024 D13, module-facing setup/context surfaces
must stay narrow capability surfaces and must not receive `Engine&`. Shared
synchronous infrastructure should be provided in `OnRegister`, required or found
from the two-phase `ServiceRegistry` in `OnResolve`, and shut down after the
runtime shutdown announce event has pumped. Module sim systems declare
wait/signal labels on `SimSystemDesc` so pass ordering is data-driven rather
than dependent on `AddModule` order.

This surface is not required for a one-caller, synchronous probe that still fits
the floor. Add it when a second caller, a backend split, scheduled work,
persisted config, command routing, UI control, or telemetry-backed diagnostics
appears.

---

## 5) Frame-phase ownership (3-fold graph alignment)

Place feature work intentionally:

1. **CPU Task Graph:** gameplay-safe incremental logic, lightweight topology ops.
2. **GPU Frame Graph:** per-frame render/compute execution with transient resources.
3. **Async Streaming Graph:** expensive mesh processing, file IO, offline-like preparation.

Default policy:

- If it touches swapchain-visible frame output, schedule through render graph.
- If it is heavy and not needed this frame, schedule in async streaming.
- If it mutates authoritative simulation state, run in fixed/update lanes with explicit barriers.

---

## 6) Refactor protocol for legacy code

Use this exact sequence to avoid regressions:

1. **Freeze behavior:** add characterization tests around current outputs.
2. **Extract pure kernel:** move algorithmic logic out of runtime into domain module.
3. **Introduce facade:** keep old API, internally route through new module.
4. **Move data ownership:** replace ad-hoc pointers with handles/snapshots.
5. **Add telemetry + limits:** frame time budget markers, queue depth, failure counters.
6. **Delete compatibility path** once parity and perf gates are green.

---

## 7) Naming and discoverability standard

Use stable naming so features are self-indexing:

- Module names: `Runtime.<Feature>Module`, `Geometry.<Feature>`, `Graphics.<Feature>System`
- Entry points: `Register`, `Prepare`, `Execute`, `Commit`, `Shutdown`
- Config/result types: `<Feature>Config`, `<Feature>Result`, `<Feature>Diagnostics`

Also add each feature doc under `docs/architecture/` and link it from `README.md`.

---

## 8) Performance and robustness gate (required)

Before merging, a feature should satisfy:

- No heap allocation in hot per-frame loops (use frame allocators / preallocated pools).
- Degenerate-input guards (empty sets, NaN/Inf, zero-area, non-manifold rejection path).
- Deterministic error contract with `std::expected`.
- Telemetry counters and scoped timings.
- At least one focused test for success path + one for degenerate/failure path.

---

## 9) Minimal module template

```cpp
// Runtime.SomethingModule.cppm
export module Runtime:SomethingModule;

import Core:Error;
import std;

export namespace Runtime {
struct SomethingConfig final {
    uint32_t MaxIterations = 16;
};

struct SomethingInputSnapshot final {
    std::span<const float> Values{};
};

struct SomethingResult final {
    uint32_t IterationsUsed = 0;
};

[[nodiscard]] std::expected<SomethingResult, Core::Error>
ExecuteSomething(SomethingInputSnapshot input, const SomethingConfig& cfg);
} // namespace Runtime
```

Treat this as a facade; call into a lower-level `Geometry:*` or `Core:*` kernel for actual heavy logic.


---

## 11) UI integration seam (where and how to hook feature UI)

Define UI-facing command/state seams in runtime editor modules, not inside the
domain algorithm module and not in `src/app/` glue code.

### Placement rules

- **Algorithm UI state + widgets:** `src/runtime/Editor/Runtime.SandboxEditorUi.*`
  or a future `src/runtime/Editor/Runtime.<Feature>Editor.*` promoted module.
- **Panel/menu registration:** the promoted `SandboxEditorUi` frame model and
  ImGui adapter attachment.
- **Execution trigger:** UI **enqueues a command** on the kernel CommandBus
  (a `<Feature>Requested` payload), drained pre-sim and handled by the feature's
  RuntimeModule (ADR-0024 D5). It does not call an `Engine` facade method.
- **Background progress/result polling:** runtime/streaming lane, then UI only renders readonly status

Current promoted precedent follows this pattern in
`Extrinsic.Runtime.SandboxEditorUi`: editor panels expose deterministic frame
models and typed command surfaces, while execution and ownership stay in
runtime.

### Data flow contract

Use a one-way command path:

1. UI captures params and validates cheap constraints.
2. UI enqueues a `FeatureRequested` command on the kernel CommandBus (D5).
3. Runtime routes to CPU graph / GPU frame graph / async streaming graph.
4. Runtime publishes `FeatureResult` + diagnostics.
5. UI renders status/results from immutable snapshots.

Keep UI callbacks non-blocking: no heavy compute inside ImGui draw functions.

### Minimal UI/controller skeleton

```cpp
// Runtime.MyFeatureEditor.cpp
module Extrinsic.Runtime.MyFeatureEditor;

import Extrinsic.Runtime.MyFeature;

namespace Extrinsic::Runtime {

void MyFeatureController::RegisterPanelsAndMenu() {
    // Register a panel descriptor with SandboxEditorUi's frame model.
}

void MyFeatureController::DrawPanel() {
    // 1) Draw controls
    // 2) On Apply: enqueue ExecuteMyFeatureAsync(request)
    // 3) Draw progress + diagnostics from readonly result snapshot
}

} // namespace Extrinsic::Runtime
```

### UI performance guardrails

- Persist panel state in controller members; avoid per-frame heap churn.
- Use bounded request queues and cancel/replace semantics for repeated Apply clicks.
- Debounce expensive preview recompute (e.g., slider drag -> apply on release).
- Emit telemetry markers for `UI.Submit`, `Feature.Enqueue`, `Feature.Complete`.

---

## 12) Discoverability for UI-backed features

For each UI-backed feature that has grown past the floor, register all four
artifacts:

1. **Runtime facade module** (`Extrinsic.Runtime.<Feature>`)
2. **Editor UI controller/panel** (`Extrinsic.Runtime.<Feature>Editor` or a
   `SandboxEditorUi` command/window extension)
3. **Architecture note** in `docs/architecture/` with command/result contract
4. **Serializable config/command entry** that can be driven by an agent,
   command surface, or config file without going through ImGui

If one is missing, feature discoverability degrades quickly.

---

## 13) Decision checklist (copy into PR description)

- [ ] Is the full slice actually needed now, or would a parameter struct plus
      free function satisfy a one-caller probe?
- [ ] Does this module orchestrate, rather than own domain kernels?
- [ ] Is the dependency direction acyclic and layer-correct?
- [ ] Is there a stable `Config/Input/Result` contract?
- [ ] Is the work assigned to CPU graph vs GPU frame graph vs async graph intentionally?
- [ ] Are degenerate inputs and error surfaces explicit?
- [ ] Are telemetry and tests in place?
- [ ] Is the agent/config-drivable command entry present when UI control exists?
- [ ] Is the architecture doc + README link updated when the feature grows past
      the floor?

If all boxes are checked, the feature is likely maintainable, performant, and reusable.
