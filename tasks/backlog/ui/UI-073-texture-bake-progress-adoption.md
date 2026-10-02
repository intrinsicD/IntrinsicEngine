---
id: UI-073
theme: F
depends_on: [UI-069]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI/runtime slice; evidence is the diff, ImGui tests, review and CI.
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality, repo.source-documentation]
---
# UI-073 — Texture bake panels use the shared operation progress widget

## Goal
The texture bake panels show their run's progress through `OperationRunSlot`
and `DrawOperationProgress` ([UI-069](../../done/UI-069-shared-operation-progress-widget.md)),
replacing the "Bake pending." overlay text.

## Context
- `Runtime.TextureBakeModule.cpp` submits its jobs straight to `JobService` with
  neither an editor identity nor a correlation id, so `EditorJobCommandSurface::Progress`
  cannot see them. `PropertyTextureBakeResult::Job` already carries the token.
- Left out of UI-069 slice 4 for that reason; the other method panels are
  adopted or synchronous (see the UI-069 note).

## Acceptance criteria
- [ ] Bake jobs carry a correlation id (or an editor identity) so the progress surface resolves them; the bake worker reports progress where a fraction is known.
- [ ] The texture bake controls and the UV texture tab draw the widget through a run slot keyed by the submitted bake, shown for its entity only.
- [ ] An ImGui test starts a bake and sees the run, then another entity shows nothing.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|TextureBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
