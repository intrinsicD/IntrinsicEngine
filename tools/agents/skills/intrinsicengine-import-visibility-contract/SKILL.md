---
name: intrinsicengine-import-visibility-contract
description: The checklist a new or changed asset import/materialization path in IntrinsicEngine must satisfy so that a "successful" import is actually visible AND selectable in the sandbox — render-critical component parity with the reference triangle (GeometrySources residency, RenderSurface, SelectableTag, VisualizationConfig, StableId), count-matched v:normal (authored preserved, area-weighted fallback, never overwritten), resolved v:texcoord (authored or generated atlas UVs before first extraction), runtime-authored culling bounds plus one-shot camera focus for off-origin geometry, derived post-processing that never blocks the first upload or clobbers recomputed attributes, deferred generated-normal/texture bindings, and receipt/route/queue/completion logging so failures are never silent. Use this skill whenever adding or changing a mesh/point-cloud/graph import, drag-and-drop or file-backed materialization, progressive raw-model handoff, or post-import derived work; whenever an import "succeeds" but nothing renders or is pickable; or when normals/UVs are dropped, geometry is off-origin/culled/out-of-view, or a dropped file fails silently.
---

# IntrinsicEngine Import Visibility Contract

This skill is the acceptance checklist for any import or materialization path
that ends with geometry the user is supposed to see and select in
`ExtrinsicSandbox`. It codifies invariants that already hold in the tree; it
does not propose new behavior.

The core invariant: **a "successful" import is not done when the asset
decodes — it is done when the resulting entity is drawn and pickable in the
live sandbox view, at parity with the default `ReferenceTriangle`.** Roughly a
dozen retired bugs are the same failure class: decode/materialization reported
success, but nothing visible or selectable appeared, because one render-critical
attribute or component was missing, dropped, overwritten, or the geometry sat
off-camera. Each checklist item below cites the retired task(s) that proved it.

Owner layers: `runtime` owns import routing and materialization; the promoted
`graphics/renderer` owns the default-recipe surface/depth/selection draw state.
This skill does **not** own the parser/decoder slice shape — that is
`intrinsicengine-geometry-io-format` (`PROC-019`).

## The visibility checklist

### 1. Render-critical component parity with `ReferenceTriangle`

The materialized entity must carry the same render-critical components the seed
`ReferenceTriangle` does (see `Runtime.ReferenceScene.cpp`), or it materializes
"successfully" yet invisibly / unpickably:

- `ECS::Components::GeometrySources` residency (`PopulateFromMesh`/equivalent).
- `Graphics::Components::RenderSurface` with the correct `SourceDomain`.
- `Graphics::Components::VisualizationConfig` (default `UniformColor` is lit).
- `ECS::Components::Selection::SelectableTag`.
- A stable, selectable identity via `ECS::Components::StableId`
  (`Runtime.StableEntityLookup`).

Evidence: `BUG-022` (non-manifold OBJ produced no renderable entity), `BUG-023`
(file-backed OBJ materialized as a hidden/culled entity without parity).

### 2. Count-matched `v:normal` — authored preserved, area-weighted fallback, never overwritten

Explicit decoded `v:normal` values are copied through; when source normals are
absent, deterministic area-weighted fallback normals are computed **before the
first ECS/render extraction upload**. Later stages must not overwrite them.

Evidence: `BUG-041` (authored normals lost during materialization), `BUG-050`
(direct-mesh first upload lacked computed normals), `BUG-048` (post-process
overwrote recomputed normals), `BUG-047` (a normal texture overrode
vertex-normal shading).

### 3. Resolved `v:texcoord` policy — authored UVs preserved, else generated atlas UVs before first extraction

Authored `v:texcoord` survives materialization; missing/invalid source UVs are
replaced by generated xatlas-backed atlas UVs before ECS population and any
generated-texture bake, not left absent (which reads as invisible).

Evidence: `BUG-043` (dropped OBJ without UVs loaded but was invisible), `BUG-045`
(progressive raw-mesh surface UV fallback), `ASSETIO-008` (default UV-atlas
materialization for imported meshes).

### 4. Culling bounds + one-shot camera focus for off-origin geometry

Arbitrary imported geometry materializes at its authored coordinates. Runtime
must author local/world culling bounds and issue a one-shot camera focus so
off-origin geometry is not culled against fallback bounds or left out of view.

Evidence: `BUG-023` (off-origin OBJ culled / out of view without runtime-authored
bounds or camera focus), `BUG-022` (reference-triangle frustum visibility).

### 5. Derived post-processing must not block the first upload — or overwrite recomputed attributes

Publish decoded raw geometry first; run derived missing-normal / UV-atlas /
generated-texture work asynchronously (the runtime streaming executor) and apply
it back to the same entity with dirty tags. Derived apply must preserve
count-matched current attribute values.

Evidence: `BUG-044` (runtime import blocked on derived post-processing), `BUG-048`
(post-process overwrote recomputed normals), `BUG-047` (surface normal texture
override).

### 6. Generated normal/texture bindings registered after the deferred result is ready

Generated normal-map / texture material bindings are registered only once the
deferred bake result exists, bound to the entity that requested them — never
speculatively before the async result lands.

Evidence: `ASSETIO-006` (generated normal-map bake from mesh vertex normals),
`ASSETIO-007` (direct-mesh generated normal-texture binding), `BUG-044`
(post-import derived-work queue).

### 7. Receipt / route / queue / completion logging — failures are never silent

Every import logs file-drop receipt, per-path routing/queue decisions, and
shared completion (success or a failed `RuntimeAssetImportEvent`). A path that
can fail must emit a diagnostic, never a silent no-op.

Evidence: `BUG-038` (dropped file imports failed silently in the sandbox).

## How to prove a change satisfies this contract

- **Default CPU/null gate** proves materialization, component parity, attribute
  presence, and logging via runtime contract tests — this is the
  `CPUContracted` floor for an import path.
- **Visible-pixel proof is `Operational`**, owned by an opt-in `gpu;vulkan`
  readback smoke that drives the real import and asserts non-background pixels
  for the imported entity — see `intrinsicengine-gpu-smoke-authoring`. CPU
  contract coverage alone does not prove the frame is drawn.

## Related

- `intrinsicengine-geometry-io-format` (`PROC-019`) — the parser/decoder/exporter
  slice shape this skill deliberately does not own.
- `intrinsicengine-gpu-smoke-authoring` — the `Operational` visible-frame proof.
- `intrinsicengine-vulkan-frame-triage` — when the frame is black/wrong despite
  the entity being materialized correctly.
- `intrinsicengine-core` — layering rules for how `runtime` materialization and
  `graphics/renderer` draw state interact.
