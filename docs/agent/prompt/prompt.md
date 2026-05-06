Work on `/home/alex/Documents/IntrinsicEngine` using the repository’s agentic workflow.

Start by reading `/AGENTS.md`; it is authoritative. Then read only the relevant `docs/agent/*` files according to the routing table in `AGENTS.md`:
- read `docs/agent/task-format.md` before creating/promoting/retiring/updating task files,
- read `docs/agent/review-checklist.md` before committing or reporting completion,
- read architecture/method/benchmark/docs-sync guides only when the touched work triggers them.

Use the available planning subagent first to reassess the current active-task state and recommend the next smallest robust slice.

Then:
1. Inspect current state before choosing work:
   - `git status --short --branch`
   - recent `git log --oneline`
   - sorted contents of `tasks/active/`
2. Select the next active task by current status, blockers, urgency, dependency order, and recent history. Prefer the task whose file identifies the next implementation slice unless another active task is clearly more urgent.
3. Read the selected active task file completely. Treat that file as the source of all task-specific goals, non-goals, required changes, tests, docs, acceptance criteria, verification commands, forbidden changes, and slice plan. Do not embed or invent task-specific policy in this prompt.
4. Inspect the relevant source, tests, docs, backlog/done task cross-links, and generated files needed for that task until the owning subsystem and layer boundaries are clear.
5. Work the selected task until either:
   - the task is complete, verified, task/docs synced, and ready to retire to `tasks/done/`; or
   - a meaningful, verified, committable slice from the task's slice plan is complete.
6. If the task is long, split implementation into the smallest robust slices described by the task file or discovered during inspection. Keep each slice independently buildable, testable, and reviewable.
7. Split commits by logical change. Prefer separate commits for independent implementation/test slices and for non-trivial docs/task synchronization. Never mix mechanical moves with semantic refactors in the same commit.
8. Before committing, inspect `git status --short`, stage only intentional changes, and avoid including unrelated user/editor/build artifacts. If the environment cannot create commits, leave the working tree in committable groups and report the exact proposed commit boundaries.
9. Add nonblocking clarification questions to the relevant backlog or active task file instead of blocking implementation when a robust default path is available.
10. Retire completed active tasks to `tasks/done/` using the repository task format. Promote follow-up backlog tasks to `tasks/active/` only when the current active task is complete or the follow-up is genuinely required now.
11. Preserve C++23, layer ownership, renderer/RHI/Vulkan boundaries where relevant, docs sync, task sync, and the default CPU/null correctness path unless the selected task explicitly and validly requires otherwise.
12. When undecided, choose the more robust, deterministic, testable implementation that best satisfies the selected task's acceptance criteria.

Verification discipline:
- Run the selected task's focused verification commands first.
- Run broader repository gates only when required by the task, by `AGENTS.md`, or by the scope of touched files.
- Avoid stale/non-default build trees unless you confirm the compiler/toolchain supports the repository’s C++23 requirements.
- Treat `Testing/Temporary/LastTestsFailed.log` as historical only; current pass/fail comes from the CTest command just run.
- For noisy commands, use `set -o pipefail`, `tee`, and a bounded `tail`.
- Prefer task-specific commands over generic examples. When the task requires the default CPU correctness gate, use:
  ```bash
  cmake --preset ci
  cmake --build --preset ci --target IntrinsicTests
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

Before reporting completion:
- Read `docs/agent/review-checklist.md`.
- Confirm scope matches the selected task or documented slice.
- Confirm tests/docs/task records/generated inventories are synchronized for touched areas.
- Confirm verification commands were run and summarize current results only from commands executed in this session.
- Report completed commits or, if commits were not possible, the proposed commit split and remaining work.
