---
id: GRAPHICS-105
theme: B
depends_on:
  - GRAPHICS-104
  - RUNTIME-128
  - RUNTIME-129
maturity_target: Operational
---
# GRAPHICS-105 — Unified mesh shading-model + per-attribute source authority

## Goal
- Make the **material** the single authority for mesh shading: one lit/unlit decision (shading model) and one per-attribute source decision (vertex attribute vs texture), resolved by one shared shader path, applied uniformly across every mesh import route and selectable per attribute in the UI — with point clouds and graphs remaining vertex-attribute-only.

## Non-goals
- Adding texture-sourced attributes to point clouds or graphs — those render from vertex attributes only, and the texture-source option must not exist for them.
- Tangent-space normal-map authoring or MikkTSpace tangent generation (object-space bake stays the texture path for normals per GRAPHICS-104).
- Removing debug visualization overlays (scalar field / per-vertex / per-face color) — they stay as an explicit, clearly-separate debug layer, just no longer a lit/unlit authority for ordinary mesh imports.
- New vertex channels beyond the existing `VertexChannel` set.
- The GPU bake scheduling itself (owned by RUNTIME-129; consumed here for the normal texture path).

## Context
- Owner/layer: `graphics` for the material shading-model + per-channel attribute-source metadata and the unified shader resolution; `runtime` for uniform default-lit material assignment across import routes, extraction, and mesh-only gating; `app`/editor for the UI selector.
- The architectural smell — **two lit/unlit authorities** historically existed:
  1. `Graphics::Components::VisualizationConfig::ColorSource::UniformColor` explicitly set `MaterialFlags::Unlit` (`Graphics.Component.VisualizationConfig.cppm:60-68`, resolved in `Graphics.VisualizationSyncSystem.cpp`). The `main` commit `3485151` worked around this for the **direct import** route by switching it to `ColorSource::Material`; BUG-052 removed the visualization-mode-to-unlit coupling for uniform, scalar, and per-element SciVis overrides.
  2. The material slot/type itself (`DefaultDebugSurface` + `MaterialFlags::Unlit`). RUNTIME-128 fixed the **model-scene** route by binding a lit default instead of the unlit slot 0.
  These route-specific fixes are symptoms of the missing single authority; this task consolidates them.
- The correct model (matches glTF 2.0 / Unreal / Unity / Godot): material owns a `ShadingModel { Lit, Unlit }` where **Unlit is an explicit opt-in**, never a fallback; missing material → a default **lit** material; vertex normals are guaranteed at import; a normal/attribute **texture perturbs/overrides the always-present vertex attribute**, gated by "is a texture bound + ready," in one über-shader. glTF `KHR_materials_unlit` maps onto `ShadingModel::Unlit`.
- Existing building blocks to reuse: `MaterialFlags` (`Unlit`, `ObjectSpaceNormalMap`), the `VertexChannel`/`VertexAttributeBinding` resolver (`Runtime.VertexAttributeBinding.cppm`, RUNTIME-120), generic mesh attribute texture bake (RUNTIME-109), GPU object-space normal bake (GRAPHICS-104) + scheduling (RUNTIME-129), default-lit material for material-less model-scene imports (RUNTIME-128), and direct-import material-driven shading (`3485151`).
- "Works end to end / only this path exists": this task must **remove** the divergent lit/unlit decision for ordinary mesh imports, not merely add a parallel one, and prove the unified path through the real runtime extraction path plus an opt-in Vulkan smoke.
- ARCH-013 re-review (2026-07-08): Decision confirmed with a standing-event
  requirement. Attribute-source and generated-texture readiness changes must be
  observable through kernel events or a standing runtime reaction that feeds
  extraction/dirty-attribute state; no `Engine` callback or direct
  renderer/runtime live coupling should be introduced. This is the graphics
  consumer side of ADR-0024 D6.

### Open questions (non-blocking — defaults chosen; revisit before Slice B/D)
- **Source-choice scope.** Default: **per-material instance** — the material owns texture bindings and the per-channel `AttributeSource` flags; the UI edits the selected mesh's material; meshes sharing a material change together. Alternatives: per-entity/mesh-instance ECS override, or global-default-plus-override. (The question prompt did not reach the user; default chosen per repo "robust default" policy.)
- **V1 attribute scope.** Default: ship **normal** end-to-end first (reuses GRAPHICS-104/RUNTIME-129), with the generic per-channel `AttributeSource` mechanism designed so albedo/color, metallic-roughness, emissive, and generic named attributes (via RUNTIME-109) are thin follow-on additions.
- **Unlit provenance on import.** Default: only mark a material `Unlit` when the source asset says so (`KHR_materials_unlit`) or the user opts in; never as a missing-data fallback.

## Required changes
- [ ] Add a material `ShadingModel { Lit, Unlit }` as the single authority for whether lighting runs; map `KHR_materials_unlit` and existing unlit cases onto it, and make the unified shader honor only this (not `ColorSource`) for ordinary mesh surfaces.
- [ ] Add a per-`VertexChannel` `AttributeSource { VertexAttribute, Texture }` selector owned by the material instance, with per-channel defaults chosen at import (e.g. Normal = VertexAttribute unless an authored/baked normal texture exists).
- [ ] Resolve each mesh channel in one shared shader: the vertex attribute is always available; when the channel source is `Texture` and that texture is `Ready`, sample it, otherwise fall back to the vertex attribute (progressive). Reuse the existing normal fallback shape.
- [ ] Assign a default **lit** material uniformly across **all** mesh import routes (direct + model-scene) via one shared helper, retiring the route-specific divergence (subsumes RUNTIME-128 and `3485151` into a single rule).
- [ ] Demote `VisualizationConfig` to an explicit debug-overlay concern: remove its role as the default lit/unlit decision for imported meshes while keeping scalar/per-vertex/per-face overlays as opt-in debug modes.
- [ ] Mesh-only gating: point cloud and graph domains resolve channels from vertex attributes only; reject/ignore any `Texture` source for non-mesh domains in extraction, and hide the option in the UI.
- [ ] Editor UI: a per-attribute source selector (vertex attribute vs texture) on the selected mesh's material, disabled/absent for non-mesh domains; choosing `Texture` schedules/consumes the bake for that channel.

## Tests
- [ ] CPU/null contract: material `ShadingModel` is the only lit/unlit authority — an imported mesh with a lit material shades regardless of `VisualizationConfig`, and `Unlit` only results from explicit shading-model.
- [ ] CPU/null contract: per-channel `AttributeSource` resolution — `Texture` source with a non-ready/absent texture falls back to the vertex attribute; `Ready` texture is used; `VertexAttribute` source ignores any bound texture.
- [ ] CPU/null contract: default **lit** material assigned identically through both the direct and model-scene import routes (one helper, one result).
- [ ] CPU/null contract: mesh-only gating — point cloud and graph domains reject/ignore a `Texture` channel source and resolve from vertex attributes.
- [ ] CPU/null contract: editor command sets a channel's source and (for meshes) requests the bake; is a no-op/disabled for non-mesh domains.
- [ ] Opt-in `gpu;vulkan` smoke: a mesh with the normal channel set to `Texture` renders with the vertex normal before the bake completes and with the baked object-space normal texture after `Ready`.

## Docs
- [ ] Update `src/graphics/renderer/README.md` and the material doc for the single `ShadingModel` authority, the per-channel `AttributeSource` model, and the unified shader resolution/fallback.
- [ ] Update `src/runtime/README.md` for uniform default-lit assignment across import routes and mesh-only attribute-source gating.
- [ ] Add/extend an architecture doc (and an ADR if the shading-authority decision is hard to reverse) describing the three orthogonal axes (shading model / attribute source / attribute existence) and the single-authority rule.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if any `.cppm` surfaces change.

## Acceptance criteria
- [ ] Exactly one lit/unlit authority exists for mesh surfaces (the material `ShadingModel`); `VisualizationConfig` no longer decides lit/unlit for ordinary mesh imports.
- [ ] Imported meshes shade end to end through the material via the real runtime extraction path, identically across the direct and model-scene routes.
- [ ] Each supported mesh attribute's source (vertex attribute vs texture) is selectable in the UI, with the texture path falling back to the vertex attribute until the bake is `Ready`.
- [ ] Point clouds and graphs are unaffected and have no texture-source option.
- [ ] No layering violations; no `Vk*` across RHI/renderer/runtime APIs; graphics-owned modules carry no live ECS/runtime/AssetService knowledge.
- [ ] `Operational` cited by an actually-run `gpu;vulkan` smoke for the normal texture path; CPU contract gate green for shading-model authority, attribute-source resolution, route-uniform default-lit, and mesh-only gating.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root .
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host only):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes
- Keeping or reintroducing a second lit/unlit authority for ordinary mesh imports.
- Enabling a texture attribute source for point cloud or graph domains.
- Using `Unlit` as a missing-material/missing-data fallback instead of an explicit shading-model choice.
- Passing `Vk*` types through RHI/renderer/runtime/cache public APIs.
- Adding live ECS/runtime/AssetService knowledge to graphics-owned modules.
- Deleting the CPU bake path before the GPU texture path is proven end to end.
- Mixing this consolidation with unrelated renderer/runtime/asset/UI features.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the authority/resolution/gating contracts on CPU/null.
- Slices A–D close `CPUContracted`; Slice E closes `Operational` (cites an actually-run `gpu;vulkan` smoke). Depends on RUNTIME-129 for the normal texture path's GPU scheduling.

## Slice plan
- **Slice A (CPUContracted).** Add material `ShadingModel` as the single lit/unlit authority; route the unified shader to honor it; assign a default **lit** material uniformly across both import routes via one helper (subsuming RUNTIME-128 + `3485151`); demote `VisualizationConfig` lit/unlit role for imports. Defers attribute-source and UI to later slices.
  - **A1 — landed (this slice).** `Graphics::ShadingModel{Lit,Unlit}` added to `MaterialParams` (default `Lit`) and `GpuMaterialSlot.ShadingModel` (former `_pad0`, layout-preserving) + GLSL mirror/constants; `PackSlot` writes it; the forward surface shader's lit/unlit gate now keys on `ShadingModel` (legacy `MaterialFlags::Unlit` kept as a transitional alias) and the `DefaultDebugSurface` type-branch is removed from the gate; slot-0 default and RUNTIME-128's default-lit material set `ShadingModel` explicitly. Tests: material-system round-trip + default-slot `Unlit`, shader-source contract. **Build/CPU gate not run here (vcpkg egress block).**
  - **A2 — remaining.** Map glTF `KHR_materials_unlit` → `ShadingModel::Unlit` at import; one shared `ResolveImportedMeshMaterial` helper across both routes; migrate the remaining `MaterialFlags::Unlit` writers off the flag. Folded into the scivis-collapse slice below since they share the `VisualizationSyncSystem` surface.
- **Slice B (CPUContracted) — landed.** Added `Graphics::AttributeSource{VertexAttribute,Texture}` + `MaterialChannel` + `Set/GetChannelSource`, and `MaterialParams::ChannelSourceBits` → `GpuMaterialSlot.ChannelSourceBits` (former `_pad1`, layout-preserving) + GLSL mirror, constants, and the `GpuMaterialChannelSource` accessor. Both promoted `ResolveSurfaceNormal` paths (forward `default_debug_surface.frag`, deferred `gbuffer.frag`) now gate the Normal texture lane on the per-channel source (legacy `ObjectSpaceNormalMap` flag kept as transitional alias; default `VertexAttribute` preserves behavior). The producer (`ResolveTextureAssetBindings`) sets the Normal channel source = Texture wherever it sets the object-space flag. Tests: channel-source round-trip, producer mirrors flag↔source, shader-source contract in both promoted paths. **Build/CPU gate not run here (vcpkg egress block).** Defers other channels (color/MR/emissive/generic) and UI.
- **Slice C (CPUContracted).** Mesh-only gating in extraction (point cloud/graph reject `Texture` source), with contract tests across domains.
- **Slice D (CPUContracted).** Editor UI per-attribute source selector for meshes, disabled for non-mesh domains; command contract tests.
- **Slice E (Operational).** End-to-end Vulkan wiring + opt-in `gpu;vulkan` smoke proving the normal `Texture` source uses vertex normals before `Ready` and the baked texture after. Generalize remaining channels (albedo/metallic-roughness/emissive/generic) as thin follow-ons here or in a named follow-up.

## Implementation design (data-driven, single receiver)

The principle: **data controls which lane is active; one shader receiver reads
that data; runtime only *populates* the data (no per-frame material synthesis).**
Three data inputs, one receiver, zero override-material creation.

### Data (the only things that decide a lane)
- **Material slot** (`GpuMaterialSlot`, `RHI.Types.cppm:460-482`) — the single
  surface authority. Spend the reserved 16B padding (`_pad0.._pad3`, lines
  474-477) on:
  - `uint ShadingModel` — `0 = Lit`, `1 = Unlit`. The **only** lit/unlit
    authority. Replaces the `MaterialFlags::Unlit` bit and the
    `MaterialTypeID == DefaultDebugSurface` shader branch.
  - `uint ChannelSourceBits` — 2 bits per `VertexChannel`
    (`0 = VertexAttribute`, `1 = Texture`); V1 uses the Normal field, others
    reserved. Mirrored in `gpu_scene.glsl` (`GpuMaterialSlot`, lines 165-180)
    and written by `MaterialSystem::PackSlot` (`Graphics.MaterialSystem.cpp:157-172`)
    — no new upload path, `SetParams`/`SyncGpuBuffer` already carry it.
- **Entity visualization config** (`GpuEntityConfig`, `gpu_scene.glsl:141`,
  `ColorSourceMode` at 154; set by `GpuWorld::SetEntityConfig`,
  `Graphics.GpuWorld.cppm:222`) — the **scivis data overlay** (scalar field /
  per-element color). Already read by the shader
  (`GpuVisualizationReadColor` / `GpuResolveVisualizationColorFallback`,
  `gpu_scene.glsl:355,392`). Stays as pure data; **no override material**.
- **Vertex attribute BDA buffers** (`GpuGeometryRecord`: Position/Normal/
  Texcoord/Color, `RHI.Types.cppm:140-157`) — always present, the attribute lane.

### Receiver (one shader, fixed resolution order)
The unified surface frag (forward `default_debug_surface.frag` + deferred
`gbuffer.frag`) resolves, in order:
1. `baseColor` = material PBR (`BaseColorFactor` / albedo per Color source).
2. **overlay**: if `GpuEntityConfig.ColorSourceMode != None`, replace `baseColor`
   with the scivis color (existing helpers) — data-viz modes are intentionally
   unlit and say so via the config, not via a synthesized material.
3. `N` = vertex normal; if `ChannelSource(Normal) == Texture` and the normal
   texture is `Ready`, sample+decode; else vertex normal (existing
   `ResolveSurfaceNormal` fallback shape).
4. **shade**: `ShadingModel == Unlit` (or an unlit overlay) → `outColor =
   baseColor`; else `lighting(N, baseColor)`. Remove the
   `MaterialTypeID == DefaultDebugSurface` branch (`default_debug_surface.frag:137-138`).

### Producers (populate data in one place each)
- One shared `ResolveImportedMeshMaterial` helper assigns a real base material
  for **every** mesh import route (folds in RUNTIME-128's `EnsureDefaultLitMaterial`
  and the `3485151` direct-import fix): lit `StandardPBR` by default, `Unlit`
  only when the asset says so (`KHR_materials_unlit`).
- Per-channel source defaults chosen at import (Normal = VertexAttribute unless
  an authored/baked normal texture exists).
- Graph/PointCloud imports assign a material too (no `UniformColor` unlit
  default) and never set a `Texture` channel source.

## Cleanup — removal inventory (the old wrong implementation)

Delete/collapse, with the single data path replacing each:

- [ ] **Import-as-UniformColor default** — `ImportedGeometryVisualization()` /
  `ImportedMeshVisualization()` (`Runtime.Engine.cpp:292-310`, callers at
  1088/1156/1208). Replace with `ResolveImportedMeshMaterial`. This removes the
  `3485151` workaround by subsumption.
- [ ] **Override-material synthesis** in `VisualizationSyncSystem` for
  `UniformColor` (`BuildUniformColorParams`,
  `Graphics.VisualizationSyncSystem.cpp:229-239`) and the per-entity
  `EnsureOverrideLease`/`OverrideLeases`/`EffectiveSlot`-per-frame machinery
  (lines 56-86, 553-589). The scivis data path (`GpuEntityConfig`) already
  exists; the synthesized SciVis material is redundant.
- [ ] **Scivis override materials** — `BuildScalarFieldParams`/`BuildPerElementParams`
  (`:189-227`, `:242-250`). KEEP the visualization *capability* via
  `GpuEntityConfig` data; remove the material synthesis. (Design decision below.)
- [ ] **Dual lit/unlit shader branch** — drop `MaterialTypeID == DefaultDebugSurface`
  from the unlit test (`default_debug_surface.frag:137-138`); lit/unlit = `ShadingModel`.
- [ ] **`MaterialFlags::Unlit` as the authority** — migrate its 4 writers
  (`MaterialSystem.cpp:327`; `VisualizationSyncSystem.cpp:195,234,245`) to
  `ShadingModel`; retire the flag bit (or keep as a deprecated alias for one slice).

### Keep (intentional, not part of the bug)
- **Slot 0 `DefaultDebugSurface`** (`MaterialSystem.cpp:313-338`) as the genuine
  *invalid-material-handle* indicator only — its unlit purple is correct for a
  true error. RUNTIME-128 already moved *missing-material* off this slot.
- **Scivis visualization** (scalar field / per-element color, colormaps) — a
  first-class research feature; only its *delivery mechanism* changes (data, not
  synthesized material).

### Key design decision (scope of the collapse)
- **Recommended:** fully collapse `VisualizationSyncSystem` override-material
  synthesis into the existing `GpuEntityConfig` data path read by the one shader
  receiver — best matches "data controls the lane, one receiver, no runtime
  trashing," and removes per-entity override leases + per-frame `EffectiveSlot`
  rewrites entirely.
- **Smaller alternative:** keep override-material synthesis for scivis only, and
  just stop using `UniformColor` as the import default. Less code churn, but
  leaves two appearance-delivery mechanisms. Prefer the collapse unless the
  scivis override path has behavior the data path can't yet express (verify the
  `GpuEntityConfig` shader helpers cover scalar isolines/binning + per-edge/face
  before deleting `Build*Params`).
