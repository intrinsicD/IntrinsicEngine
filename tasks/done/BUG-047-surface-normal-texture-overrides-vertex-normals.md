---
id: BUG-047
theme: G
depends_on: []
---
# BUG-047 — Surface normal texture overrides vertex-normal shading

## Goal
- Make promoted surface shading use the packed vertex-normal attribute as the shading normal instead of overriding it with generated normal-texture bakes.

## Non-goals
- No tangent, bitangent, or TBN generation.
- No authored tangent-space normal-map support.
- No removal of runtime normal-bake APIs, generated texture assets, or material binding DTOs.
- No broad PBR lighting rewrite.

## Context
- Owner: `src/graphics/renderer` and `assets/shaders`.
- The user reported incorrect shading after normal recomputation and asked to use the normal attribute in the shader instead of texture baking for now.
- Diagnosis found promoted forward/GBuffer surface fragment shaders sampled `MaterialParams::NormalID` as `texture * 2 - 1`. That is correct only for tangent-space normal maps, but the current generated normal bake encodes mesh `v:normal` data without a promoted tangent-space transform.
- Runtime may still create and bind generated normal texture assets as data-only state; this bug only changes current shader consumption so lighting uses packed vertex normals.

## Completion
- Completed: 2026-06-21. Commit/PR: `843e4fb3`.
- Audit: 2026-06-22 backlog audit confirmed this task was already fixed and
  superseded as an open backlog bug. Promoted forward and GBuffer shader
  contracts assert absence of `mat.NormalID` / `normalTex` sampling for current
  surface normals, and focused renderer lifecycle coverage still passes.

## Required changes
- [x] Remove normal-texture override logic from promoted forward and deferred surface shaders.
- [x] Remove the same override from the older surface shader pair so raw shader paths stay consistent.
- [x] Keep albedo texture sampling and material-slot binding behavior intact.
- [x] Add/update shader contract tests proving promoted surface shaders use vertex normals and no longer sample `NormalID`.

## Tests
- [x] Build focused graphics contract tests.
- [x] Run focused renderer shader-contract CTest.
- [x] Compile sandbox shader SPIR-V outputs.

## Docs
- [x] Update graphics docs to state that current surface shaders ignore `NormalID` and use packed vertex normals until tangent-space normal maps are promoted.

## Acceptance criteria
- [x] No surface fragment shader samples `mat.NormalID` for shading.
- [x] Promoted forward and GBuffer shader source contracts assert that normal-texture sampling is absent.
- [x] Material normal texture bindings may still resolve without affecting current shading normals.

## Verification
```bash
cmake --build --preset ci --target ExtrinsicSandbox_Shaders IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle.ForwardSurfacePipelineSurvivesOperationalRebuild' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Reinterpreting generated object/world normal textures as tangent-space normal maps.

## Maturity
- Target: `CPUContracted`.
- This bug closes at shader-source and SPIR-V compilation coverage. `Operational` visual proof can be handled by a future Vulkan smoke or tangent-space normal-map task; no `Operational` follow-up is owed for this temporary attribute-normal policy.
