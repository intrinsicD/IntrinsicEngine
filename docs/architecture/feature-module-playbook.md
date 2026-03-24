# Feature Module Playbook (Refactor + New Development)

This playbook defines the **default way** to add and refactor runtime features without growing accidental coupling.

Use it when introducing anything like `Runtime.SomethingModule.cppm`, geometry tooling, streaming jobs, or render integrations.

---

## 1) Core rule: a feature is a vertical slice with explicit seams

A feature module is not just one file; it is a small contract-driven slice:

1. **Runtime facade** (orchestrates frame phase integration).
2. **Domain/service layer** (pure logic, testable without GPU/UI).
3. **Data contract** (typed IDs/handles, immutable inputs, explicit outputs).
4. **Adapters** (ECS, GPU, IO, UI hooks).

If any part is missing, the feature will drift into "works now" code instead of reusable architecture.

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

## 4) Mandatory feature contract

Every new feature module should export the same minimum shape:

- `Config` (validated user tuning / policy)
- `InputSnapshot` (immutable per-dispatch data)
- `Result` (diagnostics + payload)
- `Execute(...) -> std::expected<Result, Error>`
- `EnqueueAsync(...)` for streaming/off-main-thread heavy work (if applicable)

This makes features discoverable and consistent for call sites.

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

Define UI in the `Runtime::EditorUI` layer, not inside the domain algorithm module.

### Placement rules

- **Algorithm UI state + widgets:** `src/Runtime/EditorUI/Runtime.EditorUI.<Feature>Controller.cpp`
- **Panel/menu registration:** `Interface::GUI::RegisterPanel(...)` and `RegisterMainMenuBar(...)`
- **Execution trigger:** UI emits a typed request to the runtime feature facade (`Runtime.<Feature>Module`)
- **Background progress/result polling:** runtime/streaming lane, then UI only renders readonly status

Current engine precedent already follows this pattern (see `Runtime.EditorUI.GeometryWorkflowController.cpp`).

### Data flow contract

Use a one-way command path:

1. UI captures params and validates cheap constraints.
2. UI submits `FeatureRequest` to runtime facade.
3. Runtime routes to CPU graph / GPU frame graph / async streaming graph.
4. Runtime publishes `FeatureResult` + diagnostics.
5. UI renders status/results from immutable snapshots.

Keep UI callbacks non-blocking: no heavy compute inside ImGui draw functions.

### Minimal UI/controller skeleton

```cpp
// Runtime.EditorUI.MyFeatureController.cpp
module Runtime.EditorUI;

import Interface;
import Runtime:MyFeatureModule;

namespace Runtime::EditorUI {

void MyFeatureController::RegisterPanelsAndMenu() {
    Interface::GUI::RegisterPanel("Geometry - My Feature", [this]() { DrawPanel(); }, true, 0, false);
    Interface::GUI::RegisterMainMenuBar("Geometry", [this]() {
        if (ImGui::BeginMenu("Geometry")) {
            if (ImGui::MenuItem("My Feature")) {
                Interface::GUI::OpenPanel("Geometry - My Feature");
            }
            ImGui::EndMenu();
        }
    });
}

void MyFeatureController::DrawPanel() {
    // 1) Draw controls
    // 2) On Apply: enqueue Runtime::ExecuteMyFeatureAsync(request)
    // 3) Draw progress + diagnostics from readonly result snapshot
}

} // namespace Runtime::EditorUI
```

### UI performance guardrails

- Persist panel state in controller members; avoid per-frame heap churn.
- Use bounded request queues and cancel/replace semantics for repeated Apply clicks.
- Debounce expensive preview recompute (e.g., slider drag -> apply on release).
- Emit telemetry markers for `UI.Submit`, `Feature.Enqueue`, `Feature.Complete`.

---

## 12) Discoverability for UI-backed features

For each new feature, register all three artifacts:

1. **Runtime facade module** (`Runtime.<Feature>Module`)
2. **Editor UI controller/panel** (`Runtime.EditorUI.<Feature>Controller`)
3. **Architecture note** in `docs/architecture/` with command/result contract

If one is missing, feature discoverability degrades quickly.

---

## 13) Decision checklist (copy into PR description)

- [ ] Does this module orchestrate, rather than own domain kernels?
- [ ] Is the dependency direction acyclic and layer-correct?
- [ ] Is there a stable `Config/Input/Result` contract?
- [ ] Is the work assigned to CPU graph vs GPU frame graph vs async graph intentionally?
- [ ] Are degenerate inputs and error surfaces explicit?
- [ ] Are telemetry and tests in place?
- [ ] Is the architecture doc + README link updated?

If all boxes are checked, the feature is likely maintainable, performant, and reusable.
