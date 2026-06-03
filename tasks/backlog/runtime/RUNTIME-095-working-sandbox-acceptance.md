# RUNTIME-095 — Working sandbox app acceptance path

## Goal
- Add the end-to-end acceptance task for the promoted `ExtrinsicSandbox`: render at least one mesh, one graph, and one point cloud through the default runtime/graphics path with working camera controls, entity/primitive selection, selection outline, and core UI panels.

## Non-goals
- No implementation of prerequisite renderer/runtime/UI tasks inside this acceptance task.
- No performance benchmark claims; this is functional smoke/acceptance only.
- No legacy graphics fallback path or direct app-layer rendering shortcuts.
- No requirement that every asset format or every visualization mode be complete.
- No mandatory GPU run in the default CPU CI gate; Vulkan acceptance remains opt-in and label-gated.

## Context
- Owner/layer: `runtime` composition and acceptance harness, with dependencies across rendering, assets, runtime adapters, and UI.
- This task is the discoverable path from the current default-recipe visible-triangle baseline to the working sandbox described in Theme A of `tasks/backlog/README.md`.
- Required upstreams include default-recipe rendering (`GRAPHICS-072..079`, `GRAPHICS-081`), runtime geometry residency (`RUNTIME-085..088`), selection (`GRAPHICS-074`, `RUNTIME-089`, `RUNTIME-092`, `RUNTIME-093`), asset/UI plumbing (`ASSETIO-001` — which subsumed the retired `RUNTIME-080` texture bridge —, `RUNTIME-090`, `UI-001`), and optional visualization/spatial-debug adapters (`RUNTIME-082`, `RUNTIME-083`, `GRAPHICS-077`, `GRAPHICS-078`).
- The sandbox app itself must remain policy-light: composition and test scene setup live in runtime/reference-scene/editor seams, not direct graphics imports from `src/app`.

## Required changes
- [ ] Define a deterministic sandbox acceptance scene provider or fixture that creates/loads one mesh, one graph, and one point cloud using promoted runtime/ECS/asset seams.
- [ ] Ensure each fixture entity has camera-visible transforms, bounds, render hints, selectable state, and valid `GpuWorld` residency via the appropriate runtime bridge.
- [ ] Add an opt-in `gpu;vulkan;integration` smoke that launches or drives `ExtrinsicSandbox`/runtime for bounded frames and asserts default-recipe command recording reaches present with no canonical pass falling through the unavailable branch.
- [ ] Add CPU/null integration coverage that verifies extraction/submission state for the same scene without requiring Vulkan.
- [ ] Assert camera controller updates produce finite/invertible frame cameras for the acceptance scene.
- [ ] Assert selection flow can select an entity and at least one primitive domain per geometry family where supported: mesh face/edge/vertex, graph edge/node, point-cloud point.
- [ ] Assert outline snapshot state is populated for selected/hovered entities and the graphics command path records selection/outline passes when operational.
- [ ] Assert core UI panels register and produce deterministic enabled/disabled states for the acceptance scene.
- [ ] Record unsupported but non-blocking features explicitly (for example advanced PBR, transparent selection, Gaussian splats, full scene serialization) so the acceptance stop-state is reviewable.

## Tests
- [ ] Add `integration;runtime` CPU/null acceptance test for mesh/graph/point-cloud extraction and residency sidecars.
- [ ] Add `integration;runtime` selection acceptance test using mocked pick results and runtime selection/refinement APIs.
- [ ] Add `integration;ui` or `contract;runtime` UI callback acceptance for core panels over the acceptance scene.
- [ ] Add opt-in `gpu;vulkan;integration` smoke for default-recipe visible rendering on Vulkan-capable hosts.
- [ ] Keep slow/GPU tests out of the default CPU gate with labels documented in `tests/README.md` and `tests/CMakeLists.txt`.

## Docs
- [ ] Update `README.md` or sandbox-specific docs with current run instructions and expected capabilities once acceptance passes.
- [ ] Update `tasks/backlog/README.md` Theme A status when this task retires.
- [ ] Update `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md` or add a newer review note marking the old visible-triangle gap analysis as superseded.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` for sandbox-visible mesh/graph/point-cloud rendering and interaction parity.

## Acceptance criteria
- [ ] `ExtrinsicSandbox` can render the acceptance mesh, graph, and point cloud through the default recipe on a Vulkan-capable host.
- [ ] Runtime camera controls work for the acceptance scene.
- [ ] Entity selection, primitive selection, and selection outline work for the supported geometry domains.
- [ ] Core UI panels are present and wired to runtime/editor command surfaces.
- [ ] CPU/null tests prove extraction/residency/selection/UI contracts without GPU.
- [ ] Opt-in GPU/Vulkan smoke proves the operational default-recipe path without reintroducing the retired bootstrap recipe scaffold.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Optional Vulkan acceptance on capable hosts:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan|integration' -LE 'slow|flaky-quarantine' --timeout 120
```

## Forbidden changes
- Reintroducing legacy render orchestration or graphics-owned ECS access to make the sandbox pass.
- Adding app-layer shortcuts that bypass runtime composition or renderer snapshots.
- Treating the retired bootstrap recipe scaffold as final acceptance for the working sandbox.
- Making GPU/Vulkan tests mandatory in the default CPU gate.
- Claiming broad performance, PBR, transparency, or serialization parity from this functional acceptance task.

## Maturity
- Target: `Operational` on Vulkan-capable hosts for the scoped acceptance scene; `CPUContracted` everywhere else.
- `GRAPHICS-081` has retired the visible-triangle bootstrap scaffold; this task treats the default recipe as the only rendering acceptance path.
