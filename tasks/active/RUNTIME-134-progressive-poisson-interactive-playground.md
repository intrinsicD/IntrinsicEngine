---
id: RUNTIME-134
theme: none
depends_on: [METHOD-012, CORE-003, RUNTIME-131]
maturity_target: CPUContracted
---
# RUNTIME-134 — Interactive progressive-Poisson sampling playground in the Sandbox

## Goal
- Wire the progressive Poisson-disk sampler into the `ExtrinsicSandbox` app as an interactive playground: load a point cloud or mesh, expose every `SamplerConfig` knob as a live ImGui control, re-run the sampler on edit, and visualize the result as points colored by level or phase with a prefix-count slider — giving full controllability and immediate visual iteration on real point clouds and meshes.

## Non-goals
- No new rendering primitives (point rendering and colormaps already exist).
- No new sampler algorithm work (METHOD-012/013 own that); this task only drives and visualizes them.
- No figure/file export here (that is RUNTIME-133 / GRAPHICS-109), though the playground may invoke those seams once available.

## Context
- Status: active. Depends on METHOD-012 (the sampler to drive), CORE-003 (engine-config file/preview lane) and RUNTIME-131 (agent/CLI config-control facade) so knob state is driven through the same validated config path the UI uses.
- Owning subsystem/layer: `runtime`/`ui` composition — imports runtime only (app → runtime). The pieces exist: point rendering with per-point colormap (`Pass.Forward.Point`, `Graphics.ColormapSystem`), `Graphics::Components::VisualizationConfig` (ScalarField + colormap sliders), the ImGui editor shell (`Runtime.SandboxEditorUi` with PointCloud/Mesh windows), camera controllers, and mesh/point packers. What is **missing** is a panel that binds the sampler's knobs and re-runs it, plus mapping the result's `level`/`phase`/`splat_radius` onto a per-point scalar property for coloring. No method is currently wired into the interactive app, so this is the first method-in-sandbox integration.
- For meshes, use GEOM-035 to sample a surface cloud first, then feed the sampler.

## Slice plan
- **Slice A.** Add the CPU reference playground command and PointCloud processing UI. The runtime command validates selected point-cloud `GeometrySources`, runs METHOD-012 through a typed config DTO, publishes deterministic point properties (`p:poisson_level`, `p:poisson_phase`, `p:poisson_splat_radius`, `p:poisson_prefix_visible`), enables point rendering, and routes visualization to the selected scalar channel. Headless tests prove deterministic property population and command/direct-method equivalence.
- **Slice B.** Add mesh-selection preprocessing via GEOM-035, including mesh-specific UI affordances and tests. Mesh selections are sampled to a point cloud, run through the same METHOD-012 path, and published back as point-cloud `GeometrySources` for visualization.
- **Slice C (this slice).** Route playground knob state through CORE-003/RUNTIME-131 engine-config control, add hover tooltips, and schedule debounced METHOD-012 reruns from the Sandbox UI.
- **Slice D.** Add backend identity/per-level-count readout and a backend toggle once METHOD-013 is unblocked and available.

## Slice A status (2026-06-30)
- Landed: CPU reference point-cloud command, explicit Processing-window run button, typed runtime command DTO validation, deterministic per-point publication for `p:poisson_level`, `p:poisson_phase`, `p:poisson_splat_radius`, and `p:poisson_prefix_visible`, visualization routing to the selected scalar channel, and headless command/direct-method equivalence coverage.
- Still required before retirement: route knob persistence/preview through the CORE-003/RUNTIME-131 config-control facade, add debounced rerun-on-edit behavior, and perform the broader default gate for the final interactive milestone.

## Slice B status (2026-06-30)
- Landed: `Mesh > Processing > Progressive Poisson Sampling` availability, GEOM-035 mesh-surface sampling controls (`sample_count`, `seed`, `min_triangle_area`, normal interpolation), deterministic mesh-to-cloud command path, surface-sampling diagnostics in `SandboxEditorProgressivePoissonResult`, point-cloud publication via `GeometrySources::PopulateFromCloud`, stale surface render-hint removal, and headless coverage proving the published sampled cloud matches a direct METHOD-012 run.
- Still required before retirement: config-control facade routing, debounced rerun-on-edit behavior, and future backend identity/toggle once METHOD-013 exists.

## Slice C status (2026-06-30)
- Landed: `EngineConfig.sandbox.progressive_poisson` value-type config with parser/serializer coverage, hot-apply support in `Engine::ApplyEngineConfigHotSubset`, Sandbox editor config command routing through `PreviewEngineConfigControlDocument`/`ApplyEngineConfigHotSubset`, UI hover tooltips, and debounced auto-rerun after knob edits for the METHOD-012 CPU reference path.
- Remaining before retirement: backend identity/per-level-count readout and backend toggle once METHOD-013 exists. The Slice C default CPU/headless gate is green.

## Required changes
- [x] Add a Sandbox editor panel exposing all `SamplerConfig` knobs (`dimension`, `grid_width`, `max_levels`, `hash_load_factor`, `radius_alpha`, `randomize_grid_origin`, `grid_origin_seed`, `shuffle_within_levels`, `shuffle_seed`) with sensible ranges and tooltips, routed through the CORE-003/RUNTIME-131 config path.
- [x] On knob edit (debounced), re-run METHOD-012 (or METHOD-013 if available, with a backend toggle) on the loaded cloud and update residency.
- [x] Map the result onto per-point scalar properties (`level`, `phase`, `splat_radius`) and drive `VisualizationConfig::ScalarField` + colormap so points color by the selected channel; add a prefix-count slider (`k`) that shows only `order[0..k)`.
- [ ] Support loading a point cloud (PLY/XYZ/PCD) or a mesh (sampled to a cloud via GEOM-035) as the playground input, with a backend-identity / per-level-count readout.

## Tests
- [x] Add a headless runtime contract test that builds the playground state from a fixture cloud, applies a knob set through the config path, runs the sampler, and asserts the per-point `level`/`phase` scalar properties and prefix selection are populated deterministically.
- [x] Add a config-path test asserting knob edits route through the validated CORE-003/RUNTIME-131 lane (no ad-hoc mutation) and produce the same result as a direct method call.
- [x] Confirm the default CPU/headless gate stays green; any interactive Vulkan coverage is `gpu;vulkan` label-gated.

## Docs
- [x] Document the playground panel, knob ranges, and color channels in the Sandbox/UI docs and the method `README.md` "interactive usage" note.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] A user can load a point cloud or mesh, adjust every sampler knob live, and immediately see the result colored by level/phase with a working prefix slider.
- [x] Knob state flows through the validated config facade (CORE-003/RUNTIME-131), not ad-hoc UI mutation.
- [x] Headless tests cover deterministic property population and the config-path equivalence; default gate green.
- [x] App imports runtime only; no layering violations introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PoissonPlayground|Sandbox' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding sampler algorithm logic in the UI layer (drive METHOD-012/013 only).
- Bypassing the config facade with ad-hoc parameter mutation.
- Introducing layering violations (app imports runtime only; graphics stays ECS-free).

## Maturity
- Target: `Operational` interactive playground on Vulkan-capable hosts; `CPUContracted` (headless deterministic state + property population) everywhere else. This task owns the interactive `Operational` milestone (`Operational` owned by RUNTIME-134).
