---
id: RUNTIME-218
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Scene lighting composition and ECS light authoring. No geometry
  element-domain source, geometry property, support-radius, parameterization,
  or method-integration surface is consumed or published.
---
# RUNTIME-218 — Nothing in the scene is ever lit: add default lighting and light authoring

## Goal
- Ensure a Sandbox scene is lit by default, so imported geometry renders with
  readable shape, and give the editor a way to author and adjust lights.

## Non-goals
- No PBR/IBL/shadow-quality work; this is about having *any* usable light.
- No new lighting model or shader authoring system.
- No environment/skybox capture pipeline.

## Context
- Symptom: `sculpt.obj` imported into `ExtrinsicSandbox` renders as a
  near-uniform white silhouette with no depth or form. Orbiting the camera does
  not change the shading.
- Cause: **no light is authored anywhere.**
  `Runtime.ReferenceScene.cpp` creates no light; the import path creates none;
  and there is no UI to create one (`grep -rn 'Light' src/app/Sandbox/` is
  empty, and there is no add-entity command in the editor at all). Lights exist
  only if a loaded scene document happens to contain them.
- With zero lights every shading path collapses to its ambient term —
  `deferred_lighting.frag:92` (`result = (ambient + diffuse*shadow) * albedo`),
  `debug_surface.frag:26`, `point.frag:55` — which yields flat unshaded fill.
- The consuming machinery already exists and is unused: ECS
  `Lights::DirectionalLight` / `Lights::PointLight` components,
  `Runtime.RenderExtraction.cpp:1279-1285` extraction, `LightSystem`,
  and clustered-light resources
  (`Graphics.Renderer.cpp:EnsureClusterLightResources`). Only authoring is
  missing.
- Impact: high for the engine's stated purpose. For a geometry-processing tool
  the viewport is the primary instrument, and denoise, curvature, remesh and
  parameterization all change *shading*, not silhouette — so their results are
  currently invisible in the 3D view.
- Owner: `runtime` scene composition (default light) plus the editor scene
  command surface (authoring). `graphics` already consumes lights.
- Design axis to resolve: whether the default light is part of
  `BootstrapReferenceScene`, a separate default-policy step in
  `SandboxSession`, or a camera-attached headlight. A camera-relative headlight
  gives usable shading from any view without scene authoring; a scene
  directional light is more honest as scene content. Record the choice in
  `Context` before implementing.

## Control surfaces
- Config: default-light presence/intensity should be reachable through the
  existing validated engine-config lane, not a panel-private switch.
- UI: an editor affordance to add/select/edit a light on the active world.
- Agent/CLI: no new surface required beyond the config section.

## Required changes
- [ ] Author a default light for the Sandbox reference scene (or a
      camera-relative headlight), with the choice recorded in `Context`.
- [ ] Add an editor command to create a light entity on the active world, routed
      through the existing runtime command/history seam.
- [ ] Expose direction/color/intensity editing for a selected light through the
      existing Inspector/property widget path.
- [ ] Ensure light authoring participates in undo/redo and scene serialization.

## Tests
- [ ] Add a runtime contract test asserting the default Sandbox scene extracts
      at least one `LightSnapshot`.
- [ ] Add a contract test asserting the create-light command adds an entity that
      round-trips through scene save/load.
- [ ] Add an opt-in `gpu;vulkan` readback smoke asserting an imported mesh
      renders with non-uniform luminance (shading present) rather than flat
      fill.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `src/app/Sandbox/README.md` reference-scene prose to describe the
      default lighting.
- [ ] Document the light config section if one is added.

## Acceptance criteria
- [ ] A freshly launched Sandbox with an imported mesh shows shaded geometry,
      not flat fill.
- [ ] A light can be created and edited from the editor.
- [ ] Light state survives scene save/load and undo/redo.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'ReferenceScene|RenderExtraction|SceneSerialization|LightSystem' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Raising the ambient term to fake shading instead of adding a light.
- Authoring the default light from `app` in a way that bypasses the runtime
  scene/command seam.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere
  else. The readback smoke that proves non-uniform luminance is what closes
  `Operational`.
