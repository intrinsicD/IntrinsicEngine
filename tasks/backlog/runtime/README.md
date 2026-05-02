# Runtime Backlog Index

This README is the agent-facing entry point into the `tasks/backlog/runtime/`
queue. It lists runtime-owned backlog work and cross-links rendering tasks
whose ownership lives in `src/runtime` even when the task file is filed under
another backlog directory.

## Runtime backlog tasks

- [RORG-031C — Runtime composition backlog seed](RORG-031-runtime-composition.md):
  composition-root and lifecycle backlog work for `begin_frame`, extraction,
  prepare, execute, end, shutdown determinism, and subsystem wiring.

## Cross-linked rendering tasks (runtime-owned)

Some rendering backlog tasks are runtime-owned for extraction/wiring even
though they currently live under `tasks/backlog/rendering/`. Runtime reviewers
must treat these as runtime work when scheduling and review:

- [GRAPHICS-016 — Runtime extraction and graphics handoff](../../active/GRAPHICS-016-runtime-extraction-handoff.md):
  - Runtime owns live ECS access, extraction, sidecar/cache mappings from ECS
    entities and asset/source handles to graphics handles, dirty-domain
    interpretation, deletion events, and compaction/relocation handoff.
  - Graphics must not import live ECS ownership; promoted graphics layers
    consume snapshots/views only.
  - GRAPHICS-016 is the first implementation gate for the rendering backlog
    and must land before most rendering pass implementation work begins. See
    the rendering DAG in
    [`tasks/backlog/rendering/README.md`](../rendering/README.md) for
    downstream ordering.

## Related docs

- [`AGENTS.md`](../../../AGENTS.md) — authoritative repository agent contract.
- [`tasks/backlog/rendering/README.md`](../rendering/README.md) — rendering
  backlog DAG and selection rules.
- [`tasks/backlog/rendering/GRAPHICS-001-rendering-parity-inventory.md`](../rendering/GRAPHICS-001-rendering-parity-inventory.md) —
  canonical rendering backlog index.
