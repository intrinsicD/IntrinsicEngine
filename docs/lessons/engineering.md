# Engineering safeguards

These lessons summarize completed work; no measurements were rerun for the
retention cleanup. Exact prior artifacts remain in Git at
`36a7b5062d9be3239d667efd9e41c4d4028ba249`.

## Compile cost follows work, not file size

Before moving ownership, identify repeated parsing/instantiation that the move
can actually remove. Measure representative consumers and the receiving owner;
fewer imports, a smaller BMI, or many consumers do not establish a speedup.
Preserve complete-type and lifetime constraints. The recorded tradeoffs remain
in [RUNTIME-266](../../tasks/done/RUNTIME-266-editor-snapshot-consumer-locality.md),
[RUNTIME-268](../../tasks/done/RUNTIME-268-workspace-snapshot-bmi-serialization.md)
and [GRAPHICS-145](../../tasks/done/GRAPHICS-145-renderer-implementation-frontend-cost.md).
The [compile-hotspot checks](../../tests/regression/tooling/Test.CompileHotspots.py)
exercise the attribution tooling.

Two closed feasibility investigations shipped no implementation:

- EnTT `extern template` declarations for private deduced-return members or
  storage classes still instantiated the allocation chain. Class declarations
  removed only part of it, and exposing registry headers adds dependency cost.
  A future attempt needs a different demonstrated boundary; this rejects those
  candidates, not every EnTT optimization. The existing component owner is
  `src/ecs/Components/ECS.Component.GeometrySources.cpp`.
- Moving mock lifetime bodies out of line added code without a useful consumer
  diagnostic reduction and increased receiving-owner work. Source was restored.
  Single compiler samples were feasibility observations, not target-build
  measurements; the target speedup criteria were never tested. Establish
  removable instantiation cost before repeating this extraction.

Original scoped reports: [EnTT feasibility](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/ara/evidence/diagnostics/entt_storage_rejected/README.md)
and [mock lifetime pilot](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/ara/evidence/diagnostics/mock_lifetime_rejected/README.md).

## Test asynchronous states deliberately

When a test observes a running job and then its completion, hold the job at
that observation boundary through the existing submit/work seam. Repeated
passing runs cannot exclude a race: BUG-219's test sometimes observed completion
twice while the production cache was correct. Avoid timing sleeps and dummy
jobs; order gate/lock lifetime so teardown can release the worker.
The [BUG-219 record](../../tasks/done/BUG-219-uv-panel-cache-snapshot-race.md)
and [runtime contract test](../../tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp)
retain the actual safeguard.

## State the GPU path that actually ran

GPU neighborhood queries with CPU reductions are not full GPU arithmetic.
Bounded readback tests do not establish arbitrary-input parity, speedup,
universal cancellation safety or leak freedom. Keep requested/actual/fallback
reporting specific to the selected path. See
[RUNTIME-227](../../tasks/done/RUNTIME-227-compact-density-weight-spatial-backends.md),
[RUNTIME-229](../../tasks/done/RUNTIME-229-lop-framed-lbvh-projection.md), and the
[LBVH smoke](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp).

Immediate fenced readback also does not eliminate once-per-frame pump latency
for small iterative jobs. [GRAPHICS-150](../../tasks/done/GRAPHICS-150-low-latency-compute-readback.md)
records that unresolved distinction and its follow-up owner.

For unexplained module-layout or virtual-dispatch failures, reuse the existing
[stale-build triage procedure](../../tools/agents/skills/intrinsicengine-stale-build-triage/SKILL.md)
before changing source; do not duplicate its cache-disabled reproduction rules.

## A fail-closed message must not name a single cause

Five weekly `ci-source-coverage` runs (2026-09-07 to 2026-10-19) failed with an
error that blamed non-atomic profile updates, although atomic updates were
already enabled. The real cause was a
Clang counter-map gap: a `?:` with lvalue operands, one a named constant,
written directly inside a braced initializer borrows the function-entry
counter. Before blaming threads, check whether the bad region's true count
equals its function's count. The collector error now names the location and
both causes, and the [collector notes](../../tools/ci/README.md) give the
coding rule. [BUG-242](../../tasks/done/BUG-242-cpu-source-coverage-counter-invalid.md)
records the diagnosis.
