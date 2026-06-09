# PLATFORM-006 — Platform event value gate and editor boundary

## Goal
- Promote only currently useful legacy `Core.Window` / `Core.Input` behavior into `src/platform` event contracts and document explicit non-goals or follow-up triggers for multi-window, IME/text input, and file-dialog ownership.

## Non-goals
- No graphics, runtime, ECS, or app imports from `src/platform`.
- No native file-dialog implementation unless the boundary decision proves it belongs in platform and a follow-up implementation slice is accepted.
- No non-Linux backend implementation in this task; `PLATFORM-004` owns alternative backend onboarding.

## Context
- Owner/layer: `platform -> core` only.
- `PLATFORM-003` and `PLATFORM-005` promoted explicit Null/GLFW backend structure and module hygiene. The parity matrix value-gates current-workflow event hardening and defers multi-window behavior, IME/text input details, native dialogs, and extra backend behavior until concrete runtime/UI owners require them.
- Runtime owns composition and consumes platform events; UI may request file dialogs through runtime/app command surfaces but must not own platform state.

## Value gate
- Current state: promoted platform already has explicit Null/GLFW backend structure and headless-safe event flow for current runtime/UI paths.
- Improvement: current editor workflows get deterministic event/file-boundary semantics without making platform depend on runtime/UI or forcing GLFW in headless tests.
- Scope decision: retain drop/focus/minimize/resize/cursor/clipboard/text events only when current consumers need them. Defer IME, multi-window, native dialogs, and non-Linux backends unless a concrete UI/runtime task depends on them.

## Required changes
- [ ] Inventory legacy window/input event semantics against `Platform.IWindow` and `Platform.Input`.
- [ ] Add or harden event payloads for text input, cursor, clipboard, dropped paths, focus/minimize/resize, and high-DPI framebuffer sizing where they are already required by runtime/UI.
- [ ] Decide whether multi-window and IME are current implementation goals, explicit non-goals, or separate follow-up tasks.
- [ ] Define the editor file-dialog boundary: platform-native service, app-owned shell integration, or explicit out-of-scope path-entry UI.
- [ ] Preserve `INTRINSIC_PLATFORM_BACKEND=Null` as the default headless-safe route for CPU/null tests.

## Tests
- [ ] Add `contract;platform` tests for Null backend event buffering and text/drop/clipboard/cursor semantics.
- [ ] Add `integration;platform` GLFW smoke only where host capability is required and label it correctly.
- [ ] Add layering regression coverage for `platform` imports.

## Docs
- [ ] Update `src/platform/README.md` and `docs/migration/nonlegacy-parity-matrix.md`.
- [ ] Update `tasks/backlog/platform/README.md`.
- [ ] Update `tests/README.md` and `tests/CMakeLists.txt` only if new labels are introduced.

## Acceptance criteria
- [ ] Runtime/UI consumers have a deterministic platform event contract for currently supported editor workflows.
- [ ] Unsupported multi-window or IME behavior is explicitly recorded as non-goal or follow-up, not left as an unnamed legacy gap.
- [ ] `src/platform` remains independent of graphics/runtime/UI/app.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'platform' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding graphics/runtime/UI/app imports to `src/platform`.
- Making GLFW mandatory for headless tests.

## Maturity
- Target: `CPUContracted` for platform event contracts; `Operational` GLFW behavior only for labelled smoke coverage.
