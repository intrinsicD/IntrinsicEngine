---
id: GEOM-025
theme: none
depends_on: [INFRA-001, GEOM-018]
maturity_target: CPUContracted
---
# GEOM-025 — UV atlas backend contract and xatlas default

## Goal
- Add a geometry-owned, replaceable UV atlas backend contract with `xatlas` as the default backend for meshes that need generated texture coordinates.

## Non-goals
- No runtime asset import, ECS, renderer, material, UI, or GPU integration in this task.
- No ARAP, SLIM, harmonic/Tutte, or paper-specific solver implementation.
- No persistent generated texture assets or bake scheduling.
- No performance or quality-win claims without benchmark evidence.

## Context
- Status: done (retired 2026-06-16; CPUContracted).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: none; GEOM-025 retired after focused geometry tests, `IntrinsicTests` build, module inventory regeneration, and strict structural checks passed on 2026-06-16.
- Owning subsystem/layer: `geometry` (`geometry -> core` only) plus build dependency wiring through the repository vcpkg path.
- User goal: every renderable mesh should have valid texture coordinates; `xatlas` is the default fallback when source assets do not provide valid UVs.
- The upstream `jpcy/xatlas` API creates an atlas with `Create`, accepts meshes through `AddMesh`, runs `Generate`, and returns output meshes with UVs. Its output can have more vertices than the input because UV seams duplicate vertices; `xatlas::Vertex::xref` records the originating input vertex.
- `xatlas` exposes chart and pack options, progress callbacks, and a custom parameterization callback, but IntrinsicEngine still needs an engine-level backend interface so other atlas/parameterization implementations can replace it completely.
- `GEOM-018` supplies the shared parameterization diagnostics vocabulary this task must use for quality reports.
- `INFRA-001` owns the vcpkg migration. If the pinned vcpkg baseline still has no first-class `xatlas` port when this task starts, add a repository overlay port instead of adding FetchContent or `external/cache` traffic.

## Required changes
- [x] Define backend-neutral records in the promoted geometry API: `UvAtlasInput`, `UvAtlasOptions`, `UvAtlasResult`, `UvAtlasDiagnostics`, backend identity, provenance, and failure status.
- [x] Define the required output shape for seam-aware atlas results: output positions, output faces/indices, output `v:texcoord`, source-vertex xref, source-face xref, chart count, atlas resolution, and normalized UV range.
- [x] Add a backend registration/selection seam that can run the default backend or a caller-provided backend without importing runtime, assets, graphics, ECS, platform, or app.
- [x] Add a deterministic authored-UV validator that accepts only finite, count-matched, triangle-usable UV data and reports invalid/missing cases through diagnostics.
- [x] Add the `xatlas` backend implementation behind the geometry contract, including option mapping for padding, resolution, texels-per-unit, chart options, progress/cancel status, and UV normalization from atlas pixel space.
- [x] Preserve and propagate source vertex properties through seam splits using the `xref` table, including `v:normal`, colors, scalar fields, and vector fields when their source counts match.
- [x] Route `GEOM-018` diagnostics over generated and preserved UVs so backend quality can be compared without renderer involvement.
- [x] Wire the `xatlas` dependency through `vcpkg.json` or a repository overlay port after `INFRA-001`; do not add a FetchContent fallback.

## Tests
- [x] Add `unit;geometry` tests proving a fake backend can replace the default backend and receives the expected input/options.
- [x] Add `unit;geometry` tests proving valid authored `v:texcoord` data can be preserved without invoking the fallback backend.
- [x] Add `unit;geometry` tests proving a mesh without UVs generates finite normalized UVs through the default `xatlas` backend.
- [x] Add seam-split tests proving output vertex count may exceed input vertex count and `sourceVertex` xrefs preserve copied properties.
- [x] Add invalid-input tests for empty meshes, missing positions, non-finite positions, non-finite authored UVs, out-of-range indices, and degenerate-all-face inputs.
- [x] Add diagnostics tests that consume `GEOM-018` quality metrics for generated UVs.

## Docs
- [x] Update [`docs/architecture/parameterization-mapping-roadmap.md`](../../docs/architecture/parameterization-mapping-roadmap.md) with the promoted atlas backend contract and `xatlas` default decision.
- [x] Update [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md) with the public UV atlas contract if module surfaces change.
- [x] Update `docs/build-troubleshooting.md` if an `xatlas` overlay port or dependency bootstrap step is added.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Geometry exposes a backend-neutral UV atlas API that returns valid, finite texture coordinates or an explicit failure status.
- [x] `xatlas` is the default registered backend and is reachable from tests through the public geometry seam.
- [x] A caller-supplied backend can replace `xatlas` without touching runtime, renderer, or asset code.
- [x] Seam-split atlas output carries enough xref/provenance data for runtime to preserve normals, colors, scalar fields, selection, and bake sources.
- [x] Quality diagnostics are emitted through the shared parameterization diagnostics path.
- [x] Layering remains `geometry -> core`; no cross-layer convenience imports are introduced.

## Progress notes
- 2026-06-16: Added `Geometry.UvAtlas` with backend-neutral input/options/result/diagnostics records, authored-UV validation, provenance/failure statuses, source xrefs, property remapping, and caller-supplied backend replacement.
- 2026-06-16: Added the default `jpcy/xatlas` backend through a pinned repository vcpkg overlay port and private geometry implementation link; no third-party headers are exposed in the public module API.
- 2026-06-16: Added geometry tests covering fake backend replacement, authored UV preservation, default xatlas generation, seam-split xref property preservation, invalid inputs, and GEOM-018 diagnostics over atlas output.
- 2026-06-16 retirement verification: `cmake --preset ci`, `cmake --build --preset ci --target IntrinsicGeometryTests`, `cmake --build --preset ci --target IntrinsicTests`, focused `ctest -R 'Parameterization|UvAtlas|XAtlas'`, module inventory regeneration, strict layering/test-layout/task checks, and doc-link checks passed.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryUnitTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization|UvAtlas|XAtlas' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not make `assets`, `runtime`, `graphics`, `ECS`, `platform`, or `app` depend on a concrete `xatlas` header.
- Do not flatten xatlas seam output into a single UV per original vertex when that would make overlapped or invalid chart seams.
- Do not add new third-party dependency traffic to `external/cache`.
- Do not hide fallback failures behind zero UVs.
- Do not claim that generated UVs are visually optimal without benchmark and diagnostic evidence.

## Maturity
- Target: `CPUContracted`.
- This task closes the backend contract and default CPU backend. `Operational` owned by `GRAPHICS-088`.
