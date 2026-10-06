---
id: RUNTIME-222
theme: J
depends_on: [RUNTIME-221]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive follow-up; implementation diff and CPU/Vulkan readback evidence.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence]
maturity_target: Operational
---
# RUNTIME-222 — Model-space point radius rendering

## Goal
Bind published radius properties to retained points with explicit model-space units and correct camera projection. RUNTIME-221 estimates radii and offers scalar colors; current retained point sizes are pixels. `Runtime.RenderExtraction.cpp` rejects the `PointSize` presentation slot and supplies only uniform point size. `GpuEntityPointConfig::PointSizeBDA` is existing transport, not proof that a named model-space radius is uploaded, projected or rendered.

## Scope and design decisions
- Source selection of the radius property is the point-size attribute binding owned by `RUNTIME-315` (operator feedback 2026-10-02: every render attribute is user-selectable); this task keeps units, projection, depth and picking, and consumes that binding rather than adding a second one.
- Reuse geometry presentation/config recipes and canonical typed properties. Do not bind model-space radii directly to the existing pixel-size slot.
- Runtime owns property upload/residency and source mappings; graphics consumes immutable buffers/config without ECS knowledge.
- Reuse RUNTIME-221 and SpatialIndexCache for optional estimation. Rendering consumes the published property and must not rebuild an LBVH each frame. Preserve the estimator's k+1/self/duplicate policy.
- Define zero radii, transforms (including nonuniform scale), camera projection, depth and picking consistently; retain current pixel-size behavior for existing recipes.

## References
- REVIEW-007 G01 deleted unloaded shaders that implement world-space point radius (read with `git show 087e6e17b:<path>`): `assets/shaders/point_flatdisc.vert @ 087e6e17b` and `assets/shaders/point_sphere.vert @ 087e6e17b` (radius buffer → camera-facing billboard with world radius; sphere center/radius for depth), and `assets/shaders/pick_point.vert @ 087e6e17b` (picking quad with the same world radius, so pick and visible footprint agree). Reuse the projection principle only; not their size clamp or Camera-UBO/push-constant ABI.

## Acceptance criteria
- [ ] A serialized, validated config/UI path binds a canonical float radius with explicit units and diagnostics.
- [ ] Runtime uploads and invalidates radius buffers with position/deletion mapping, preserving unrelated properties.
- [ ] Projection and source mapping work for compatible point domains; other domains show truthful availability or an explicit rendering follow-up.
- [ ] CPU contract tests and actual Vulkan size/depth/picking readbacks cover camera distance, transforms, edits and stale resources.
- [ ] Update presentation/rendering docs and Framework24 convergence evidence with the verified scope.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
