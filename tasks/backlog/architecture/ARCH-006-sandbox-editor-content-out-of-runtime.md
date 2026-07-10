---
id: ARCH-006
theme: F
depends_on:
  - ARCH-012
---
# ARCH-006 — Move Sandbox application editor content out of runtime

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
- Not gated on, but should sequence after, the async-lane tasks that touch
  the same files (`RUNTIME-141`, `RUNTIME-138`) to avoid churn collisions —
  coordinate slice timing in `tasks/active/README.md` when promoting.

## Context
- Owner/layer: boundary decision between `runtime` and `app`.
- Today `app/Sandbox` is an empty shell (`Sandbox.cppm:20-32` no-op ticks;
  it only attaches `Runtime::SandboxEditorUi`), while the actual application
  lives in `src/runtime/Editor/Runtime.SandboxEditorUi.cpp` (18,556 lines),
  inverting "app depends only on runtime; application specifics belong in
  app/" (`AGENTS.md`, `docs/architecture/runtime.md`).
- The generic seams the panels use (`SetImGuiEditorCallback`, command
  surfaces, `DerivedJobRegistry`, config-control facade) already exist and
  stay in runtime; the move is about *content* ownership.
- Retired `UI-031` already normalized the domain-window information
  architecture in the same file. Open `RUNTIME-138` (nonblocking selected-
  entity pipeline) still touches the same file; this task must slice around
  it, not duplicate it.
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

## Required changes
- [ ] Slice 0 (planning, this file): inventory `Runtime.SandboxEditorUi`
      content into (a) generic editor infrastructure that stays in runtime,
      (b) sandbox/method panels that move to app, (c) engine command
      facades the panels call (stay). Record the inventory and the slice
      boundaries in this task file before any move.
- [ ] Slice 1: extract the generic panel-host/editor-shell module in runtime
      (registration API for panels; no content moves yet); Sandbox attaches
      through it.
- [ ] Slices 2..N: move panel families to `app/Sandbox` one reviewable slice
      at a time (method panels first: K-Means, Poisson, registration,
      mesh-processing, figure export), each slice green on the CPU gate and
      layering check.
- [ ] Split the monolithic implementation and matching
      `Test.SandboxEditorUi.cpp` coverage along the same panel-family boundaries
      so moves create independently compilable units rather than one equally
      large app-side translation unit.
- [ ] Final slice: `src/runtime/Editor` contains no method/sandbox-specific
      panel code; update module inventories.

## Tests
- [ ] Per slice: default CPU gate + `check_layering.py --strict` green.
- [ ] Existing editor command/contract tests keep passing unmoved or move
      with their subject per `check_test_layout.py`.
- [ ] Sandbox smoke (headless null-backend `Engine::Run()` coverage) stays
      green after each slice.

## Docs
- [ ] Update `docs/architecture/runtime.md` and `src/app/README.md` /
      `src/runtime/README.md` ownership text as slices land.
- [ ] Regenerate `docs/api/generated/module_inventory.md` per moved module.

## Acceptance criteria
- [ ] `app/Sandbox` owns its panels; `runtime` owns only generic editor
      infrastructure; `app → runtime` remains the only dependency direction.
- [ ] No panel behavior change (mechanical moves verified by unchanged
      tests).
- [ ] Layering gate green at every slice boundary.
- [ ] Record before/after interface lines/imports, top translation-unit compile
      durations, and clean build edges against the `CI-003` baseline; no
      compile-time claim is made from one run.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

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
