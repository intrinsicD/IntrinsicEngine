# Clean Workshop Task Pack

A coordinated set of tasks that protect the engine's architectural foundations
from slow drift: guardrail blindness, boundary leaks, stringly-typed routing,
god-object accumulation, scaffold completion that masquerades as capability,
and absence of a standing architecture-review gate.

## Completed execution record (was: suggested order)

1. [`WORKSHOP-001` (done)](../../archive/WORKSHOP-001-layer-check-module-and-cmake-aware.md) —
   the architecture guardrail now catches real dependencies (C++23 module
   imports and CMake link edges). Retired 2026-05-17.
2. [`WORKSHOP-002` (done)](../../archive/WORKSHOP-002-remove-platform-window-from-rhi.md) —
   fixed the `graphics/rhi -> platform` leak. Retired 2026-05-17 jointly
   with [`ARCH-005`](../../archive/ARCH-005-resolve-graphics-platform-layering-violations.md);
   `RHI::IDevice::Initialize` now takes a platform-neutral
   `RHI::DeviceCreateDesc`, `ExtrinsicRHI` no longer links
   `ExtrinsicPlatform`, and the strict layering check runs unguarded in
   `pr-fast` / `ci-linux-clang`.
3. [`WORKSHOP-003` (done)](../../archive/WORKSHOP-003-typed-frame-pass-and-resource-identity.md) and
   [`WORKSHOP-004` (done)](../../archive/WORKSHOP-004-typed-command-router.md) —
   make frame-pass identity typed and remove stringly command routing. Retired
   2026-06-06.
4. [`WORKSHOP-005` (done)](../../archive/WORKSHOP-005-renderer-subsystem-registry.md) and
   [`WORKSHOP-006` (done)](../../archive/WORKSHOP-006-extract-render-prep-pipeline.md) — split
   renderer responsibilities before the renderer becomes the new god object.
   Both tasks retired 2026-06-06.
5. [`WORKSHOP-007` (done)](../../archive/WORKSHOP-007-dependency-driven-default-recipe.md) — move
   the default recipe toward true graph semantics. Retired 2026-06-06.
6. [`WORKSHOP-008` (done)](../../archive/WORKSHOP-008-task-maturity-taxonomy.md) —
   prevent "scaffold done" from looking like "capability done." Retired
   2026-05-17; taxonomy lives at [`docs/agent/task-maturity.md`](../../../docs/agent/task-maturity.md).
7. [`WORKSHOP-009` (done)](../../archive/WORKSHOP-009-clean-workshop-review-gate.md) —
   added the standing clean-workshop architecture-review gate
   ([`docs/agent/clean-workshop-review.md`](../../../docs/agent/clean-workshop-review.md)
   + `tools/ci/run_clean_workshop_review.sh`) to keep the workshop clean.
   Retired 2026-06-06.

## Cross-domain dependency anchors

- **WORKSHOP-002 (done) ⇐ WORKSHOP-001 (done).** WORKSHOP-001 retired
  2026-05-17 and the guardrail caught the
  `graphics_rhi -> Extrinsic.Platform.Window` import and the matching
  `target_link_libraries(... ExtrinsicPlatform)` link edge that
  WORKSHOP-002 removed; the strict `check_layering.py --root src --strict`
  run is now clean and `pr-fast` / `ci-linux-clang` invoke it directly
  (expected-failure wrapper deleted with WORKSHOP-002's retirement).
- **WORKSHOP-004 (done) ⇐ WORKSHOP-003 (done).** Typed command routing consumes
  the typed pass identity introduced by WORKSHOP-003. Retired 2026-06-06.
- **WORKSHOP-006 (done) ⇐ WORKSHOP-005 (done).** Render-prep extraction is easier
  once subsystem ownership has been moved into the registry. `WORKSHOP-005`
  and `WORKSHOP-006` retired 2026-06-06.
- **WORKSHOP-007 (done) ⇐ WORKSHOP-003 (done).** Dependency-driven recipes
  benefit from typed resource identity for ordering decisions. `WORKSHOP-007`
  retired 2026-06-06.
- **WORKSHOP-009 ⇐ WORKSHOP-001 (done), WORKSHOP-008 (done).** The review
  gate references the strict layer checker (WORKSHOP-001 retired
  2026-05-17) and the maturity taxonomy at
  [`docs/agent/task-maturity.md`](../../../docs/agent/task-maturity.md)
  (WORKSHOP-008 retired 2026-05-17).

## Related

- [`/AGENTS.md`](../../../AGENTS.md) — authoritative repository contract.
- [`tasks/README.md`](../../README.md) — task lifecycle and ID prefix
  conventions.
