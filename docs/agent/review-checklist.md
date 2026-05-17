# Review Checklist

Use this checklist before commit and PR creation.

## Scope and ownership

- [ ] Change maps to exactly one task (unless batching explicitly allowed).
- [ ] Owning subsystem/layer is identified.
- [ ] Mechanical moves and semantic edits are not mixed.

## Maturity and closure

- [ ] For tasks closing to `tasks/done/`: the reached maturity level is
      named (see [`task-maturity.md`](task-maturity.md): `Scaffolded`,
      `CPUContracted`, `Operational`, `ParityProven`, `Retired`). State it
      in the task `Status` block, retirement commit message, or completion
      summary — pick one and use it consistently.
- [ ] If the closing task body uses "scaffold", "stub", "fail-closed", or
      "minimal" language, it either names a follow-up task ID for the next
      maturity level or records an explicit `Non-goals` line that pins the
      scaffold as the intended endpoint.
- [ ] For rendering, Vulkan, asset ingest, hot reload, pass command bodies,
      runtime composition, and legacy retirement tasks: an `Operational`
      claim cites the backend-labeled or integration-labeled run that
      actually executed (not just CPU contract coverage).

## Architecture and layering

- [ ] Dependency flow follows `AGENTS.md` invariants.
- [ ] No cross-layer convenience imports introduced.
- [ ] Runtime wiring remains in `runtime`.
- [ ] CMake `target_link_libraries(...)` edges between promoted targets
      treat the link as an architecture dependency, not a build-system
      convenience. `tools/repo/check_layering.py --root src --strict`
      covers both C++23 module imports and CMake link edges.

## Testing

- [ ] Strongest relevant verification subset was run.
- [ ] Tests for behavior changes were added or updated.
- [ ] Test labels/category are correct (`unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`).
- [ ] Focused build/test targets were run before broad or long-running targets.
- [ ] If `tools/ci/touched_scope.py` was used, its selected commands are recorded and any broad fallback/full-gate requirements are still addressed.
- [ ] Build trees used for evidence were confirmed current and compatible with repository C++23/toolchain requirements.
- [ ] Current CTest output, not stale `LastTestsFailed.log` contents, was used to assess pass/fail state.
- [ ] Noisy command output was captured to a log and filtered with `set -o pipefail` so failures remain visible.

## Performance and benchmarking

- [ ] No unsubstantiated performance claims.
- [ ] Benchmarks/manifests updated where required.
- [ ] Smoke vs heavy benchmark intent is explicit.

## Documentation and tasks

- [ ] Docs updated for structural/policy/behavior changes.
- [ ] Links are updated and valid.
- [ ] Task records synchronized (`active`, `backlog`, `done` as appropriate).

## CI and temporary shims

- [ ] Touched CI/workflow logic remains readable and maintainable.
- [ ] Any temporary shim is recorded in tracker with removal task and timeline.


Related: `docs/agent/architecture-review-checklist.md`.
