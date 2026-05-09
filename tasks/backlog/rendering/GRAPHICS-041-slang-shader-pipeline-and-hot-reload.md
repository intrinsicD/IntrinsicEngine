# GRAPHICS-041 — Slang as canonical shading language with module compilation and hot reload (planning)

## Goal
Lock down the contract for adopting Slang as the canonical shading language across the engine: offline + runtime compilation pipeline, module/generic system for material composition, autodiff seam for differentiable rendering work, file-watcher-driven hot reload coordinated with the existing `framesInFlight` retire-deadline pattern, and a clean fallback path that keeps SPIR-V shipping with the engine. Planning only — no Slang dependency added in this slice.

## Non-goals
- No Slang dependency added to `cmake/Dependencies.cmake` in this planning slice.
- No removal of the existing direct-SPIR-V loader path; it remains the unconditional baseline.
- No HLSL or GLSL transpiler bringup; Slang is the declared canonical language.
- No autodiff-using shaders here; the seam is recorded for `GRAPHICS-051`.
- No live-edit IDE integration. File-watcher reload only.
- No live ECS access from shader compilation infrastructure.

## Context
- Owner layer: `tools/shader-compile/` (offline compiler binary), `graphics/assets` (compiled-shader cache + hot-reload retire queue), `graphics/rhi` (no surface change; loads SPIR-V the same way).
- Today the Vulkan backend reads pre-compiled SPIR-V from disk. This is functional but blocks (a) material composition (shading kernels per material need generic instantiation), (b) cross-API portability (DXIL/MSL/WGSL targets), (c) differentiable rendering (Slang's first-class autodiff is the natural seam).
- Slang (NVIDIA → Khronos open governance, Nov 2024) targets SPIR-V/DXIL/MSL/WGSL/GLSL with modules + generics + autodiff. Adopting Slang does not require a runtime dependency in promoted graphics layers — offline compilation produces SPIR-V that the existing loader consumes.
- Cross-links: `GRAPHICS-006` (material/shader/pipeline registry), `GRAPHICS-023` (hot reload), `GRAPHICS-043` (visibility-buffer materialization wants Slang modules per material type), `GRAPHICS-051` (differentiable rendering autodiff seam).

## Design decisions to record
1. **Compiler placement.** Slang compiler binary lives under `tools/shader-compile/`, invoked from CMake at build time. Promoted graphics layers never link Slang at runtime in the default build. Record the build-time invocation contract.
2. **Source layout.** `.slang` source files live under `src/graphics/shaders/` (or chosen canonical location). Output `.spv` artifacts live under `build/.../shaders/`. Record the directory rule.
3. **Module system.** Materials, lighting, BRDFs, shadow sampling, and post-process kernels each become Slang modules with explicit imports. Record the canonical module list + ownership.
4. **Generics for materials.** Material types from `MaterialSystem` instantiate a generic `Surface<MaterialT>` shading kernel. Record how the per-material specialization is named and cached.
5. **Hot reload.** A runtime-side file watcher under `runtime/` (not graphics) detects source changes, invokes the offline compiler, and pushes updated SPIR-V into `Graphics.GpuAssetCache::ReuploadShader()`. The `framesInFlight` retire-deadline pattern guards stale-pipeline hazards. Record the message-passing API.
6. **Failure handling.** Compile failure on hot reload retains the previous shader and surfaces a structured diagnostic on `RenderDiagnostics`. Counter: `ShaderHotReloadFailureCount`.
7. **Autodiff seam.** Reserve a `differentiable` annotation policy and a paired forward/backward kernel naming convention. No autodiff-using kernels land in this slice; `GRAPHICS-051` opens the consumers.
8. **Cross-API targeting.** Slang outputs SPIR-V today. DXIL/MSL/WGSL targets are recorded as future build options gated by `IDevice` capability. Record the policy that the canonical source remains Slang.
9. **Reflection.** Slang reflection produces descriptor-set / push-constant layouts that `MaterialSystem` and the pipeline registry consume. Record the JSON or binary reflection format and its consumer.
10. **Dependency-free baseline.** The default CI build remains buildable without Slang installed by shipping pre-compiled SPIR-V in `external/cache/` alongside source. Record the offline-deps cache rule.
11. **Test split.** `unit` for reflection parsing; `contract;graphics` for hot-reload retire-deadline wiring under null RHI; opt-in `gpu;vulkan` smoke for compiled-shader correctness.
12. **Layering.** `graphics/` does not import the Slang compiler at runtime. `runtime/` owns the file watcher and compiler invocation. Reflection data is consumed in graphics; the compiler is not.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-041-Impl-A** — `tools/shader-compile/` Slang invocation + CMake integration + offline-deps cache update.
- **GRAPHICS-041-Impl-B** — Reflection format + `MaterialSystem` consumer + `unit` parsing tests.
- **GRAPHICS-041-Impl-C** — Hot-reload runtime-side file watcher + retire-deadline wiring + `contract;graphics` tests.
- **GRAPHICS-041-Impl-D** — Generic `Surface<MaterialT>` lowering + per-material specialization caching + `contract;graphics` tests.
- **GRAPHICS-041-Impl-E** — `differentiable` annotation policy + paired kernel naming convention (no consumers; reserved for `GRAPHICS-051`).

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` shader-pipeline section.
- Update `src/graphics/renderer/README.md` material-shader section.
- Update `cmake/Dependencies.cmake` doc only when Impl-A lands; no edits in planning slice.

## Acceptance criteria
- Twelve decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- Default CI build buildability without Slang is preserved through cached SPIR-V in `external/cache/`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No Slang runtime dependency in promoted graphics layers.
- No removal of the direct-SPIR-V loader.
- No live ECS access from compilation infrastructure.
- No mixing of mechanical file moves with semantic refactors.
