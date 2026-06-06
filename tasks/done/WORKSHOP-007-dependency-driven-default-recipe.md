# WORKSHOP-007 — Make default frame recipe dependency-driven

Status: completed (2026-06-06)
Owner: Codex (GPT-5)
Branch / PR: current branch / TBD
Completion date: 2026-06-06
Follow-ups: none for the CPU-contracted default-recipe dependency model. Backend operational parity for the default-recipe pass-body chain remains tracked by the existing graphics/Vulkan parity tasks.

## Goal
- Move the default frame recipe away from forced linear pass chaining and toward true render-graph semantics where ordering follows resource dependencies, with explicit dependencies only for side effects or real ordering constraints.

## Non-goals
- Do not remove the ability to force order where required.
- Do not change visual output or resource set intentionally.
- Do not implement missing command bodies.
- Do not reintroduce the retired bootstrap recipe scaffold while changing default-recipe dependencies.

## Context
- The default recipe used an `addOrderedPass` helper that chained each pass to the previous pass.
- That was acceptable as a walking skeleton, but it could accidentally turn the render graph into a linear script with resource annotations.
- The default recipe now relies on declared resource reads/writes for ordering wherever the dependency is representable in the graph.
- Explicit dependency edges remain available through `RenderGraphBuilder::DependsOn(...)`, are stored in compiled pass declarations, and are printed in debug dumps for review.

## Required changes
- [x] Audit every explicit dependency currently added by default recipe construction.
- [x] Classify each ordering edge as either:
  - required by real side effect/order semantics, or
  - derivable from resource reads/writes.
- [x] Replace forced sequential dependencies with resource-driven dependencies wherever tests prove equivalent compile order/barrier correctness.
- [x] Keep explicit dependencies available for side-effect passes such as presentation, ImGui, readback, or other passes whose ordering is not fully represented by resources; no default-recipe pass needed an explicit dependency after the resource audit.
- [x] Add recipe helper APIs that make explicit dependencies rare and intentional.
- [x] Ensure render graph validation catches missing producer/consumer or side-effect-order mistakes.
- [x] Preserve debug dump readability so graph order remains inspectable.

## Tests
- [x] Add a frame recipe contract test proving default recipe compiles without unnecessary linear dependencies.
- [x] Add tests proving required side-effect order is still preserved.
- [x] Add tests proving barrier packet ordering remains valid after removing redundant explicit dependencies.
- [x] Add regression tests for picking/selection/debug/postprocess feature combinations.
- [x] Keep existing `Test.FrameRecipeContract`, `Test.RenderGraphValidation`, and renderer lifecycle tests passing.

## Docs
- [x] Update render graph architecture docs to state when explicit dependencies are allowed.
- [x] Update frame recipe docs/comments to distinguish resource dependency from side-effect dependency.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` graphics/framegraph row if needed.

## Acceptance criteria
- [x] Default recipe no longer serializes every pass solely through previous-pass dependency chaining.
- [x] Explicit dependencies are documented by reason.
- [x] Render graph compile/validation tests cover the new dependency model.
- [x] No rendering feature is silently dropped from the default recipe.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'FrameRecipeContract|RenderGraphValidation|PostProcessChainContract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
tools/ci/run_clean_workshop_review.sh . --strict
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'contract|unit' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not remove explicit dependencies that protect real side effects without replacing them with an equivalent typed/resource contract.
- Do not rely on incidental vector insertion order as a correctness guarantee.
- Do not change the public render graph API more than necessary.
- Do not mix this with renderer class splitting.
