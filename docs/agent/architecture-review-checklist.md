# Architecture Review Checklist

Use this checklist for architecture-impacting changes (code, build, docs, CI, and task policy).

## Layering and ownership

- [ ] Owning layer/subsystem is explicit (`core`, `geometry`, `assets`, `ecs`, `graphics/*`, `runtime`, `app`, `legacy`).
- [ ] New dependency edges are justified and align with `AGENTS.md` invariants.
- [ ] No lower layer imports higher layers.
- [ ] `runtime` remains composition root; lower layers remain reusable.

## Lifetime and resource ownership

- [ ] Ownership model is explicit (value, handle, unique owner, borrowed view).
- [ ] Cross-system references avoid hidden lifetime coupling.
- [ ] Temporary compatibility shims are tracked with removal task ID.

## Concurrency and synchronization

- [ ] Threading model is explicit for touched paths (main thread, task graph, async workers, GPU queue).
- [ ] Shared mutable state has clear synchronization strategy.
- [ ] No blocking behavior introduced on hot paths without justification.

## Error handling and diagnostics

- [ ] Failure states are explicit and propagated deterministically.
- [ ] New failure/error modes include actionable diagnostics.
- [ ] Fallback behavior is documented when applicable.

## Testing and verification

- [ ] Test categories are correct (`unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`).
- [ ] Verification command subset is strong enough for touched scope.
- [ ] Behavior changes include test additions/updates.

## Benchmarking and performance

- [ ] Performance-sensitive changes include benchmark/SLO impact assessment.
- [ ] No unsupported performance claims.
- [ ] Smoke vs deep benchmark expectations are explicit.

## Documentation and cleanup

- [ ] Docs are synchronized for architecture/policy/path changes.
- [ ] Task tracker is updated for temporary exceptions and blockers.
- [ ] Mechanical moves are separated from semantic refactors.
- [ ] Follow-up cleanup tasks are recorded when needed.

## CI and PR contract gates

- [ ] PR template sections are completed (`Summary`, `Type`, `Layering`, `Tests`, `Docs`, `Performance`, `Benchmarking`, `Agent self-review`, `Temporary shims`).
- [ ] Workflow impacts are reviewed for readability and trigger correctness.
- [ ] Strict validators remain green (or warning-mode exceptions are documented).
