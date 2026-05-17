# Clean Workshop Task Pack

A coordinated set of tasks that protect the engine's architectural foundations
from slow drift: guardrail blindness, boundary leaks, stringly-typed routing,
god-object accumulation, scaffold completion that masquerades as capability,
and absence of a standing architecture-review gate.

## Suggested order

1. [`WORKSHOP-001` (done)](../../done/WORKSHOP-001-layer-check-module-and-cmake-aware.md) —
   the architecture guardrail now catches real dependencies (C++23 module
   imports and CMake link edges). Retired 2026-05-17.
2. [`WORKSHOP-002`](WORKSHOP-002-remove-platform-window-from-rhi.md) — fix the
   known `graphics/rhi -> platform` leak.
3. [`WORKSHOP-003`](WORKSHOP-003-typed-frame-pass-and-resource-identity.md) and
   [`WORKSHOP-004`](WORKSHOP-004-typed-command-router.md) — make frame-pass
   identity typed and remove stringly command routing.
4. [`WORKSHOP-005`](WORKSHOP-005-renderer-subsystem-registry.md) and
   [`WORKSHOP-006`](WORKSHOP-006-extract-render-prep-pipeline.md) — split
   renderer responsibilities before the renderer becomes the new god object.
5. [`WORKSHOP-007`](WORKSHOP-007-dependency-driven-default-recipe.md) — move
   the default recipe toward true graph semantics.
6. [`WORKSHOP-008` (done)](../../done/WORKSHOP-008-task-maturity-taxonomy.md) —
   prevent "scaffold done" from looking like "capability done." Retired
   2026-05-17; taxonomy lives at [`docs/agent/task-maturity.md`](../../../docs/agent/task-maturity.md).
7. [`WORKSHOP-009`](WORKSHOP-009-clean-workshop-review-gate.md) — add a
   standing architecture-review gate to keep the workshop clean.

## Cross-domain dependency anchors

- **WORKSHOP-002 ⇐ WORKSHOP-001 (done).** WORKSHOP-001 retired
  2026-05-17 and the guardrail now catches the
  `graphics_rhi -> Extrinsic.Platform.Window` import and the matching
  `target_link_libraries(... ExtrinsicPlatform)` link edge that
  WORKSHOP-002 will remove. Until WORKSHOP-002 lands, the strict
  `check_layering.py --root src --strict` run is expected to fail with
  that violation reported; the `pr-fast` and `ci-linux-clang` workflows
  wrap the invocation as an expected-failure check (revert to the
  unguarded form once WORKSHOP-002 lands).
- **WORKSHOP-004 ⇐ WORKSHOP-003.** Typed command routing consumes the typed
  pass identity introduced by WORKSHOP-003.
- **WORKSHOP-006 ⇐ WORKSHOP-005.** Render-prep extraction is easier once
  subsystem ownership has been moved into the registry.
- **WORKSHOP-007 ⇐ WORKSHOP-003.** Dependency-driven recipes benefit from
  typed resource identity for ordering decisions.
- **WORKSHOP-009 ⇐ WORKSHOP-001 (done), WORKSHOP-008 (done).** The review
  gate references the strict layer checker (WORKSHOP-001 retired
  2026-05-17) and the maturity taxonomy at
  [`docs/agent/task-maturity.md`](../../../docs/agent/task-maturity.md)
  (WORKSHOP-008 retired 2026-05-17).

## Related

- [`/AGENTS.md`](../../../AGENTS.md) — authoritative repository contract.
- [`tasks/README.md`](../../README.md) — task lifecycle and ID prefix
  conventions.
