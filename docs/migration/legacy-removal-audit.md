# Legacy Removal Audit — Final Snapshot

**Finalized:** 2026-07-01

## Summary

`src/legacy/` is gone. The final audit found no promoted-source consumers of
bare legacy modules, retired the remaining legacy-only tests under
[`LEGACY-012`](../../tasks/done/LEGACY-012-migrate-legacy-consumer-tests.md),
and deleted the remaining subtrees in dependency order.

## Final Deletion Order

1. Runtime:
   [`LEGACY-010`](../../tasks/done/LEGACY-010-delete-src-legacy-runtime.md)
2. Graphics:
   [`LEGACY-008`](../../tasks/done/LEGACY-008-delete-src-legacy-graphics.md)
3. Interface, ECS, and Asset:
   [`LEGACY-001`](../../tasks/done/LEGACY-001-delete-src-legacy-interface.md),
   [`LEGACY-006`](../../tasks/done/LEGACY-006-delete-src-legacy-ecs.md),
   [`LEGACY-004`](../../tasks/done/LEGACY-004-delete-src-legacy-asset.md)
4. RHI:
   [`LEGACY-009`](../../tasks/done/LEGACY-009-delete-src-legacy-rhi.md)
5. Core:
   [`LEGACY-005`](../../tasks/done/LEGACY-005-delete-src-legacy-core.md)

This order was required because legacy-internal imports made runtime the leaf and
core the foundation. Deleting core or RHI earlier would have invalidated
remaining doomed consumers; deleting runtime first removed the only subtree with
no legacy-internal dependents.

## Feature Status

Feature-level blockers were already resolved before deletion. The
[`LEGACY-011`](../../tasks/done/LEGACY-011-src-legacy-feature-reimplementation-map.md)
map and its child tasks recorded retained, deferred, or retired outcomes for
legacy behavior. The final sweep removed compatibility code and compatibility
tests; it did not introduce replacement features.

## Consumer Status

- Promoted source imports only promoted `Extrinsic.*` / canonical module
  surfaces.
- Legacy CMake targets are no longer configured.
- Legacy test consumers were either migrated to promoted coverage or retired as
  non-endpoint compatibility tests.
- The layering allowlist is empty, so there are no active legacy exception
  owners.
