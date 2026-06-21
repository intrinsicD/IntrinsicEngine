---
id: RUNTIME-123
theme: F
depends_on: [RUNTIME-120, RUNTIME-121]
maturity_target: CPUContracted
---
# RUNTIME-123 — Editor "bind any property as normals / colors"

## Goal
- Add a Sandbox EditorUI control that lets the user choose which geometry
  property feeds the normal and color vertex channels for the selected entity,
  built on the `VertexAttributeBinding` resolver.

## Non-goals
- No new geometry algorithms (owned by GEOM-026 and friends).
- No GPU/RHI calls from the UI command (keep the dirty-tag contract).
- No persistent material/asset authoring.

## Context
- Owning subsystem/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.*`.
- Today the normal source is fixed to `v:normal` and color has no vertex-stream
  source. RUNTIME-120 provides the resolver and RUNTIME-121 the color channel;
  this task exposes the *binding selection* so any feasible `vec3`/`vec4`
  property can be bound as normals/colors, publishing the binding into the
  packer path via an entity-side channel-binding descriptor and the existing
  `DirtyVertexAttributes` extraction trigger.

## Required changes
- [ ] Add an editor control listing eligible properties per channel (typed,
      count-matched) and a fail-closed preview of the resolver diagnostics.
- [ ] Persist the per-entity channel binding (ECS descriptor) consumed by the
      packer; stamp `DirtyVertexAttributes` on change.
- [ ] Surface resolver status/counters in the window without a graphics backend.

## Tests
- [ ] Contract test: selecting a non-`v:normal` vec3 property rebinds the normal
      channel and stamps the dirty tag; invalid choices report fail-closed.
- [ ] Contract test: binding a `vec4` property as color round-trips through the
      packer.

## Docs
- [ ] Update `src/runtime/README.md` editor workflow section.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if surfaces change.

## Acceptance criteria
- [ ] The user can bind any feasible property as normals or colors from the
      editor; unsupported choices are reported, not silently dropped.
- [ ] Default-gate contract tests and structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Calling renderer/RHI upload APIs directly from the UI command.

## Maturity
- Target: `CPUContracted`; CPU/null-safe editor command + binding contract.
- No `Operational` follow-up is owed; backend-specific visual proof, if ever
  wanted, would be a separate task.
