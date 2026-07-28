---
id: RUNTIME-197
theme: B
depends_on: [RUNTIME-192]
maturity_target: Retired
---
# RUNTIME-197 — Unified geometry upload and residency coordinator

## Status

- Promoted to active on 2026-07-28 after `RUNTIME-192` retired the duplicate
  property vocabularies and `RUNTIME-193` retired the progressive-named
  presentation path.
- Intake census found five exported packer modules (about 2,100 interface and
  implementation lines), four domain-specific retirement queues in
  `RenderExtractionCache::State`, and a separate procedural cache owning the
  same generation/reuse/retirement concerns. The existing graphics
  `GpuWorld` already owns allocation, upload/update, handle invalidation, and
  slot reuse, so this task must compose with it rather than introduce another
  device or allocator service.
- Shared mesh surface-topology helpers and mesh primitive-view settings have
  legitimate production consumers outside extraction. Slice B will move them
  into truthful value/function modules before deleting the packer modules;
  only topology-to-byte-plan builders become private extraction details.
- Slice A completed on 2026-07-28. The new graphics-owned
  `GeometryUploadPlan` is an owning copy with stable graphics-only identity,
  generation, fixed stream formats, update class/channels, storage hint, and
  deterministic validation diagnostics. One concrete
  `GeometryResidencyCoordinator` composes with `GpuWorld` for unique
  reconciliation, shared acquisition, partial update/full replacement,
  stale rejection, reference counting, retire cancellation, frame-safe free,
  and hard shutdown. It introduces no device, allocator, interface, factory,
  registry, or app service. The focused target builds and all 5 coordinator
  contracts pass; the old runtime packer/lifecycle paths remain until Slice B.

## Goal

- Replace the mesh/graph/point-cloud/procedural packer families and their
  repeated cache/reupload/retire logic with one graphics-owned
  `GeometryUploadPlan` contract and residency coordinator, while runtime
  retains small private typed plan builders only where topology layouts
  genuinely differ.

## Non-goals

- No universal in-memory geometry container and no erasure of mesh, graph, or
  point-cloud topology types.
- No default AoS policy change, renderer pass rewrite, or graphics ownership of
  ECS/live runtime state.
- No exported packer interface, factory, registry, or service per geometry
  domain.

## Context

- `Runtime.MeshGeometryPacker`, `GraphGeometryPacker`,
  `PointCloudGeometryPacker`, `ProceduralGeometryPacker`, and
  `MeshPrimitiveViewPacker` repeat generation checks, buffer planning,
  upload/reupload accounting, residency sidecars, and retirement behavior.
- Domain-specific topology conversion is real, but the surrounding lifecycle
  is not. Runtime can build copied plans; graphics executes buffer/residency
  operations without ECS knowledge.
- `graphics/renderer` owns the execution-facing plan value, GPU allocation,
  residency, and deferred retirement. Runtime imports that lower-layer
  contract, resolves ECS/geometry snapshots into plans, and submits them from
  existing extraction/asset-workflow owners. Graphics never imports runtime or
  ECS, and no app service is introduced.

## Right-sizing decision

- **Elements:** the five exported packer modules, four extraction retirement
  queues, procedural cache, and per-domain generation/reupload branches
  trigger the role-fragmentation and parallel-lifecycle heuristics. The typed
  topology conversions are real; their surrounding GPU ownership is repeated.
- **Simpler alternative:** retain `GpuWorld` as the sole allocator/uploader and
  add one concrete graphics `GeometryResidencyCoordinator` composed with it.
  The coordinator owns copied `GeometryUploadPlan` bytes, stable graphics-only
  keys, generation publication, partial/full replacement, reference counts,
  and frame-safe retirement. Runtime keeps private free plan builders and maps
  coordinator outcomes into the existing per-domain extraction diagnostics.
  Add no interface, factory, registry, service locator, or second device path.
- **Layer boundary:** `GeometryPropertyRef` remains runtime vocabulary because
  graphics cannot import runtime. Runtime-side channel bindings use the
  canonical property reference, resolve it against copied geometry snapshots,
  and submit only resolved bytes plus graphics-owned format/update metadata.
- **Blast radius:** renderer/runtime modules and CMake, extraction sidecars,
  scene/editor channel-binding DTOs, packer contracts, Vulkan acceptance
  coverage, runtime/graphics residency docs, task records, and the generated
  module inventory. The allocator contract and frame recipe are unchanged.
- **Reintroduction trigger:** split another residency owner only if a second
  independently scheduled GPU world with different lifetime semantics exists.
  Another topology domain, UI, test, or storage lane remains plan data handled
  by this coordinator.

## Slice plan

- **Slice A — common plan/lifecycle.** Define the plain upload plan and common
  generation, allocation, update, stats, and deferred-retire implementation.
- **Slice B — domain adoption.** Convert each packer into a private typed plan
  builder and migrate mesh, graph, point-cloud, procedural, and primitive-view
  workflows one at a time with parity tests.
- **Slice C — cleanup.** Delete public packer modules, duplicated caches/
  sidecars/retire queues, aliases, and obsolete CMake/tests after all domains
  run through the coordinator.

## Required changes

- [ ] Define a graphics-owned backend-neutral `GeometryUploadPlan` describing
      stable geometry identity/generation, vertex/index/property byte ranges,
      formats, update class, storage-lane requirements, and deterministic
      diagnostics; it contains no ECS/runtime types.
- [ ] Implement one graphics residency coordinator for allocation, initial
      upload, partial/full reupload, generation publication, stats, and
      frame-safe retirement, driven by runtime-submitted copied plans.
- [ ] Keep mesh, graph, point-cloud, procedural, and primitive-view topology
      conversion as small private free functions that only build plans.
- [ ] Use `GeometryPropertyRef` for optional property streams instead of
      packer-specific attribute identity.
- [ ] Preserve geometry-specific validation, shared-index slices, dirty-range
      semantics, stable render identity, and graphics snapshot boundaries.
- [ ] Migrate all production callers and then delete the exported packer
      modules plus their duplicated caches, sidecars, retire queues, and
      forwarding tests.

## Tests

- [ ] Table-driven CPU contracts cover plan validation and lifecycle parity for
      every domain, initial upload, partial/full update, generation change,
      stale plan, and deferred retirement.
- [ ] Existing domain extraction/render contracts run through the coordinator
      and preserve exact buffer/layout/topology results.
- [ ] Opt-in Vulkan smokes cover at least one representative of each live
      geometry lane through the unified residency path.
- [ ] Structural tests prove old public packer modules and per-domain
      lifecycle owners are gone.

## Docs

- [ ] Update runtime extraction and renderer residency docs with the
      plan/coordinator boundary and private typed adapters.
- [ ] Regenerate module inventory and update storage-lane documentation.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] Every geometry domain publishes the same lower-layer upload-plan shape
      and uses one graphics allocation/update/retire lifecycle.
- [ ] Domain-specific topology code remains typed and private; no live ECS
      knowledge enters graphics.
- [ ] Public packer modules and duplicated lifecycle state are deleted after
      CPU and Vulkan parity.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryPacker|GeometryUpload|Residency|RenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'Geometry|Residency' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- A domain-erased geometry blob, public packer registry, or new residency
  service exposed to app.
- Moving ECS access into graphics or silently changing layout/storage policy.
- Deleting a domain path before its plan and operational parity are proven.

## Maturity

- Target: `Retired`; contract parity precedes domain-by-domain adoption, actual
  Vulkan proof, and deletion of the old packer/lifecycle family.
