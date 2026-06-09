Operate on this IntrinsicEngine checkout using the repository's agentic workflow. The repo contract beats your prior habits; when in doubt, follow `AGENTS.md`.

This prompt is the default generic onboarding for any agent session. It tells you how to find work, how to scope it, and how to verify and ship it. It deliberately contains no theme-, task-, or schedule-specific policy — those live in `tasks/backlog/README.md` and the individual task files, which are authoritative for what to pick and in what order.

# Authority and reading order

If the `intrinsicengine-core` skill is available in this session, its description and routing table are the canonical entry point — let it load on the first task-shaped prompt and use its routing to specialist skills (`intrinsicengine-task-workflow`, `intrinsicengine-review`, `intrinsicengine-method`, `intrinsicengine-benchmark`, `intrinsicengine-docs-sync`). The skills mirror this prompt and the `docs/agent/*` procedures; their `references/` are the same content. Reading a skill does not replace reading `/AGENTS.md`, but it does replace reading the matching `docs/agent/*` file by hand — do not read both. If no skills are available (bare API client, web sandbox without skill auto-discovery, etc.), follow the file-path reading order below unchanged.

Read in this order, only as deep as the touched scope requires:

1. `/AGENTS.md` — authoritative contract. Mission, layering invariants, source-tree map, coding rules, method/test/benchmark/docs/CI protocols, task workflow. Re-read at the start of every session. Skills do not supersede this file.
2. `tasks/active/README.md` and the contents of `tasks/active/` — currently in-progress or blocked work that may already be assigned to your branch/owner.
3. `tasks/backlog/README.md` — convergence themes, priorities, and cross-domain dependency anchors. This file is the authoritative source for what is in-scope, what is gated, and in what order to pick from the backlog. Do not duplicate its priorities or anchors into this prompt.
4. `docs/agent/*` (or the equivalent `intrinsicengine-*` skill) — read only the routing-table entry that applies. The skill bodies and their `references/` mirror the docs; pick whichever path is available, do not load both:
   - `task-format.md` / `intrinsicengine-task-workflow` before creating, promoting, retiring, or materially editing a task file;
   - `review-checklist.md` / `intrinsicengine-review` before committing or reporting completion;
   - `architecture-review-checklist.md` / `intrinsicengine-review` (architecture-review section) when changing dependency boundaries, source layout, or runtime wiring;
   - `method-workflow.md` / `method-review-checklist.md` / `intrinsicengine-method` for paper/method work under `methods/`;
   - `benchmark-workflow.md` / `benchmark-review-checklist.md` / `intrinsicengine-benchmark` for benchmark manifests, runners, baselines, or reports;
   - `docs-sync-policy.md` / `intrinsicengine-docs-sync` when moving files, changing public APIs, or refreshing generated inventories;
   - `roles.md` / `intrinsicengine-core` (roles reference) when clarifying handoff or role-specific expectations.

Do not load every guide for every task. Do not invent task-specific policy not present in these files. For pure lookup-shaped questions that a direct grep of `AGENTS.md` answers (e.g. "what does layer X depend on"), grepping the contract directly is appropriate and skills should not be force-loaded — they add value on multi-step procedural work, not single-fact lookups.

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

If you intend to land more than one slice, promote the task into `tasks/active/` with status, owner, branch, and next verification step (see `docs/agent/task-format.md` or `intrinsicengine-task-workflow`). Single-slice patches may stay in `tasks/backlog/` while you work them.

# Implement the smallest robust slice

- Preserve buildability and testability at every commit.
- Never mix mechanical moves and semantic refactors in the same commit.
- Keep patches scoped to one task unless batching is explicitly allowed by the task file.
- Preserve the default CPU/null correctness path unless the task explicitly and validly requires otherwise.
- Add or update tests for any behavior change. Label by category (`unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`, etc., per `tests/README.md`).
- Update docs and task records in the same patch as the code that motivates them.
- Regenerate `docs/api/generated/module_inventory.md` (`python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`) when public module surfaces change.
- Keep `.cppm` module interfaces limited to exported types, declarations, small inline accessors, and templates that must be visible to importers. Move non-trivial bodies into matching `.cpp` module implementation units and add them as private target sources; non-trivial means algorithm/control-flow bodies, allocation-heavy work, topology/container traversal, backend calls, diagnostics assembly, file/IO handling, or imports only needed by implementation details.
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
python3 tools/agents/check_task_state_links.py --root . --strict
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

Apply `docs/agent/review-checklist.md` (or the `intrinsicengine-review` skill if loaded) to the touched scope. Confirm:
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
- Loading both the `docs/agent/*` file and its mirror `intrinsicengine-*` skill for the same touched scope — they are equivalent content; pick one and continue.

# Multi-task loop mode

Continue implementing tasks sequentially until one stop condition is met.

For each iteration:
1. Inspect repo state: `git status --short --branch`, `ls tasks/active/`.
2. Continue active work first; otherwise pick the earliest unblocked backlog task.
3. Read the selected task file completely.
4. Plan the smallest robust slice.
5. Implement the smallest robust slice.
6. Update tests/docs/task records as required.
7. Run the strongest relevant verification.
8. If complete, retire/promote the task according to repository policy and commit the changes with a clear commit message.
9. Self-review, then start the next iteration.

Stop immediately if:
- verification fails and cannot be resolved locally,
- an unexpected dirty worktree change appears,
- dependencies/blockers are ambiguous,
- the next task would violate `AGENTS.md`,
- more than N tasks have completed,
- runtime exceeds the configured budget.
- user input is required to resolve a blocker.
- the task backlog is empty.