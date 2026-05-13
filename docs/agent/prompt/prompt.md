Operate on this IntrinsicEngine checkout using the repository's agentic workflow. The repo contract beats your prior habits; when in doubt, follow `AGENTS.md`.

# Authority and reading order

Read in this order, only as deep as the touched scope requires:

1. `/AGENTS.md` — authoritative contract. Mission, layering invariants, source-tree map, coding rules, method/test/benchmark/docs/CI protocols, task workflow. Re-read at the start of every session.
2. `tasks/backlog/README.md` — convergence themes and cross-domain dependency anchors. Use this to pick what to work on.
3. `docs/agent/*` — read only the routing-table entry that applies:
   - `task-format.md` before creating, promoting, retiring, or materially editing a task file;
   - `review-checklist.md` before committing or reporting completion;
   - `architecture-review-checklist.md` when changing dependency boundaries, source layout, or runtime wiring;
   - `method-workflow.md` / `method-review-checklist.md` for paper/method work under `methods/`;
   - `benchmark-workflow.md` / `benchmark-review-checklist.md` for benchmark manifests, runners, baselines, or reports;
   - `docs-sync-policy.md` when moving files, changing public APIs, or refreshing generated inventories.

Do not load every guide for every task. Do not invent task-specific policy not present in these files.

# Inspect state before choosing work

```
git status --short --branch
git log --oneline -10
ls tasks/active/
```

# Pick the next slice

Apply this priority strictly:

1. If `tasks/active/` contains an in-progress task that matches your branch/owner, continue that task.
2. Otherwise pick from the backlog using `tasks/backlog/README.md` "Convergence themes":
   - **Theme A — sandbox visible geometry (P0)** outranks all P1 work.
   - Specific tasks inside **Theme D (ECS hardening)** and **Theme E (geometry IO completion)** gate specific Theme A tasks; see the anchors in step 3. Do not treat all of Theme D or Theme E as a prerequisite of Theme A — `HARDEN-063` and `HARDEN-064` do not gate any current Theme A task, and `GEOIO-002` only gates `GRAPHICS-034` (not the rest of Theme A).
   - **Theme F — foundation seeds** is cross-cutting and may be touched alongside any theme.
   - **Theme C — physics readiness** is gated by `ARCH-001`. Do not start `METHOD-001` runtime/ECS integration or `HARDEN-064` until `ARCH-001` lands.
   - **Theme B — rendering modernization (GRAPHICS-035..058)** stays planning-only until Theme A is unblocked.
   - **Theme G — bugs** trumps feature work for any reproducible regression.
3. Respect cross-domain dependency anchors. These are the only theme-crossing gates; treat anything not listed here as independent:
   - `GRAPHICS-034 ⇐ ASSETIO-001 ⇐ GEOIO-002` (`GRAPHICS-029..033` do not depend on `GEOIO-002` or `ASSETIO-001`).
   - `GRAPHICS-029..034 ⇐ HARDEN-060..062` (only this ECS subset; `HARDEN-063` and `HARDEN-064` are independent of Theme A).
   - `METHOD-001 ⇐ ARCH-001`.
   - `HARDEN-064 ⇐ ARCH-001`.
   - `GRAPHICS-035..058 ⇐ Theme A`.
4. Within a theme, prefer the earliest unblocked task. "Unblocked" means every upstream dependency is either marked done in `tasks/done/` or explicitly recorded as out-of-scope in the candidate task file.

Read the chosen task file completely before touching code. Treat it as the source of all task-specific goals, non-goals, required changes, tests, docs, acceptance criteria, verification commands, forbidden changes, and slice plan.

If you intend to land more than one slice, promote the task into `tasks/active/` with status, owner, branch, and next verification step. Single-slice patches may stay in `tasks/backlog/` while you work them.

# Implement the smallest robust slice

- Preserve buildability and testability at every commit.
- Never mix mechanical moves and semantic refactors in the same commit.
- Keep patches scoped to one task unless batching is explicitly allowed by the task file.
- Preserve the default CPU/null correctness path unless the task explicitly and validly requires otherwise.
- Add or update tests for any behavior change. Label by category (`unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`).
- Update docs and task records in the same patch as the code that motivates them.
- Regenerate `docs/api/generated/module_inventory.md` (`python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`) when public module surfaces change.
- Do not introduce new engine features during reorganization or hardening tasks.
- Do not introduce backwards-compatibility shims unless the task records a removal task ID and timeline.
- Do not import across layers in violation of `AGENTS.md` §2; runtime owns composition, graphics never sees live ECS, assets are CPU-only, etc.

# Verify with the strongest relevant subset

Run focused targets first; broaden only when the focused gate passes and the task requires it.

For local iteration on changed paths, you may use the touched-scope helper to
plan or run conservative affected checks. Treat it as an iteration aid, not a
replacement for the default CPU gate when PR/merge-level confidence is required.

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

- Picking a Theme B/Theme C task while Theme A prerequisites are open.
- Mixing mechanical moves with semantic edits in the same commit.
- Adding speculative abstractions, fallback paths, or "nice-to-have" cleanup outside the selected task.
- Bypassing the layering check by adding allowlist exceptions without a tracked removal task.
- Reporting completion without running the task's verification commands in the current session.
- Embedding task-specific policy into this prompt instead of into the task file.
