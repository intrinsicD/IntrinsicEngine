#pragma once

// Include-only Engine frame-loop helpers. Include after module imports in
// Runtime.Engine.cpp so these declarations remain implementation-local.

namespace Extrinsic::Runtime
{
    constexpr double kIdleSleepSeconds = 0.016; // ~60 Hz event wake

    [[nodiscard]] std::uint64_t ElapsedMicros(
        const std::chrono::steady_clock::time_point start) noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count());
    }

    struct RuntimeFrameContext
    {
        double FrameDeltaSeconds{0.0};
        double FixedStepAlpha{0.0};
        std::uint64_t FrameIndex{0};
        Graphics::RenderFrameInput RenderInput{};
        RuntimeRenderExtractionStats ExtractionStats{};
        EditorInputCaptureSnapshot EditorCapture{};
        std::uint32_t PooledFrontSlot{RenderWorldPool::kInvalidSlot};
    };


    struct PlatformFrameHooks final : Core::IPlatformFrameHooks
    {
        Platform::IWindow& Window;

        explicit PlatformFrameHooks(Platform::IWindow& window)
            : Window(window)
        {
        }

        void PollEvents() override { Window.PollEvents(); }
        [[nodiscard]] bool ShouldClose() const override
        {
            return Window.ShouldClose();
        }
        [[nodiscard]] bool IsMinimized() const override
        {
            return Window.IsMinimized();
        }
        void WaitForEventsTimeout(double seconds) override
        {
            Window.WaitForEventsTimeout(seconds);
        }
    };

    struct OperationalTransitionHooks final : Core::IOperationalTransitionHooks
    {
        RHI::IDevice& Device;
        Graphics::IRenderer& Renderer;
        bool& RendererOperational;

        OperationalTransitionHooks(RHI::IDevice& device,
                                   Graphics::IRenderer& renderer,
                                   bool& rendererOperational)
            : Device(device)
            , Renderer(renderer)
            , RendererOperational(rendererOperational)
        {
        }

        [[nodiscard]] bool IsDeviceOperational() const override { return Device.IsOperational(); }
        [[nodiscard]] bool IsRendererOperational() const override { return RendererOperational; }
        void WaitDeviceIdle() override { Device.WaitIdle(); }
        [[nodiscard]] bool RebuildRendererOperationalResources() override
        {
            return Renderer.RebuildOperationalResources(Device);
        }
        void MarkRendererOperational() override { RendererOperational = true; }
    };

    struct RuntimeRenderFrameHooks final : Core::IRenderFrameHooks
    {
        Graphics::IRenderer& Renderer;
        ECS::Scene::Registry& Scene;
        RenderExtractionCache& Extraction;
        Graphics::GpuAssetCache* GpuAssetCache;
        RenderWorldPool& Pool;
        bool SynchronousExtraction;
        RuntimeRenderExtractionStats& Stats;
        std::uint64_t FrameIndex;
        std::uint32_t& OutFrontSlot;
        RHI::FrameHandle& Frame;
        const Graphics::RenderFrameInput& Input;
        WorldHandle ActiveWorld;
        Graphics::RenderWorld& World;
        RuntimeFramePacingDiagnostics* Pacing;

        RuntimeRenderFrameHooks(Graphics::IRenderer& renderer,
                                ECS::Scene::Registry& scene,
                                RenderExtractionCache& extraction,
                                Graphics::GpuAssetCache* gpuAssetCache,
                                RenderWorldPool& pool,
                                const bool synchronousExtraction,
                                RuntimeRenderExtractionStats& stats,
                                std::uint64_t frameIndex,
                                std::uint32_t& outFrontSlot,
                                RHI::FrameHandle& frame,
                                const Graphics::RenderFrameInput& input,
                                WorldHandle activeWorld,
                                Graphics::RenderWorld& world,
                                RuntimeFramePacingDiagnostics* pacing)
            : Renderer(renderer)
            , Scene(scene)
            , Extraction(extraction)
            , GpuAssetCache(gpuAssetCache)
            , Pool(pool)
            , SynchronousExtraction(synchronousExtraction)
            , Stats(stats)
            , FrameIndex(frameIndex)
            , OutFrontSlot(outFrontSlot)
            , Frame(frame)
            , Input(input)
            , ActiveWorld(activeWorld)
            , World(world)
            , Pacing(pacing)
        {
        }

        bool BeginFrame() override
        {
            const auto begin = std::chrono::steady_clock::now();
            const bool result = Renderer.BeginFrame(Frame);
            if (Pacing != nullptr)
            {
                Pacing->RenderBeginFrameMicros = ElapsedMicros(begin);
            }
            return result;
        }
        void ExtractRenderWorld() override
        {
            const auto begin = std::chrono::steady_clock::now();
            // GRAPHICS-036C — producer half: acquire a back slot, write the
            // snapshot into it via ExtractAndSubmit, then publish it as the
            // front. AcquireBack only fails closed (kInvalidSlot) when the
            // pool is exhausted; in that case the previous front stays current
            // and we skip the publish so no in-flight slot is overwritten.
            const std::uint32_t backSlot = Pool.AcquireBack(FrameIndex);

            // RUNTIME-089 Slice B — mirror the runtime selection snapshot
            // into RenderWorld::Selection via the extraction batch. In
            // pipelined mode the renderer writes into the acquired back
            // slot while the consumer below reads the previous front slot.
            const std::uint32_t submitSlot =
                backSlot != RenderWorldPool::kInvalidSlot ? backSlot : 0u;
            if (backSlot != RenderWorldPool::kInvalidSlot)
            {
                Stats = Extraction.ExtractAndSubmit(Scene,
                                                     Renderer,
                                                     GpuAssetCache,
                                                     submitSlot,
                                                     ActiveWorld);
                Pool.PublishFront(backSlot);
            }
            else
            {
                Stats = Extraction.GetLastStats();
            }

            // Consumer half: synchronous mode preserves the existing
            // same-frame consume. Pipelined mode intentionally consumes the
            // previously published front (render-N-1) after extraction has
            // published N.
            OutFrontSlot = SynchronousExtraction
                ? Pool.AcquireFront(FrameIndex)
                : Pool.AcquirePreviousFront(FrameIndex);

            const std::uint32_t extractSlot =
                OutFrontSlot != RenderWorldPool::kInvalidSlot ? OutFrontSlot : submitSlot;
            World = Renderer.ExtractRenderWorld(Input, extractSlot);

            // GRAPHICS-036B — surface the pool's three counters on the
            // extraction stats for editor overlays / tests.
            MirrorRenderWorldPoolDiagnostics(Pool, Stats);
            if (Pacing != nullptr)
            {
                Pacing->RenderExtractionMicros = ElapsedMicros(begin);
            }
        }
        void PrepareFrame() override
        {
            const auto begin = std::chrono::steady_clock::now();
            Renderer.PrepareFrame(World);
            if (Pacing != nullptr)
            {
                Pacing->RenderPrepareMicros = ElapsedMicros(begin);
            }
        }
        void ExecuteFrame() override
        {
            const auto begin = std::chrono::steady_clock::now();
            Renderer.ExecuteFrame(Frame, World);
            if (Pacing != nullptr)
            {
                Pacing->RenderExecuteMicros = ElapsedMicros(begin);
            }
        }
        std::uint64_t EndFrame() override
        {
            const auto begin = std::chrono::steady_clock::now();
            const std::uint64_t result = Renderer.EndFrame(Frame);
            if (Pacing != nullptr)
            {
                Pacing->RenderEndFrameMicros = ElapsedMicros(begin);
            }
            return result;
        }
    };

    struct TransferHooks final : Core::ITransferFrameHooks
    {
        RHI::IDevice& Device;

        explicit TransferHooks(RHI::IDevice& device)
            : Device(device)
        {
        }

        void CollectCompletedTransfers() override
        {
            // GPU-side resource retirement, staging GC, readback processing.
            Device.GetTransferQueue().CollectCompleted();
        }
    };

    struct AssetHooks final : Core::IAssetFrameHooks
    {
        Core::IAssetFrameHooks*   AssetWorkflow;
        RHI::IDevice&             Device;
        RenderExtractionCache&    Extraction;
        Graphics::IRenderer&      Renderer;

        AssetHooks(Core::IAssetFrameHooks* assetWorkflow,
                   RHI::IDevice& device,
                   RenderExtractionCache& extraction,
                   Graphics::IRenderer& renderer)
            : AssetWorkflow(assetWorkflow)
            , Device(device)
            , Extraction(extraction)
            , Renderer(renderer)
        {
        }

        void TickAssets() override
        {
            if (AssetWorkflow != nullptr)
                AssetWorkflow->TickAssets();

            const std::uint64_t currentFrame = Device.GetGlobalFrameNumber();
            const std::uint32_t framesInFlight = Device.GetFramesInFlight();
            // GRAPHICS-030C: drive the procedural geometry cache's
            // deferred-retire window with the same CPU frame counter and
            // framesInFlight the asset cache uses. Final FreeGeometry calls
            // fall through to GpuWorld here.
            Extraction.TickProceduralGeometry(currentFrame, framesInFlight, Renderer);
            // RUNTIME-085 Slice C — mirror the same window for the
            // runtime-owned mesh-residency retire queue.
            Extraction.TickMeshGeometry(currentFrame, framesInFlight, Renderer);
            // RUNTIME-086 Slice B — and for the graph-residency queue.
            Extraction.TickGraphGeometry(currentFrame, framesInFlight, Renderer);
            // RUNTIME-087 — and for the point-cloud-residency queue.
            Extraction.TickPointCloudGeometry(currentFrame, framesInFlight, Renderer);
            // RUNTIME-088 Slice B — and for the mesh edge/vertex primitive
            // view residency queue (one queue for both view lanes).
            Extraction.TickMeshPrimitiveViewGeometry(currentFrame, framesInFlight, Renderer);
        }
    };

    [[nodiscard]] bool HasPendingPreRenderTransformFlush(
        const ECS::Scene::Registry& scene)
    {
        const entt::registry& raw = scene.Raw();
        const auto dirtyTransforms =
            raw.view<ECS::Components::Transform::IsDirtyTag>();
        if (dirtyTransforms.begin() != dirtyTransforms.end())
        {
            return true;
        }

        const auto worldUpdated =
            raw.view<ECS::Components::Transform::WorldUpdatedTag>();
        return worldUpdated.begin() != worldUpdated.end();
    }

    void RunFixedStepSimulationTicks(Engine& engine,
                                     IApplication& application,
                                     Core::FrameGraph& frameGraph,
                                     ECS::Scene::Registry& scene,
                                     double& accumulator,
                                     const double fixedDt,
                                     const int maxSubSteps,
                                     auto&& registerModuleSystems)
    {
        int substeps = 0;
        while (accumulator >= fixedDt && substeps < maxSubSteps)
        {
            application.OnSimTick(engine, fixedDt);

            // BUG-069 (restores BUG-066): register the promoted baseline ECS
            // systems FIRST. The core FrameGraph orients every resource hazard
            // edge from earlier- to later-inserted passes and preserves
            // insertion order for passes that touch the same resource, so a
            // module sim-system that reads a baseline output (e.g.
            // Transform::WorldMatrix) or waits on the baseline "TransformUpdate"
            // signal must be appended AFTER the bundle. Registering the bundle
            // last (RUNTIME-091) inserts such a module before its producer,
            // which reads the previous substep's data or fails to compile with
            // "no prior signaler" and drops the substep's passes.
            (void)RegisterPromotedEcsSystemBundle(frameGraph, scene);

            registerModuleSystems(frameGraph, scene, fixedDt);

            if (frameGraph.PassCount() > 0)
            {
                if (auto r = frameGraph.Compile(); r.has_value())
                {
                    if (auto exec = frameGraph.Execute(); !exec.has_value())
                    {
                        Core::Log::Error("[Runtime] FrameGraph Execute() failed: error={}",
                                         static_cast<int>(exec.error()));
                    }
                }
                else
                {
                    Core::Log::Error("[Runtime] FrameGraph Compile() failed: error={}",
                                     static_cast<int>(r.error()));
                }
            }

            if (auto reset = frameGraph.ResetForReplay();
                !reset.has_value())
            {
                Core::Log::Error(
                    "[Runtime] FrameGraph ResetForReplay() failed: error={}",
                    static_cast<int>(reset.error()));
            }

            accumulator -= fixedDt;
            ++substeps;
        }
    }

}
