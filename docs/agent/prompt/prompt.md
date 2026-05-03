Work on `/home/alex/Documents/IntrinsicEngine` using the repository’s agentic workflow.

Start by reading `/AGENTS.md`; it is authoritative. Then read only the relevant `docs/agent/*` files according to the routing table in `AGENTS.md`:
- read `docs/agent/task-format.md` before creating/promoting/retiring/updating task files,
- read `docs/agent/review-checklist.md` before committing or reporting completion,
- read architecture/method/benchmark/docs-sync guides only when the touched work triggers them.

Use the available planning subagent first to reassess the current active-task state and recommend the next smallest robust slice.

Then:
1. Inspect git status/history and `tasks/active/`.
2. Select the next active task to work on. If `GRAPHICS-018-vulkan-renderer-integration.md` is still active, continue it unless another active task is clearly more urgent.
3. Work the selected task until either:
    - the task is complete and can be retired to `tasks/done/`, or
    - a meaningful, verified, committable slice is complete.
4. Add nonblocking clarification questions to the relevant backlog task instead of blocking implementation.
5. Retire completed active tasks to `tasks/done/`.
6. Promote follow-up backlog tasks to `tasks/active/` only when the current active task is complete or the follow-up is genuinely required now.
7. Keep changes scoped; do not mix mechanical moves with semantic refactors.
8. Split commits by logical change, typically code/tests separately from docs/task sync when both are non-trivial.
9. When undecided, choose the more robust, deterministic, testable implementation.
10. Preserve C++23, layer ownership, renderer/RHI/Vulkan boundaries, docs sync, and task sync.

For `GRAPHICS-018` specifically:
- Keep Vulkan opt-in.
- Do not make Vulkan/GPU tests mandatory in the default CPU gate.
- Preserve the CPU/null correctness path.
- Prefer incremental fail-closed or guarded real Vulkan resource/backend slices over broad swapchain/runtime rewrites.
- Do not special-case Vulkan in renderer code; keep backend-specific logic behind RHI/backend seams.
- Add/update CPU-testable contract coverage where default CI cannot build or run Vulkan paths.
- Update `src/graphics/vulkan/README.md`, `docs/architecture/graphics.md`, active task text, and `GRAPHICS-018Q` backlog clarifications when the behavior/policy changes.

Verification discipline:
- Run focused relevant build/test targets first.
- Avoid stale/non-default build trees unless you confirm the compiler/toolchain supports the repository’s C++23 requirements.
- Treat `Testing/Temporary/LastTestsFailed.log` as historical only; current pass/fail comes from the CTest command just run.
- For noisy commands, use `set -o pipefail`, `tee`, and a bounded `tail`.
- Prefer commands like:
  ```bash
  cmake --build --preset ci --target IntrinsicGraphicsContractTests
  ctest --test-dir build/ci --output-on-failure -R '^RendererRhiBoundary\.' --timeout 60

  compiler=$(cmake -LA -N build/dev-clang-ninja | sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p')
  "$compiler" --version | head -n 1
  set -o pipefail
  cmake --build build/dev-clang-ninja --target ExtrinsicBackendsVulkan -j2 2>&1 | tee /tmp/intrinsic-vulkan-backend-build.log | tail -n 160

  python3 tools/agents/check_task_policy.py --root . --strict
  python3 tools/docs/check_doc_links.py --root .
  python3 tools/repo/check_layering.py --root src --strict
  python3 tools/repo/check_test_layout.py --root . --strict

  cmake --build --preset ci --target IntrinsicTests
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --quiet