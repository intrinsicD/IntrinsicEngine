---
id: BUG-100
theme: G
depends_on: []
maturity_target: Operational
---
# BUG-100 — Manual geometry import blocks the Sandbox frame loop

## Goal

- Route Sandbox manual Mesh, Graph, and PointCloud file imports through the
  existing streaming ingest lane so path reads/decodes never block the ImGui
  frame and completion applies once on the bounded main-thread drain.

## Non-goals

- No removal of the explicit synchronous `ImportAssetFromPath()` API used by
  tests/tools outside frame-driven UI routes.
- No second executor, import queue, state machine, or app-owned worker.
- No background ECS, `AssetService`, selection, camera-focus, or document
  mutation.
- No change to model-scene/texture decode semantics beyond sharing the same
  user-visible queued-result contract.
- No check-in of the local `M16.xyz` or `child.obj` datasets.

## Context

- Symptom: clicking Import for local `M16.xyz` freezes the visible Sandbox for
  about 1.95 seconds; local `child.obj` reports a 4.87-second import duration.
  Neither route presents `Pending` while its file read/decode runs.
- Expected behavior: every File / Import route returns a queued operation
  promptly, remains visibly responsive, and publishes selection/focus/result
  only from main-thread apply.
- Root cause: `BuildContextFromEngine()` queues ModelScene/Texture2D but calls
  synchronous `ImportAssetFromPath()` for geometry. Runtime already contains
  a nearly identical streaming geometry path for dropped files, so the fix is
  to generalize and reuse that path rather than invent another seam.
- Owner: runtime import composition and facade command wiring. `app` renders
  the existing pending/queue model and remains `app -> runtime` only.
- The separately observed multi-minute OBJ UV enrichment worker and close-time
  drain are owned by `BUG-101`; stale postprocess overwrite/readiness is owned
  by `BUG-095`.

## Required changes

- [ ] Generalize the existing dropped-geometry streaming helper into one
      source-aware queued geometry import path returning
      `RuntimeQueuedAssetImport`; preserve dropped-file multi-candidate
      fallback and manual explicit-hint behavior.
- [ ] Make the Sandbox import command queue every supported payload and return
      `Pending` plus the ingest handle immediately; remove its geometry-only
      synchronous branch.
- [ ] Keep route/ingest state creation on the frame thread, decode on the
      worker, and `AssetService`/ECS/post-import/selection/focus/history apply
      on the bounded main-thread completion lane.
- [ ] Preserve cancellation, terminal queue diagnostics, duplicate-active
      request handling, completed-handler behavior, and exactly-once dirty
      marking for manual and dropped sources.
- [ ] Keep the direct synchronous API available and clearly documented as
      inappropriate for frame-driven UI callbacks.

## Tests

- [ ] Add a deterministic fake/blocking geometry decoder contract proving a
      real Sandbox import command returns `Pending` before the worker is
      released and the engine continues producing ImGui frames.
- [ ] Assert queued manual Mesh, Graph, and PointCloud results materialize,
      select, focus, mark dirty, and publish one terminal ingest event after
      the worker/apply barrier is released.
- [ ] Add cancellation and failure coverage proving no partial asset/entity
      apply and no history mutation.
- [ ] Preserve dropped-file candidate routing and synchronous direct-API
      contracts.

## Docs

- [ ] Update `src/runtime/README.md` and `src/app/Sandbox/README.md` so all
      frame-driven File / Import routes are documented as queued while the
      direct API remains synchronous for explicit non-frame callers.
- [ ] Regenerate the module inventory if the public queued import surface
      changes.
- [ ] Refresh task indexes/session brief and retirement records on closure.

## Acceptance criteria

- [ ] No Sandbox File / Import geometry read/decode runs inside the ImGui
      callback; the command promptly returns `Pending` with an operation.
- [ ] While a deterministic worker is blocked, the frame counter and UI
      continue advancing and no ECS/asset/history mutation is visible.
- [ ] Successful completion applies once on the main thread with the same
      entity authoring, visibility, selection, focus, and queue diagnostics as
      the former synchronous path.
- [ ] Cancellation/failure remains fail-closed and all focused/default CPU
      gates pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeAssetImportFormatCoverage\.QueuedManualGeometry|^SandboxEditorUi\.FileImportGeometryRemainsResponsive' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src \
  --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Calling the synchronous import API from the Sandbox ImGui command path.
- Mutating live ECS, assets, selection, focus, or history on a worker thread.
- Copy-pasting the dropped-geometry executor/state-machine implementation into
  a second manual-import path.
- Waiting on a worker/future/scheduler fence from `Engine::RunFrame()`.

## Maturity

- Target: `Operational` through a real Null-window Sandbox import command that
  remains responsive while decode is deterministically blocked and then
  observes queued completion. No Vulkan-specific follow-up is owed.
