# Task Graph Architecture

IntrinsicEngine uses explicit task graphs to structure concurrent CPU and async work.

## Graph categories

- CPU task graph: engine/runtime scheduling and deterministic work partitioning.
- Async streaming graph: background IO and heavy processing.

## Ownership

- Runtime owns graph composition/wiring.
- Subsystems expose work units/contracts; they do not own global scheduling policy.

## Related references

- Migration-era details: `task-graph-domains.md`.
- Runtime ownership rules: [runtime.md](runtime.md).
