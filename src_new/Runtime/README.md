# Runtime

`src_new/Runtime` is the composition root for the `src_new` engine. It owns
the explicit instantiation order of subsystems, the canonical frame loop, and
deterministic startup/shutdown.

## Public module surface

| Module | Responsibility |
|---|---|
| `Extrinsic.Runtime.Engine` | Composition root, frame loop, subsystem wiring |
| `Extrinsic.Runtime.FrameClock` | Per-frame wall-clock management (clamped delta, resample after sleep) |
| `Extrinsic.Runtime.StreamingExecutor` | Persistent background task table for async streaming jobs |

## File inventory

```text
Runtime.Engine.cppm          — Engine + IApplication interface
Runtime.Engine.cpp           — implementation (RunFrame canonical loop)
Runtime.FrameClock.cppm      — FrameClock (header-only, no .cpp)
Runtime.StreamingExecutor.cppm — Streaming task API + state machine contract
Runtime.StreamingExecutor.cpp  — Streaming executor implementation
```

## Application contract

`IApplication` receives three callbacks per run:

```cpp
OnInitialize(engine)               // once, after all subsystems are live
OnSimTick(engine, fixedDt)         // 0..N times per frame (fixed 60 Hz)
OnVariableTick(engine, alpha, dt)  // once per frame, after all sim ticks
OnShutdown(engine)                 // once, before subsystem teardown
```

`alpha ∈ [0,1)` is the interpolation blend between the last committed tick
and the next. Use it for camera and position smoothing in `OnVariableTick`.

## Canonical frame loop (RunFrame phases)

```
Phase 1   Platform          PollEvents → BeginFrame(clock)
                            [minimized: WaitForEventsTimeout → Resample → return]
                            [resized:   WaitIdle → Resize(device, renderer)]

Phase 2   Fixed-step sim    accumulator += FrameDeltaClamped(maxFrameDelta)
                            while (accumulator >= fixedDt && substeps < maxSubSteps)
                              OnSimTick(fixedDt); accumulator -= fixedDt

Phase 3   Variable tick     OnVariableTick(alpha, dt)

Phase 4   Snapshot          RenderFrameInput { Alpha, Viewport }

Phase 5   BeginFrame        Renderer::BeginFrame(frame)  [skip frame on false]

Phase 6   Extract           Renderer::ExtractRenderWorld(renderInput) → RenderWorld

Phase 7   Prepare           Renderer::PrepareFrame(renderWorld)

Phase 8   Execute           Renderer::ExecuteFrame(frame, renderWorld)

Phase 9   EndFrame+Present  Renderer::EndFrame(frame) → completedGpuValue
                            Device::Present(frame)

Phase 10  Maintenance       Device::CollectCompletedTransfers()
                            StreamingExecutor::DrainCompletions()
                            StreamingExecutor::ApplyMainThreadResults()
                            AssetService::Tick()
                            Streaming compatibility bridge submit/reset
                            StreamingExecutor::PumpBackground(budget)
                            [future: MaintenanceService::CollectGarbage(completedGpuValue)]

Phase 11  Clock             FrameClock::EndFrame()
```

## Dependency direction

`Runtime` depends on `Core`, `Assets`, `ECS`, `Platform`, and `Graphics`.
It wires them together but does not itself own rendering or scene logic.
No subsystem is reached through a global — all dependencies are injected
by constructor or accessor.
