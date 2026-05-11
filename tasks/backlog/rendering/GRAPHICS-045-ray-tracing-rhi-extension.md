# GRAPHICS-045 — Ray tracing RHI extension (IRayTracingDevice) (planning)

## Goal
Lock down the contract for an optional `IRayTracingDevice` capability extension on the RHI: BLAS / TLAS build and refit, inline ray tracing in compute, ray pipelines + shader binding tables, ownership of the BVH lifecycle, and an explicit "not supported" path that keeps the engine compiling and running without RT-capable hardware. Planning only — no Vulkan ray-tracing extension enables in this slice.

## Non-goals
- No global illumination consumers (`GRAPHICS-046` opens the consumer side).
- No path tracer or reference-mode renderer in this task.
- No removal of the existing rasterization paths; RT is purely additive.
- No CPU ray tracing.
- No `VK_KHR_ray_tracing_*` extension enables in `src/graphics/vulkan/` in this planning slice.

## Context
- Owner layer: `graphics/rhi` (`IRayTracingDevice` capability extension), `graphics/vulkan` (extension wiring + BVH build commands), `graphics/framegraph` (acceleration-structure resource lifetime).
- The RHI today exposes `IDevice::QueryInterface<T>()` (or equivalent) to fetch capability sub-interfaces. RT is a natural capability extension: the engine compiles and runs without it on devices that lack RT.
- Industry baseline: DXR 1.1 / `VK_KHR_ray_tracing_pipeline` + `VK_KHR_acceleration_structure` + `VK_KHR_ray_query`. Inline RT in compute (`rayQuery`) is the most flexible primitive; ray pipelines + SBT are needed for hit-shader-driven workflows.
- Cross-links: `GRAPHICS-033` (operational gate; RT is appended as a future gate without rewriting prior gates), `GRAPHICS-044` (meshlet representation; BLAS may use proxy meshes), `GRAPHICS-046` (GI consumer), `GRAPHICS-047` (VSM page rendering may use RT for occlusion in some configurations).

## Design decisions to record
1. **Capability surface.** `IRayTracingDevice` is fetched via `IDevice::QueryInterface<IRayTracingDevice>()`. Returns `nullptr` when RT is unavailable. The renderer guards every RT usage on this query.
2. **Acceleration-structure types.** `BlasHandle`, `TlasHandle`. Lifecycle: build → refit → destroy, with the same retire-deadline pattern as other GPU resources.
3. **BLAS build inputs.** Geometry inputs reuse `GpuGeometryRecord` (vertex BDA + index BDA + counts). Optional meshlet path documented but not required for BLAS.
4. **TLAS build inputs.** Per-instance: BLAS handle + transform (4×3) + instance custom-index (24-bit) + instance mask (8-bit) + flags. Match VK/D3D12 TLAS semantics.
5. **Inline RT primitive.** `rayQuery` exposed via Slang module; ray pipelines + SBT exposed separately. Default: inline RT in compute. Ray pipelines are opt-in.
6. **SBT layout.** Lock the layout (raygen + miss + closest-hit + any-hit groups, hit-group indexing rule). Record the SBT buffer ownership (graphics-owned, retire-deadline pattern).
7. **Build queue affinity.** BLAS/TLAS build runs on async-compute queue (gated by `GRAPHICS-037`) where available; falls back to graphics queue otherwise.
8. **Refit policy.** TLAS rebuild every frame (cheap); BLAS refit on transform-only changes; full BLAS rebuild on topology changes. Record the rule.
9. **Diagnostics.** `BlasBuildCount`, `TlasBuildCount`, `RayTracingFallbackPathCount` (when consumers run software fallback because RT is unavailable). Atomic counters.
10. **Operational-gate addition.** Append "RT capabilities probed and recorded" as a gate in `GRAPHICS-033`'s reason enum without rewriting earlier gates.
11. **Failure mode.** RT unavailable returns `nullptr` from QueryInterface; the diagnostic `RayTracingUnavailableReason { NotCompiled, NotRequested, DeviceLacksFeature, FailedInit }` records why. Forbid silent enablement.
12. **Test split.** `unit` + `contract;graphics` against null-RHI mocks (no real device); opt-in `gpu;vulkan` smoke that builds a single triangle BLAS + TLAS + traces one ray.
13. **Layering.** No live ECS access. Graphics never imports a vendor RT SDK (RTXDI / FidelityFX-RT) directly.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-045-Impl-A** — `IRayTracingDevice` interface + null-RHI mock + `contract;graphics` capability tests.
- **GRAPHICS-045-Impl-B** — BLAS/TLAS lifecycle + retire-deadline wiring + `contract;graphics` tests.
- **GRAPHICS-045-Impl-C** — `rayQuery` Slang module + inline-RT consumer seam (no consumers; reserved for `GRAPHICS-046`).
- **GRAPHICS-045-Impl-D** — Ray pipelines + SBT layout + opt-in `gpu;vulkan` smoke.
- **GRAPHICS-045-Impl-E** — Operational-gate extension in `GRAPHICS-033` reason enum + diagnostic wiring.

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```
- [ ] Optional GPU smoke: `-L 'gpu|vulkan'`.

## Docs
- [ ] Update `docs/architecture/graphics.md` RHI capability section.
- [ ] Update `src/graphics/rhi/README.md` capability surface.
- [ ] Update `src/graphics/vulkan/README.md` operational-gate section.

## Acceptance criteria
- [ ] Thirteen decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] Engine compiles and runs without RT-capable hardware.
- [ ] `GRAPHICS-033` operational-gate extension policy is preserved.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No vendor RT SDK imports in promoted graphics layers.
- No silent extension enablement.
- No relaxation of fail-closed Vulkan behavior.
- No live ECS access from RT code.
- No mixing of mechanical file moves with semantic refactors.
