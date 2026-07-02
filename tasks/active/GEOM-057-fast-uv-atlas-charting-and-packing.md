---
id: GEOM-057
theme: none
depends_on: [GEOM-018, GEOM-025]
---
# GEOM-057 — Fast UV atlas charting and packing replacement path

## Goal
- Replace the monolithic xatlas-first UV generation policy with a staged, geometry-owned fast atlas path that can cut charts, parameterize them, and pack the final atlas under explicit diagnostics.

## Non-goals
- Do not remove xatlas in this task; it remains the concrete fallback until the fast staged path passes CPU correctness and quality gates.
- Do not add learning-model dependencies, PyTorch, Blender integration, UVPackMaster integration, renderer hooks, runtime scheduling, or asset import policy changes.
- Do not claim TABI or PartUV parity until benchmark manifests and quality baselines exist.

## Context
- Owning subsystem/layer: `src/geometry`; the dependency boundary remains `geometry -> core` only.
- The near-term direction is a deterministic PartUV-inspired charting path with a conservative built-in per-triangle fast backend today, then multi-face chart growth and a TABI-inspired fast packer as soon as geometry-owned quality/runtime contracts exist.
- TABI (`arXiv:2602.07782`) informs the future atlas layout stage: tight, balanced, interactive chart packing.
- PartUV (`arXiv:2511.16659`) informs the future charting policy: fewer part-aligned charts with distortion thresholds, but the engine path must avoid direct learning/runtime/tool dependencies.
- `Geometry.UvAtlas` exposes source xrefs, chart IDs, chart records, seam-cut records, authored-UV preservation, GEOM-018 diagnostics, a requestable fast staged method, and visible xatlas fallback diagnostics when a caller-supplied fast backend fails with fallback enabled.

## Slice plan
- **Slice A (this slice).** Add the public fast-method selection surface, fail-closed fallback control, diagnostics, and unit tests. This closes `Scaffolded -> CPUContracted` for the replacement seam only.
- **Slice B.** Add deterministic chart-record and seam-cut records plus a conservative geometry-only chart proposal backend.
- **Slice C.** Replace per-triangle charting with multi-face chart proposals, parameterize proposed charts with existing LSCM/harmonic-style solvers where topology allows, and emit per-chart quality diagnostics.
- **Slice D.** Add the TABI-inspired fast packer, benchmark manifests, and quality/runtime comparisons against the xatlas fallback.
- **Slice E.** Promote the fast staged method to the default once quality and performance gates pass; keep xatlas only as explicit compatibility fallback or remove it in a separate retirement task.

## Control surfaces
- Config: `UvAtlasOptions::Method` selects xatlas or the fast staged method; `UvAtlasOptions::AllowXAtlasFallback` controls whether a missing fast backend falls back or fails closed.
- UI: deferred to later runtime/editor tasks.
- Agent/CLI: geometry tests and benchmarks select methods through `UvAtlasOptions`.

## Backends
- Backend axis: present. `UvAtlasBackend` remains the injectable backend seam; xatlas is the default concrete backend for now, and the fast staged method is a requestable method target with a conservative built-in backend.

## Required changes
- [x] Add `UvAtlasMethod` and method/fallback diagnostics to the public UV atlas module interface.
- [x] Preserve authored-UV behavior while recording method diagnostics for generated and preserved results.
- [x] Route `UvAtlasMethod::FastStaged` to xatlas only when fallback is explicitly allowed; otherwise fail closed with `BackendUnavailable`.
- [x] Keep caller-supplied backends able to satisfy the fast staged method without importing xatlas.
- [x] Add deterministic chart records and seam-cut records for the fast staged backend.
- [x] Add a conservative geometry-only built-in backend that cuts per-face charts, locally flattens them, and grid-packs the atlas without invoking xatlas.
- [ ] Replace per-face charting with a geometry-only multi-face chart proposal policy inspired by PartUV's top-down chart policy.
- [ ] Parameterize accepted multi-face charts with existing geometry solvers and per-chart quality diagnostics.
- [ ] Add a TABI-inspired fast packer with benchmark manifests and baseline comparison against xatlas.
- [ ] Promote the fast staged method to the default only after quality/runtime gates pass.

## Tests
- [x] Add unit coverage for built-in fast staged requests producing finite, normalized, non-overlapping UVs without xatlas fallback.
- [x] Add unit coverage for fast staged backend failures falling back to xatlas only when fallback is enabled.
- [x] Add unit coverage for fast staged backend failures with fallback disabled failing closed.
- [x] Add unit coverage proving a caller-supplied fast backend can satisfy the method request.
- [x] Add runtime contract coverage proving distinct selected mesh properties bake to distinct generated texture assets and update their progressive slots.
- [x] Add runtime contract coverage proving multiple generated property textures bind to unique material texture slots.
- [x] Run the Vulkan shader normal-bake smoke proving the GPU-rendered object-space normal texture matches interpolated vertex-normal expectations at sampled texels.

## Docs
- [x] Update `docs/architecture/geometry.md` with the method selector and xatlas fallback policy.
- [x] Update `docs/architecture/parameterization-mapping-roadmap.md` with the fast staged replacement path and follow-up slices.
- [x] Regenerate `docs/api/generated/module_inventory.md` after the module surface change.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening this active task.

## Acceptance criteria
- [x] `Geometry.UvAtlas` exposes a stable method selector for `XAtlas` and `FastStaged`.
- [x] Fallback to xatlas is visible through diagnostics and can be disabled by callers.
- [x] Existing xatlas default behavior and authored-UV preservation remain compatible.
- [x] The task file names the follow-up work required before xatlas can be replaced as the default.
- [x] A built-in fast staged backend cuts charts, parameterizes charts, and packs the atlas without invoking xatlas.
- [ ] Benchmarks show the fast staged backend is suitable to replace xatlas as the default.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'UvAtlas' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci --output-on-failure -R 'ObjectSpaceNormalTextureBakeGpuSmoke' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
git diff --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Removing xatlas before the fast method has benchmarked quality/runtime evidence.
- Adding renderer, runtime, asset, ECS, platform, app, or method-package dependencies to `src/geometry`.
- Introducing paper-specific or learned-model code into the generic geometry layer.
- Treating a fallback result as if the fast method actually ran.

## Maturity
- Current: `Operational` lower-bound fast charting/packing for deterministic per-triangle charts with finite non-overlapping UVs, chart records, seam records, and property xrefs.
- Remaining: multi-face chart quality, TABI-style packing, benchmark baselines, and default promotion stay open before xatlas can be retired or demoted to compatibility fallback.
