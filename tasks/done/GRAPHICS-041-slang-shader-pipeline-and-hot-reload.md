# GRAPHICS-041 — Slang as canonical shading language with module compilation and hot reload (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

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

## Recorded decisions
1. **Compiler placement.** The Slang compiler is invoked through a thin wrapper under **`tools/shader-compile/`**, driven by a CMake custom command at build time that consumes `.slang` sources and emits `.spv` + a reflection sidecar. Promoted graphics layers **never link Slang at runtime** in the default build. The build-time contract: `(slang_source, entry_point, stage, target=spirv, defines) -> (spv_artifact, reflection_json)`, deterministic for a fixed compiler version pinned in `cmake/Dependencies.cmake`. Rationale: an offline tool keeps the runtime dependency-free and reproducible; pinning the compiler version makes artifacts cacheable in `external/cache/`.
2. **Source layout.** `.slang` source files live under **`src/graphics/shaders/`** (the canonical promoted shader root); compiled `.spv` + `.refl.json` artifacts are written under **`build/<preset>/shaders/`** and mirrored into **`external/cache/shaders/`** for the dependency-free baseline. Authoring assets that are not promoted-graphics-owned stay under `assets/shaders/` as today. Rationale: co-locating canonical source with the consuming layer keeps ownership obvious; routing outputs through the build tree + cache preserves the offline-deps story.
3. **Module system.** The canonical Slang module set is **`Material`, `Lighting`, `BRDF`, `ShadowSampling`, `PostProcess`, and `Common`** (shared math/packing), each a `.slang` module with explicit `import` edges and a single owning area: `BRDF`/`Material` own surface shading, `Lighting`/`ShadowSampling` own light evaluation, `PostProcess` owns the resolve chain, `Common` owns shared helpers. Rationale: an explicit, small module list with one owner each prevents the include-soup that plain GLSL `#include` encourages and gives `GRAPHICS-042`/`043` clean carriers.
4. **Generics for materials.** Each `MaterialTypeDesc` instantiates a generic **`Surface<MaterialT>`** kernel; the specialization is named **`Surface_<MaterialTypeName>_<stage>`** and cached in `Graphics.GpuAssetCache` keyed by `(MaterialTypeID, stage, compiler-version-hash)`. Rationale: deterministic specialization names make the cache key stable and debuggable, and let the vis-buffer materialization path (`GRAPHICS-043`) request a known kernel by material type.
5. **Hot reload.** A **runtime-side** file watcher under `runtime/` (never `graphics/`) detects `.slang` source changes, invokes the `tools/shader-compile/` wrapper, and pushes updated SPIR-V into **`Graphics.GpuAssetCache::ReuploadShader()`**; the existing `framesInFlight` retire-deadline guards in-flight pipelines against stale-BMI hazards. The message API is a runtime→graphics one-way push of `(ShaderId, new_spv, new_reflection)`; graphics never calls back into the watcher. Rationale: runtime owns composition and process/IO (AGENTS.md §2); reusing the retire-deadline pattern avoids inventing a second lifetime mechanism.
6. **Failure handling.** A compile failure on hot reload **retains the previously-good shader** (no pipeline teardown), surfaces a structured diagnostic on `RenderDiagnostics`, and increments **`ShaderHotReloadFailureCount`**. Success replaces the cache entry on the next `BeginFrame()` drain. Rationale: fail-closed-to-last-good keeps an interactive session alive through a typo; the counter + structured diagnostic make failures observable without crashing.
7. **Autodiff seam.** Reserve a **`[Differentiable]`** annotation policy and a paired **`fwd_<kernel>` / `bwd_<kernel>`** forward/backward naming convention; no autodiff-using kernels land here. `GRAPHICS-051` opens the consumers. Rationale: declaring the naming/annotation contract now lets the differentiable-rendering task slot in without renaming existing kernels later.
8. **Cross-API targeting.** Slang emits **SPIR-V today**; DXIL/MSL/WGSL are recorded as future build options gated by `IDevice` capability, and the **canonical authored source stays Slang** regardless of target. No transpiler from GLSL/HLSL is introduced. Rationale: one canonical source with pluggable targets is the whole point of Slang; gating extra targets on device capability avoids speculative backend work.
9. **Reflection.** The compiler emits a **`.refl.json`** sidecar per artifact describing descriptor-set/binding layout, push-constant ranges, and specialization constants; `MaterialSystem` and the pipeline registry consume it at load time. The format is JSON (human-diffable) in this contract; a binary form is an optional later optimization behind the same consumer API. Rationale: JSON keeps reflection inspectable and version-control-friendly during bring-up; pinning the consumer API lets the storage form change without churn.
10. **Dependency-free baseline.** The default CI build remains buildable **without Slang installed** by shipping pre-compiled SPIR-V + reflection in **`external/cache/shaders/`** alongside source; `INTRINSIC_OFFLINE_DEPS=ON` consumes the cache strictly, and the compile step is skipped when the cached artifact hash matches. Rationale: this is the same FetchContent/`external/cache/` offline discipline the repo already mandates; it keeps `cmake --preset ci` green on hosts without the compiler.
11. **Test split.** `unit` for reflection-JSON parsing (layout extraction round-trip); `contract;graphics` for the hot-reload retire-deadline wiring + fail-closed retention + `ShaderHotReloadFailureCount` under null RHI; opt-in `gpu;vulkan` smoke for compiled-shader correctness on a real device. Rationale: reflection parsing and reload bookkeeping are pure-CPU contracts; only shader execution needs a device.
12. **Layering.** `graphics/` **does not import the Slang compiler at runtime**; `runtime/` owns the watcher and compiler invocation; reflection *data* is consumed in graphics while the *compiler* is not. The offline tool under `tools/` depends on neither. Rationale: preserves AGENTS.md §2 — graphics stays free of process/IO and external compiler linkage, runtime owns composition.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-041-Impl-A** — `tools/shader-compile/` Slang invocation + CMake integration + offline-deps cache update.
- **GRAPHICS-041-Impl-B** — Reflection format + `MaterialSystem` consumer + `unit` parsing tests.
- **GRAPHICS-041-Impl-C** — Hot-reload runtime-side file watcher + retire-deadline wiring + `contract;graphics` tests.
- **GRAPHICS-041-Impl-D** — Generic `Surface<MaterialT>` lowering + per-material specialization caching + `contract;graphics` tests.
- **GRAPHICS-041-Impl-E** — `differentiable` annotation policy + paired kernel naming convention (no consumers; reserved for `GRAPHICS-051`).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The shader-pipeline section for `docs/architecture/graphics.md` is deferred to the implementation children (`GRAPHICS-041-Impl-A/B/C/D`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The material-shader section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.
- [x] The `cmake/Dependencies.cmake` Slang entry lands only when `GRAPHICS-041-Impl-A` lands; no edits in this planning slice.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Default CI build buildability without Slang is preserved through cached SPIR-V in `external/cache/`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve Slang-adoption decisions are recorded with explicit answers and trade-off rationales: the `tools/shader-compile/` offline build-time invocation with a pinned compiler version, the `src/graphics/shaders/` source root with build-tree + `external/cache/shaders/` artifacts, the six-module canonical set (`Material`/`Lighting`/`BRDF`/`ShadowSampling`/`PostProcess`/`Common`), the generic `Surface<MaterialT>` specialization naming/caching, the runtime-side file-watcher → `ReuploadShader()` retire-deadline hot-reload, the fail-closed-to-last-good policy with `ShaderHotReloadFailureCount`, the `[Differentiable]` + `fwd_/bwd_` autodiff naming seam reserved for GRAPHICS-051, the SPIR-V-today cross-API policy, the `.refl.json` reflection sidecar consumed by `MaterialSystem`, the dependency-free cached-SPIR-V CI baseline, the test split, and the layering audit. Implementation children `GRAPHICS-041-Impl-A..E` are identified but not opened; no Slang dependency, runtime compiler linkage, or direct-SPIR-V-loader removal lands. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No Slang runtime dependency in promoted graphics layers.
- No removal of the direct-SPIR-V loader.
- No live ECS access from compilation infrastructure.
- No mixing of mechanical file moves with semantic refactors.
