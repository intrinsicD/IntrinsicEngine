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

# Clean-workshop review — ARCH-010 kernel WorldRegistry

## Change Under Review

- Change: `ARCH-010` kernel `WorldRegistry` with boot-world creation,
  deferred active-world swaps, two-phase destroy, world-scoped job
  cancellation, and explicit `WorldHandle` render extraction plumbing.
- Trigger(s): changes runtime wiring/composition order.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` ran clean. `Runtime.WorldRegistry` imports ECS scene registry, runtime events, and runtime job service from the runtime composition layer. |
| 2 | CMake target links match layer policy | pass | No new target link edge; `ExtrinsicRuntime` owns the new runtime module interfaces and implementation unit. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The new public APIs are runtime-layer surfaces only: `WorldRegistry`, `WorldHandle`, kernel world events, and render extraction stats. No lower-layer module exposes runtime, ECS, renderer, platform, or asset-service ownership. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member, subsystem, or pass added. Render extraction only receives an explicit runtime world scope. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass added. |
| 6 | New frame-recipe dependencies are resource-driven or explicitly justified | n/a | No frame-recipe dependency changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | `ARCH-010` retires at `CPUContracted`; `Operational` is explicitly owned by `ARCH-012`. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No allowlist row, temporary shim, or compatibility exception added. |

## Findings -> Follow-ups

- None.

# Clean-workshop review — ARCH-011 RuntimeModule contract + ServiceRegistry

## Change Under Review

- Change: `ARCH-011` RuntimeModule composition seam — new
  `Extrinsic.Runtime.ServiceRegistry` and `Extrinsic.Runtime.Module` modules
  plus additive `Engine` wiring (`AddModule`/`EmplaceModule`, two-phase
  `OnRegister`→`OnResolve` boot, module sim-system application in the
  fixed-step graph, frame hooks at four phases, `EngineWillShutDown`
  announce-pump + reverse-order `OnShutdown`).
- Trigger(s): adds runtime modules; changes runtime wiring/composition order.
- Reviewer: Claude.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/repo/check_layering.py --root src --strict` ran clean (0 violations). `Runtime.ServiceRegistry` imports only `Core.FrameGraph` (plus `Core.Logging` in its impl); `Runtime.Module` imports core FrameGraph, ECS scene registry, and the runtime kernel substrate modules only. |
| 2 | CMake target links match layer policy | pass | No new target link edge; `ExtrinsicRuntime` owns both new module interfaces and their implementation units. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | New public API is runtime-layer only. `EngineSetup`/`FrameHookContext` expose no `Engine&` (ADR-0024 D13); `ServiceRegistry` stores borrowed `void*` keyed by compile-time `TypeToken` and surfaces nothing from a lower layer. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member, subsystem, or pass added. |
| 5 | New passes use typed IDs, not string routing | pass | Module sim systems declare FrameGraph Read/Write via `Core::TypeToken<T>()` and order by those tokens + named signals; pass names are diagnostic labels, not routing keys. |
| 6 | New frame-recipe dependencies are resource-driven or explicitly justified | n/a | No frame-recipe dependency changed (extension-pass registration is deferred to the D10 slot-contract task). |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | `ARCH-011` retires at `CPUContracted`; `Operational` is explicitly owned by `ARCH-012` (ClusteringModule proving extraction). |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No allowlist row, temporary shim, or compatibility exception added; the change is additive and existing `IApplication` behavior is unchanged. |

## Findings -> Follow-ups

- The C++ build and default CPU-supported test gate could not run in the cloud
  session (vcpkg tool download blocked by egress policy with a 403; no Ninja
  generator), so CI must confirm the build and the new
  `Test.RuntimeModuleContract.cpp` scenarios before merge. Tracked in the
  `ARCH-011` Status block, not a separate task.
