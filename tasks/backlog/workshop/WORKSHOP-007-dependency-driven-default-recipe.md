# WORKSHOP-007 — Make default frame recipe dependency-driven

## Goal
- Move the default frame recipe away from forced linear pass chaining and toward true render-graph semantics where ordering follows resource dependencies, with explicit dependencies only for side effects or real ordering constraints.

## Non-goals
- Do not remove the ability to force order where required.
- Do not change visual output or resource set intentionally.
- Do not implement missing command bodies.
- Do not reintroduce the retired bootstrap recipe scaffold while changing default-recipe dependencies.

## Context
- The current default recipe uses an `addOrderedPass` helper that chains each pass to the previous pass.
- That is acceptable as a walking skeleton, but it can accidentally turn the render graph into a linear script with resource annotations.
- The foundation should let the graph compiler derive order from declared reads/writes wherever safe.

## Required changes
- [ ] Audit every explicit dependency currently added by default recipe construction.
- [ ] Classify each ordering edge as either:
  - required by real side effect/order semantics, or
  - derivable from resource reads/writes.
- [ ] Replace forced sequential dependencies with resource-driven dependencies wherever tests prove equivalent compile order/barrier correctness.
- [ ] Keep explicit dependencies for side-effect passes such as presentation, ImGui, readback, or other passes whose ordering is not fully represented by resources.
- [ ] Add recipe helper APIs that make explicit dependencies rare and intentional.
- [ ] Ensure render graph validation catches missing producer/consumer or side-effect-order mistakes.
- [ ] Preserve debug dump readability so graph order remains inspectable.

## Tests
- [ ] Add a frame recipe contract test proving default recipe compiles without unnecessary linear dependencies.
- [ ] Add tests proving required side-effect order is still preserved.
- [ ] Add tests proving barrier packet ordering remains valid after removing redundant explicit dependencies.
- [ ] Add regression tests for picking/selection/debug/postprocess feature combinations.
- [ ] Keep existing `Test.FrameRecipeContract`, `Test.RenderGraphValidation`, and renderer lifecycle tests passing.

## Docs
- [ ] Update render graph architecture docs to state when explicit dependencies are allowed.
- [ ] Update frame recipe docs/comments to distinguish resource dependency from side-effect dependency.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` graphics/framegraph row if needed.

## Acceptance criteria
- [ ] Default recipe no longer serializes every pass solely through previous-pass dependency chaining.
- [ ] Explicit dependencies are documented by reason.
- [ ] Render graph compile/validation tests cover the new dependency model.
- [ ] No rendering feature is silently dropped from the default recipe.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L "contract|unit" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not remove explicit dependencies that protect real side effects without replacing them with an equivalent typed/resource contract.
- Do not rely on incidental vector insertion order as a correctness guarantee.
- Do not change the public render graph API more than necessary.
- Do not mix this with renderer class splitting.
