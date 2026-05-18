# GRAPHICS-033C — Vulkan command recording for the minimal-debug-surface recipe

## Goal
Land the third GRAPHICS-033 implementation child: implement the Vulkan
command-recording bodies for the GRAPHICS-032 `FrameRecipe::MinimalDebugSurface`
recipe (passes `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug`)
on top of the existing `IRenderer::RebuildOperationalResources()` seam
from GRAPHICS-018R. After this slice the Vulkan backend can record and
submit one minimal visible-triangle frame; CPU/null command-sequence
parity tests assert recording produces the same pass/resource/command
order as the CPU mock. Real-device submission stays opt-in (covered by
GRAPHICS-033D). The operational gate may legitimately flip
`MinimalRecipeRecordingMissing` → satisfied as a consequence of this
work, but flipping to fully `Operational` still requires the remaining
gate items (barrier validation + public service reconciliation) — those
stay outside this slice.

## Non-goals
- No new operational-gate code (GRAPHICS-033A) and no new diagnostics
  counters (GRAPHICS-033B).
- No real-device smoke test as part of the default CPU gate
  (GRAPHICS-033D owns the opt-in `gpu;vulkan` smoke).
- No new recipe authoring; consume the GRAPHICS-032-locked recipe
  exactly. No new framegraph pass kinds.
- No barrier-validation or public-service-reconciliation work beyond
  the minimum required to record the two passes against the real
  command context. Remaining gate items (`BarrierValidationFailed`,
  `PublicServiceReconciliationFailed`) intentionally stay reasons that
  this slice does not clear on its own.
- No new shader stages, materials, or framegraph diagnostics features
  beyond what GRAPHICS-031 (default debug surface material), GRAPHICS-032
  (recipe), and GRAPHICS-022 (rendergraph validation) already provide.
- No live ECS access from `src/graphics/vulkan/*`.
- No present-mode policy growth beyond GRAPHICS-013CQ.

## Context
- Owning subsystem/layer: `src/graphics/vulkan` (recording bodies);
  reads contracts from `src/graphics/renderer/`, `src/graphics/framegraph/`,
  and `src/graphics/rhi/`.
- Depends on:
  - [`GRAPHICS-018R` (done)](../../done/GRAPHICS-018R-operational-transition.md)
    operational-transition reset seam.
  - [`GRAPHICS-032` (done)](../../done/GRAPHICS-032-minimal-surface-present-command-path.md)
    minimal recipe contract and CPU-mock parity assertions.
  - [`GRAPHICS-031` (done)](../../done/GRAPHICS-031-default-debug-surface-material.md)
    default debug surface material (slot 0).
  - [`GRAPHICS-033A` (done)](../../done/GRAPHICS-033A-vulkan-operational-status-evaluator.md)
    seam, because the recording prerequisite flag feeds the gate.
- Planning lock: [`GRAPHICS-033`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md)
  Decisions 1 (gate item 6), 7 (required vs optional capabilities),
  8 (queue-family ownership), 11 (test split). Recording must use the
  graphics queue only for the minimal recipe; transfer-queue uploads
  must complete before the surface pass consumes uploaded resources.
- The Vulkan backend currently records nothing for these passes; the
  CPU/null path is the only place the GRAPHICS-032 recipe executes.

## Required changes
- [ ] Implement `RecordPass.Surface.MinimalDebug` and
  `RecordPass.Present.MinimalDebug` command-recording bodies inside
  `src/graphics/vulkan/` against the existing command context. Bodies
  consume the GRAPHICS-032 framegraph and the GRAPHICS-031 default
  debug surface material slot.
- [ ] Ensure recording bodies do not allocate per frame and do not access
  any non-public Vulkan symbol from outside the backend.
- [ ] Set the `MinimalRecipeRecordingMissing` gate input to `false` only
  when both recording bodies are present and the recipe's
  prerequisites (default debug material slot populated, framegraph
  validation result clean) are satisfied. The other unsatisfied gate
  items remain unchanged.
- [ ] Wire recording into `RebuildOperationalResources()` so that the
  GRAPHICS-018R transition seam is the single re-entry point on
  swapchain/device recreate. No recording occurs outside of an
  operational gate-eligible window.
- [ ] Backbuffer-import declaration, fullscreen-triangle present body, and
  framegraph barrier inference all stay graphics-owned; renderer/runtime
  do not learn about Vulkan-native barriers.

## Tests
- [ ] New `tests/contract/graphics/Test.VulkanMinimalRecipeRecording.cpp`
  (labels `contract;graphics`): drives the recording bodies against a
  null/mock command context that captures the recorded command stream
  and asserts the pass/resource/command sequence matches the
  GRAPHICS-032 CPU-mock parity contract exactly. No real Vulkan device.
- [ ] New `tests/contract/graphics/Test.VulkanMinimalRecipeGateFlip.cpp`
  (labels `contract;graphics`): asserts that with `Pass.Surface.MinimalDebug`
  and `Pass.Present.MinimalDebug` bodies in place, the
  `MinimalRecipeRecordingMissing` input goes `true → false`, but with
  any other gate item unsatisfied the overall status remains
  `RequestedButIncompleteGate` (not `Operational`).
- [ ] Update the existing GRAPHICS-022 / GRAPHICS-032 CPU contract tests
  to consume the new recording bodies through the null command-context
  shim; no behavior change in those existing assertions.
- [ ] Real-device smoke is intentionally deferred to GRAPHICS-033D.

## Docs
- [ ] Update `src/graphics/vulkan/README.md` minimal-recipe section to
  reflect that recording bodies are now implemented and the gate item
  6 is consequently satisfiable.
- [ ] Update `docs/architecture/rendering-three-pass.md` to mark
  `Pass.Surface.MinimalDebug` / `Pass.Present.MinimalDebug` recording
  as Vulkan-backed and to record the still-outstanding gate items
  (barrier validation, public service reconciliation).
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` Vulkan operational
  rows accordingly.
- [ ] Refresh `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug` recording
  bodies exist in `src/graphics/vulkan/` and are reachable through
  `RebuildOperationalResources()` only.
- [ ] CPU-mock parity tests show identical pass/resource/command sequences
  to the GRAPHICS-032 contract.
- [ ] The `MinimalRecipeRecordingMissing` gate input becomes flippable;
  full `Operational` status still requires the remaining gate items.
- [ ] Default CPU gate passes; no `gpu|vulkan|slow|flaky-quarantine` label
  is added to the new tests.
- [ ] Layering remains `graphics/vulkan -> core, graphics/rhi, backend-local
  Vulkan deps`; no new edges.
- [ ] No per-frame allocations or string formatting on the recording path.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check

cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'contract' --timeout 60
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Flipping the operational gate to `Operational` from this slice alone.
  Remaining unsatisfied gate items must still report their first
  failing reason.
- Adding shader variants, materials, or recipe variants beyond what
  GRAPHICS-031 / GRAPHICS-032 lock.
- Introducing a real-device CTest under the default CPU gate; smoke
  coverage stays opt-in under GRAPHICS-033D.
- Calling `vk*` symbols from `src/runtime/`, `src/app/`, or any
  graphics layer other than `src/graphics/vulkan/`.
- Per-frame heap allocations or string formatting on the recording
  path.
- Live ECS access from `src/graphics/vulkan/*`.
- Adding new CMake options or changing
  `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN` semantics.
