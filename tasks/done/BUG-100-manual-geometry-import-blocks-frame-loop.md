---
id: BUG-100
theme: G
depends_on: []
maturity_target: Operational
---
# BUG-100 — Manual geometry import blocks the Sandbox frame loop

## Status

- Completed on 2026-07-16 at `Operational`; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`.
- Commit references: `56352aa7` queues every frame-driven import route and
  cancels outstanding imports before shutdown tears down application policy;
  `694b8b3f` aligns stale-session coverage with the queued result contract.
  Focused runtime coverage passed 5/5, focused
  presentation coverage passed 3/3, and the default CPU-supported gate passed
  3,830/3,830.

## Right-sizing note

- Element: the existing dropped-geometry streaming helper and the manual
  geometry synchronous branch currently duplicate one behavioral route.
- Simpler alternative: generalize the existing runtime helper with the source
  and explicit payload hint it already needs; add no executor, queue, service,
  facade, or app-owned worker. The current executor stays because the real
  cross-thread lifetime boundary is load-bearing.
- Blast radius: runtime import composition/facade wiring, its public queued
  result only if strictly necessary, focused runtime/Sandbox tests, and the two
  consumer docs; layering remains `runtime`-owned.
- Reintroduction trigger: a second queue is justified only if a future active
  task requires materially different execution, cancellation, or apply-thread
  semantics that the shared lane cannot represent.

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
  quadratic drain were fixed by `BUG-101`; stale postprocess
  overwrite/readiness remains owned by `BUG-095`.

## Required changes

- [x] Generalize the existing dropped-geometry streaming helper into one
      source-aware queued geometry import path returning
      `RuntimeQueuedAssetImport`; preserve dropped-file multi-candidate
      fallback and manual explicit-hint behavior.
- [x] Make the Sandbox import command queue every supported payload and return
      `Pending` plus the ingest handle immediately; remove its geometry-only
      synchronous branch.
- [x] Keep route/ingest state creation on the frame thread, decode on the
      worker, and `AssetService`/ECS/post-import/selection/focus/history apply
      on the bounded main-thread completion lane.
- [x] Preserve cancellation, terminal queue diagnostics, duplicate-active
      request handling, completed-handler behavior, and exactly-once dirty
      marking for manual and dropped sources.
- [x] Keep the direct synchronous API available and clearly documented as
      inappropriate for frame-driven UI callbacks.

## Tests

- [x] Add a deterministic fake/blocking geometry decoder contract proving a
      real Sandbox import command returns `Pending` before the worker is
      released and the engine continues producing ImGui frames.
- [x] Assert queued manual Mesh, Graph, and PointCloud results materialize,
      select, focus, mark dirty, and publish one terminal ingest event after
      the worker/apply barrier is released.
- [x] Add cancellation and failure coverage proving no partial asset/entity
      apply and no history mutation.
- [x] Preserve dropped-file candidate routing and synchronous direct-API
      contracts.

## Docs

- [x] Update `src/runtime/README.md` and `src/app/Sandbox/README.md` so all
      frame-driven File / Import routes are documented as queued while the
      direct API remains synchronous for explicit non-frame callers.
- [x] Regenerate the module inventory if the public queued import surface
      changes.
- [x] Refresh task indexes/session brief and retirement records on closure.

## Acceptance criteria

- [x] No Sandbox File / Import geometry read/decode runs inside the ImGui
      callback; the command promptly returns `Pending` with an operation.
- [x] While a deterministic worker is blocked, the frame counter and UI
      continue advancing and no ECS/asset/history mutation is visible.
- [x] Successful completion applies once on the main thread with the same
      entity authoring, visibility, selection, focus, and queue diagnostics as
      the former synchronous path.
- [x] Cancellation/failure remains fail-closed and all focused/default CPU
      gates pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure \
  -R '^SandboxEditorUi\.(QueuedManualGeometryImportsRemainResponsiveAndApplyOnce|QueuedManualGeometryCancellationPreventsApply|QueuedManualGeometryDecodeFailureIsFailClosed|ShutdownCancelsBlockedManualGeometryBeforePolicyUnregister|DroppedFileImportFailureLogsDiagnostics)$' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -R '^SandboxEditorSession\.StaleCopiedSurfacesFailAfterDetachAndReattach$' \
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

Verification completed on 2026-07-16. The blocked decoder contract proved
that ImGui frames continue before release, completion applies once on the main
thread, cancellation and decode failure remain fail-closed, and shutdown
cancels blocked or apply-ready work before unregistering policy state. The
aggregate build and default CPU-supported gate passed 3,830/3,830.

## Forbidden changes

- Calling the synchronous import API from the Sandbox ImGui command path.
- Mutating live ECS, assets, selection, focus, or history on a worker thread.
- Copy-pasting the dropped-geometry executor/state-machine implementation into
  a second manual-import path.
- Waiting on a worker/future/scheduler fence from `Engine::RunFrame()`.

## Maturity

- Achieved: `Operational` through a real Null-window Sandbox import command that
  remains responsive while decode is deterministically blocked and then
  observes queued completion. No Vulkan-specific follow-up is owed.
