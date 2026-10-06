---
id: RUNTIME-322
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive integration of an existing CPU kernel; evidence is the diff, kernel/transform/config/agent/UI tests, a Vulkan acceptance smoke, review and CI.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration]
contract_review: The product case consumes entity orientations and writes entity TRS; it reads and writes no geometry properties or element domains, so geometry.element-domain-sources and geometry.property-coherence do not apply.
---
# RUNTIME-322 — Average instance orientations with RotationAveraging in Sandbox and agent

## Goal
- Bind the test-only `Geometry.RotationAveraging` (`ChordalMean`, `QuaternionMean`, `KarcherMean`,
  `GeodesicMedian`, `QuaternionMedian`) into the editor: average the world orientations of the
  selected entities and apply the result to an explicitly chosen target (or to all selected, "align
  to average"), as one undoable transform command.
- This is the bounded single-rotation-averaging product case. Multi-view alignment from pairwise
  registrations needs relative-rotation synchronization over a view graph and is owned by
  METHOD-068 (RobustPCA rotation synchronization), which builds on this task.
- Origin: REVIEW-007 E7 (2026-10-06), GE06. Draft: Codex, read-only on `e47a1484b`;
  paths re-checked on `098d47df9`.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Non-empty list of finite SO(3) orientations, optional non-negative weights of equal length. |
| Compatible entity sources | Any entity with a transform (mesh, graph, point cloud, light, empty); no geometry required. |
| RuntimeModule | New `Runtime.RotationAveragingOperations` on existing editor processing commands; existing transform-history owner (`Runtime.SceneEditingOperations.Actions.cpp` pattern), no second history. |
| Config/agent | `sandbox.rotation_averaging`: method, weights, iteration/tolerance/outlier options, target mode; agent `preview_rotation_averaging`/`run_rotation_averaging` sharing the editor operation. |
| UI | "Orientation average" block in the selection/transform panel with explicit target choice and readiness. |
| Publication | Target rotation(s) only, one undo transaction; position and scale preserved. Property output N/A: the result is entity TRS, not an element field. |
| End-to-end tests | Selection → config → panel/agent → numeric result → target transform → undo/redo and visible orientation change. |

## Acceptance criteria
- [ ] Averaging happens in world orientation and is mapped back to each target's local rotation;
      hierarchies that are not SO(3) (mirroring, shear under non-uniform parent scale) are rejected
      with a reason, never silently decomposed.
- [ ] All five methods are selectable; `EmptyInput`, `NonFiniteInput`, `InvalidOptions` and
      `NoConvergence` surface unchanged; result reports method, status, iterations and residual.
- [ ] Preview changes neither config nor scene; no apply on failure; entity count and iteration
      budget are bounded.
- [ ] Quaternion sign equivalence, near-180° inputs, weights, parent transforms and deleted/empty
      selection are tested; one undo entry, correct dirty state, save/load, stale-selection guard.
- [ ] `RuntimeSandboxAcceptanceGpuSmoke.RotationAverageChangesTargetOrientation` shows the change
      through the real command path (no GPU-algorithm claim).
- [ ] `docs/architecture/geometry.md`, `agent-control-lane.md`, runtime/Sandbox READMEs and the module
      inventory are updated; `geometry-pipeline-modularity.md`'s global-voting sketch stays future work.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(GeometryRotationAveraging|RotationAveragingOperations|RotationAveragingConfig|EditorCommandHistory|AgentOperations|SandboxConfigSections|SandboxEditorPresentation|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.RotationAverageChangesTargetOrientation$'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
```

## Context
- Kernel stays unchanged; REVIEW-007 GE24/GE25 cleanups (`Geometry.Rotation`, local `kPi`) are not
  mixed in. No ICP/CPD rework, pose graph or quaternion storage here.
- UI-078 (ImGuizmo) and RUNTIME-284 own adjacent gizmo/history work; neither blocks a normal
  undoable transform command.
