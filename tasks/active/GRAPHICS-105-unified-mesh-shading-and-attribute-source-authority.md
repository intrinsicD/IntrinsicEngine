---
id: GRAPHICS-105
theme: B
depends_on:
  - GRAPHICS-104
  - RUNTIME-128
  - RUNTIME-129
  - RUNTIME-191
  - RUNTIME-192
  - RUNTIME-193
  - RUNTIME-197
  - RUNTIME-198
maturity_target: Operational
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive refactoring; evidence is the diff, tests, and review
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
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
- Replacing the unified property-to-texture producer. `RUNTIME-191` removes the
  CPU/specialized compatibility paths and owns the sole bake execution route;
  this task consumes its completed output through caller-owned material/
  presentation processing.
- Introducing a public global `TexturePool`, a second texture manager, or another GPU-resource owner. CPU payloads remain asset-owned, `GpuAssetCache` owns asset-to-GPU residency, and the RHI `TextureManager` owns durable GPU texture leases.
- Adding another texture-bake module, controller hierarchy, queue/registry
  framework, or role-named `Service`/`Binding`/`Submission` family.
  `RUNTIME-191` is the dedicated right-sized consolidation; GRAPHICS-105
  neither duplicates nor expands it.
- Synchronous GPU readback or a general-purpose texture debugger. The editor may inspect CPU-backed payload values and preview ready GPU textures, but must report when numeric GPU-only inspection is unavailable.
- Defining a second edge/halfedge-to-surface lifting rule. `RUNTIME-191`
  explicitly defines mesh-edge baking as nearest-triangle-edge selection in UV
  space; that generalized bake policy is outside this normal-channel
  authority task.

## Context
- GRAPHICS-144 completed compiler-dependency isolation. This task owns the
  remaining material/visualization authority cleanup; LEGACY-043 follows its
  explicit decision about the surviving deferred shader contract.
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
- Existing building blocks to reuse: `ShadingModel`, `MaterialFlags`
  (`ObjectSpaceNormalMap`), the structural `VertexChannel` resolver,
  `RUNTIME-192` canonical property references, the independently owned unified
  `TextureBakeModule` (`RUNTIME-191`), the general presentation recipe
  (`RUNTIME-193`), common residency (`RUNTIME-197`), visualization recipes
  (`RUNTIME-198`), default-lit material for material-less model-scene imports
  (`RUNTIME-128`), and direct-import material-driven shading (`3485151`).
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
- **Desired state vs. operational state.** The serialized
  `GeometryPresentationRecipe` (`RUNTIME-193`) stores values, canonical
  property references, stable asset identities, and requested source choices
  — never component references, borrowed pointers, bindless indices, GPU
  handles, job handles, readiness, or diagnostics. Runtime snapshots own
  pending/ready/failed/stale state and generation tokens. Commands carry a
  stable entity id and resolve/validate the entity and mesh domain when
  applied.
- **V1 source scope.** `Normal` is the required
  `VertexAttribute | Texture` material-channel source for GRAPHICS-105. The
  vertex normal remains mandatory and is the fallback until the exact
  object-space texture is ready. `RUNTIME-191` produces property textures but
  accepts no consumer/material semantics; this task owns the normal-channel
  result binding.
- **Color and scalar semantics.** Authored base color belongs to the material; scientific scalar/per-element color belongs to `VisualizationConfig`. A future scalar texture source must preserve raw linear scalar values plus range/encoding metadata and apply the colormap in the shader; baking an RGBA colormap is an explicit export/material-color operation, not the default scientific representation. Normal textures carry explicit object-space encoding; true color textures carry explicit linear/sRGB metadata.
- **Surface-domain semantics.** Vertex values interpolate over triangles and
  face values are flat per face. `RUNTIME-191` uses its documented
  nearest-triangle-edge policy for edge fields; GRAPHICS-105 defines no
  alternate rule. UV seams, overlap, degenerate triangles, padding/dilation,
  and source/texcoord dirty stamps are explicit validation/invalidation
  concerns, never silent assumptions.
- **UI and command ownership.** `app` owns ImGui/menu layout. Runtime exposes data-only commands and snapshots; it does not expose GUI callbacks or retain component references. The UI uses the same command/apply path available to tests and agents. Per-entity selections are scene state; any engine-wide default or bake policy introduced later must also round-trip through the validated config lane.
- **Preview boundary.** Reuse the existing UV/background-texture view for ready image preview. Decode hover values from retained CPU payloads using texture metadata. A GPU-only texture reports that numeric inspection needs asynchronous readback; this task must not stall the frame with an implicit readback.
- **Unlit provenance.** Only source assets (`KHR_materials_unlit`) or an explicit user command select `Unlit`; missing material/data never does.

## Control surfaces
- Scene/serialization: the desired `GeometryPresentationRecipe` material/
  visualization projection and stable asset identities only; no transient
  runtime state.
- Runtime/test/agent: existing stable-id editor commands plus read-only models/snapshots for requested/effective source, output `AssetId`, encoding, readiness, and diagnostics.
- UI: Sandbox editor renders those snapshots and submits the same commands; no private material-system mutation path.
- Engine config: only engine-wide defaults/policies, if added by a slice, use preview/validate-then-apply and file round-trip. Per-renderable edits are not duplicated into global config.

## Required changes
- [ ] Finish material `ShadingModel { Lit, Unlit }` as the sole ordinary-surface lighting authority: map `KHR_materials_unlit` and align promoted forward/deferred receivers. The redundant Unlit flag is removed; keep slot 0's explicit error material distinct from the missing-material default.
- [x] Allocate missing-material defaults through the existing render-extraction owner for both direct and model-scene routes, with an independent **lit** instance per renderable.
- [x] Remove redundant import-owned authored material leases and texture-resolution bookkeeping; extraction owns effective per-renderable materials.
- [ ] Complete authored factor/texture projection through that effective runtime owner, including base-color modulation for textured albedo and authored metallic-roughness texture routing; preserve existing supported recipe behavior.
- [ ] Make the per-renderable material lease plus stable-id-keyed `MaterialTextureAssetBindings` the effective appearance authority. Merging a generated normal `AssetId` must preserve the other slots and must not mutate another renderable that shares the authored material.
- [ ] Complete the `Normal` `AttributeSource { VertexAttribute, Texture }` path: runtime/material resolution publishes `Texture` plus a valid bindless normal only for the exact ready object-space asset generation; both promoted forward and deferred shaders sample/decode that effective texture and otherwise use the required vertex normal.
- [ ] Route source-choice and generated-texture readiness changes through a standing runtime reaction/kernel event that invalidates the affected extraction/material state. Preserve exact identity/generation checks, stale completion rejection, and frame-ready/deferred-retire behavior.
- [x] Collapse `VisualizationSyncSystem` override-material synthesis into the existing `GpuEntityConfig` visualization-data path once contract tests prove all retained scalar/color/isoline modes are representable. Fill a missing data field if needed; do not preserve a second material authority merely as a delivery mechanism.
- [ ] Enforce normal-source capability at the command, extraction, and UI
      seams: only mesh surfaces with valid topology, texcoords, and canonical
      normal property references may select/bake this task's normal texture
      source; point clouds and graphs remain vertex-buffer-only. Generalized
      vertex/face/nearest-edge property gating belongs to `RUNTIME-191`.
- [ ] Extend `GeometryPresentationRecipe` plus its typed editor operation and
      runtime snapshot for the selected renderable's requested/effective normal
      source, source/output identities, encoding, readiness, and diagnostics.
      Commands resolve the stable entity id at apply time and participate in
      existing undo/serialization; no facade/module or persistent controller
      object holds an ECS reference.
- [ ] In the app-owned editor panel, expose the normal source selector and bake request for eligible meshes, the effective/fallback state, output `AssetId` and encoding, and a ready-texture preview through the existing UV view. Decode CPU-backed hover values; report GPU-only numeric inspection as unavailable without async readback.
- [ ] Audit the reserved `Color`, `MetallicRoughness`, and `Emissive` source bits: keep existing authored texture behavior intact, but hide or reject any generalized property-source choice that this task does not contract and test.

## Tests
- [ ] CPU/null contract: material `ShadingModel` is the only lit/unlit authority — an imported mesh with a lit material shades regardless of `VisualizationConfig`, and `Unlit` only results from explicit shading-model.
- [x] CPU/null contract: default **lit** material assigned identically through both the direct and model-scene import routes (one owner, independent leases).
- [ ] CPU/null contract: two renderables sharing one authored material receive independent effective material leases/bindings; changing or completing the normal bake for one leaves the other's source, slots, and generations unchanged.
- [ ] CPU/null contract: normal `AttributeSource` resolution — `Texture` with an absent/fallback/stale/non-ready generation uses the vertex normal; the exact `Ready` object-space texture is used; `VertexAttribute` ignores a bound texture.
- [ ] CPU/null contract: readiness/source events dirty only the affected stable render id, stale completions cannot publish, and rebinding preserves unrelated albedo/metallic-roughness/emissive slots.
- [x] CPU/null contract: retained `VisualizationConfig` modes reach `GpuEntityConfig` without allocating or rewriting an override material and do not change the base material's `ShadingModel`.
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
- [ ] No controller retains component references and no additional
      `IRuntimeModule`/queue/registry/facade family or parallel editor-command
      surface is introduced by this task; the independently scoped
      `RUNTIME-191` module and caller-owned presentation operation are reused.
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
- Inventing an alternate implicit edge/halfedge lifting rule or ignoring
  missing/invalid UVs; generalized edge baking must use `RUNTIME-191`'s
  explicit nearest-triangle-edge policy.
- Using `Unlit` as a missing-material/missing-data fallback instead of an explicit shading-model choice.
- Persisting component references, borrowed pointers, GPU/bindless/job handles, or operational readiness as scene/config authoring state.
- Introducing a public global texture pool/manager, another `IRuntimeModule`,
  or a controller/queue/registry/facade framework in this authority task
  instead of reusing `RUNTIME-191` and `RUNTIME-193`.
- Passing material channels, consumers, or normal-space semantics into
  `TextureBakeService`; this task processes the completed generic texture
  result in its own presentation/material operation.
- Importing ImGui/app concerns into runtime or lower layers, or adding an app-only mutation path that bypasses runtime commands.
- Blocking the frame for synchronous GPU texture readback in the editor.
- Passing `Vk*` types through RHI/renderer/runtime/cache public APIs.
- Adding live ECS/runtime/AssetService knowledge to graphics-owned modules.
- Restoring the CPU/specialized bake paths already retired by `RUNTIME-191`.
  This task still owes its own end-to-end normal-source Vulkan evidence.
- Mixing this consolidation with unrelated renderer/runtime/asset/UI features.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the authority/resolution/gating contracts on CPU/null.
- Slices A–E close `CPUContracted`; Slice F closes `Operational` for the normal texture path and cites an actually-run `gpu;vulkan` smoke. Depends on RUNTIME-129 for GPU scheduling.
- This task does not use `RUNTIME-191`'s property-to-texture or nearest-edge
  evidence to claim GRAPHICS-105 `Operational` maturity; its own normal-source
  Vulkan smoke remains required. GPU-only numeric texture inspection is also
  out of scope.

## Slice plan
- **Slice A (CPUContracted).** Add material `ShadingModel` as the single lit/unlit authority; route the unified shader to honor it; use the existing extraction owner for default **lit** materials across both import routes; demote `VisualizationConfig` lit/unlit role for imports. Defers attribute-source and UI to later slices.
  - **A1 — landed (this slice).** `Graphics::ShadingModel{Lit,Unlit}` added to `MaterialParams` (default `Lit`) and `GpuMaterialSlot.ShadingModel` (former `_pad0`, layout-preserving) + GLSL mirror/constants; `PackSlot` writes it; the forward surface shader's lit/unlit gate now keys on `ShadingModel` (legacy `MaterialFlags::Unlit` kept as a transitional alias) and the `DefaultDebugSurface` type-branch is removed from the gate; slot-0 default and RUNTIME-128's default-lit material set `ShadingModel` explicitly. Tests: material-system round-trip + default-slot `Unlit`, shader-source contract. **Historical slice-time verification: build/CPU gate was blocked by vcpkg egress.**
  - **A2 — remaining.** Map glTF `KHR_materials_unlit` → `ShadingModel::Unlit` at import and consolidate authored material factors/bindings into the effective per-renderable state. Missing-material allocation already uses extraction alone, and the obsolete Unlit flag has been removed.
- **Slice B (CPUContracted) — landed.** Added `Graphics::AttributeSource{VertexAttribute,Texture}` + `MaterialChannel` + `Set/GetChannelSource`, and `MaterialParams::ChannelSourceBits` → `GpuMaterialSlot.ChannelSourceBits` (former `_pad1`, layout-preserving) + GLSL mirror, constants, and the `GpuMaterialChannelSource` accessor. Both promoted `ResolveSurfaceNormal` paths (forward `default_debug_surface.frag`, deferred `gbuffer.frag`) now gate the Normal texture lane on the per-channel source (legacy `ObjectSpaceNormalMap` flag kept as transitional alias; default `VertexAttribute` preserves behavior). The producer (`ResolveTextureAssetBindings`) sets the Normal channel source = Texture wherever it sets the object-space flag. Tests: channel-source round-trip, producer mirrors flag↔source, shader-source contract in both promoted paths. **Historical slice-time verification: build/CPU gate was blocked by vcpkg egress.** Keeps non-Normal source semantics out of the UI/acceptance scope and defers the normal controls to Slice E.
- **Slice C (CPUContracted), partial.** Visualization material synthesis and the duplicate Unlit flag are removed; missing-material defaults have one owner. Authored material policy, `KHR_materials_unlit` and the shared forward/deferred authority remain.
- **Slice D (CPUContracted).** Make the normal binding explicitly per-renderable; preserve unrelated slots, prove shared-authored-material isolation, exact-generation fallback, standing readiness invalidation, stale rejection, and mesh/domain/UV gating.
- **Slice E (CPUContracted).** Extend the stable-id
  `GeometryPresentationRecipe` operation/snapshot plus app-owned controls. Show
  requested/effective/fallback state, output metadata, ready UV preview,
  CPU-backed decoded hover values, and an explicit GPU-readback-required state
  without adding a parallel facade or synchronous readback path.
- **Slice F (Operational).** End-to-end Vulkan wiring + opt-in `gpu;vulkan`
  smoke proving the normal `Texture` source uses vertex normals before `Ready`
  and the exact baked object-space normal texture after. `RUNTIME-191`'s other
  generalized property sources remain outside GRAPHICS-105 evidence and
  closure.

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
- **Per-renderable desired presentation** (`GeometryPresentationRecipe`) — the
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
- The existing `RenderExtractionCache::State::EnsureRenderable` owns base-material
  allocation for every import route. Missing materials use lit `StandardPBR`;
  the remaining authored-material work must feed this effective per-renderable
  state, with `Unlit` only when explicitly authored (`KHR_materials_unlit`).
  No separate imported-material allocation helper is needed.
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
  meaning. Generalized mesh-edge requests use `RUNTIME-191`'s explicit nearest
  triangle-edge rule; this task neither changes nor duplicates it.

## Cleanup — removal inventory (the old wrong implementation)

Delete/collapse, with the single data path replacing each:

- [x] **Import material allocation consolidation** — direct meshes already choose
  `ColorSource::Material` in `ImportedMeshVisualization()`; graph/point imports
  use `ImportedGeometryVisualization()`. Missing-material defaults now use
  extraction alone. Authored imports now seed their existing recipes and texture
  assets without allocating another GPU material. Broader authored factor/texture
  projection remains in Required changes; no visualization-to-lighting coupling
  or extra missing-material default remains here.
- [x] **Override-material synthesis** in `VisualizationSyncSystem` for
  `UniformColor` (`BuildUniformColorParams`,
  `src/graphics/renderer/Graphics.VisualizationSyncSystem.cpp`) and the per-entity
  `EnsureOverrideLease`/`OverrideLeases`/`EffectiveSlot`-per-frame machinery
  in that same implementation. The scivis data path (`GpuEntityConfig`) already
  exists; the synthesized SciVis material is redundant.
- [x] **Scivis override materials** — `BuildScalarFieldParams`/`BuildPerElementParams`
  in `Graphics.VisualizationSyncSystem.cpp`. KEEP the visualization *capability* via
  `GpuEntityConfig` data; remove the material synthesis. (Design decision below.)
- [x] **Obsolete `MaterialFlags::Unlit` bit** — removed the redundant flag
  write in `Graphics.MaterialSystem.cpp` and the bit declaration. The slot-0
  material already sets `ShadingModel::Unlit`; preserve that genuine invalid-
  handle indicator. Visualization synthesis has no remaining Unlit writes.
  The shader authority switch already landed in A1; no compatibility alias
  or transition period is required.

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
- Reuse `GeometryPresentationRecipe`, its typed editor operation/snapshot, the
  per-renderable material lease, `MaterialTextureAssetBindings`,
  `GpuAssetCache`, RHI `TextureManager`, `RUNTIME-191`, and the UV view.
  Do not add a controller class, manager/pool, module registry, or another
  Service → Queue → Binding → Submission chain.
- `RUNTIME-191` is the separate texture-bake runtime-module consolidation.
  GRAPHICS-105 reuses its pure producer service, then owns the material/
  presentation binding of the completed `AssetId`; it preserves bake
  identity/readiness invariants rather than wrapping the service.

## 2026-09-16 — Visualization material duplication slice

Operator direction: continue reuse and compilation cleanup with Claude; this
bounded rendering cleanup is explicitly selected alongside the standing product
convergence priority. Broader source selection and normal-texture readiness
acceptance remains open.

Reuse/right-sizing plan: `BuildEntityConfig` already publishes uniform color,
scalar range/colormap/binning/isolines and per-element buffers to the shader's
shared `gpu_scene.glsl` receiver. Synthesizing a second material repeats those
settings and discards authored texture/shading state. Remove that map, packing,
lease lifecycle and count API. Preserve the existing per-renderable material,
tint patch, sidecar targeting/inheritance and property-buffer generation path.
Remove the redundant pre-visualization material upload; both prep paths upload
once after visualization/tint sync. No new owner, file, or dependency edge.
Reintroduction would require a shader feature that cannot be represented by the
existing material plus entity config, with an explicit ownership decision.

Review focus: base material preservation, invalid-lease fallback, uniform alpha,
debug-UV early return, scalar/color and material/removal transitions, sidecar
config, and prep graph ordering. Claude reviews fixed read-only source/diffs.
Validation: existing graphics contracts plus full CPU gate and relevant promoted
Vulkan readback smokes. No measured compilation-speed claim for this slice.

Pre-merge architecture sweep: layer imports and CMake links pass (rows 1–2);
no higher-layer export or renderer member was added (rows 3–4); frame-graph
passes/recipes are unchanged (rows 5–6, not applicable), and the existing typed
CPU prep dependencies now connect pipeline commit → visualization → material
upload → transform. This is partial task progress, with no maturity closure or
new exception (rows 7–8). The automated clean-workshop bundle passes strict.

Claude plan review confirmed the graph edge rewire, debug-UV visualization
guard, and invalid-lease fallback. Diff review found no confirmed defect; the
remaining null-pointer question is answered by the preserved unconditional
`if (matInst == nullptr) continue` before material-slot resolution. The shared
uniform-color shader resolver returns `cfg.UniformColor` directly, preserving
its independence from base tint. Production delta: 255 net lines removed in
eight existing files; no new production file or module.

Remaining authority work includes import policy consolidation and the normal-source
per-renderable/config/UI/readiness contracts listed above. This slice does not
retire GRAPHICS-105 or unblock LEGACY-043 prematurely.

Validation checkpoint:
- Canonical `ci` configure and full `IntrinsicTests` build passed. Fixed two
  test-only defects found during verification: an explicit GLM include and
  teardown of newly allocated fixture leases before material-system shutdown.
- Focused graphics/material/prep/import contracts: 56 passed.
- Full exclusion-only CPU gate: 4,677 passed, one expected ASan-only lifecycle
  skip (4,678 selected), 141.00 seconds. This is test runtime, not a compile
  performance measurement.
- Claude's final resolution review found no unresolved correctness defect.
  The upload test deliberately uses adjacent dirty slots to check the single
  post-tint upload in both sequential and task-graph preparation.
- Strict layering, task policy/state links, test layout, docs sync/links,
  root hygiene, skill mirrors, and clean-workshop checks passed. Module
  inventory regeneration produced no content change.

- Promoted `ci-vulkan` ASan+UBSan build of the two relevant smoke targets and
  all seven selected GPU/Vulkan readback tests passed, no skips, 114.95 seconds.
  This validates the retained visualization and material paths for this slice;
  it does not close the task's broader normal-source UI/readiness acceptance.

```bash
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicGraphicsVulkanSmokeTests -j2
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '(VisualizationOverlaySurfaceGpuSmoke|RuntimeSandboxAcceptanceGpuSmoke\.(ReferenceTriangleScalarField|ReferenceTriangleVertexColor|ImportedObjectSpaceNormalBake|SurfaceAppearance))' --no-tests=error --timeout 120 --parallel 1
```

Session logs and bounded Claude review packets:
`/tmp/intrinsic-graphics105-overrides/` (local working artifacts).

## 2026-09-16 — Remove retired material metadata and lighting alias

Operator continuation of compilation/reuse cleanup. Canonical-owner search found
no remaining producer or receiver for the `SciVis` material type: visualization
is already carried by `GpuEntityConfig`. Remove its registration and custom
layout description, and compact built-in IDs in CPU/GLSL together. Also remove
`MaterialFlags::Unlit`, whose sole writer duplicates slot 0's existing
`ShadingModel::Unlit`, and the forward shader's alternate flag test. Other flag
bit values and the 128-byte slot layout remain unchanged. No compatibility
transition is needed under the operator's API/scene-format decision.

Narrow the material interface and implementation GLM umbrella include to the
actual `vec4` dependency. Correct stale public owner comments that described
white slot 0 and a per-material descriptor set. No new helper/module/owner.
Normal-space flags and the line/point render classification flag have live
consumers and are preserved; normal-source/import policy remains open.

Claude reviewed the plan. Checks will cover all three registered IDs/counts,
actual uploaded slot 0/UV material type and shading fields, CPU/GLSL agreement,
existing material/visualization contracts, full CPU and relevant Vulkan readback.
This slice makes no compile-time speedup claim.

Review checkpoint: Claude's final review found no confirmed correctness defect.
The packed-slot test accepts any upload large enough to contain the two slots;
remaining conditional include/accessor concerns were resolved against complete
source and the removed-symbol search. Existing shader contract tests now bind
all built-in type IDs and both shading values to their CPU declarations. No
new dependency edge, resource owner, frame pass or compatibility shim was added.
Source delta is 95 fewer physical production lines in five existing files,
including removal of stale source-documentation examples; this is not a timing
measurement. The task's completed synthesis inventory and actual remaining
import-policy wording were reconciled with the current code.

Validation checkpoint:
- Canonical `ci` and promoted `ci-vulkan` configure/build passed with Clang 23.
  The final CPU build includes the upload-size assertion correction.
- Focused material/renderer contracts: 125 passed.
- Full exclusion-only CPU gate: 4,677 passed, one expected ASan-only lifecycle
  skip (4,678 selected), 139.88 seconds.
- Eight selected Vulkan readbacks passed under ASan+UBSan, no skips, 54.26
  seconds: default recipe/debug view, sandbox default surface, vertex color,
  mesh line/point lanes, scalar colormap/surface/isolines, and imported
  object-space normal texture binding.
- Strict layering, task policy/state links, test layout, documentation
  links/sync, root hygiene, skill mirrors and clean-workshop checks passed.
  Module inventory regeneration produced no content change.
- Architecture/workshop sweep: layer imports, CMake links and exported types
  pass (rows 1–3); no renderer member/subsystem, pass or recipe change (rows
  4–6 n/a), no maturity closure or temporary exception (rows 7–8 n/a). Slot
  ownership/lifetime and shader push-constant layouts are unchanged. CPU and
  GLSL built-in IDs changed together and have explicit contract coverage.

```bash
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicGraphicsVulkanSmokeTests -j2
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '(DefaultRecipeSurfaceGpuSmoke\.(RecipeSelector|ReferenceTriangleDebugViewReadback)|RuntimeSandboxAcceptanceGpuSmoke\.(ExtrinsicSandboxDefaultConfigPresentsReferenceTriangleAtFrameCenter|ReferenceTriangle|ImportedObjectSpaceNormalBake))' --no-tests=error --timeout 120 --parallel 1
```

Local logs and fixed Claude plan/review/resolution packets:
`/tmp/intrinsic-graphics105-material-authority/`. GRAPHICS-105 remains open for
the broader import/material-source contracts; LEGACY-043 remains gated.


## 2026-09-16 — Extraction-owned defaults for every import route

Operator continuation of duplicate-code/compilation cleanup with Claude.
Canonical-owner discovery: `RenderExtractionCache::State::EnsureRenderable`
already allocates every effective default material. The model materializer's
`EnsureDefaultLitMaterial` allocates another lease whose primitive-record slot
has no draw consumer. Remove that lease/slot/boolean, the parameter builder,
allocation helper and default-only counters. Delete unused slot copies in both
primitive and authored-material records; retain the authored leases and texture
resolution because that broader integration still needs its own proof. Reuse the
existing extraction owner, with no additional helper, map, module or wrapper.
A second import-owned default would require an actual independent draw consumer.

The rendered default remains `MaterialParams{}` (white, roughness 0.5, Lit), not
the unused import parameters (gray, roughness 1.0). Claude confirmed this distinction;
its concerns about other consumers and counters were checked by repository search:
primitive records serve entity completion/destruction, while the remaining generic
material-instance counter still measures authored allocations. The constant-name
lookup reuses the material module already imported by extraction. No new edge.

Regression-first check: synchronous and queued material-less instanced glTF plus
a direct OBJ retain correct default parameters and independent renderable leases.
Before removal, only six live-material count assertions fail: one extra import
lease in each route at import, after extraction, and after extraction clear.
The existing rendered-parameter and independence checks pass. Preserve authored
presentation recipes, texture readiness/reload, normal/UV processing, selection,
focus and invalid-handle slot 0. Full authored-material handoff consolidation
remains open; this slice does not establish compile-time improvement.

Review/validation checkpoint:
- Claude reviewed the plan, fixed diff and resolution. Fixed its useful test
  findings by pinning queued/synchronous extraction counts and verifying the
  edited material before checking untouched peers. Existing includes, handle
  equality and authored-lease retention in `Impl::Records` resolve its conditional
  concerns; the final review has no confirmed unresolved defect.
- Canonical `ci` configure and full `IntrinsicTests` build passed with Clang 23.
  Full exclusion-only CPU gate: 4,677 passed, one expected ASan-only lifecycle
  skip, 138.58 seconds. After the final test-only strengthening, rebuilt the
  affected target and reran all 32 import contracts: passed in 3.06 seconds.
  That focused selection intentionally includes the existing finite-work
  enrichment/shutdown test labeled `slow` (0.35 seconds).
- Promoted `ci-vulkan` configure/build and four import readback tests passed
  under ASan+UBSan, no skips, 40.18 seconds: off-origin OBJ visibility,
  model-scene visibility/picking, model replacement, and imported object-space
  normal baking/binding.
- Strict layering, task policy/state links, test layout, docs links/sync,
  root hygiene, skill mirrors and clean-workshop checks passed; module inventory
  regeneration produced no content diff.
- Manual architecture/workshop sweep: imports, target links and downward type
  ownership pass (rows 1–3). No renderer members/passes/recipe edges changed
  (rows 4–6 n/a); no maturity closure or exception (rows 7–8 n/a). The sole
  effective lease remains per-renderable, render-thread-owned and released by
  extraction; shader layouts, asset-generation guards and failure defaults are
  unchanged.

Production delta: 95 net lines removed across three existing source/interface
files, no new production file or helper. Required/default-allocation and CPU-test
rows are closed; the old proposed helper and already-deleted Unlit migrations
were removed from the remaining-work wording. Authored-material integration,
`KHR_materials_unlit`, the common forward/deferred authority and normal-source
readiness/config/UI work remain open; LEGACY-043 remains gated.

```bash
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicRuntimeContractTests -j4
ctest --test-dir build/ci --output-on-failure -R '^RuntimeAssetImportFormatCoverage\.' --no-tests=error --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j2
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.(ImportedOffOriginObjTriangleAutoFramesAtCenter|ImportedModelSceneIsVisibleAndClickPickable|ImportedModelSceneReplacementIsVisibleAndClickPickable|ImportedObjectSpaceNormalBakeBindsAndReadsBackExactTargetSlice)$' --no-tests=error --timeout 120 --parallel 1
```

Local regression/build/test logs and bounded Claude packets:
`/tmp/intrinsic-graphics105-import-defaults/`.

## Authored import material ownership — 2026-09-16

Operator-directed duplication/compile-locality continuation from `d583fc83d`.
Reused `RenderExtractionCache`'s per-renderable lease and presentation texture
resolution. Deleted the model materializer's unused authored leases, copied
material records, parameter mapping, readiness/reload polling and diagnostics.
Removed its renderer dependency and collapsed the private state wrapper into
its copied import record. No new owner, file or compatibility path.
A second allocation owner would require a real independent draw consumer.

Keep embedded texture assets/uploads, model Ready/Destroyed lifetime, recipe
seeding and progressive enrichment. `AssetWorkflowModule` already forwards
texture Reloaded/Destroyed to `GpuAssetCache`; extraction resolves current
ready assets into its own leases. Model replacement still waits for Ready.
Import no longer rejects solely because the renderer's StandardPBR type is
unavailable; material allocation/fallback belongs to extraction for authored
and missing-material imports alike.

Claude reviewed the plan, diff and fix. The new regression failed six allocation
assertions before deletion: exactly one extra lease per model, for both sync and
queued routes at import, extraction and clear. Recipe values, texture identity
and independence checks passed. The fixed import suite passes all 33 tests.
The final fixture uses one shared triangle builder instead of string surgery;
Claude accepted that fix and withdrew a uniqueness concern after seeing the
existing assertion outside its diff context. The edit now targets an imported
instance explicitly rather than the sorted first entity, which was a direct OBJ.
Broader authored factor/texture
projection, KHR_materials_unlit and normal-source readiness remain open; dead
lease contents never supplied those missing live-path semantics. No measured
compile-time claim follows from dependency/code removal alone.

Architecture/workshop review: imports, target links and public downward type
ownership pass (rows 1–3). No renderer member/pass/recipe-edge additions (4–6 n/a),
no whole-task maturity closure or exception (7–8 n/a). Model records retain entity
lifetime and transactional replacement; texture assets/residency stay with the
asset service and GPU cache. Source-documentation audit: zero objective errors;
138 existing README size/chronology findings are outside the changed paragraph.
The module inventory was regenerated with no content diff. Strengthened
`AssetCompilationLocality.ModelMaterialization` to forbid Renderer, Material and
MaterialSystem for both interface and implementation; the compiler check passes.

Production delta: **368 lines removed across three existing source/interface
files**. Existing tests reuse one fixture builder; no production helper or file
was introduced. This closes duplicate import allocation, not the broader
GRAPHICS-105 appearance/shader/UI work or LEGACY-043 prerequisites.

A provisional full CPU gate caught a missing closing array bracket in the revised
shared glTF test fixture: six decode-dependent tests failed before materialization.
Fixed the fixture construction; production source and Vulkan evidence were
unchanged. Retain this failed attempt in the local review logs rather than
attributing it to the material cleanup or weakening import assertions.

Final verification: canonical `ci` configure and `IntrinsicTests` build pass
with Clang 23. After the fixture correction, all 33 import contracts pass
(3.15 s, including the existing bounded enrichment/shutdown slow case), and the
full exclusion-only CPU gate passes **4,683 tests plus one expected ASan-only
GLFW skip**, zero failures (4,684 selected, 142.51 s). Promoted `ci-vulkan`
configure/build and four ASan+UBSan readbacks pass with no skips (42.28 s): model
visibility/picking, model replacement, generated albedo and imported normal bake.
Strict layering, test layout, task policy/state links, docs links/sync, root
hygiene, skill mirrors, clean-workshop checks and diff checks pass.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^RuntimeAssetImportFormatCoverage\.' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.(ImportedModelSceneIsVisibleAndClickPickable|ImportedModelSceneReplacementIsVisibleAndClickPickable|ImportedObjWithoutAuthoredUvsSamplesGeneratedAlbedoTexture|ImportedObjectSpaceNormalBakeBindsAndReadsBackExactTargetSlice)$' --no-tests=error --timeout 120
```

Local regression/build/test logs and bounded Claude packets:
`/tmp/intrinsic-authored-material-reuse/`.
