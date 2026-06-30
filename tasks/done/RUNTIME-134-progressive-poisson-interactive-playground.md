---
id: RUNTIME-134
theme: none
depends_on: [METHOD-012, CORE-003, RUNTIME-131]
maturity_target: CPUContracted
completed_on: 2026-06-30
---
# RUNTIME-134 — Interactive progressive-Poisson sampling playground in the Sandbox

## Goal
- Wire the progressive Poisson-disk sampler into the `ExtrinsicSandbox` app as an interactive playground: load a point cloud or mesh, expose every `SamplerConfig` knob as a live ImGui control, re-run the sampler on edit, and visualize the result as points colored by level or phase with a prefix-count slider — giving full controllability and immediate visual iteration on real point clouds and meshes.

## Non-goals
- No new rendering primitives (point rendering and colormaps already exist).
- No new sampler algorithm work (METHOD-012/013 own that); this task only drives and visualizes them.
- No figure/file export here (that is RUNTIME-133 / GRAPHICS-109), though the playground may invoke those seams once available.

## Context
- Status: done at `CPUContracted`. Depends on METHOD-012 (the sampler to drive), CORE-003 (engine-config file/preview lane) and RUNTIME-131 (agent/CLI config-control facade) so knob state is driven through the same validated config path the UI uses.
- Commit references: `a26d27a6`, `17323865`, `847a2790`, `77960cd4`, and this retirement commit.
- Owning subsystem/layer: `runtime`/`ui` composition — imports runtime only (app → runtime). The existing pieces were point rendering with per-point colormap (`Pass.Forward.Point`, `Graphics.ColormapSystem`), `Graphics::Components::VisualizationConfig` (ScalarField + colormap sliders), the ImGui editor shell (`Runtime.SandboxEditorUi` with PointCloud/Mesh windows), camera controllers, and mesh/point packers. This task added the panel that binds sampler knobs, re-runs the method, and maps the result's `level`/`phase`/`splat_radius` onto per-point scalar properties for coloring.
- For meshes, use GEOM-035 to sample a surface cloud first, then feed the sampler.

## Slice plan
- **Slice A.** Add the CPU reference playground command and PointCloud processing UI. The runtime command validates selected point-cloud `GeometrySources`, runs METHOD-012 through a typed config DTO, publishes deterministic point properties (`p:poisson_level`, `p:poisson_phase`, `p:poisson_splat_radius`, `p:poisson_prefix_visible`), enables point rendering, and routes visualization to the selected scalar channel. Headless tests prove deterministic property population and command/direct-method equivalence.
- **Slice B.** Add mesh-selection preprocessing via GEOM-035, including mesh-specific UI affordances and tests. Mesh selections are sampled to a point cloud, run through the same METHOD-012 path, and published back as point-cloud `GeometrySources` for visualization.
- **Slice C.** Route playground knob state through CORE-003/RUNTIME-131 engine-config control, add hover tooltips, and schedule debounced METHOD-012 reruns from the Sandbox UI.
- **Slice D.1 (this slice).** Add CPU reference backend identity and per-level-count readout to the runtime command result and Sandbox UI.
- **Deferred follow-up.** Add a backend toggle once METHOD-013 is unblocked and available; this is split to RUNTIME-136.

## Slice A status (2026-06-30)
- Landed: CPU reference point-cloud command, explicit Processing-window run button, typed runtime command DTO validation, deterministic per-point publication for `p:poisson_level`, `p:poisson_phase`, `p:poisson_splat_radius`, and `p:poisson_prefix_visible`, visualization routing to the selected scalar channel, and headless command/direct-method equivalence coverage.
- Follow-up slices completed knob persistence/preview through the CORE-003/RUNTIME-131 config-control facade, debounced rerun-on-edit behavior, and the broader default gate for this milestone.

## Slice B status (2026-06-30)
- Landed: `Mesh > Processing > Progressive Poisson Sampling` availability, GEOM-035 mesh-surface sampling controls (`sample_count`, `seed`, `min_triangle_area`, normal interpolation), deterministic mesh-to-cloud command path, surface-sampling diagnostics in `SandboxEditorProgressivePoissonResult`, point-cloud publication via `GeometrySources::PopulateFromCloud`, stale surface render-hint removal, and headless coverage proving the published sampled cloud matches a direct METHOD-012 run.
- Follow-up slices completed config-control facade routing, debounced rerun-on-edit behavior, and CPU backend identity readout; the future backend toggle is split to RUNTIME-136 once METHOD-013 exists.

## Slice C status (2026-06-30)
- Landed: `EngineConfig.sandbox.progressive_poisson` value-type config with parser/serializer coverage, hot-apply support in `Engine::ApplyEngineConfigHotSubset`, Sandbox editor config command routing through `PreviewEngineConfigControlDocument`/`ApplyEngineConfigHotSubset`, UI hover tooltips, and debounced auto-rerun after knob edits for the METHOD-012 CPU reference path.
- Slice D.1 completed backend identity/per-level-count readout; the backend toggle is split to RUNTIME-136 once METHOD-013 exists. The Slice C default CPU/headless gate was green.

## Slice D.1 status (2026-06-30)
- Landed: METHOD-012 `cpu_reference` backend id and accepted-point counts per progressive level are exposed through `SandboxEditorProgressivePoissonResult`, rendered in the Sandbox UI readout, and pinned in headless command tests for point-cloud and mesh inputs.
- Split follow-up: backend toggle and CPU/GPU parity readout are blocked until METHOD-013 exists and are tracked by RUNTIME-136.

## Required changes
- [x] Add a Sandbox editor panel exposing all `SamplerConfig` knobs (`dimension`, `grid_width`, `max_levels`, `hash_load_factor`, `radius_alpha`, `randomize_grid_origin`, `grid_origin_seed`, `shuffle_within_levels`, `shuffle_seed`) with sensible ranges and tooltips, routed through the CORE-003/RUNTIME-131 config path.
- [x] On knob edit (debounced), re-run METHOD-012 (or METHOD-013 if available, with a backend toggle) on the loaded cloud and update residency.
- [x] Map the result onto per-point scalar properties (`level`, `phase`, `splat_radius`) and drive `VisualizationConfig::ScalarField` + colormap so points color by the selected channel; add a prefix-count slider (`k`) that shows only `order[0..k)`.
- [x] Support loading a point cloud (PLY/XYZ/PCD) or a mesh (sampled to a cloud via GEOM-035) as the playground input, with a backend-identity / per-level-count readout.

## Tests
- [x] Add a headless runtime contract test that builds the playground state from a fixture cloud, applies a knob set through the config path, runs the sampler, and asserts the per-point `level`/`phase` scalar properties and prefix selection are populated deterministically.
- [x] Add a config-path test asserting knob edits route through the validated CORE-003/RUNTIME-131 lane (no ad-hoc mutation) and produce the same result as a direct method call.
- [x] Assert the runtime command result reports the active CPU backend id and per-level accepted counts for point-cloud and mesh inputs.
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

Completed verification:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.*ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding sampler algorithm logic in the UI layer (drive METHOD-012/013 only).
- Bypassing the config facade with ad-hoc parameter mutation.
- Introducing layering violations (app imports runtime only; graphics stays ECS-free).

## Maturity
- Retired at `CPUContracted`: the Sandbox command/UI, config-control routing, point-cloud and mesh input paths, deterministic property publication, backend identity, and per-level-count readouts are covered by the default CPU/headless gate.
- `Operational` owned by `METHOD-013` for the Vulkan-compute sampler backend and parity proof; RUNTIME-136 owns the future Sandbox backend-toggle UI once METHOD-013 lands.
