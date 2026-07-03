---
id: RUNTIME-142
theme: F
depends_on: []
---
# RUNTIME-142 — Async model-scene/texture import and scene-file IO

## Goal
- Extend the deferred streaming-import path (already used by dropped
  geometry) to model-scene and texture drops and to scene save/load, so no
  file read or decode runs synchronously on the main thread inside
  `Engine::RunFrame()`.

## Non-goals
- No change to supported formats, decode semantics, or scene JSON schema.
- No import-pipeline extension seam (that is `RUNTIME-144`).
- The global-barrier removal in the apply path is `RUNTIME-140`.

## Context
- Owner/layer: `runtime` (`Runtime.Engine` import routing,
  `Runtime.StreamingExecutor`, `Runtime.AssetModelSceneHandoff`,
  `Runtime.SceneSerialization`).
- Geometry drops already stream correctly: decode on the worker lane,
  main-thread apply (`src/runtime/Runtime.Engine.cpp:3766-3924`, BUG-044).
  But model-scene/texture drops and all editor-menu imports run synchronous
  file IO + full decode inline
  (`Runtime.Engine.cpp:3645-3661` synchronous route;
  `4226-4293, 4418`: `Core::IO::FileIOBackend backend;
  bridge.ImportModelScene(request.Path, backend)`), and scene save/load is
  synchronous too (`4526-4561`). Drop events fire during Phase-1
  `PollEvents`; editor commands during the ImGui callback — both inside the
  frame.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R11.

## Required changes
- [ ] Route model-scene and texture drop/menu imports through
      `StreamingExecutor`: file read + decode on the worker lane, existing
      handoff/materialization on the main-thread apply, preserving current
      event/diagnostic semantics (BUG-038 logging contract).
- [ ] Route scene load through the same shape: parse into the temporary
      registry off-thread; keep the swap-in (registry replacement, sidecar
      drains, `StableEntityLookup` rebuild) on the main thread per the
      documented scene-replacement lifecycle.
- [ ] Route scene save off-thread: snapshot serializable state on the main
      thread, serialize + write on the worker lane, report completion via
      the existing import/editor event surface.
- [ ] Keep failure behavior fail-closed and identical (a bad parse never
      touches the live scene).

## Tests
- [ ] Contract: model-scene and texture imports complete with identical
      materialized results to the synchronous path for fixture assets.
- [ ] Contract: scene load with an invalid document leaves the live scene
      untouched (existing guarantee preserved through the async route).
- [ ] Contract: frame does not block during a large import (timing probe
      with a slow-IO fake backend).
- [ ] Existing drop/import/scene-serialization suites stay green.

## Docs
- [ ] Update `src/runtime/README.md` and
      `docs/architecture/runtime.md` scene-replacement/import notes.

## Acceptance criteria
- [ ] No synchronous file IO or asset decode reachable from
      `Engine::RunFrame()` for model-scene/texture/scene-document routes
      (audited call-site list recorded in this file).
- [ ] Import/save/load observable behavior unchanged apart from latency now
      spanning frames.
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Worker-thread mutation of the live ECS registry, renderer, or asset
  service (decode produces data; apply mutates, on the main thread).
- Relaxing the fail-closed scene-load guarantee.
- Format/schema changes.
