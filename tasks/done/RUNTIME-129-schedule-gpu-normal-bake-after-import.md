---
id: RUNTIME-129
theme: B
depends_on:
  - GRAPHICS-104
  - GRAPHICS-115
  - GRAPHICS-128
  - RUNTIME-137
  - RUNTIME-183
maturity_target: Operational
---
# RUNTIME-129 — Schedule GPU object-space normal bake jobs after import

## Status

- Completed and retired on 2026-07-19 at `Operational`; owner: Codex team;
  implementation branch:
  `codex/runtime-129-operational-bake`.
- Implementation checkpoints: task promotion and corrected contract
  `23e2d118`; generation-aware produced-texture readiness `35e0fc80`; geometry
  residency revisions `d86dbd0d`; authoritative identity `d5ade642`;
  physical-storage channel alignment `d2f838f2`; operational workflow
  `f3aacda9`. Commit reference: this retirement commit records the final task
  state, review, and generated session inventory.
- Verification evidence:
  - `IntrinsicTests` built in the canonical `ci` tree and the complete
    CPU-supported selector passed 4,273/4,273 with one expected self-skip;
  - the alignment-focused CPU selector passed 88/88, including exact
    graphics-plan/recorder and runtime-residency rejection of misaligned
    physical-storage addresses;
  - `IntrinsicTests` built in `ci-vulkan` with ASan+UBSan, and the complete
    `gpu` + `vulkan` selector passed 47/47 with zero skips on the live Vulkan
    host;
  - the real-app acceptance smoke imported a decoy and target, consumed a
    nonzero shared-index slice, bound the exact generated cache generation,
    preserved existing material state, and read back target-normal coverage,
    the four-texel dilation gutter, and a far alpha-zero texel;
  - that smoke exposed and then verified the fix for an invalid
    `base + 36` `RG32_FLOAT` texcoord BDA: managed uniform-SoA texcoords are
    now eight-byte aligned, the graphics/runtime seams fail closed on
    misalignment, and the Vulkan shader reads the authored UV footprint;
  - strict task/state, documentation-link, layering, test-layout,
    root-hygiene, clean-workshop, generated-inventory, and whitespace checks
    pass.

## Goal

- After an eligible mesh is imported and its vertex normals are resolved,
  schedule an asynchronous GPU object-space normal-texture bake through the
  existing private workflow queue, then merge the exact-generation `Ready`
  result into the material/progressive normal binding while vertex-normal
  shading remains active until completion.

## Non-goals
- The graphics-owned RHI bake pass, shaders, and `GpuAssetCache` GPU-produced texture residency — owned by GRAPHICS-104 (this task consumes them).
- The graphics-owned nonzero shared-index-slice command contract — owned by
  `GRAPHICS-128` (this task consumes `FirstIndex` from the selected live
  `GpuGeometryRecord`).
- GPU dilation for padded object-space normal bakes — already owned and retired
  by `GRAPHICS-115`; this task consumes the graphics-owned dilation resources
  when runtime scheduling asks for padded bakes.
- Tangent-space normal-map baking or MikkTSpace tangents.
- GPU porting of scalar/label/vector/face/selected-mesh attribute bakes.
- A CPU fallback for GPU bake failure on a non-operational backend (fail closed; keep the existing vertex-normal shading).
- Changing default material assignment (RUNTIME-128) or vertex-normal computation (`ResolveVertexNormals`).

## Context
- Owner/layer: `runtime` for job orchestration, generated `AssetId` selection, stale-generation keys, render-thread submission timing, and material-binding swap; `graphics` owns the bake command recorder (`Extrinsic.Graphics.ObjectSpaceNormalTextureBake`, `RecordObjectSpaceNormalTextureBake(...)` at `src/graphics/renderer/Graphics.ObjectSpaceNormalTextureBake.cppm:168`) and the cache residency path (`GpuAssetCache::BeginGpuProducedTexture(...)` / `SetGpuProducedTextureReadyFrame(...)`).
- `GRAPHICS-104` is retired at `CPUContracted`: zero-padding raster-bake
  planning/recording, shader/material metadata, GPU-produced texture residency,
  and runtime queue/submission/binding helpers exist. `GRAPHICS-115` is retired
  at `Operational`: requested padding is submittable when the runtime provides
  graphics-owned dilation resources, and still fails closed when those resources
  are unavailable. Retired `GRAPHICS-128` now makes the bake plan/record
  descriptors preserve `GpuGeometryRecord::SurfaceFirstIndex` while keeping
  indexed-draw base vertex zero; retired `RUNTIME-183` supplies the required
  AssetWorkflow composition owner.
- Completed state: `Extrinsic.Runtime.ObjectSpaceNormalBakeQueue` owns exact
  resolved bake identities separately from target lifetime; the private
  `ObjectSpaceNormalBakeService` allocates collision-safe registry metadata,
  coalesces pending waiters, retains bounded proven-ready provenance, validates
  live `GpuWorld` residency, and records through the existing `JobService`
  `GpuQueue` frame-command lane. Exact cache-generation publication and
  completion revalidation prevent older/current/pending views from binding as
  newer work. `ObjectSpaceNormalBakeBinding` transactionally merges only the
  generated normal semantic while preserving albedo, metallic-roughness, and
  emissive, and advances the matching progressive normal slot only when all
  world/entity/content/cache/presentation generations still match.
  `AssetWorkflowModule` privately composes the service and supplies the same
  queue-backed producer path to default/progressive model imports, direct-mesh
  postprocessing, and selected-mesh editor commands. Callers without a
  composed queue retain the legacy CPU compatibility route; an unavailable or
  lost operational backend never selects that route as a fallback. The
  derived-job graph still fail-closes any non-CPU domain:
  `IsUnsupportedJobDomain(domain) { return domain != ProgressiveJobDomain::Cpu; }`
  (`Runtime.DerivedJobGraph.cpp:36-47`, rejection at `:348-355`).
  `ProgressiveJobDomain` already reserves `GpuCompute`/`GpuGraphics`/`Auto`
  (`Runtime.ProgressiveRenderData.cppm:122`).
- The shader fallback already exists: forward/deferred sample the object-space normal texture only when the material flag is set and `NormalID` is valid, otherwise use the vertex-normal attribute (`ResolveSurfaceNormal`). So "use texture when ready, else attribute" needs no shader work — only the runtime swap of the `Normal` binding once the cache entry is `Ready`.
- A GPU graphics bake cannot run on the background streaming `Execute`
  callback (that lane is CPU). It records through the settled render-thread
  `JobService` `GpuQueue` participant and promotes through the GPU-completion
  frame already wired into `GpuAssetCache`.
- Requires an operational Vulkan device for `Operational`; `IDevice::IsOperational()` is false under the default Null backend, so on non-operational hosts this path must no-op deterministically and leave vertex-normal shading in place.
- ARCH-013 re-review (2026-07-08): Decision re-gated and re-scoped. Post
  `ARCH-009`, the render-thread/GPU completion lane for Slice B must consume
  the `JobService` `GpuQueue` target/readback substrate owned by `RUNTIME-137`
  instead of adding another engine-resident GPU queue. The existing
  `RuntimeObjectSpaceNormalBakeQueue` may remain as request/stale-key metadata,
  but submission/completion should route through the kernel job seam, and
  "bake finished → material/attribute refresh" should be a standing kernel
  event reaction rather than an `Engine` callback.
- `RUNTIME-183` is the accepted composition owner. The remaining production
  plan provider, request drain, ready publication, material swap, diagnostics,
  and smoke land inside `AssetWorkflowModule`; this task must not restore
  queue/service state or callbacks on `Engine`.

### Implementation-readiness findings (corrected 2026-07-19)

- **Ready promotion is frame-based.** `GpuAssetCache` promotes GPU-produced
  textures conservatively at `issueFrame + FramesInFlight`; it has no fence or
  completion token. This task preserves that truthful contract and makes the
  ready-frame write exact-generation-aware. A real fence is out of scope and
  would require a separate RHI prerequisite.
- **Authoritative bake identity.** Replace the partial content key with one
  versioned resolved identity containing exact packed position bytes, exact
  fan-triangulated surface-index order, resolved texcoord and normal streams,
  vertex/surface-index counts, width, height, padding, normal space, and
  canonical bit patterns for the three resolved epsilon options. Normalize
  `-0` before hashing. Equality, hashing, submission, stale/latest matching,
  reuse, and failure cleanup all use this identity. Geometry, texcoord, and
  normal source generations derive from the corresponding fingerprints.
- **Target lifetime is not reusable content.** Carry world/binding epoch, raw
  validated entity handle, stable render id, presentation key/normal semantic,
  and expected progressive binding generation separately from content
  identity. The raw `entt::entity` handle is the entity-lifetime generation;
  destroy/recreate reuse must invalidate the old target even when its stable
  name is reused.
- **Collision-safe generated assets.** Never fabricate
  `AssetId{stableId, 1}` or derive a strong handle from a fingerprint. The
  existing private service uses its borrowed `AssetService` to load a tiny
  runtime-private metadata payload at a reserved deterministic path keyed by
  full identity; `AssetRegistry` supplies collision-safe handle allocation and
  path reuse. An identity-less target gets a distinct non-reusable metadata
  asset. Queue scheduling carries identity/target metadata and need not expose
  an `AssetId`; the private main-thread service drain allocates it before
  render recording.
- **Pending versus proven-ready reuse.** The existing service owns
  `Queued`, exact `PendingGpu {AssetId, CacheGeneration}`, and
  `ProvenReady {AssetId, CacheGeneration}` provenance. A second target waiting
  on the same pending identity attaches without another cache generation.
  Only exact `ProvenReady` may fast-bind, and only while cache state is `Ready`
  and the current view generation equals the proven generation. First
  insertion, fallback, queued, and pending identities never fast-bind.
- **Cache-generation correctness.** Add
  `SetGpuProducedTextureReadyFrame(id, generation, frame)` or an equivalent
  exact operation; the current two-argument form can stamp a newer pending
  replacement from an older ticket. Record failure, ready-publication failure,
  operational loss, stale completion, scene retirement, and shutdown affect
  only the matching pending generation. An older current view exposed during
  replacement remains unbound.
- **Minimal GPU residency extension.** `GpuGeometryHandle::Generation` changes
  only on allocation/replacement, not channel updates, and
  `RHI::GpuGeometryRecord` exposes no content revision or layout eligibility.
  Add one plain `Graphics::GpuGeometryResidencyView` DTO plus a `GpuWorld`
  accessor; keep the RHI record shader-facing and unchanged. The view carries
  the record, a monotonic content revision, exact position/topology/texcoord/
  normal fingerprints and byte/count metadata, and storage/channel layout.
  `UploadGeometry()` seeds it; successful `UpdateGeometryChannels()` refreshes
  the affected fingerprint and revision.
- **Production provider.** Extend the existing private service dependencies
  with borrowed `AssetService` and `Graphics::IRenderer`; build one private
  plan lambda in `AssetWorkflowModule::Impl::ConfigureDependencies()`. At
  record time recheck `IDevice::IsOperational()`, resolve the live extraction
  surface and residency view, match the full identity/content revision/counts,
  require a bake-readable separate-channel lane with tightly packed `vec2`
  texcoords and `vec3` normals plus the managed index buffer, and use
  `SurfaceFirstIndex`, `SurfaceIndexCount`, and base vertex zero. Recheck
  operational state, submit pending upload barriers, then begin/record/publish
  the exact cache generation.
- **Default producer route.** The default non-progressive
  `AssetModelSceneHandoff` still CPU-bakes missing normals; queue scheduling
  exists only in optional progressive mode. When the workflow queue is
  composed, both modes skip CPU normal-texture generation, preserve CPU albedo
  generation, and schedule every eligible missing-authored-normal primitive
  after resolved UVs/normals and entity materialization.
- **Completion is a merge.** Exact-ready completion revalidates world epoch,
  entity lifetime, latest identity, resident content revision, cache
  generation, and expected progressive binding generation. It merges only
  `Normal` and `NormalSpace=ObjectSpaceNormal` into the existing extraction
  binding, preserving albedo, metallic-roughness, and emissive. When
  progressive bindings exist, the matching normal slot becomes
  `GeneratedTextureAsset`/`Ready` with the generated id and incremented binding
  generation; changed progressive state rejects the completion so extraction
  cannot overwrite it on the next frame.
- **Live capability and truthful results.** Model, direct-mesh, and editor
  producers query the borrowed device immediately before scheduling; no async
  callback captures a stale operational bool. A scheduled selected-mesh result
  does not claim `BoundGeneratedTexture`; that becomes true only for exact
  ready binding. CPU compatibility remains only when no queue is composed,
  never as fallback for unavailable Vulkan, record failure, or stale
  completion.
- **Bounded retained state.** Use fixed implementation limits, not new config,
  for queued targets/in-flight identities, proven-ready generated output by
  entry/byte count, and dilation resources by entry/byte count.
  `MaxSubmissionsPerFrame=1` keeps shared dilation scratch serialized.
  Dilation entries track extent, last use/safe-retire frame, references, and
  the scratch image's actual post-use `ShaderReadOnly` layout. Evict only after
  no ticket can reference an entry.
- **Shutdown and scene replacement.** `HasInFlightWork()` remains true while
  retained bake resources exist so the existing JobService shutdown path waits
  for device idle. After idle, fail/retire exact pending generations, clear
  target/provenance state, retire generated cache/assets, release dilation
  resources and the pipeline lease, then clear dependencies. Scene replacement
  detaches old target waiters and discards unrecorded work without blindly
  clearing recorded tickets; completed old work remains unbound or retires at
  its safe frame.
- **Remaining editor producer.** Add the borrowed queue to
  `SandboxEditorContext` through a narrow `AssetImportPipeline` accessor; do
  not publish the private bake service. Select the queue-backed mesh-normal
  path before requiring `AssetService`; non-normal CPU bakes retain that
  requirement. Selected custom UV/normal channels must match the bake-readable
  resident channels or fail closed.
- **Right-sizing verdict.** Two narrow graphics extensions are justified by
  correctness at existing seams: generation-aware cache publication and the
  plain `GpuWorld` residency DTO/accessor. Everything else stays private to the
  existing queue/submission/binding/service units and
  `AssetWorkflowModule`. Add no new provider, service, module, queue, registry,
  facade, or test-only production seam. Blast radius is limited to
  `GpuAssetCache`, `GpuWorld`, the four existing bake units, their current
  AssetWorkflow/import/editor producers, and focused tests. Reconsider an
  exported provider only when a second production plan source with different
  policy exists; reconsider config only after a measured limit needs tuning.
- **Implementation order.** Land authoritative identity, registry-backed ids,
  exact cache-generation provenance, and CPU contracts first; then residency/
  provider resources; then default/editor producers and completion merge;
  finally the real-app Vulkan smoke. Never restore bake ownership or callbacks
  on `Engine`.

### Settled policy
- **GPU job lane.** Keep `DerivedJobGraph` CPU-only and route render-thread GPU
  bake submission/completion through the `JobService` `GpuQueue` target owned
  by `RUNTIME-137`, while preserving stale-key/generation bookkeeping as bake
  request state.
- **Bake trigger scope.** Default: schedule a bake only for primitives that currently receive a generated object-space normal (i.e. the cases the CPU path bakes today), not for every imported mesh with authored tangent-space normals.
- **Identity reuse.** Key completed bakes by the authoritative versioned
  resolved identity above. The private service allocates collision-safe
  registry-backed generated assets; an identity-less target is non-reusable.

## Required changes
- [x] Replace `RuntimeObjectSpaceNormalBakeContentKey` with the authoritative
      versioned identity and separate target-lifetime record described above.
      Carry both through queue latest-state, submission, ticket, waiters,
      completion, stale checks, exact-ready reuse, and cleanup; derive source
      generations from exact position/topology/texcoord/normal fingerprints
      and entity lifetime from the raw validated handle.
- [x] Allocate generated ids through the existing private
      `AssetWorkflowModule` `AssetService` using a runtime-private metadata
      payload and reserved deterministic identity path. Do not fabricate strong
      handles or publish an id from `Schedule()` before the service drain;
      identity-less target allocations are distinct and non-reusable.
- [x] Add generation-aware GPU-produced-texture ready-frame publication and
      exact matching failure/retirement operations to `GpuAssetCache`. Track
      queued, pending-with-generation, waiter, and proven-ready state privately;
      only exact proven-ready cache views may fast-bind. Bound queued/in-flight
      and proven-ready state by fixed entry/byte limits with deterministic
      capacity diagnostics.
- [x] Add the plain `GpuGeometryResidencyView` and `GpuWorld` accessor/update
      contract: exact fingerprints/counts/byte widths, monotonic content
      revision, and storage/channel eligibility update on upload and partial
      channel mutation without changing the RHI record surface.
- [x] Implement the production Vulkan plan and retained-resource state inside
      the existing `ObjectSpaceNormalBakeService` owned by
      `AssetWorkflowModule`: borrow the exact asset service and renderer,
      recheck operational state around resource resolution, validate the live
      extraction surface plus full residency identity/revision/layout, consume
      nonzero `SurfaceFirstIndex`, submit pending upload barriers, and
      begin/record/publish the exact cache generation. Lazily retain the
      pipeline and bounded extent-keyed dilation resources with truthful scratch
      layouts; generated textures include transfer-source usage for the
      acceptance readback. Add no `Engine` callback or exported provider.
- [x] Slice B partial: add service-private `ObjectSpaceNormalBakeService` state as the `JobService` `GpuQueue` participant owner, retain pending queue submissions, record injected graphics bake plans through `RecordObjectSpaceNormalTextureBake(...)`, stamp `GpuAssetCache` ready frames, drain ready completions into material bindings, and discard superseded pending submissions before recording.
- [x] Add an `AssetModelSceneHandoff` queue option so progressive model-scene generated-normal work schedules `RuntimeObjectSpaceNormalBakeQueue` through a dependency-only main-thread apply job instead of marking a fake CPU normal texture ready when the queue is supplied.
- [x] Wire the then-Engine-owned queue services into model-scene handoff options and direct-mesh post-import processors; `RUNTIME-183` moves that unchanged wiring into `AssetWorkflowModule`. Direct mesh post-process schedules a queue request from resolved ECS geometry/UV/normal properties instead of registering the CPU-generated texture when the queue is supplied.
- [x] Add a `SelectedMeshTextureBake` queue option so selected mesh-vertex normal outputs schedule `RuntimeObjectSpaceNormalBakeQueue` requests instead of creating CPU-generated normal textures when the queue is supplied.
- [x] Route the default non-progressive model handoff through the composed
      queue for every eligible missing-authored-normal primitive after resolved
      UV/normal materialization, while preserving CPU albedo generation. Replace
      model/direct/selected hardcoded generations with the authoritative
      identity and query the borrowed device live immediately before schedule;
      direct async work must not capture an operational bool.
- [x] Expose the existing workflow queue narrowly through
      `AssetImportPipeline` into `SandboxEditorContext`, select the queue-backed
      mesh-normal command before checking `AssetService`, and require selected
      UV/normal channels to match bake-readable resident channels. Non-normal
      compatibility bakes still require `AssetService`; a scheduled result does
      not claim a generated texture is already bound.
- [x] At exact `Ready`, revalidate world epoch, raw entity lifetime, latest
      identity/residency revision, cache generation, presentation key, and
      expected progressive binding generation. Merge only generated `Normal`
      and object-space normal metadata into the existing material binding,
      preserve albedo/metallic-roughness/emissive, and update the matching
      progressive normal slot to generated/ready with incremented generation.
      Any mismatch discards the completion without partial mutation.
- [x] Route the chosen GPU lane through `JobService` `GpuQueue` without
      regressing CPU job fail-closed behavior for unimplemented
      `DerivedJobGraph` domains.
- [x] Make scene replacement detach only old target waiters/unrecorded work;
      preserve recorded tickets until safe completion/retirement. Keep retained
      resources visible to `HasInFlightWork()`, then after the existing
      device-idle shutdown boundary fail exact pending generations, retire
      generated cache/assets, and release dilation/pipeline state before
      clearing dependencies.
- [x] Retain CPU generated-normal compatibility only for callers with no
      composed queue. Never select it because Vulkan is unavailable, recording
      fails, completion is stale, or capacity is exhausted.

## Tests
- [x] CPU/null contract: queue-level scheduling on a non-operational backend no-ops deterministically and emits a no-CPU-fallback diagnostic.
- [x] CPU/null contract: queue-level stale-key lifecycle rejects a completion whose recorded entity/source generation, resolution, padding, or normal-map type no longer matches.
- [x] CPU/null contract: generated `AssetId` selection and content-key reuse return the same id for identical resolved geometry/UV/normal inputs.
- [x] CPU/null contract: model-scene handoff queue scheduling replaces the fake CPU normal-bake readiness job when configured and leaves the progressive normal slot pending.
- [x] CPU/null contract: model-scene handoff queue scheduling on a non-operational backend leaves the material `Normal` binding unset and records the no-CPU-fallback diagnostic.
- [x] CPU/null contract: composed non-operational direct-mesh import scheduling leaves the vertex-normal binding and no `ObjectSpaceNormalMap` flag in place.
- [x] CPU/null contract: selected-mesh normal texture command schedules an object-space normal bake queue request without creating a CPU texture when a queue is supplied.
- [x] CPU/null contract: selected-mesh normal texture command on a non-operational backend records the no-CPU-fallback diagnostic and leaves the progressive normal binding unchanged.
- [x] CPU/null contract: `JobService` `GpuQueue` participant records an injected object-space normal bake plan, promotes the generated cache entry by ready frame, drains the ready completion, and installs the generated normal binding.
- [x] CPU/null contract: superseded pending object-space normal bake submissions are discarded before command recording and do not allocate GPU-produced texture cache entries.
- [x] CPU/null contract: content-key reuse binds an already-ready generated normal texture without recording a duplicate bake or allocating the unused entity-scoped fallback texture.
- [x] CPU/null identity contract: exact topology/order, any resolved size/
      padding/space option, or any of the three canonical epsilon bit patterns
      changes the versioned identity; `-0` normalization is deterministic.
- [x] CPU/null generated-asset contract: registry-backed ids reuse the same
      full identity, never collide with unrelated live assets, never repurpose
      one id for different content, and no production path fabricates a strong
      handle. Identity-less targets allocate distinct non-reusable ids.
- [x] CPU/null provenance contract: same-identity pending requests attach as
      waiters without duplicate allocation/recording; first insertion,
      identity-less fallback, and pending state never fast-bind, while exact
      proven-ready reuse does.
- [x] CPU/null cache contract: a pending replacement may expose an older
      current view but remains unbound until its generation matches; wrong-
      generation ready publication is rejected without touching the newer
      pending generation.
- [x] CPU/null cleanup contract: record failure, ready-publication failure,
      operational loss, stale completion, scene reset, and shutdown purge only
      matching pending provenance and never mutate a material/newer identity.
- [x] CPU/null residency contract: upload records full fingerprints/layout;
      partial UV/normal channel updates change content revision/fingerprint
      without changing the geometry handle. The provider rejects topology,
      content-revision, channel, and storage-layout mismatch and preserves the
      live nonzero `SurfaceFirstIndex`.
- [x] CPU/null capability contract: schedule followed by device loss performs
      no pipeline/dilation/cache creation, command recording, or material
      mutation.
- [x] CPU/null binding contract: exact-ready completion preserves existing
      albedo, metallic-roughness, and emissive; the progressive normal slot
      becomes generated/ready. Changed world/entity/presentation/binding
      generation or resident content revision rejects the entire completion.
- [x] CPU/null producer contract: default non-progressive model import with the
      live queue creates no CPU normal payload; model/direct/selected topology
      or channel changes and destroy/recreate entity reuse change the recorded
      source/target versions.
- [x] CPU/null editor contract: the Sandbox mesh-normal command supplies the
      existing queue and live operational state without context
      `AssetService`, does not claim an already-bound generated texture while
      scheduled, and rejects custom nonresident channels; a non-normal CPU bake
      still fails without `AssetService`.
- [x] CPU/null capacity/lifetime contract: queue, in-flight/proven-ready, and
      dilation entry/byte limits fail deterministically; safe-frame eviction,
      scene replacement with queued/recorded work, and shutdown cannot bind
      into a replacement scene or release referenced resources early.
- [x] CPU/source contract: runtime frame-command hooks remain on a
      graphics-capable final command context suitable for the offscreen raster
      bake, with no second frame acquire/present path.
- [x] Opt-in `gpu;vulkan` real-app smoke: bootstrap
      `CreateSandboxApp`, import a decoy OBJ then a distinguishable target OBJ
      through `AssetImportPipeline`, let the real direct-import postprocessor
      schedule the private workflow queue, and assert target
      `SurfaceFirstIndex > 0`. Within a bounded frame limit prove the exact
      generated cache generation is `Ready`, prior material bindings survive,
      and object-space normal metadata swaps. Read back the production texture:
      a covered texel and a dilation-gutter texel encode the target normal
      rather than the decoy, while a far uncovered texel keeps alpha zero.

## Docs
- [x] Update `src/runtime/README.md` for queue-level GPU bake scheduling metadata, stale-key lifecycle, and the non-operational no-op contract.
- [x] Update `src/runtime/README.md` for optional `AssetModelSceneHandoff` queue scheduling and the deferred binding/submission boundary.
- [x] Update `src/runtime/README.md` for the pre-`RUNTIME-183` queue services and direct-mesh post-import queue scheduling.
- [x] Update `src/runtime/README.md` for optional `SelectedMeshTextureBake` queue scheduling and the non-operational no-CPU-fallback contract.
- [x] Update `src/runtime/README.md` for the CPU-contracted `JobService` GPU-queue participant, pending-submission retention, command-recording plan-provider seam, ready-frame promotion, and material-binding drain.
- [x] Update `src/runtime/README.md` for production Vulkan geometry-buffer plan-provider wiring once landed.
- [x] Update `src/runtime/README.md` and `docs/architecture/runtime.md` for the
      versioned identity/target split, registry-backed ids,
      pending/proven-ready reuse, exact cache-generation binding, material/
      progressive merge, bounded lifetime, default import/editor producers, and
      truthful frame-based readiness.
- [x] Update `src/graphics/assets/README.md` for generation-aware
      GPU-produced-texture publication and `src/graphics/renderer/README.md`
      for the residency DTO/content-revision/layout contract plus retained
      bake-resource/readback requirements.
- [x] Update `RUNTIME-139` compatibility wording so optional AoS residency must
      either preserve advertised bake-readable separate channels or return a
      deterministic unsupported-lane result.
- [x] Regenerate `docs/api/generated/module_inventory.md` after the changed
      `.cppm` surfaces.

## Acceptance criteria
- [x] Default and progressive imported meshes eligible for generated
      object-space normals schedule through the composed queue after resolved
      normals/UVs, create no CPU normal texture, and keep vertex-normal shading
      until exact-ready completion.
- [x] Exact-ready completion preserves unrelated material bindings, merges the
      GPU-resident normal/object-space metadata, updates matching progressive
      state, and discards stale world/entity/content/cache/presentation
      generations without partial mutation.
- [x] Distinct full identities cannot alias one generated asset; generated ids
      cannot collide with unrelated live assets; old/current/pending cache
      generations cannot bind as a newer bake.
- [x] The production provider validates live content revision/layout and
      consumes a real nonzero shared-index slice with local tightly packed UV/
      normal BDAs and zero base vertex.
- [x] The Sandbox editor's queue-backed normal command reaches the same private
      runtime path without requiring a CPU texture payload or publishing a
      private service.
- [x] Non-operational/lost graphics backends, failed recording, stale
      completion, and capacity exhaustion run no CPU fallback and keep
      vertex-normal shading with deterministic diagnostics.
- [x] Queue/proven-ready/dilation state is bounded, scene replacement cannot
      bind into a new target, and retained GPU resources release only after the
      existing idle/safe-frame boundary.
- [x] No layering violations (graphics-owned bake stays free of live ECS/runtime/AssetService knowledge; `Vk*` types do not cross RHI/renderer/runtime APIs).
- [x] `Operational` cited by an actually-run `gpu;vulkan` smoke; CPU contract gate green for the orchestration logic.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
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
- Adding a CPU bake fallback for GPU bake failure or non-operational backends.
- Blocking asset import on GPU bake completion.
- Passing `Vk*` types through RHI/renderer/runtime/cache public APIs.
- Adding live ECS/runtime/AssetService knowledge to graphics-owned bake modules.
- Treating ordinary glTF tangent-space normal textures as object-space normal maps.
- Mixing this orchestration with unrelated renderer/runtime/asset features.
- Restoring object-space-normal bake ownership, diagnostics, or callbacks on
  `Engine` after `AssetWorkflowModule` owns the workflow.
- Treating a content-key insertion or entity fallback as proof that a generated
  cache view is ready for the requested full bake identity.
- Binding a `GpuAssetView` whose generation differs from the submitted or
  proven-ready bake generation.
- Fabricating `AssetId` values from stable ids/fingerprints, repurposing one
  live generated id for different identity content, or exposing the private
  bake service solely to allocate ids.
- Treating geometry-handle generation as a content revision, accepting
  nonzero channel addresses without verified packing/layout, or capturing one
  device-operational boolean across asynchronous work.
- Replacing the whole material binding when only its normal semantic changes,
  or mutating extraction without the matching progressive binding generation.
- Adding config for queue/cache/dilation limits before a demonstrated tuning
  need; use fixed implementation safety limits and diagnostics in this task.
- Creating/destroying raster or dilation resources per frame or releasing them
  while GPU work may still reference them.

## Maturity
- Target reached: `Operational` on the exercised Vulkan host, with the
  scheduling, stale-key, fail-closed, capacity, lifetime, and transactional
  binding contracts `CPUContracted` on CPU/null.
- The production provider records and publishes a real padded object-space
  normal texture through the existing runtime frame-command lane, and the
  real-app Vulkan readback proves covered, gutter, and uncovered semantics on
  a nonzero shared-index slice.
- No maturity follow-up is owed by this task. A future backend or tangent-space
  method would require its own scoped task rather than extending this retired
  orchestration contract.
