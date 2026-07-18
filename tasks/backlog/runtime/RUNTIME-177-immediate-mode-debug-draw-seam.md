---
id: RUNTIME-177
theme: B
depends_on:
  - RUNTIME-166
  - RUNTIME-181
maturity_target: CPUContracted
---
# RUNTIME-177 — Immediate-mode debug-draw seam for runtime integrations

## Goal
- Add a runtime-owned immediate-mode debug-draw API
  (`DebugDraw::Line/WireSphere/Aabb/Axes(...)`-style free functions over a
  plain per-frame accumulator) so editor, module, and method-integration call
  sites can visualize transient geometry with one call from runtime-owned
  code.

## Non-goals
- No new render pass or shader work: the transient-debug pass and
  `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket` consumption in
  `Pass.TransientDebug.Surface` already exist and stay unchanged.
- No retained/persistent debug scene; the accumulator clears every frame.
- No replacement of the spatial-debug adapter machinery
  (`SpatialDebugBinding` stays the seam for acceleration-structure
  visualization).
- No `IRenderer` contract changes.
- No `methods` or lower-layer import of runtime. A method visualization is
  emitted by its runtime/app harness or adapter from method results.

## Context
- 2026-07 right-sizing audit: `RuntimeRenderSnapshotBatch::DebugLines/
  DebugPoints/DebugTriangles` have no runtime writer — the only debug
  geometry source is the spatial-debug adapter pump, so adding a trivially
  scoped debug primitive (e.g. a wireframe sphere) costs 8–11 files across
  4 layers. The missing piece is one direct seam, not more layers.
- Owner: `src/runtime` (accumulator + submit wiring in the extraction/
  snapshot path); graphics consumes existing packet spans.
- `RUNTIME-166` lands the behavior-preserving RenderExtraction implementation
  split first so this task attaches to its final private-state shape.
  `RUNTIME-181` owns the validated config-control path; the debug toggle is a
  section/field consumed through that owner, not a new Engine facade.
- Design follows `AGENTS.md` §5 P1 (plain structs/free functions, no new
  interfaces) and the `intrinsicengine-right-sizing` keep-list.

## Control surfaces

- Config file: one serializable `render.debug_draw_enabled` boolean, default
  `true`, round-trips through the existing validated config lane.
- Agent/CLI: the same field is previewed and applied through the
  `ConfigControlModule` service; there is no debug-draw command API.
- UI: the existing Sandbox render/debug settings presentation edits that same
  field through the shared preview/validate/apply path; it owns no duplicate
  state.

## Required changes
- [ ] `Runtime.DebugDraw` module: plain frame accumulator (line/point/triangle
  packet vectors) + free-function helpers (`Line`, `WireSphere`, `Aabb`,
  `Axes`) that tessellate to line packets.
- [ ] Attach the accumulator's spans to `RuntimeRenderSnapshotBatch` during
  snapshot submit and clear the accumulator once per frame.
- [ ] Config-lane toggle to disable debug-draw submission
      through all three Control surfaces above, per AGENTS.md §5 P3.
- [ ] Resolve the toggle through the ConfigControl owner delivered by
      `RUNTIME-181`; do not add `Engine::GetDebugDraw*` or a UI-only mutation
      path.

## Tests
- [ ] Contract test: helper calls produce packets in the submitted batch
      (null path), accumulator clears between frames, disabled toggle submits
      no packets.
- [ ] WireSphere/Aabb tessellation counts and endpoints are deterministic.
- [ ] Config parse/serialize/round-trip and agent/CLI apply coverage proves the
      one field reaches submission at the validated frame boundary.
- [ ] Sandbox UI integration coverage proves its control writes the same
      config field/path and a rejected preview does not mutate live state.

## Docs
- [ ] Add the seam and shared control path to `src/runtime/README.md` and the
      existing Sandbox render/debug settings documentation.
- [ ] Regenerate `docs/api/generated/module_inventory.md` for the new
      `Runtime.DebugDraw` module.

## Acceptance criteria
- [ ] A single runtime/app call site (e.g. in the Sandbox or a runtime-owned
      method harness) can draw a wireframe sphere with one line of code and no
      per-primitive changes to extraction, stats, adapters, or passes.
- [ ] Default CPU gate stays green.
- [ ] Config file, agent/CLI, and UI are co-equal controls over one serialized
      value and one validated apply path.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- New `I*` interfaces, registries, or event indirection for this seam.
- Changes to `RenderWorldPool` slot lifecycle or the frame-phase order.
- A debug-draw or config-control facade on `Runtime.Engine`.
- Importing `Runtime.DebugDraw` from `methods`, geometry, graphics, ECS, core,
  or another lower layer.

## Maturity
- Target: CPUContracted (packet contents proven on the null path; the
  consuming pass is already Operational via existing transient-debug
  coverage). CPUContracted is the intended endpoint: no Operational
  follow-up is owed.
