# GRAPHICS-045 — Ray tracing RHI extension (IRayTracingDevice) (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until a GI consumer (`GRAPHICS-046`) needs the capability.

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

## Recorded decisions
1. **Capability surface.** `IRayTracingDevice` is fetched via `IDevice::QueryInterface<IRayTracingDevice>()`, returning `nullptr` when RT is unavailable; the renderer guards every RT usage on this query. Rationale: reusing the existing capability-query seam keeps RT purely additive and lets the engine compile/run unchanged on non-RT devices, matching the `GRAPHICS-033` fail-closed posture rather than introducing a parallel feature-flag system.
2. **Acceleration-structure types.** `BlasHandle` and `TlasHandle` with a build → refit → destroy lifecycle managed by the same retire-deadline pattern as other GPU resources. Rationale: opaque handles keep `Vk*`/`D3D12*` BVH types out of the RHI surface (AGENTS.md §2), and reusing the established `framesInFlight` retire-deadline avoids a bespoke BVH lifetime that could free a structure still referenced by an in-flight frame.
3. **BLAS build inputs.** Geometry inputs reuse `GpuGeometryRecord` (vertex BDA + index BDA + counts); the optional meshlet path is documented but not required for BLAS. Rationale: feeding BLAS from the existing geometry record avoids a second authoritative geometry description and keeps one upload path, while leaving meshlet-proxy BLAS as a `GRAPHICS-044` follow-up rather than a prerequisite.
4. **TLAS build inputs.** Per-instance: BLAS handle + 4×3 transform + 24-bit instance custom-index + 8-bit instance mask + flags, matching VK/D3D12 TLAS semantics. Rationale: mirroring the hardware instance descriptor verbatim avoids a translation layer and lock-in to a non-standard layout, so backends map the record directly to `VkAccelerationStructureInstanceKHR`/`D3D12_RAYTRACING_INSTANCE_DESC`.
5. **Inline RT primitive.** `rayQuery` is exposed via a Slang module as the default; ray pipelines + SBT are exposed separately and are opt-in. Rationale: inline RT in compute is the most portable and composable primitive (no SBT plumbing, integrates into existing compute passes), so it is the baseline; hit-shader-driven ray pipelines carry more backend surface and are reserved for workflows that actually need recursive hit shading.
6. **SBT layout.** Lock the raygen + miss + closest-hit + any-hit group layout and the hit-group indexing rule; the SBT buffer is graphics-owned under the retire-deadline pattern. Rationale: pinning the SBT record layout and indexing up front keeps shader-group offsets stable across recompiles, and graphics ownership (not runtime) keeps the GPU-resource lifetime inside the layer that records the trace.
7. **Build queue affinity.** BLAS/TLAS build runs on the async-compute queue where available (gated by `GRAPHICS-037`), falling back to the graphics queue otherwise. Rationale: BVH builds overlap naturally with raster work, so async-compute affinity recovers frame time on multi-queue hardware while the graphics-queue fallback guarantees correctness on single-queue devices.
8. **Refit policy.** TLAS is rebuilt every frame (cheap); BLAS is refit on transform-only changes and fully rebuilt on topology changes. Rationale: a full TLAS rebuild is inexpensive relative to per-instance refit bookkeeping, while BLAS refit-vs-rebuild keyed on topology change is the standard quality/cost trade — refit preserves the tree on rigid motion and a rebuild restores quality when the geometry actually changes.
9. **Diagnostics.** `BlasBuildCount`, `TlasBuildCount`, and `RayTracingFallbackPathCount` (incremented when a consumer runs its software fallback because RT is unavailable) are atomic counters. Rationale: build counts surface BVH churn and the fallback counter makes "RT silently unavailable" observable without strings or a parallel readback, consistent with the engine's atomic-counter diagnostics convention.
10. **Operational-gate addition.** Append "RT capabilities probed and recorded" to `GRAPHICS-033`'s reason enum without rewriting earlier gates. Rationale: the `GRAPHICS-033` gate enum is append-only by contract, so RT joins as a new optional gate that stays `NotRequested` until a consumer opts in — preserving the existing first-failing-gate ordering and every prior gate's meaning.
11. **Failure mode.** RT unavailable returns `nullptr` from `QueryInterface`; the diagnostic `RayTracingUnavailableReason { NotCompiled, NotRequested, DeviceLacksFeature, FailedInit }` records why, and silent enablement is forbidden. Rationale: an explicit reason enum turns "no RT" into a diagnosable state instead of an unexplained null, and forbidding silent enablement keeps RT opt-in so a capable device never pays RT cost the recipe did not request.
12. **Test split.** `unit` + `contract;graphics` against null-RHI mocks (no real device) plus an opt-in `gpu;vulkan` smoke that builds a single-triangle BLAS + TLAS and traces one ray. Rationale: capability-surface and lifecycle correctness are fully exercisable on the CPU gate under null RHI, so only the actual trace needs a device — keeping the default gate green on hosts without RT hardware.
13. **Layering.** No live ECS access, and graphics never imports a vendor RT SDK (RTXDI / FidelityFX-RT) directly. Rationale: preserves AGENTS.md §2 (graphics consumes snapshots, never live ECS) and keeps vendor middleware behind the same kind of seam used elsewhere, so promoted layers stay backend-portable.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-045-Impl-A** — `IRayTracingDevice` interface + null-RHI mock + `contract;graphics` capability tests.
- **GRAPHICS-045-Impl-B** — BLAS/TLAS lifecycle + retire-deadline wiring + `contract;graphics` tests.
- **GRAPHICS-045-Impl-C** — `rayQuery` Slang module + inline-RT consumer seam (no consumers; reserved for `GRAPHICS-046`).
- **GRAPHICS-045-Impl-D** — Ray pipelines + SBT layout + opt-in `gpu;vulkan` smoke.
- **GRAPHICS-045-Impl-E** — Operational-gate extension in `GRAPHICS-033` reason enum + diagnostic wiring.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```
- [x] Optional GPU smoke deferred to `GRAPHICS-045-Impl-D`: `-L 'gpu' -L 'vulkan'`.

## Docs
- [x] The RHI capability section of `docs/architecture/graphics.md` is deferred to the implementation children (`GRAPHICS-045-Impl-A/B`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The capability-surface section of `src/graphics/rhi/README.md` is deferred to the same implementation children for the same reason.
- [x] The operational-gate section of `src/graphics/vulkan/README.md` is deferred to `GRAPHICS-045-Impl-E` for the same reason.

## Acceptance criteria
- [x] Thirteen decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Engine compiles and runs without RT-capable hardware (capability query returns `nullptr`; no extension enables land here).
- [x] `GRAPHICS-033` operational-gate extension policy is preserved (append-only gate, stays `NotRequested`).

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All thirteen ray-tracing RHI decisions are recorded with explicit answers and trade-off rationales: the `QueryInterface`-fetched optional `IRayTracingDevice` capability returning `nullptr` when unavailable, the opaque `BlasHandle`/`TlasHandle` retire-deadline lifecycle, BLAS inputs reusing `GpuGeometryRecord`, the hardware-matching TLAS instance descriptor, inline `rayQuery` as the portable default with opt-in ray pipelines + graphics-owned SBT, async-compute build affinity with graphics-queue fallback, the per-frame-TLAS / refit-BLAS-on-transform / rebuild-on-topology policy, the atomic build/fallback counters, the append-only `GRAPHICS-033` gate addition, the `RayTracingUnavailableReason` fail-closed enum forbidding silent enablement, the null-RHI contract + opt-in single-triangle `gpu;vulkan` test split, and the no-live-ECS / no-vendor-SDK layering audit. Implementation children `GRAPHICS-045-Impl-A..E` are identified but not opened; no Vulkan RT extension enables and the engine compiles/runs unchanged without RT hardware. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No vendor RT SDK imports in promoted graphics layers.
- No silent extension enablement.
- No relaxation of fail-closed Vulkan behavior.
- No live ECS access from RT code.
- No mixing of mechanical file moves with semantic refactors.
