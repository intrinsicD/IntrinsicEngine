Operate on this IntrinsicEngine checkout using the repository's agentic workflow. The repo contract beats your prior habits; when in doubt, follow `AGENTS.md`.

This prompt is the default generic onboarding for any agent session. It tells you how to find work, how to scope it, and how to verify and ship it. It deliberately contains no theme-, task-, or schedule-specific policy — those live in `tasks/backlog/README.md` and the individual task files, which are authoritative for what to pick and in what order.

# Authority and reading order

Read in this order, only as deep as the touched scope requires:

1. `/AGENTS.md` — authoritative contract. Mission, layering invariants, source-tree map, coding rules, method/test/benchmark/docs/CI protocols, task workflow. Re-read at the start of every session.
2. `tasks/active/README.md` and the contents of `tasks/active/` — currently in-progress or blocked work that may already be assigned to your branch/owner.
3. `tasks/backlog/README.md` — convergence themes, priorities, and cross-domain dependency anchors. This file is the authoritative source for what is in-scope, what is gated, and in what order to pick from the backlog. Do not duplicate its priorities or anchors into this prompt.
4. `docs/agent/*` — read only the routing-table entry that applies:
   - `task-format.md` before creating, promoting, retiring, or materially editing a task file;
   - `review-checklist.md` before committing or reporting completion;
   - `architecture-review-checklist.md` when changing dependency boundaries, source layout, or runtime wiring;
   - `method-workflow.md` / `method-review-checklist.md` for paper/method work under `methods/`;
   - `benchmark-workflow.md` / `benchmark-review-checklist.md` for benchmark manifests, runners, baselines, or reports;
   - `docs-sync-policy.md` when moving files, changing public APIs, or refreshing generated inventories;
   - `roles.md` when clarifying handoff or role-specific expectations.

Do not load every guide for every task. Do not invent task-specific policy not present in these files.

# Inspect state before choosing work

```
git status --short --branch
git log --oneline -10
ls tasks/active/
```

Also skim `tasks/active/` task files for any in-progress slice tagged to your branch or owner.

# Pick the next slice

Apply this priority strictly:

1. **Continue active work first.** If `tasks/active/` contains an in-progress or blocked task that matches your branch or owner, continue that task. If it is blocked, address the recorded blocker or escalate via a nonblocking clarification in the task file; do not open new work to dodge a blocker.
2. **Otherwise pick from the backlog.** Use `tasks/backlog/README.md` as the authoritative source for priorities, convergence themes, and cross-domain dependency anchors. Respect every theme gate and dependency edge it records; treat anything it does not list as independent.
3. **Within a theme, prefer the earliest unblocked task.** "Unblocked" means every upstream dependency is either marked done in `tasks/done/` or explicitly recorded as out-of-scope in the candidate task file.
4. **Reproducible regressions trump feature work.** If `tasks/backlog/README.md` records a bugs theme (or equivalent), a reproducible regression there outranks new feature work in any other theme unless the task or backlog README explicitly says otherwise.

Read the chosen task file completely before touching code. Treat it as the source of all task-specific goals, non-goals, required changes, tests, docs, acceptance criteria, verification commands, forbidden changes, and slice plan. If the task file disagrees with this prompt on task-specific policy, the task file wins; if it disagrees with `/AGENTS.md` on repository contract, `/AGENTS.md` wins.

If you intend to land more than one slice, promote the task into `tasks/active/` with status, owner, branch, and next verification step (see `docs/agent/task-format.md`). Single-slice patches may stay in `tasks/backlog/` while you work them.

# Implement the smallest robust slice

- Preserve buildability and testability at every commit.
- Never mix mechanical moves and semantic refactors in the same commit.
- Keep patches scoped to one task unless batching is explicitly allowed by the task file.
- Preserve the default CPU/null correctness path unless the task explicitly and validly requires otherwise.
- Add or update tests for any behavior change. Label by category (`unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`, etc., per `tests/README.md`).
- Update docs and task records in the same patch as the code that motivates them.
- Regenerate `docs/api/generated/module_inventory.md` (`python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`) when public module surfaces change.
- Do not introduce new engine features during reorganization or hardening tasks.
- Do not introduce backwards-compatibility shims unless the task records a removal task ID and timeline.
- Do not import across layers in violation of `AGENTS.md` §2; runtime owns composition, graphics never sees live ECS, assets are CPU-only, etc.

# Verify with the strongest relevant subset

Run focused targets first; broaden only when the focused gate passes and the task requires it.

For local iteration on changed paths, you may use the touched-scope helper to plan or run conservative affected checks. Treat it as an iteration aid, not a replacement for the default CPU gate when PR/merge-level confidence is required.

```
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir <configured-build> --print
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir <configured-build> --run
```

Default CPU gate (when code/tests touched):
```
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Docs/task-only changes:
```
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

Layering-touching changes (in addition to default gate):
```
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

If the chosen task file lists additional or stricter verification commands, run those too — task-level verification supersedes the defaults above.

Verification hygiene:
- For noisy commands use `set -o pipefail`, `tee /tmp/<name>.log`, and a bounded `tail -n 120` so failures stay visible.
- Do not trust non-default build trees unless their compiler satisfies the C++23 requirement.
- Treat `Testing/Temporary/LastTestsFailed.log` as historical; current pass/fail comes from the CTest run you just executed.
- Do not skip GPU/Vulkan tests with a flag rename; only the labels `gpu|vulkan|slow|flaky-quarantine` are exempt by default policy.

# Review before commit

Apply `docs/agent/review-checklist.md` to the touched scope. Confirm:
- scope matches exactly one task or one documented slice from it,
- layering invariants intact,
- tests/docs/task records/generated inventories synchronized,
- verification commands actually ran in this session,
- temporary shims tracked with removal task IDs.

# Commit and PR hygiene

- One task per PR unless explicitly batched.
- Separate commits for independent slices and for non-trivial docs/task synchronization.
- Stage only intentional changes; never include editor/build artifacts.
- Never use `--no-verify`, `--amend` on shared history, or force-push to `main`/`master`.
- Commit messages: imperative subject ≤ 72 chars, body explains *why* and lists verification commands actually run.
- Retire completed active tasks to `tasks/done/` with completion date (YYYY-MM-DD) and commit/PR reference. Promote follow-up backlog tasks to active only when the current task is complete or the follow-up is genuinely required now.

# When stuck

- Add a nonblocking clarification question to the relevant active/backlog task file rather than blocking; pick the more robust default and continue.
- Prefer the more deterministic, more testable, smaller-blast-radius option.
- If a task is too large for one slice, write the slice plan into the task file before implementing.
- If state on disk surprises you (unfamiliar files, branches, locks), investigate before deleting or overwriting — it may be in-progress work.

# Anti-patterns to refuse

- Starting a new backlog task while an active task on your branch/owner is in-progress or has an addressable blocker.
- Picking a backlog task whose upstream dependencies (per `tasks/backlog/README.md` or the task file) are still open.
- Mixing mechanical moves with semantic edits in the same commit.
- Adding speculative abstractions, fallback paths, or "nice-to-have" cleanup outside the selected task.
- Bypassing the layering check by adding allowlist exceptions without a tracked removal task.
- Reporting completion without running the task's verification commands in the current session.
- Embedding task-specific policy, theme priorities, or dependency anchors into this prompt instead of into `tasks/backlog/README.md` or the task file.
