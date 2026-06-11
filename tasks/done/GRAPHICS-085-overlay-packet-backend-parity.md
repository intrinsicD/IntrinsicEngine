---
id: GRAPHICS-085
theme: F
depends_on: [RUNTIME-104]
---
# GRAPHICS-085 — Overlay packet backend parity

## Goal
- Prove backend consumption only for the runtime-produced overlay packet lanes retained after `RUNTIME-104`, including selection and outline behavior where those lanes participate in current workflows.

## Non-goals
- No runtime/editor overlay creation API; `RUNTIME-104` owns producer lifecycle.
- No geometry algorithm execution; runtime/geometry/method tasks own algorithms.
- No live ECS access from graphics.
- No broad property-buffer residency; `GRAPHICS-084` owns that seam.

## Context
- Owner/layer: `graphics` consumes immutable overlay/debug/visualization packets and records backend commands.
- Existing pieces include transient debug and visualization overlay upload helpers, line/point/surface passes, selection outline, and render command routing. `RUNTIME-104` retired a separate persistent derived-overlay producer for current workflows: mesh/graph/point child overlays map to ordinary `GeometrySources` entities or mesh primitive-view sidecars, and vector-field/isoline overlays use data-only visualization packets without child ECS entities.
- This task is downstream of `RUNTIME-104` and scopes against that classification rather than adding a new runtime/editor overlay creation API.

## Value gate
- Current state: graphics already consumes transient debug and visualization packets, records render command stats, and has selection outline infrastructure.
- Improvement: retained overlay-like packet lanes get backend evidence without graphics importing runtime/ECS or reviving immediate GPU upload from overlay creation.
- Scope decision: implement no new packet class unless a current workflow proves existing visualization/debug packet lanes cannot cover it.

## Required changes
- [x] No new packet class or pass route was added; existing transient debug and
  visualization packet types represent the retained overlay-like lanes from
  `RUNTIME-104`.
- [x] Add backend command-shape coverage for retained transient debug line,
  point, triangle, visualization vector-field, and isoline overlay lanes.
- [x] Ensure selection/outline eligibility for ordinary renderable/
  primitive-view overlays remains covered by immutable runtime snapshots;
  packet-only visualization overlays remain visual-only because this task adds
  no immutable selection metadata.
- [x] Keep malformed packet diagnostics on the existing graphics validation
  surfaces without querying runtime/ECS.
- [x] No new opt-in Vulkan proof was needed for this retirement: CPU/null
  command-shape tests prove command routing and stats, while the existing
  labelled `gpu;vulkan` transient-debug and visualization-overlay smokes remain
  the operational evidence path when a Vulkan-capable host is available.

## Tests
- [x] Add `contract;graphics` tests for overlay packet validation, pass routing,
  command stats, and selection/outline eligibility. `Test.VisualizationOverlayPass.cpp`
  now includes the combined retained-lane command-shape proof; existing
  transient-debug, visualization-packet, and selection/outline tests cover the
  per-lane validation/stat surfaces.
- [x] Add `integration;runtime;graphics` tests using the `RUNTIME-104`
  classification and existing runtime visualization/primitive-view fixtures.
  Existing runtime render-extraction coverage proves visualization packets
  reach `RenderWorld` without child ECS entities, and existing render-extraction
  selection/outline coverage proves selectable ordinary/primitive-view
  renderables remain snapshot-owned.
- [x] Labelled `gpu;vulkan` smoke coverage already exists for the two backend
  overlay families: `TransientDebugSurfaceGpuSmoke` and
  `VisualizationOverlaySurfaceGpuSmoke`. This retirement does not claim a fresh
  Vulkan host run.

## Docs
- [x] Update `src/graphics/renderer/README.md`,
  `docs/architecture/graphics.md`, and
  `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Update `tasks/backlog/rendering/README.md`.
- [x] No module inventory regeneration is required because no public module
  surface changed.

## Acceptance criteria
- [x] Retained runtime-produced overlay/debug/visualization packets are consumed
  by graphics without live ECS/runtime imports.
- [x] Selection and outline eligibility for retained selectable overlay-like
  renderables is visible in command stats or readback tests; packet-only
  visualization overlays are explicitly visual-only because no selection
  metadata is added.
- [x] Legacy `Graphics.OverlayEntityFactory` backend behavior is either
  represented by retained packet-consumption evidence or explicitly retired by
  `RUNTIME-104`.

## Status
- Completed 2026-06-11 at maturity `CPUContracted`.
- PR/commit: this retirement commit.
- Scope: no runtime/editor overlay creation API, no new packet class, and no
  graphics import of runtime/ECS. The proof composes existing retained lanes:
  transient debug line/point/triangle packets, visualization vector-field and
  isoline packets, ordinary renderable selection/outline snapshots, and the
  `RUNTIME-104` no-persistent-child-overlay classification.
- Operational note: existing opt-in `gpu;vulkan` transient-debug and
  visualization-overlay smokes remain the Vulkan evidence path, but this task
  retirement is a CPU/null command-shape proof and did not run a fresh Vulkan
  smoke.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Overlay|Visualization|Selection|Outline' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Verification results
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
bash -lc "set -o pipefail; ctest --test-dir build/ci --output-on-failure -R 'Overlay|Visualization|Selection|Outline' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 | tee /tmp/graphics-085-focused-ctest.log"
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
tools/ci/run_clean_workshop_review.sh . --strict
git diff --check
```

Result: configure succeeded with Clang 23; `IntrinsicTests` built; focused
CTest passed 189/189, including
`VisualizationOverlayPassContract.RetainedOverlayPacketLanesRecordTogether`;
layering, test layout, doc links, task policy, task-state links, session-brief
freshness, docs-sync diff checks, clean-workshop automated scorecard rows, and
`git diff --check` passed. The `gpu;vulkan` smokes were not run in this
session. Root hygiene remains warning-mode with pre-existing `.agents/` and
`imgui.ini` root entries.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime/ECS/editor state into graphics.
- Reintroducing immediate GPU upload from runtime overlay creation.

## Maturity
- Closed at `CPUContracted` for command-shape parity. No separate
  `Operational` follow-up is owed by this task because the retained backend
  lanes already have opt-in `gpu;vulkan` smokes; rerunning them remains
  environment-dependent verification, not new scope.
