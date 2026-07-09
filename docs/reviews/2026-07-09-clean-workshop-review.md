# Clean-workshop review — ARCH-008 kernel event bus

## Change Under Review

- Change: `ARCH-008` queued-only runtime kernel event bus with two
  `Engine::RunFrame()` pump points.
- Trigger(s): changes runtime wiring/composition order.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` ran clean. Runtime imports only allowed lower layers and runtime substrate modules. |
| 2 | CMake target links match layer policy | pass | No new cross-layer target link edge; `ExtrinsicRuntime` owns the new runtime module. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | New public API is runtime-layer only and does not expose runtime types from a lower layer. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member, subsystem, or pass added. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass added. |
| 6 | New frame-recipe dependencies are resource-driven or explicitly justified | n/a | No frame-recipe dependency changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | `ARCH-008` retires at `CPUContracted`; `Operational` is explicitly owned by `ARCH-012`. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No allowlist row, temporary shim, or compatibility exception added. |

## Findings -> Follow-ups

- None.

# Clean-workshop review — ARCH-009 kernel JobService

## Change Under Review

- Change: `ARCH-009` kernel `JobService` with CPU-pool submission,
  main-thread completion gating before event pump B, and maintenance-phase
  reaping.
- Trigger(s): changes runtime wiring/composition order.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` ran clean. `Runtime.JobService` imports core substrate and runtime events only. |
| 2 | CMake target links match layer policy | pass | No new target link edge; `ExtrinsicRuntime` owns the new runtime module. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The new public API is runtime-layer only and exposes no ECS registry/component, RHI, renderer, asset-service, or platform type. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member, subsystem, or pass added. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass added. |
| 6 | New frame-recipe dependencies are resource-driven or explicitly justified | n/a | No frame-recipe dependency changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | `ARCH-009` retires at `CPUContracted`; `Operational` is explicitly owned by `ARCH-012`, and `GpuQueue` is deferred to `RUNTIME-137`. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No allowlist row, temporary shim, or compatibility exception added. |

## Findings -> Follow-ups

- None.
