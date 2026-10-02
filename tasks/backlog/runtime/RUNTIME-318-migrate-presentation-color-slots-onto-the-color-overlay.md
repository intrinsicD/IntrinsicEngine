---
id: RUNTIME-318
theme: J
depends_on: [RUNTIME-315]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
maturity_target: Operational
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, runtime.editor-prepared-frame-locality]
---
# RUNTIME-318 — Migrate the presentation color slots onto the Color overlay

## Goal
- One Color mechanism (operator decision in [RUNTIME-315](../../done/RUNTIME-315-per-domain-render-attribute-source-binding.md#operator-decisions-2026-10-02)):
  the `PointColor`, `LineColor`, `PointScalarField` and `LineScalarField`
  `GeometryPresentationSlotRecipe` PropertyBuffer slots stop being a second,
  live way to color point and line lanes; their effect is expressed by the
  visualization overlay that `ApplyEditorAttributeBindingCommand` (Color) and
  `show_property` write.

## Non-goals
- No change to the overlay itself, to surface material slots (Albedo, Normal,
  Roughness, Metallic, ScalarField, Displacement) or to texture bakes.
- No new color path; no change to canonical `v:color` vertex colors.

## Context
- Found in RUNTIME-315 slice 3 review (2026-10-02). `BuildPresentationVisualizationRecipe`
  (`src/runtime/Rendering/Runtime.RenderExtraction.cpp`, the
  `PointColor`/`LineColor`/`*ScalarField` cases) turns these slots into
  Color/Scalar visualization recipes and packets. They are covered by
  `RuntimeRenderExtraction` (edge_color/node_heat packet assertions) and the
  `gpu;vulkan` smoke `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`
  (LineColor slot).
- Until this task lands, RUNTIME-315's attribute model reports such a slot as
  the lane's Color source so the precedence is visible.
- Touch points: render extraction (presentation recipe lowering), the
  presentation snapshot and editor models (`Runtime.EditorWorkspaceSnapshots.Models.cpp`),
  scene serialization of the slot semantics, the Sandbox shell
  (`Sandbox.EditorShell.cpp` PointColor/LineColor handling), asset-workflow
  material reconciliation switches, and the tests above.

## Control surfaces
- UI/agent: none new; the Color attribute row and `show_property` remain the
  only color controls.
- Config/scene: the slot semantics disappear from the scene format. AGENTS.md
  §5 allows no compatibility reader unless the operator approves one; record
  that decision here before implementing (scenes that carry the slots either
  load with the slot converted to the equivalent overlay or fail with a typed
  diagnostic).
- Format decision (2026-10-02, AGENTS.md §5 default, no compatibility reader):
  scenes that carry a retired slot semantic fail to load with
  `Core::ErrorCode::InvalidFormat`; `kSceneDocumentVersion` advances to 3, so
  every version 2 document is rejected the same way. No converter.

## Maturity
- `Operational`: CPU contract tests plus the edited `gpu;vulkan` smoke actually
  run under the Xephyr nested X server (pending GPU until then).

## Slice plan
1. **Lowering (~300 lines).** Convert an authored color/scalar point or line
  slot to the equivalent lane overlay at load/authoring; extraction no longer
  lowers these semantics; model shows one source.
2. **Retire semantics (~250 lines).** Remove the four semantics from the enum,
  parsers, switches, presentation commands and Sandbox shell; update tests and
  the gpu smoke (run pending GPU).

## Acceptance criteria
- [ ] No code path colors a point or line lane from a presentation slot.
- [ ] Every previously slot-colored lane renders the same colors through the overlay (CPU extraction assertion and Vulkan readback).
- [ ] Scene save/load round trip covers the converted lanes; the format decision is recorded.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'RuntimeRenderExtraction|GeometryPresentation|RuntimeSceneSerialization|SandboxEditorModels|VertexChannelBindings' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-02: Slice 1 (identity evidence). `RuntimeRenderExtraction.PresentationColorSlotLanesDrawIdenticallyThroughTheOverlay`
  authors each former slot (PointColor, PointScalarField, LineColor,
  LineScalarField; point cloud and graph edges) and its overlay equivalent.
  Both encode the same packet (name, domain, count, colormap, range), but only
  the overlay reaches the lane's GPU entity config: the slot packet's
  `presentation.<Lane>` key is not on the lane's sync record, so a color slot
  left the lane on its material color and a scalar slot suppressed the
  overlay's own packet (scalar mode with no buffer). The overlay therefore
  draws what the slot meant to draw; nothing that rendered correctly before
  changes.
