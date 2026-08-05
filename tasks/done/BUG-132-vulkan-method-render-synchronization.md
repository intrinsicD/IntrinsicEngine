---
id: BUG-132
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-05T11:34:58Z"
contract_schema: 1
contracts: []
contract_review: The catalog was reviewed; this task repairs internal frame-graph attachment synchronization and the existing graphics transfer lifetime without changing geometry source, method integration, support-radius, or task-authoring contracts.
---
# BUG-132 — Vulkan method/render synchronization and resource lifetime

## Status

- Completed on 2026-08-05 at `Operational` on the exercised Vulkan host and
  `CPUContracted` for the backend-neutral compiler/transfer behavior.
- Implementation commit: pending retirement commit.

## Goal

- Ensure the promoted Vulkan LOP compute-to-publication-to-render path completes without synchronization-validation errors or destruction of in-flight buffers.

## Non-goals

- No general CPU/GPU geometry-property residency contract; `RUNTIME-214` owns that follow-up.
- No dedicated-transfer-queue optimization, renderer recipe redesign, or method-specific readback service.
- No changes to LOP mathematics, support-radius policy, or property-domain eligibility.

## Context

- Symptom: the validation-enabled imported-child-mesh LOP parity workflow emits color-attachment synchronization hazards and destroys buffers still referenced by submitted work; the ordinary test hides the defects by disabling validation.
- Expected behavior: frame-graph attachment access and final method-result transfer use explicit ordering and resource retirement, and a validation-enabled regression fails on any new validation error.
- Impact: method output cannot be trusted as visibly published while the Vulkan execution path is invalid.

## Required changes

- [x] Reproduce and pin the color-attachment read/write hazard in the frame-graph compiler/executor contract.
- [x] Repair attachment access coalescing and Vulkan access/layout mapping for passes that load and then write a color attachment.
- [x] Repair `Graphics.GpuTransfer` readback submission ordering and resource retirement so producer buffers remain live until the consuming transfer completes.
- [x] Keep the repair backend-neutral above `graphics/vulkan` and avoid device-wide idle waits.
- [x] Enable validation for the representative LOP Vulkan workflow and make validation messages test-fatal.

## Tests

- [x] Add CPU contract coverage for combined color-attachment read/write access and transfer lifetime/order semantics.
- [x] Run the validation-enabled imported-child-mesh LOP Vulkan regression.
- [x] Run focused frame-graph, transfer, and Vulkan GPU test cohorts.

## Docs

- [x] Update frame-graph and RHI transfer documentation with the repaired access and lifetime contract.
- [x] Refresh generated module/test inventories only if their source surfaces change.

## Acceptance criteria

- [x] The original LOP reproduction completes with zero Vulkan validation errors.
- [x] A pass with `LoadOp::Load` and color output declares/executes a read-write attachment dependency.
- [x] No buffer is destroyed while referenced by a submitted transfer or compute command.
- [x] Null/headless behavior and existing CPU transfer contracts remain green.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'FrameGraph|GpuTransfer|PointCloudConsolidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointCloudConsolidationGpuParity\\.VulkanAutoProcessesChildMeshPositionsAndPublishesDisplacement$' --timeout 180
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'FrameGraph|GpuTransfer|PointCloudConsolidation' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Executed on 2026-08-05:

- The focused frame-recipe/render-graph selector passed 77/77 cases and the
  `GpuTransferFacade` selector passed 8/8 cases under the ordinary CPU build.
- The exact sanitizer-enabled child.obj Vulkan Auto LOP regression passed with
  synchronization validation forced on. Its validation counter stayed at zero,
  the captured log contained no `SYNC-HAZARD` or `VUID`, the GPU method
  published non-zero displacement, and render residency changed.
- The repair removed the original attachment hazards and in-flight buffer
  destruction, then closed the independently exposed projected-grid
  transfer-write reuse hazard. Hash-bound command receipts live under
  `tasks/evidence/BUG-132/commands/`.
- Strict layering and task-policy checks passed. The exported module inventory
  was regenerated after the barrier-state surface changed.

## Forbidden changes

- Silencing, filtering, or disabling Vulkan validation messages.
- Fixing lifetime by leaking resources or waiting the entire device idle in the frame path.
- Adding a feature-named transfer queue, Vulkan type to a public RHI API, or a second GPU allocator.

## Maturity

- Target: `Operational` on Vulkan-capable hosts and `CPUContracted` under the Null backend.
