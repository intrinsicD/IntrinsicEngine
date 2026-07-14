# Legacy Retirement Record (`src/legacy/`)

## Status

`src/legacy/` was retired on 2026-07-01. No legacy compatibility subtree remains
in the source tree, CMake no longer wires legacy targets, and
`tools/repo/layering_allowlist.yaml` has no active legacy exceptions.

## Exit Evidence

- Canonical implementations build under promoted roots: `src/core`, `src/assets`,
  `src/ecs`, `src/geometry`, `src/physics`, `src/graphics/*`, `src/platform`,
  `src/runtime`, and `src/app`.
- Semantic feature decisions were closed by
  [`LEGACY-011`](../../tasks/archive/LEGACY-011-src-legacy-feature-reimplementation-map.md)
  and its child tasks; the final removal did not add new engine features.
- Remaining bare legacy test consumers were retired by
  [`LEGACY-012`](../../tasks/archive/LEGACY-012-migrate-legacy-consumer-tests.md).
- Subtrees were then removed consumers-first, foundation-last:
  [`LEGACY-010`](../../tasks/archive/LEGACY-010-delete-src-legacy-runtime.md) →
  [`LEGACY-008`](../../tasks/archive/LEGACY-008-delete-src-legacy-graphics.md) →
  [`LEGACY-001`](../../tasks/archive/LEGACY-001-delete-src-legacy-interface.md) /
  [`LEGACY-006`](../../tasks/archive/LEGACY-006-delete-src-legacy-ecs.md) /
  [`LEGACY-004`](../../tasks/archive/LEGACY-004-delete-src-legacy-asset.md) →
  [`LEGACY-009`](../../tasks/archive/LEGACY-009-delete-src-legacy-rhi.md) →
  [`LEGACY-005`](../../tasks/archive/LEGACY-005-delete-src-legacy-core.md).
- The generated module inventory contains promoted modules only.

## Historical Notes

The valid deletion order was established by the
[`legacy-removal-audit.md`](legacy-removal-audit.md) snapshot: legacy runtime had
no legacy-internal consumers, graphics depended on runtime-facing consumers, RHI
and core were foundational, and core had to retire last. That audit is now a
historical explanation of why the final sweep ran in that order, not an open
work queue.
