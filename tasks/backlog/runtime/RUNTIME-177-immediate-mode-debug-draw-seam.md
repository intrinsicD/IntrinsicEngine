---
id: RUNTIME-177
theme: B
depends_on: []
maturity_target: CPUContracted
---
# RUNTIME-177 — Immediate-mode debug-draw seam for runtime and method code

## Goal
- Add a runtime-owned immediate-mode debug-draw API
  (`DebugDraw::Line/WireSphere/Aabb/Axes(...)`-style free functions over a
  plain per-frame accumulator) so editor, module, and method code can
  visualize transient geometry with one call at the call site.

## Non-goals
- No new render pass or shader work: the transient-debug pass and
  `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket` consumption in
  `Pass.TransientDebug.Surface` already exist and stay unchanged.
- No retained/persistent debug scene; the accumulator clears every frame.
- No replacement of the spatial-debug adapter machinery
  (`SpatialDebugBinding` stays the seam for acceleration-structure
  visualization).
- No `IRenderer` contract changes.

## Context
- 2026-07 right-sizing audit: `RuntimeRenderSnapshotBatch::DebugLines/
  DebugPoints/DebugTriangles` have no runtime writer — the only debug
  geometry source is the spatial-debug adapter pump, so adding a trivially
  scoped debug primitive (e.g. a wireframe sphere) costs 8–11 files across
  4 layers. The missing piece is one direct seam, not more layers.
- Owner: `src/runtime` (accumulator + submit wiring in the extraction/
  snapshot path); graphics consumes existing packet spans.
- Design follows `AGENTS.md` §5 P1 (plain structs/free functions, no new
  interfaces) and the `intrinsicengine-right-sizing` keep-list.

## Required changes
- [ ] `Runtime.DebugDraw` module: plain frame accumulator (line/point/triangle
  packet vectors) + free-function helpers (`Line`, `WireSphere`, `Aabb`,
  `Axes`) that tessellate to line packets.
- [ ] Attach the accumulator's spans to `RuntimeRenderSnapshotBatch` during
  snapshot submit and clear the accumulator once per frame.
- [ ] Config-lane toggle to disable debug-draw submission
  (`Control surfaces`: config + agent/CLI reachable, per AGENTS.md §5 P3).

## Tests
- [ ] Contract test: helper calls produce packets in the submitted batch
  (null path), accumulator clears between frames, disabled toggle submits
  no packets.
- [ ] WireSphere/Aabb tessellation counts and endpoints are deterministic.

## Docs
- [ ] `src/runtime/README.md` seam entry; docs-sync per policy.

## Acceptance criteria
- [ ] A single call site (e.g. in the Sandbox or a method harness) can draw a
  wireframe sphere with one line of code and no per-primitive changes to
  extraction, stats, adapters, or passes.
- [ ] Default CPU gate stays green.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- New `I*` interfaces, registries, or event indirection for this seam.
- Changes to `RenderWorldPool` slot lifecycle or the frame-phase order.

## Maturity
- Target: CPUContracted (packet contents proven on the null path; the
  consuming pass is already Operational via existing transient-debug
  coverage). CPUContracted is the intended endpoint: no Operational
  follow-up is owed.
