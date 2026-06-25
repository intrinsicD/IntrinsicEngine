# Constraints

## K01: Immediate Geometry Visibility Versus UV-Dependent Texture Binding
- **Constraint**: Meshes may render immediately with default material values, but UV-dependent texture bakes and texture bindings require valid mesh texture coordinates. Missing UVs schedule atlas generation and do not block first geometry visibility.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tasks/done/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O02

## K02: Vertex-Position Dirty Surface Accounting
- **Constraint**: After RUNTIME-124, vertex-position-only mesh dirtiness may update the resident surface geometry in place through `GpuWorld::UpdateGeometryChannels`; surface deferred-retire/free accounting is expected only for full-upload triggers such as topology, vertex-count, storage-layout, or coarse GPU dirtiness. Mesh primitive edge/vertex sidecars still repack on position dirtiness.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tests/contract/runtime/Test.MeshPrimitiveViewExtraction.cpp], [tasks/done/RUNTIME-124-per-channel-partial-uploads.md], [tasks/done/RUNTIME-126-gpu-readback-jobs-and-property-writeback.md]
- **From staging**: O06
