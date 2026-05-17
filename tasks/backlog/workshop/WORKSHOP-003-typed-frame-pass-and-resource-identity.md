# WORKSHOP-003 — Introduce typed frame-pass and resource identity

## Goal
- Replace frame recipe pass/resource identity as a primarily string-based contract with typed IDs, while preserving human-readable debug names for diagnostics.

## Non-goals
- Do not rewrite all pass command bodies.
- Do not change visible rendering behavior.
- Do not remove debug names or graph debug dumps.
- Do not make recipe construction more complex than necessary.

## Context
- `FrameRecipePassKind` and `FrameRecipeResourceKind` already exist, but render execution still routes important command bodies by string names.
- Debug names are useful; they should not be the correctness contract.
- Typed identity is required before command routing can become robust (consumed by WORKSHOP-004).

## Required changes
- [ ] Introduce a stable typed pass ID used by frame recipes and compiled graph metadata, for example `FramePassId` or a refined use of `FrameRecipePassKind`.
- [ ] Introduce a stable typed resource ID used by frame recipe resources, for example `FrameResourceId` or a refined use of `FrameRecipeResourceKind`.
- [ ] Extend `RenderGraph::AddPass` or recipe-layer pass registration to attach optional typed pass metadata without changing debug-name output.
- [ ] Extend imported/created frame resources to carry optional typed resource metadata where recipe-owned resources are involved.
- [ ] Ensure `CompiledRenderGraph` or recipe introspection can map typed pass IDs to compiled pass indices.
- [ ] Preserve all existing pass/resource names in debug dumps and diagnostics.
- [ ] Add helper functions for converting typed pass/resource IDs to stable display names.
- [ ] Ensure duplicate typed pass IDs in one recipe are either rejected or explicitly allowed only when the recipe declares a multi-instance rule.

## Tests
- [ ] Add contract tests for typed pass ID propagation from recipe build to compiled graph metadata.
- [ ] Add contract tests for typed resource ID propagation where applicable.
- [ ] Add tests proving debug names remain stable after adding typed identity.
- [ ] Add tests proving duplicate typed IDs are diagnosed according to the chosen policy.
- [ ] Keep existing frame recipe and render graph validation tests passing.

## Docs
- [ ] Update rendering architecture docs to state that typed pass/resource identity is the contract; names are diagnostics only.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` graphics/framegraph row if needed.
- [ ] Update generated module inventory if public module surfaces changed.

## Acceptance criteria
- [ ] New recipe-owned passes/resources can be addressed by typed identity without relying on exact string names.
- [ ] Debug dumps still show readable pass/resource names.
- [ ] Existing tests for `FrameRecipe`, `RenderGraphValidation`, and renderer frame lifecycle pass.
- [ ] No production command routing has to compare user-facing debug strings for newly typed passes.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L "contract|unit" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not remove pass/resource names from diagnostics.
- Do not use raw strings as the new typed contract.
- Do not make renderer depend on ECS/runtime to obtain pass IDs.
- Do not change pass execution order except where tests prove equivalent graph semantics.
