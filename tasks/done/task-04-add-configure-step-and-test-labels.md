# Task 4 — Add configure step and label expectations to rendering tasks

- Status: completed
- Owner: claude (claude/next-active-task-OvcHb)
- Branch / PR: claude/next-active-task-OvcHb
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: none for this queue item; implementation tasks retain their configure/build/CTest gates.
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict` after normalizing GRAPHICS-002..GRAPHICS-018 verification blocks.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Normalize verification blocks and test-label expectations in rendering backlog tasks.

## Problem

`AGENTS.md` requires agents to configure the `ci` preset, build a meaningful target such as `IntrinsicTests`, and run CTest. Many GRAPHICS tasks only list build + CTest, missing the configure step. Rendering tasks also ask for tests but usually do not state required CMake/CTest labels.

## Required changes

1. For every implementation-oriented rendering task:
   - `tasks/backlog/rendering/GRAPHICS-002-*.md` through `GRAPHICS-018-*.md`
   - and any other rendering task with CMake/CTest verification

   update Verification to include:

   ```bash
   cmake --preset ci
   cmake --build --preset ci --target IntrinsicTests
   ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
   ```

2. For GPU/Vulkan optional tasks, keep optional GPU command after the CPU gate, e.g.:

   ```bash
   # Optional when hardware/driver support is available:
   ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
   ```

3. In each task's Tests section, add explicit label guidance:
   - CPU-only graphics unit tests: `unit;graphics`
   - graphics contract tests: `contract;graphics`
   - runtime extraction integration tests: `integration;runtime;graphics`
   - Vulkan/GPU smoke tests: `gpu;vulkan`
   - regression tests: `regression;graphics` or `regression;runtime;graphics`
4. Do not invent test filenames unless the task already names concrete files.
5. Do not change CMake behavior in this task.

## Acceptance criteria

- All rendering implementation tasks include configure + build + CTest.
- Test label expectations are explicit enough that future tests do not violate taxonomy.
- Optional Vulkan/GPU checks remain excluded from the default CPU gate.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes

- No C++ source changes.
- No shader changes.
- No test implementation.
- No CMake target changes.

The AGENTS contract explicitly requires configure + build + CTest. Existing CMake test registration supports category labels via the helper.
