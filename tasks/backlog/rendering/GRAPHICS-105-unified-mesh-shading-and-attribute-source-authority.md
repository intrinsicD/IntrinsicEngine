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
- Make the **effective per-renderable material instance** the single shader-facing authority for ordinary mesh shading: one lit/unlit decision (`ShadingModel`) and one explicit source decision for each contracted material channel (`Normal` in this task), resolved by the shared forward/deferred shader path. Runtime composes authored material defaults with per-renderable `AssetId` bindings without mutating other renderables that share the authored material. This task closes the object-space normal channel end to end; structural geometry bindings and scientific visualization overlays remain orthogonal.

## Non-goals
- Adding texture-sourced attributes to point clouds or graphs — those render from vertex attributes only, and the texture-source option must not exist for them.
- Tangent-space normal-map authoring or MikkTSpace tangent generation (object-space bake stays the texture path for normals per GRAPHICS-104).
- Removing debug visualization overlays (scalar field / per-vertex / per-face color) — they stay as an explicit, clearly-separate debug layer, just no longer a lit/unlit authority for ordinary mesh imports.
- New vertex channels beyond the existing `VertexChannel` set.
- Claiming that one binary `AttributeSource` contract already covers every PBR channel or arbitrary geometry property. `Normal` is the V1 operational channel; existing authored albedo, metallic-roughness, and emissive slots remain supported without making generalized property baking part of this task.
- Replacing the generic CPU compatibility baker or the GPU bake scheduling delivered by RUNTIME-109/GRAPHICS-104/RUNTIME-129. The separate RUNTIME-190 task now owns generalized interactive property baking and may land independently; this task consumes only the material-authority result it needs.
- Introducing a public global `TexturePool`, a second texture manager, or another GPU-resource owner. CPU payloads remain asset-owned, `GpuAssetCache` owns asset-to-GPU residency, and the RHI `TextureManager` owns durable GPU texture leases.
- Adding another texture-bake module, controller hierarchy, queue/registry framework, or role-named `Service`/`Binding`/`Submission` family. RUNTIME-190 is the dedicated right-sized `IRuntimeModule` consolidation; GRAPHICS-105 neither duplicates nor expands it.
- Synchronous GPU readback or a general-purpose texture debugger. The editor may inspect CPU-backed payload values and preview ready GPU textures, but must report when numeric GPU-only inspection is unavailable.
- Defining a second edge/halfedge-to-surface lifting rule. RUNTIME-190 explicitly defines mesh-edge baking as nearest-triangle-edge selection in UV space; that generalized bake policy is outside this normal-channel authority task.

## Context
- Owner/layer: `graphics` for the material shading-model + per-channel attribute-source metadata and the unified shader resolution; `runtime` for uniform default-lit material assignment across import routes, extraction, and mesh-only gating; `app`/editor for the UI selector.
- The architectural smell — **two lit/unlit authorities** historically existed:
  1. `Graphics::Components::VisualizationConfig::ColorSource::UniformColor` explicitly set `MaterialFlags::Unlit` (`Graphics.Component.VisualizationConfig.cppm:60-68`, resolved in `Graphics.VisualizationSyncSystem.cpp`). The `main` commit `3485151` worked around this for the **direct import** route by switching it to `ColorSource::Material`; BUG-052 removed the visualization-mode-to-unlit coupling for uniform, scalar, and per-element SciVis overrides.
  2. The material slot/type itself (`DefaultDebugSurface` + `MaterialFlags::Unlit`). RUNTIME-128 fixed the **model-scene** route by binding a lit default instead of the unlit slot 0.
  These route-specific fixes are symptoms of the missing single authority; this task consolidates them.
- The correct model (matches glTF 2.0 / mainstream PBR renderers): material owns a `ShadingModel { Lit, Unlit }` where **Unlit is an explicit opt-in**, never a fallback; missing material → a default **lit** material; vertex normals are guaranteed at import; an object-space normal texture overrides the always-present vertex normal only while the exact texture generation is ready. glTF `KHR_materials_unlit` maps onto `ShadingModel::Unlit`.
- Keep three vocabularies separate:
  - `Runtime::VertexChannel` describes structural geometry streams such as position, normal, texcoord, color, and tangent.
  - `Graphics::MaterialChannel` describes shader-facing PBR appearance channels and their effective material slots.
  - `Graphics::Components::VisualizationConfig` describes scientific/debug presentation such as scalar fields, colormaps, and per-element color.
  A single generic enum must not blur these contracts. Mesh drawing still requires positions plus topology/indices; texcoords are optional until a selected texture source needs them.
- Existing building blocks to reuse: `MaterialFlags` (`Unlit`, `ObjectSpaceNormalMap`), the `VertexChannel`/`VertexAttributeBinding` resolver (`Runtime.VertexAttributeBinding.cppm`, RUNTIME-120), generic mesh attribute texture bake (RUNTIME-109), GPU object-space normal bake (GRAPHICS-104) + scheduling (RUNTIME-129), the independently owned generalized `TextureBakeModule` (RUNTIME-190), default-lit material for material-less model-scene imports (RUNTIME-128), and direct-import material-driven shading (`3485151`).
- Existing texture ownership is already layered: `AssetService` owns CPU identities/payloads; `GpuAssetCache` owns generation-qualified asset residency; the RHI `TextureManager` owns durable backend leases; frame-graph allocators own transient textures. `MaterialTextureAssetBindings` carries `AssetId`s keyed by stable render id and must not become a second texture owner.
- "Works end to end / only this path exists": this task must **remove** the divergent lit/unlit decision for ordinary mesh imports, not merely add a parallel one, and prove the unified path through the real runtime extraction path plus an opt-in Vulkan smoke.
- ARCH-013 re-review (2026-07-08): Decision confirmed with a standing-event
  requirement. Attribute-source and generated-texture readiness changes must be
  observable through kernel events or a standing runtime reaction that feeds
  extraction/dirty-attribute state; no `Engine` callback or direct
  renderer/runtime live coupling should be introduced. This is the graphics
  consumer side of ADR-0024 D6.
- Exact identity, generation, stale-completion rejection, frame-ready publication, deferred retirement, UV validity, and progressive fallback are load-bearing correctness mechanisms. Right-sizing may collapse forwarding files, but must not delete these invariants.

### Locked design decisions
- **Effective authority and edit scope.** The authored/shared material supplies defaults. Each runtime renderable owns a material instance/lease and per-renderable `MaterialTextureAssetBindings`; selecting or generating a texture for entity A must not mutate entity B merely because both originated from the same material asset. Editing the shared material asset is a separate explicit operation. Graphics receives one resolved material slot, not a base material plus a competing live override.
- **Desired state vs. operational state.** The serialized projection of `ProgressivePresentationBindings` stores values, enums, property names, stable asset identities, and requested source choices — never component references, borrowed pointers, bindless indices, GPU handles, job handles, readiness, or diagnostics. Runtime-only fields/sidecars/snapshots own pending/ready/failed/stale state and generation tokens. Commands carry a stable entity id and resolve/validate the entity and mesh domain when applied.
- **V1 source scope.** `Normal` is the required `VertexAttribute | Texture` channel for GRAPHICS-105. The vertex normal remains mandatory and is the fallback until the exact object-space texture is ready. RUNTIME-190 may expose separately contracted scalar/color/normal consumers; their existence does not broaden this task's completion claims.
- **Color and scalar semantics.** Authored base color belongs to the material; scientific scalar/per-element color belongs to `VisualizationConfig`. A future scalar texture source must preserve raw linear scalar values plus range/encoding metadata and apply the colormap in the shader; baking an RGBA colormap is an explicit export/material-color operation, not the default scientific representation. Normal textures carry explicit object-space encoding; true color textures carry explicit linear/sRGB metadata.
- **Surface-domain semantics.** Vertex values interpolate over triangles and face values are flat per face. RUNTIME-190's separate generalized path uses its documented nearest-triangle-edge policy for edge fields; GRAPHICS-105 defines no alternate rule. UV seams, overlap, degenerate triangles, padding/dilation, and source/texcoord dirty stamps are explicit validation/invalidation concerns, never silent assumptions.
- **UI and command ownership.** `app` owns ImGui/menu layout. Runtime exposes data-only commands and snapshots; it does not expose GUI callbacks or retain component references. The UI uses the same command/apply path available to tests and agents. Per-entity selections are scene state; any engine-wide default or bake policy introduced later must also round-trip through the validated config lane.
- **Preview boundary.** Reuse the existing UV/background-texture view for ready image preview. Decode hover values from retained CPU payloads using texture metadata. A GPU-only texture reports that numeric inspection needs asynchronous readback; this task must not stall the frame with an implicit readback.
- **Unlit provenance.** Only source assets (`KHR_materials_unlit`) or an explicit user command select `Unlit`; missing material/data never does.

## Control surfaces
- Scene/serialization: the desired `ProgressivePresentationBindings`/material/visualization projection and stable asset identities only; no transient runtime state.
- Runtime/test/agent: existing stable-id editor commands plus read-only models/snapshots for requested/effective source, output `AssetId`, encoding, readiness, and diagnostics.
- UI: Sandbox editor renders those snapshots and submits the same commands; no private material-system mutation path.
- Engine config: only engine-wide defaults/policies, if added by a slice, use preview/validate-then-apply and file round-trip. Per-renderable edits are not duplicated into global config.

## Required changes
- [ ] Finish material `ShadingModel { Lit, Unlit }` as the sole ordinary-surface lighting authority: map `KHR_materials_unlit`, migrate remaining `MaterialFlags::Unlit` writers, remove transitional shader/type branches, and keep slot 0's explicit error material distinct from the missing-material default.
- [ ] Resolve imported mesh material policy through one shared runtime helper for direct and model-scene routes: preserve authored material data, otherwise allocate a default **lit** material instance.
- [ ] Make the per-renderable material lease plus stable-id-keyed `MaterialTextureAssetBindings` the effective appearance authority. Merging a generated normal `AssetId` must preserve the other slots and must not mutate another renderable that shares the authored material.
- [ ] Complete the `Normal` `AttributeSource { VertexAttribute, Texture }` path: runtime/material resolution publishes `Texture` plus a valid bindless normal only for the exact ready object-space asset generation; both promoted forward and deferred shaders sample/decode that effective texture and otherwise use the required vertex normal.
- [ ] Route source-choice and generated-texture readiness changes through a standing runtime reaction/kernel event that invalidates the affected extraction/material state. Preserve exact identity/generation checks, stale completion rejection, and frame-ready/deferred-retire behavior.
- [ ] Collapse `VisualizationSyncSystem` override-material synthesis into the existing `GpuEntityConfig` visualization-data path once contract tests prove all retained scalar/color/isoline modes are representable. Fill a missing data field if needed; do not preserve a second material authority merely as a delivery mechanism.
- [ ] Enforce normal-source capability at the command, extraction, and UI seams: only mesh surfaces with valid topology, texcoords, and normal properties may select/bake this task's normal texture source; point clouds and graphs remain vertex-buffer-only. Generalized vertex/face/nearest-edge property gating belongs to RUNTIME-190.
- [ ] Extend the existing progressive-presentation/editor command and model surfaces for the selected renderable's requested/effective normal source, source/output identities, encoding, readiness, and diagnostics. Commands resolve the stable entity id at apply time and participate in existing undo/serialization; no new facade/module or persistent controller object holds an ECS reference.
- [ ] In the app-owned editor panel, expose the normal source selector and bake request for eligible meshes, the effective/fallback state, output `AssetId` and encoding, and a ready-texture preview through the existing UV view. Decode CPU-backed hover values; report GPU-only numeric inspection as unavailable without async readback.
- [ ] Audit the reserved `Color`, `MetallicRoughness`, and `Emissive` source bits: keep existing authored texture behavior intact, but hide or reject any generalized property-source choice that this task does not contract and test.

## Tests
- [ ] CPU/null contract: material `ShadingModel` is the only lit/unlit authority — an imported mesh with a lit material shades regardless of `VisualizationConfig`, and `Unlit` only results from explicit shading-model.
- [ ] CPU/null contract: default **lit** material assigned identically through both the direct and model-scene import routes (one helper, one result).
- [ ] CPU/null contract: two renderables sharing one authored material receive independent effective material leases/bindings; changing or completing the normal bake for one leaves the other's source, slots, and generations unchanged.
- [ ] CPU/null contract: normal `AttributeSource` resolution — `Texture` with an absent/fallback/stale/non-ready generation uses the vertex normal; the exact `Ready` object-space texture is used; `VertexAttribute` ignores a bound texture.
- [ ] CPU/null contract: readiness/source events dirty only the affected stable render id, stale completions cannot publish, and rebinding preserves unrelated albedo/metallic-roughness/emissive slots.
- [ ] CPU/null contract: retained `VisualizationConfig` modes reach `GpuEntityConfig` without allocating or rewriting an override material and do not change the base material's `ShadingModel`.
- [ ] CPU/null contract: mesh-only gating — point cloud and graph domains reject/ignore a `Texture` channel source and resolve from vertex attributes.
- [ ] CPU/null contract: editor/runtime command resolves a stable entity id at apply time, accepts an eligible mesh, rejects invalid/non-mesh/missing-texcoord targets with diagnostics, and never retains an ECS reference.
- [ ] CPU/null contract: editor model reports requested/effective source, output identity/encoding/readiness/fallback; CPU-backed preview samples decode correctly, while GPU-only numeric inspection reports its unsupported/readback-required state.
- [ ] Shader-source contract: promoted forward and deferred receivers use the same `ShadingModel` and normal-source/readiness rules; no material-type or `VisualizationConfig` lit/unlit branch remains for ordinary surfaces.
- [ ] Opt-in `gpu;vulkan` smoke: a mesh with the normal channel set to `Texture` renders with the vertex normal before the bake completes and with the baked object-space normal texture after `Ready`.

## Docs
- [ ] Update `src/graphics/renderer/README.md` and `src/graphics/assets/README.md` for the single effective-material authority, normal-source resolution/fallback, and the existing asset/cache/RHI texture-ownership chain.
- [ ] Update `src/runtime/README.md` for uniform default-lit assignment, per-renderable binding isolation, desired-vs-operational state, stable-id command/snapshot semantics, and mesh-only gating.
- [ ] Add/extend the canonical graphics/runtime architecture docs for the three orthogonal contracts (structural geometry / material appearance / scientific visualization), app-owned UI, and the single-authority rule. Add an ADR only if implementation discovers a hard-to-reverse decision not already covered by ADR-0016/0024/0027.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if any `.cppm` surfaces change.

## Acceptance criteria
- [ ] Exactly one lit/unlit authority exists for mesh surfaces (the material `ShadingModel`); `VisualizationConfig` no longer decides lit/unlit for ordinary mesh imports.
- [ ] Imported meshes shade end to end through the material via the real runtime extraction path, identically across the direct and model-scene routes.
- [ ] The selected renderable's effective normal source is independently controllable and observable; another renderable sharing its authored material is unaffected.
- [ ] The normal texture path falls back to the vertex normal until the exact generated texture is `Ready`, then changes through the standing extraction reaction without direct engine/renderer callbacks.
- [ ] The editor exposes only contracted choices, distinguishes requested from effective/fallback source, shows output identity/encoding/readiness and a ready preview, and never mutates the material system through an app-private path.
- [ ] Structural geometry channels, PBR material channels, and scientific visualization sources remain distinct contracts; scalar fields are not silently converted to RGBA material color.
- [ ] Point clouds and graphs are unaffected and have no texture-source option.
- [ ] CPU payload, asset-to-GPU residency, and durable/transient GPU texture ownership remain with the existing owners; no global public texture pool or duplicate texture manager is introduced.
- [ ] No controller retains component references and no additional `IRuntimeModule`/queue/registry/facade family or parallel editor-command surface is introduced by this task; the independently scoped RUNTIME-190 module is reused where applicable.
- [ ] No layering violations; no `Vk*` across RHI/renderer/runtime APIs; graphics-owned modules carry no live ECS/runtime/AssetService knowledge.
- [ ] `Operational` is cited by an actually-run `gpu;vulkan` smoke for the normal texture path; CPU contract gate is green for authority, per-renderable isolation, exact-ready fallback, route-uniform defaults, visualization separation, command/snapshot behavior, and domain gating.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
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
- Mutating a shared authored material as an accidental consequence of editing or completing a bake for one renderable.
- Treating `Runtime::VertexChannel`, `Graphics::MaterialChannel`, and `VisualizationConfig` sources as one interchangeable enum or claiming untested source kinds as supported.
- Enabling a texture attribute source for point cloud or graph domains.
- Inventing an alternate implicit edge/halfedge lifting rule or ignoring missing/invalid UVs; generalized edge baking must use RUNTIME-190's explicit nearest-triangle-edge policy.
- Using `Unlit` as a missing-material/missing-data fallback instead of an explicit shading-model choice.
- Persisting component references, borrowed pointers, GPU/bindless/job handles, or operational readiness as scene/config authoring state.
- Introducing a public global texture pool/manager, another `IRuntimeModule`, or a controller/queue/registry/facade framework in this authority task instead of reusing RUNTIME-190.
- Importing ImGui/app concerns into runtime or lower layers, or adding an app-only mutation path that bypasses runtime commands.
- Blocking the frame for synchronous GPU texture readback in the editor.
- Passing `Vk*` types through RHI/renderer/runtime/cache public APIs.
- Adding live ECS/runtime/AssetService knowledge to graphics-owned modules.
- Deleting the CPU bake path before the GPU texture path is proven end to end.
- Mixing this consolidation with unrelated renderer/runtime/asset/UI features.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the authority/resolution/gating contracts on CPU/null.
- Slices A–E close `CPUContracted`; Slice F closes `Operational` for the normal texture path and cites an actually-run `gpu;vulkan` smoke. Depends on RUNTIME-129 for GPU scheduling.
- This task does not use RUNTIME-190's generalized property-to-texture baking or nearest-edge semantics to claim GRAPHICS-105 `Operational` maturity; its own normal-source Vulkan smoke remains required. GPU-only numeric texture inspection is also out of scope.

## Slice plan
- **Slice A (CPUContracted).** Add material `ShadingModel` as the single lit/unlit authority; route the unified shader to honor it; assign a default **lit** material uniformly across both import routes via one helper (subsuming RUNTIME-128 + `3485151`); demote `VisualizationConfig` lit/unlit role for imports. Defers attribute-source and UI to later slices.
  - **A1 — landed (this slice).** `Graphics::ShadingModel{Lit,Unlit}` added to `MaterialParams` (default `Lit`) and `GpuMaterialSlot.ShadingModel` (former `_pad0`, layout-preserving) + GLSL mirror/constants; `PackSlot` writes it; the forward surface shader's lit/unlit gate now keys on `ShadingModel` (legacy `MaterialFlags::Unlit` kept as a transitional alias) and the `DefaultDebugSurface` type-branch is removed from the gate; slot-0 default and RUNTIME-128's default-lit material set `ShadingModel` explicitly. Tests: material-system round-trip + default-slot `Unlit`, shader-source contract. **Build/CPU gate not run here (vcpkg egress block).**
  - **A2 — remaining.** Map glTF `KHR_materials_unlit` → `ShadingModel::Unlit` at import; one shared `ResolveImportedMeshMaterial` helper across both routes; migrate the remaining `MaterialFlags::Unlit` writers off the flag. Folded into the scivis-collapse slice below since they share the `VisualizationSyncSystem` surface.
- **Slice B (CPUContracted) — landed.** Added `Graphics::AttributeSource{VertexAttribute,Texture}` + `MaterialChannel` + `Set/GetChannelSource`, and `MaterialParams::ChannelSourceBits` → `GpuMaterialSlot.ChannelSourceBits` (former `_pad1`, layout-preserving) + GLSL mirror, constants, and the `GpuMaterialChannelSource` accessor. Both promoted `ResolveSurfaceNormal` paths (forward `default_debug_surface.frag`, deferred `gbuffer.frag`) now gate the Normal texture lane on the per-channel source (legacy `ObjectSpaceNormalMap` flag kept as transitional alias; default `VertexAttribute` preserves behavior). The producer (`ResolveTextureAssetBindings`) sets the Normal channel source = Texture wherever it sets the object-space flag. Tests: channel-source round-trip, producer mirrors flag↔source, shader-source contract in both promoted paths. **Build/CPU gate not run here (vcpkg egress block).** Keeps non-Normal source semantics out of the UI/acceptance scope and defers the normal controls to Slice E.
- **Slice C (CPUContracted).** Finish the single-authority cleanup: shared imported-material policy, `KHR_materials_unlit`, retirement of transitional lit/unlit writers/branches, and collapse of visualization override-material synthesis after retained visualization-mode contracts pass.
- **Slice D (CPUContracted).** Make the normal binding explicitly per-renderable; preserve unrelated slots, prove shared-authored-material isolation, exact-generation fallback, standing readiness invalidation, stale rejection, and mesh/domain/UV gating.
- **Slice E (CPUContracted).** Extend the existing stable-id progressive/editor command and model surfaces plus app-owned controls. Show requested/effective/fallback state, output metadata, ready UV preview, CPU-backed decoded hover values, and an explicit GPU-readback-required state without adding a parallel facade or synchronous readback path.
- **Slice F (Operational).** End-to-end Vulkan wiring + opt-in `gpu;vulkan` smoke proving the normal `Texture` source uses vertex normals before `Ready` and the exact baked object-space normal texture after. RUNTIME-190's other generalized property sources remain outside GRAPHICS-105 evidence and closure.

## Implementation design (data-driven, one effective receiver)

The principle: **data controls which lane is active; one shader receiver reads
that data; runtime resolves one per-renderable material instance (no per-frame
override-material synthesis).** Desired authoring state and operational bake
state stay separate.

### Data (the only things that decide a lane)
- **Effective material slot** (`GpuMaterialSlot`, `RHI.Types.cppm`) — the single
  ordinary-surface shader authority. The landed slices use former reserved
  fields for:
  - `uint ShadingModel` — `0 = Lit`, `1 = Unlit`. The **only** lit/unlit
    authority. Replaces the `MaterialFlags::Unlit` bit and the
    `MaterialTypeID == DefaultDebugSurface` shader branch.
  - `uint ChannelSourceBits` — 2 bits per `MaterialChannel`
    (`0 = VertexAttribute`, `1 = Texture`); GRAPHICS-105 contracts Normal and
    treats other fields as existing/reserved until their semantics are tested.
    Mirrored in `gpu_scene.glsl` and written by `MaterialSystem::PackSlot` — no
    new upload path is required.
- **Per-renderable desired bindings** (`ProgressivePresentationBindings`) — the
  scene-owned data record for requested source kind, property/asset identities,
  and generated-output policy. Its serialized projection omits transient
  readiness and diagnostics.
- **Per-renderable effective texture bindings** (`MaterialTextureAssetBindings`,
  keyed by stable render id in `RenderExtractionCache`) — resolved asset
  identities for the renderable. Resolution through `GpuAssetCache` updates
  that renderable's material lease only and preserves unrelated slots.
- **Entity visualization config** (`GpuEntityConfig`, `gpu_scene.glsl:141`,
  `ColorSourceMode` at 154; set by `GpuWorld::SetEntityConfig`,
  `Graphics.GpuWorld.cppm:222`) — the **scivis data overlay** (scalar field /
  per-element color). Already read by the shader
  (`GpuVisualizationReadColor` / `GpuResolveVisualizationColorFallback`,
  `gpu_scene.glsl:355,392`). Stays as pure data; **no override material**.
- **Structural geometry buffers** (`GpuGeometryRecord`: Position/Normal/
  Texcoord/Color plus topology/index data) — the geometry lane. Positions and
  valid surface topology are mandatory; the normal lane is mandatory for this
  task; texcoords are required only when the texture source is requested.
- **Runtime operational sidecar/snapshot** — request token, source/texcoord
  dirty stamps, exact asset generation, readiness, failure/stale diagnostics,
  and frame-ready state. None of these are alternate shader authorities or
  serialized config values.

### Receiver (one shader, fixed resolution order)
The unified surface frag (forward `default_debug_surface.frag` + deferred
`gbuffer.frag`) resolves, in order:
1. `baseColor` = material PBR (`BaseColorFactor` / albedo per Color source).
2. **overlay**: if `GpuEntityConfig.ColorSourceMode != None`, replace `baseColor`
   with the scivis result (existing helpers). Overlay composition is a distinct
   visualization policy; it does not rewrite the base material or create a
   second ordinary-surface `ShadingModel`.
3. `N` = vertex normal; if `ChannelSource(Normal) == Texture` and the normal
   bindless id is valid, sample+decode object-space normal; else use the vertex
   normal. Runtime/material resolution guarantees that this effective texture
   state is published only for the exact requested `Ready` generation.
4. **shade/compose**: ordinary material output keys only on `ShadingModel`.
   Explicit visualization overlays use their documented composition mode.
   Remove material-type and visualization-to-material flag coupling.

### Producers (populate data in one place each)
- One shared `ResolveImportedMeshMaterial` helper assigns a real base material
  for **every** mesh import route (folds in RUNTIME-128's `EnsureDefaultLitMaterial`
  and the `3485151` direct-import fix): lit `StandardPBR` by default, `Unlit`
  only when the asset says so (`KHR_materials_unlit`).
- Runtime creates/retains an effective material lease per renderable, applies
  authored defaults, then merges that stable render id's texture bindings.
- Requested normal source defaults to `VertexAttribute`. A request may remain
  `Texture` while pending, but the effective material stays on the vertex
  fallback until the exact authored/generated object-space generation is
  ready. The standing reaction dirties the affected renderable when effective
  state changes.
- Graph/PointCloud imports assign a material too (no `UniformColor` unlit
  default) and never set a `Texture` channel source.
- Existing editor commands identify the target by stable entity id. Runtime resolves the
  live scene/entity/domain/UV state at apply time and returns a data-only
  diagnostic snapshot; no GUI callback or stored component reference crosses
  the seam.

### Bake/inspection semantics retained at this seam
- Object-space normal: encode/decode convention is explicit metadata; vertex
  normal is the progressive fallback.
- True color: preserve declared linear/sRGB color space.
- Scientific scalar: preserve raw linear values and range/encoding metadata;
  apply colormap during visualization. RGBA colormap baking is opt-in export or
  material-color authoring, not the default scalar lane.
- Vertex and face surface domains keep their existing interpolation/flat
  meaning. Generalized mesh-edge requests use RUNTIME-190's explicit nearest
  triangle-edge rule; this task neither changes nor duplicates it.

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

### Locked cleanup/right-sizing decision
- Collapse `VisualizationSyncSystem` override-material synthesis into the
  existing `GpuEntityConfig` data path. Before deletion, contract-test scalar
  range/colormap/isoline/binning and per-vertex/edge/face color behavior; if a
  mode is not representable, add the smallest missing data field rather than
  preserving a competing material authority.
- Reuse `ProgressivePresentationBindings`, the existing editor command/model
  surface, the per-renderable material lease, `MaterialTextureAssetBindings`,
  `GpuAssetCache`, RHI `TextureManager`, RUNTIME-129 scheduling, and UV view.
  Do not add a controller class, manager/pool, module registry, or another
  Service → Queue → Binding → Submission chain.
- RUNTIME-190 is the separate texture-bake runtime-module consolidation. Its
  keep-list is the real import/editor consumers plus cross-frame GPU lifecycle;
  GRAPHICS-105 reuses that published service and preserves its
  identity/readiness invariants rather than wrapping it.
