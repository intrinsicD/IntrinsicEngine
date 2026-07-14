---
id: RUNTIME-123
theme: B
depends_on: [RUNTIME-120, RUNTIME-122]
maturity_target: CPUContracted
---
# RUNTIME-123 — Editor "bind any property as normals / colors"

## Completion
- Retired on 2026-06-24 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Runtime now has a `VertexChannelBindingSet` ECS descriptor consumed
  by mesh, graph, and point-cloud packers. The Sandbox Editor property catalog
  exposes normal/color binding targets, validates candidate properties through
  the `VertexAttributeBinding` resolver, persists per-entity channel bindings,
  and stamps `DirtyVertexAttributes` without calling renderer/RHI upload APIs.
- Evidence: focused SandboxEditorUi, mesh/graph/point-cloud packer, and mesh
  extraction coverage passed; the full CPU-supported CTest gate, structural
  validators, and regenerated module inventory listed in Verification passed.

## Goal
- Add a Sandbox EditorUI control that lets the user choose which geometry
  property feeds the normal and color vertex channels for the selected mesh,
  graph, or point-cloud entity, built on the `VertexAttributeBinding` resolver
  and accepted whenever dimensions, element type, and element count match the
  target channel.

## Non-goals
- No new geometry algorithms (owned by GEOM-026 and friends).
- No GPU/RHI calls from the UI command (keep the dirty-tag contract).
- No persistent material/asset authoring.

## Context
- Owning subsystem/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.*`.
- Today the normal source is fixed to `v:normal`, and arbitrary color/normal
  channel binding is not exposed in the editor. `RUNTIME-120` provides the
  fail-closed resolver, and `RUNTIME-122` provides the shared mesh/graph/point
  cloud GPU channel storage and shader-fetch path needed for one editor binding
  model across all three geometry kinds.
- This task exposes the *binding selection* so any feasible source property can
  be bound as normals/colors for the selected entity when the source dimensions,
  value type, and element count match the target channel. It publishes the
  binding into the packer path via an entity-side channel-binding descriptor and
  the existing `DirtyVertexAttributes` extraction trigger.

## Required changes
- [x] Add an editor control listing eligible mesh/graph/point-cloud properties
      per channel (typed, dimension-matched, and count-matched) and a fail-closed
      preview of the resolver diagnostics.
- [x] Persist the per-entity channel binding (ECS descriptor) consumed by the
      packer; stamp `DirtyVertexAttributes` on change.
- [x] Surface resolver status/counters in the window without a graphics backend.

## Tests
- [x] Contract test: selecting a non-`v:normal` vec3 property for each supported
      geometry kind rebinds the normal channel and stamps the dirty tag; invalid
      choices report fail-closed.
- [x] Contract test: binding a `vec4` property as color round-trips through the
      mesh, graph, and point-cloud packer paths.

## Docs
- [x] Update `src/runtime/README.md` editor workflow section.
- [x] Regenerate `docs/api/generated/module_inventory.md` if surfaces change.

## Acceptance criteria
- [x] The user can bind any feasible property as normals or colors from the
      editor for mesh, graph, and point-cloud entities; unsupported choices are
      reported, not silently dropped.
- [x] Default-gate contract tests and structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|MeshGeometryPacker|GraphGeometryPacker|PointCloudGeometryPacker|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Calling renderer/RHI upload APIs directly from the UI command.

## Maturity
- Target: `CPUContracted`; CPU/null-safe editor command + binding contract.
- No `Operational` follow-up is owed; backend-specific visual proof, if ever
  wanted, would be a separate task.
