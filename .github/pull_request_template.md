## Summary
- Task ID(s): <!-- e.g. RORG-080 -->
- What changed:
- Why this change is needed:

## Type
- [ ] Documentation-only
- [ ] Mechanical move/refactor (no semantic changes)
- [ ] Behavioral/code change
- [ ] Build/CI/tooling change

## Layering
- [ ] I verified this change preserves the architecture invariants in `AGENTS.md`.
- [ ] I verified no prohibited cross-layer dependency was added.
- [ ] If a temporary exception was introduced, it is tracked in `tasks/active/0000-repo-reorganization-tracker.md` with a removal task ID.

## Tests
- Test category decision (required):
  - [ ] `unit`
  - [ ] `contract`
  - [ ] `integration`
  - [ ] `regression`
  - [ ] `gpu`
  - [ ] `benchmark`
- [ ] I ran the strongest relevant subset of checks for this change.
- [ ] If tests were not run/updated, I explained why.

## Docs
- Docs-sync decision (required):
  - [ ] Docs updated in this PR.
  - [ ] Docs not required (reason provided below).
- Docs touched / rationale:
  - <!-- list files or explain why no docs change is required -->

## Performance
- [ ] No measurable performance impact expected.
- [ ] Performance-sensitive code changed; benchmark/SLO impact was checked.
- Notes:
  - <!-- include benchmark links/results if applicable -->

## Benchmarking
- [ ] Method manifests changed (`methods/**/method.yaml`) and validator considerations were reviewed.
- [ ] Benchmark manifests changed (`benchmarks/**`) and validator considerations were reviewed.
- [ ] Benchmark result JSON schema impact considered/updated.
- Notes:
  - <!-- N/A is acceptable with a short reason -->

## Agent self-review
- [ ] Scope matches one task (or approved batch).
- [ ] Mechanical and semantic changes are not mixed.
- [ ] Build/test/docs/tooling updates are synchronized for touched scope.
- [ ] I reviewed diff for accidental unrelated changes.

## Temporary shims
- [ ] No temporary shim introduced.
- [ ] Temporary shim introduced and tracked with owner + removal task.
- Shim details:
  - <!-- shim path, removal task ID, expected removal date -->

## References
- Contract: `/AGENTS.md`
- Task format: `/docs/agent/task-format.md`
- Review checklist: `/docs/agent/review-checklist.md`
- Method workflow: `/docs/agent/method-workflow.md`
- Benchmark workflow: `/docs/agent/benchmark-workflow.md`
