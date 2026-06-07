# BUG-017 — Sandbox selection click and outline black frame

## Goal
- Restore default sandbox triangle selection so viewport left-click submits a pick request, and selecting from the hierarchy preserves the rendered scene while drawing the selection outline.

## Non-goals
- Reworking editor layout, menu structure, or hierarchy semantics.
- Changing selection authority outside the runtime-owned `SelectionController`.
- Adding new picking modes, modifier policy, or multi-select UI behavior.
- Changing Vulkan operational-gate policy or fallback backend selection.

## Context
- Layer owner: `runtime` owns input-to-selection routing and ECS selection authority; `graphics/renderer` owns frame-recipe composition and pass pipeline descriptors.
- The selection controller already exposes `RequestClickPick(...)`, but the default sandbox frame loop did not bind platform mouse input to it.
- The selection outline shader outputs an overlay contribution only. The recipe was routing that transparent/black overlay texture as the frame's present source, which blacked out the window when hierarchy selection enabled outline rendering.
- Completed: 2026-06-07.
- PR/commit: pending local commit.

## Required changes
- [x] Add a runtime input bridge that submits a click pick from left-button viewport clicks.
- [x] Guard viewport pick submission when ImGui wants mouse capture or a gizmo drag claims the click.
- [x] Change the selection-outline recipe so it composites into the current present source instead of replacing it.
- [x] Enable alpha blending for the selection-outline pipeline.
- [x] Keep the fix scoped to runtime input routing and graphics recipe/pipeline composition.

## Tests
- [x] Add or update runtime acceptance coverage proving a queued left click reaches the selection controller as a drained pick request.
- [x] Add or update frame-recipe coverage proving selection outline writes the present source and does not allocate/present from a standalone `SelectionOutline` texture.
- [x] Update renderer lifecycle coverage for the alpha-blended outline pipeline.

## Docs
- [x] Update task records for the closed bug.
- [x] Refresh generated module inventory after the `ImGuiAdapter` public surface change.

## Acceptance criteria
- [x] Clicking in the sandbox viewport submits a `SelectionController` click pick.
- [x] Clicking ImGui panels does not also submit a viewport pick.
- [x] Hierarchy selection does not turn the frame black because selection outline is an overlay over the existing present source.
- [x] Focused regression tests pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeSandboxAcceptance|FrameRecipeContract|RendererFrameLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target ExtrinsicSandbox
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing mechanical file moves with this semantic bug fix.
- Introducing app-layer selection authority.
- Making graphics read live ECS/runtime state.
- Treating a Vulkan-capable smoke as part of the default CPU gate.

## Maturity
- Target: `CPUContracted`.
- This bug closes at `CPUContracted`; no `Operational` follow-up is owed because the failing behavior is covered by runtime input and backend-neutral frame-recipe contracts, while opt-in Vulkan smoke remains host-dependent.
