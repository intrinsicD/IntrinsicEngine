---
id: GRAPHICS-084C
theme: F
depends_on: [GRAPHICS-084]
---
# GRAPHICS-084C — Visualization property-buffer Vulkan smoke

## Goal
- Add opt-in Vulkan smoke coverage proving graphics-owned visualization property buffers are consumed through the promoted packet/BDA path on a Vulkan-capable host.

## Non-goals
- No new visualization packet family.
- No arbitrary property-array editor UI.
- No runtime-owned GPU resource lifetime.
- No RHI/CUDA retirement decision; `GRAPHICS-086` owns that audit.

## Context
- Owner/layer: `graphics/vulkan` consumes renderer-owned visualization packets and buffers; runtime remains a data-only producer of copied property arrays and packet metadata.
- `GRAPHICS-084` closes the backend-neutral residency seam at `CPUContracted`: runtime adapters can emit property-buffer upload descriptors, renderer-owned residency uploads them through `RHI::BufferManager`, and scalar/color/vector/isoline packets receive published BDAs before validation.
- This task owns only the `Operational` proof for a Vulkan-capable host. It should reuse the existing visualization-overlay GPU smoke infrastructure and avoid broadening the property-buffer scope beyond current promoted adapters.

## Required changes
- [ ] Add or extend a `gpu;vulkan` visualization smoke that submits a graphics-owned property buffer and consumes the published BDA through a concrete Vulkan frame.
- [ ] Keep packet validation and residency diagnostics on the existing `VisualizationPackets` and renderer stats surfaces.
- [ ] Preserve runtime/ECS layering by submitting immutable runtime snapshots only.

## Tests
- [ ] Add labelled `gpu;vulkan` coverage for the property-buffer-backed visualization packet path.
- [ ] Keep the default CPU gate unchanged; Vulkan smoke remains opt-in.

## Docs
- [ ] Update `src/graphics/renderer/README.md`, `docs/architecture/graphics.md`, and `docs/migration/nonlegacy-parity-matrix.md` only if the backend-proof task changes documented behavior or readiness.
- [ ] Update `tasks/backlog/rendering/README.md` and regenerate `tasks/SESSION-BRIEF.md` when this task retires.

## Acceptance criteria
- [ ] A Vulkan-labelled smoke exercises at least one graphics-owned visualization property buffer through packet BDA publication and backend command execution.
- [ ] Diagnostics distinguish skipped/non-operational Vulkan hosts from property-buffer validation or upload failures.
- [ ] No runtime/ECS live data or GPU-resource ownership is introduced below `runtime`.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Visualization.*PropertyBuffer|PropertyBuffer.*Visualization' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Importing runtime/ECS into graphics or Vulkan backend code.
- Expanding visualization residency beyond selected property arrays used by promoted adapters.

## Maturity
- Target: `Operational` on Vulkan-capable hosts.
- This task provides the Vulkan-capable-host `Operational` proof for the `GRAPHICS-084` property-buffer residency seam.
