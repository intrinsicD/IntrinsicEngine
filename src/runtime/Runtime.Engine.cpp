module;

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.Engine;

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
import Extrinsic.Backends.Vulkan;
#endif
import Extrinsic.Backends.Null;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.Core.IOBackend;
import Extrinsic.Core.Tasks;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.AssetGeometryIO;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AssetMeshNormals;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.AssetModelTextureIO;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.KMeansGpuBackend;
import Extrinsic.Runtime.KMeansGpuJobQueue;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Runtime.EcsSystemBundle;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTextureIOBridge;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Light;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.RigidBody;
import Extrinsic.ECS.Component.ShadowCaster;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Platform.Input;
import Geometry.Graph.IO;
import Geometry.Graph;
import Geometry.AABB;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.OBB;
import Geometry.PointCloud;
import Geometry.PointCloud.IO;
import Geometry.Properties;
import Geometry.Sphere;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr double kIdleSleepSeconds = 0.016; // ~60 Hz event wake
        constexpr int kGizmoMouseButton = 0;
        constexpr int kSelectionMouseButton = 0;

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
            std::uint32_t PooledFrontSlot{RenderWorldPool::kInvalidSlot};
        };

        void SubmitViewportSelectionClickForFrame(SelectionController& selection,
                                                  const Platform::Input::Context& input,
                                                  Core::Extent2D windowExtent,
                                                  Core::Extent2D viewport,
                                                  bool imguiCapturesMouse,
                                                  bool gizmoCapturesMouse) noexcept;
        void RebuildSelectedGizmoEntities(
            const SelectionController& selection,
            ECS::Scene::Registry& scene,
            std::vector<ECS::EntityHandle>& outSelected);
        void DriveGizmoInteractionForFrame(
            GizmoInteraction& gizmo,
            GizmoUndoStack& undo,
            ECS::Scene::Registry& scene,
            const Platform::Input::Context& input,
            const Graphics::CameraViewInput& cameraInput,
            Core::Extent2D windowExtent,
            Core::Extent2D viewport,
            bool imguiCapturesInput,
            std::span<const ECS::EntityHandle> selected);

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
            const SelectionController& Selection;
            RenderWorldPool& Pool;
            bool SynchronousExtraction;
            RuntimeRenderExtractionStats& Stats;
            std::uint64_t FrameIndex;
            std::uint32_t& OutFrontSlot;
            RHI::FrameHandle& Frame;
            const Graphics::RenderFrameInput& Input;
            std::span<const Graphics::TransformGizmoRenderPacket> TransformGizmos;
            Graphics::RenderWorld& World;
            RuntimeFramePacingDiagnostics* Pacing;

            RuntimeRenderFrameHooks(Graphics::IRenderer& renderer,
                                    ECS::Scene::Registry& scene,
                                    RenderExtractionCache& extraction,
                                    Graphics::GpuAssetCache* gpuAssetCache,
                                    const SelectionController& selection,
                                    RenderWorldPool& pool,
                                    const bool synchronousExtraction,
                                    RuntimeRenderExtractionStats& stats,
                                    std::uint64_t frameIndex,
                                    std::uint32_t& outFrontSlot,
                                    RHI::FrameHandle& frame,
                                    const Graphics::RenderFrameInput& input,
                                    std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos,
                                    Graphics::RenderWorld& world,
                                    RuntimeFramePacingDiagnostics* pacing)
                : Renderer(renderer)
                , Scene(scene)
                , Extraction(extraction)
                , GpuAssetCache(gpuAssetCache)
                , Selection(selection)
                , Pool(pool)
                , SynchronousExtraction(synchronousExtraction)
                , Stats(stats)
                , FrameIndex(frameIndex)
                , OutFrontSlot(outFrontSlot)
                , Frame(frame)
                , Input(input)
                , TransformGizmos(transformGizmos)
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
                                                         &Selection,
                                                         submitSlot,
                                                         TransformGizmos);
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

        struct StreamingHooks final : Core::IStreamingFrameHooks
        {
            static constexpr std::uint32_t kApplyBudgetPerFrame = 8u;

            StreamingExecutor& Executor;
            DerivedJobRegistry* DerivedJobs;

            explicit StreamingHooks(StreamingExecutor& executor,
                                    DerivedJobRegistry* derivedJobs)
                : Executor(executor)
                , DerivedJobs(derivedJobs)
            {
            }

            void DrainCompletions() override
            {
                if (DerivedJobs != nullptr)
                {
                    DerivedJobs->DrainCompletions();
                    DerivedJobs->DrainReadbacks();
                    return;
                }
                Executor.DrainCompletions();
            }
            void ApplyMainThreadResults() override
            {
                if (DerivedJobs != nullptr)
                {
                    (void)DerivedJobs->ApplyMainThreadResults(kApplyBudgetPerFrame);
                    return;
                }
                (void)Executor.ApplyMainThreadResults(kApplyBudgetPerFrame);
            }
            void SubmitFrameWork() override {}
            void PumpBackground(std::uint32_t maxLaunches) override
            {
                if (DerivedJobs != nullptr)
                {
                    DerivedJobs->Pump(maxLaunches);
                    return;
                }
                Executor.PumpBackground(maxLaunches);
            }
        };

        struct AssetHooks final : Core::IAssetFrameHooks
        {
            Assets::AssetService&     AssetService;
            Graphics::GpuAssetCache*  GpuAssetCache;
            AssetModelSceneHandoff*   ModelSceneHandoff;
            RHI::IDevice&             Device;
            RenderExtractionCache&    Extraction;
            Graphics::IRenderer&      Renderer;

            AssetHooks(Assets::AssetService& assetService,
                       Graphics::GpuAssetCache* gpuAssetCache,
                       AssetModelSceneHandoff* modelSceneHandoff,
                       RHI::IDevice& device,
                       RenderExtractionCache& extraction,
                       Graphics::IRenderer& renderer)
                : AssetService(assetService)
                , GpuAssetCache(gpuAssetCache)
                , ModelSceneHandoff(modelSceneHandoff)
                , Device(device)
                , Extraction(extraction)
                , Renderer(renderer)
            {
            }

            void TickAssets() override
            {
                // Asset service main-thread tick: advances state machines, fires
                // AssetEventBus::Ready / Reloaded / Destroyed callbacks. The
                // cache subscribed in Engine::Initialize observes those events
                // synchronously during this Tick.
                AssetService.Tick();
                const std::uint64_t currentFrame = Device.GetGlobalFrameNumber();
                const std::uint32_t framesInFlight = Device.GetFramesInFlight();
                if (GpuAssetCache)
                {
                    GpuAssetCache->Tick(currentFrame, framesInFlight);
                }
                if (ModelSceneHandoff)
                {
                    static_cast<void>(
                        ModelSceneHandoff->ResolvePendingMaterialTextureBindings());
                }
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

        [[nodiscard]] std::optional<PickReadbackContext> BuildPickReadbackContextForFrame(
            const Graphics::RenderFrameInput& renderInput,
            const Platform::Extent2D& viewport)
        {
            const Graphics::CameraViewSnapshot pickCamera =
                Graphics::BuildCameraViewSnapshot(renderInput.Camera,
                                                  viewport,
                                                  renderInput.Pick);
            if (!pickCamera.Valid)
                return std::nullopt;

            const std::uint32_t viewportWidth =
                viewport.Width > 0 ? static_cast<std::uint32_t>(viewport.Width) : 0u;
            const std::uint32_t viewportHeight =
                viewport.Height > 0 ? static_cast<std::uint32_t>(viewport.Height) : 0u;
            PickReadbackContext context{};
            context.InverseViewProjection = pickCamera.InverseViewProjection;
            context.ViewportWidth         = viewportWidth;
            context.ViewportHeight        = viewportHeight;
            context.HasWorldRay           = pickCamera.HasPickRay;
            context.WorldRayOrigin        = pickCamera.PickRayOrigin;
            context.WorldRayDirection     = pickCamera.PickRayDirection;
            // `2 / (|P[1][1]| * H)`: for perspective ([1][1] =
            // ±1/tan(fovY/2)) this is the world-units-per-pixel at view depth
            // 1; for orthographic ([1][1] = ±2/orthoHeight, e.g. the promoted
            // TopDownCameraController) the same expression is the
            // depth-invariant orthoHeight/H, and the flag tells refinement not
            // to scale it by the hit distance. The sign carries the Vulkan Y
            // flip in both cases.
            const float projectionScaleY =
                std::abs(renderInput.Camera.Projection[1][1]);
            if (projectionScaleY > 0.000001f && viewportHeight > 0u)
            {
                context.WorldUnitsPerPixelAtUnitDepth =
                    2.0f / (projectionScaleY *
                            static_cast<float>(viewportHeight));
            }
            context.OrthographicProjection =
                IsOrthographicProjection(renderInput.Camera.Projection);
            return context;
        }

        void RememberPickReadbackContextForFrame(
            auto& inFlightPickContexts,
            const std::uint64_t sequence,
            const PickReadbackContext& context)
        {
            constexpr std::size_t kMaxInFlightPickContexts = 32u;
            if (inFlightPickContexts.size() >= kMaxInFlightPickContexts)
                inFlightPickContexts.erase(inFlightPickContexts.begin());

            inFlightPickContexts.emplace_back();
            inFlightPickContexts.back().Sequence = sequence;
            inFlightPickContexts.back().Context = context;
        }

        void DrainPendingSelectionPickForFrame(
            SelectionController& selection,
            Graphics::SelectionSystem& selectionSystem,
            auto& inFlightPickContexts,
            const Platform::Extent2D& viewport,
            Graphics::RenderFrameInput& renderInput)
        {
            const std::optional<PendingSelectionPick> pick =
                selection.ConsumePendingPick();
            if (!pick.has_value())
                return;

            renderInput.HasPendingPick = true;
            renderInput.Pick = Graphics::PickPixelRequest{
                .X        = pick->PixelX,
                .Y        = pick->PixelY,
                .Pending  = true,
                .Sequence = pick->Sequence,
            };
            selectionSystem.RequestPick(Graphics::PickRequest{
                .PixelX = pick->PixelX,
                .PixelY = pick->PixelY,
            });

            if (const std::optional<PickReadbackContext> context =
                    BuildPickReadbackContextForFrame(renderInput, viewport))
            {
                RememberPickReadbackContextForFrame(
                    inFlightPickContexts,
                    pick->Sequence,
                    *context);
            }
        }

        void ApplySelectionReadbackToController(
            SelectionController& selection,
            ECS::Scene::Registry& scene,
            const Graphics::PickReadbackResult& result)
        {
            if (result.Sequence != 0u)
            {
                if (result.Hit)
                    selection.ConsumeHit(scene, result.StableEntityId, result.Sequence);
                else
                    selection.ConsumeNoHit(scene, result.Sequence);
                return;
            }

            if (result.Hit)
                selection.ConsumeHit(scene, result.StableEntityId);
            else
                selection.ConsumeNoHit(scene);
        }

        void RefineSelectionReadbackForFrame(
            ECS::Scene::Registry& scene,
            const Graphics::PickReadbackResult& result,
            auto& inFlightPickContexts,
            std::optional<PrimitiveSelectionResult>& lastRefinedPrimitive,
            std::uint64_t& lastRefinedPrimitiveGeneration)
        {
            const PickReadbackContext* pickContext = nullptr;
            auto contextIt = inFlightPickContexts.end();
            if (result.Sequence != 0u)
            {
                contextIt = std::find_if(
                    inFlightPickContexts.begin(),
                    inFlightPickContexts.end(),
                    [seq = result.Sequence](const auto& entry)
                    { return entry.Sequence == seq; });
                if (contextIt != inFlightPickContexts.end())
                    pickContext = &contextIt->Context;
            }

            lastRefinedPrimitive =
                RefinePickReadbackResult(scene, result, pickContext);
            ++lastRefinedPrimitiveGeneration;
            if (contextIt != inFlightPickContexts.end())
                inFlightPickContexts.erase(contextIt);
        }

        void DrainCompletedSelectionReadbacksForFrame(
            Graphics::SelectionSystem& selectionSystem,
            SelectionController& selection,
            ECS::Scene::Registry& scene,
            auto& inFlightPickContexts,
            std::optional<PrimitiveSelectionResult>& lastRefinedPrimitive,
            std::uint64_t& lastRefinedPrimitiveGeneration)
        {
            while (const std::optional<Graphics::PickReadbackResult> result =
                       selectionSystem.PopPickResult())
            {
                ApplySelectionReadbackToController(selection, scene, *result);
                RefineSelectionReadbackForFrame(scene,
                                                *result,
                                                inFlightPickContexts,
                                                lastRefinedPrimitive,
                                                lastRefinedPrimitiveGeneration);
            }
        }

        void RunFixedStepSimulationTicks(Engine& engine,
                                         IApplication& application,
                                         Core::FrameGraph& frameGraph,
                                         ECS::Scene::Registry& scene,
                                         double& accumulator,
                                         const double fixedDt,
                                         const int maxSubSteps)
        {
            int substeps = 0;
            while (accumulator >= fixedDt && substeps < maxSubSteps)
            {
                application.OnSimTick(engine, fixedDt);

                // RUNTIME-091: register the promoted baseline ECS systems after
                // the app has had a chance to add its own fixed-step passes.
                (void)RegisterPromotedEcsSystemBundle(frameGraph, scene);

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
                    frameGraph.Reset();
                }

                accumulator -= fixedDt;
                ++substeps;
            }
        }

        void PopulateMainCameraForFrame(
            const Core::Config::EngineConfig& config,
            CameraControllerRegistry& cameras,
            const std::optional<Graphics::CameraViewInput>& referenceCamera,
            const Platform::IWindow& window,
            const Platform::Extent2D& viewport,
            const double frameDt,
            const bool imguiCapturesInput,
            Graphics::RenderFrameInput& renderInput)
        {
            if (!config.Camera.Enabled)
                return;

            ICameraController* controller = cameras.ResolveOrNull(CameraControllerSlot::Main);
            if (controller == nullptr)
            {
                const Graphics::CameraViewInput seed = referenceCamera.has_value()
                    ? BuildReferenceCameraViewInput(*referenceCamera, viewport.Width, viewport.Height)
                    : Graphics::CameraViewInput{};
                cameras.Register(
                    CameraControllerSlot::Main,
                    CreateCameraController(config.Camera.Controller, seed));
                controller = cameras.ResolveOrNull(CameraControllerSlot::Main);
            }

            if (controller == nullptr)
                return;

            if (!imguiCapturesInput)
                controller->Update(window.GetInput(), frameDt);

            renderInput.Camera = controller->GetView(viewport);
            renderInput.Camera.ExplicitCameraTransition =
                cameras.ConsumeCameraTransition(CameraControllerSlot::Main);
        }

        void DriveGizmoAndSelectionInputForFrame(
            GizmoInteraction& gizmoInteraction,
            GizmoUndoStack& gizmoUndoStack,
            ECS::Scene::Registry& scene,
            SelectionController& selection,
            const Platform::IWindow& window,
            const Platform::Extent2D& viewport,
            const bool imguiCapturesInput,
            const bool imguiCapturesMouse,
            std::vector<ECS::EntityHandle>& gizmoSelectedEntities,
            const Graphics::CameraViewInput& camera)
        {
            RebuildSelectedGizmoEntities(selection, scene, gizmoSelectedEntities);
            const Platform::Extent2D windowExtent = window.GetWindowExtent();
            DriveGizmoInteractionForFrame(gizmoInteraction,
                                          gizmoUndoStack,
                                          scene,
                                          window.GetInput(),
                                          camera,
                                          windowExtent,
                                          viewport,
                                          imguiCapturesInput,
                                          gizmoSelectedEntities);
            SubmitViewportSelectionClickForFrame(selection,
                                                 window.GetInput(),
                                                 windowExtent,
                                                 viewport,
                                                 imguiCapturesMouse,
                                                 gizmoInteraction.IsDragging());
        }

        void FocusMainCameraOnSelectionForFrame(
            const Core::Config::EngineConfig& config,
            CameraControllerRegistry& cameras,
            SelectionController& selection,
            ECS::Scene::Registry& scene,
            const Platform::IWindow& window,
            const Platform::Extent2D& viewport,
            const bool imguiCapturesKeyboard,
            Graphics::RenderFrameInput& renderInput)
        {
            if (!config.Camera.Enabled || imguiCapturesKeyboard ||
                !window.GetInput().IsKeyJustPressed(Platform::Input::Key::F) ||
                !FocusCameraOnSelection(cameras,
                                        selection,
                                        scene,
                                        CameraControllerSlot::Main))
            {
                return;
            }

            if (ICameraController* focused =
                    cameras.ResolveOrNull(CameraControllerSlot::Main))
            {
                renderInput.Camera = focused->GetView(viewport);
                renderInput.Camera.ExplicitCameraTransition =
                    cameras.ConsumeCameraTransition(CameraControllerSlot::Main);
            }
        }

        [[nodiscard]] Graphics::Components::RenderPoints::RenderType ToRenderPointType(
            const MeshVertexViewRenderMode mode) noexcept
        {
            namespace G = Graphics::Components;
            switch (mode)
            {
            case MeshVertexViewRenderMode::FlatCircle:
                return G::RenderPoints::RenderType::Flat;
            case MeshVertexViewRenderMode::SurfaceAlignedCircle:
                return G::RenderPoints::RenderType::Surfel;
            case MeshVertexViewRenderMode::ImpostorSphere:
                return G::RenderPoints::RenderType::Sphere;
            }
            return G::RenderPoints::RenderType::Sphere;
        }

        [[nodiscard]] MeshVertexViewRenderMode ToMeshVertexViewRenderMode(
            const Graphics::Components::RenderPoints::RenderType type) noexcept
        {
            namespace G = Graphics::Components;
            switch (type)
            {
            case G::RenderPoints::RenderType::Flat:
                return MeshVertexViewRenderMode::FlatCircle;
            case G::RenderPoints::RenderType::Surfel:
                return MeshVertexViewRenderMode::SurfaceAlignedCircle;
            case G::RenderPoints::RenderType::Sphere:
                return MeshVertexViewRenderMode::ImpostorSphere;
            }
            return MeshVertexViewRenderMode::ImpostorSphere;
        }

        // RUNTIME-070: runtime-baked fallback texture bytes for GpuAssetCache.
        // A 4×4 RGBA8_UNORM magenta-and-black checkerboard repeated from a 2×2
        // base pattern. The cache never reads files; runtime owns the bytes.
        // Layout: row-major, top-left origin, RGBA8 with alpha 0xFF so sampled
        // fallback assets are visually unambiguous in development builds.
        consteval std::array<std::byte, 4 * 4 * 4> MakeFallbackTextureBytes() noexcept
        {
            std::array<std::byte, 4 * 4 * 4> bytes{};
            for (std::size_t y = 0; y < 4; ++y)
            {
                for (std::size_t x = 0; x < 4; ++x)
                {
                    const bool magenta = (((x / 2) ^ (y / 2)) & 1u) == 0u;
                    const std::size_t base = (y * 4 + x) * 4;
                    bytes[base + 0] = static_cast<std::byte>(magenta ? 0xFF : 0x00);
                    bytes[base + 1] = static_cast<std::byte>(0x00);
                    bytes[base + 2] = static_cast<std::byte>(magenta ? 0xFF : 0x00);
                    bytes[base + 3] = static_cast<std::byte>(0xFF);
                }
            }
            return bytes;
        }

        constexpr auto kFallbackTextureBytes = MakeFallbackTextureBytes();

        [[nodiscard]] Graphics::GpuTextureFallbackDesc BuildFallbackTextureDesc() noexcept
        {
            Graphics::GpuTextureFallbackDesc desc{};
            desc.Bytes = std::span<const std::byte>(kFallbackTextureBytes);
            desc.Desc.Width = 4;
            desc.Desc.Height = 4;
            desc.Desc.MipLevels = 1;
            desc.Desc.Fmt = RHI::Format::RGBA8_UNORM;
            desc.Desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
            desc.Desc.DebugName = "gpu-asset-fallback-texture";
            desc.SamplerDesc.MagFilter = RHI::FilterMode::Nearest;
            desc.SamplerDesc.MinFilter = RHI::FilterMode::Nearest;
            desc.SamplerDesc.MipFilter = RHI::MipmapMode::Nearest;
            desc.SamplerDesc.AddressU = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.AddressV = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.AddressW = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.DebugName = "gpu-asset-fallback-sampler";
            return desc;
        }

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
        constexpr bool kPromotedVulkanAvailable = true;
#else
        constexpr bool kPromotedVulkanAvailable = false;
#endif

        std::unique_ptr<RHI::IDevice> CreateDevice(
            const Core::Config::RenderConfig& config)
        {
            const RuntimeDeviceSelection selection = SelectRuntimeDeviceBackend(
                config,
                kPromotedVulkanAvailable);
            if (selection.UsePromotedVulkanDevice)
            {
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
                Core::Log::Warn("[Runtime] Promoted Vulkan device selected; backend remains fail-closed until the first clean default-recipe validation promotes it.");
                return Backends::Vulkan::CreateVulkanDevice();
#endif
            }

            if (config.EnablePromotedVulkanDevice && !kPromotedVulkanAvailable)
            {
                Core::Log::Warn("[Runtime] Promoted Vulkan device requested but not compiled into this build; using Null device fallback.");
            }

            // Vulkan execution is opt-in during GRAPHICS-018. The default path
            // routes through the Null stub so IDevice::IsOperational() remains
            // false and resource managers surface DeviceNotOperational rather
            // than faking GPU work.
            return Backends::Null::CreateNullDevice();
        }

        [[nodiscard]] std::uint64_t Delta(
            const std::uint64_t after,
            const std::uint64_t before) noexcept
        {
            return after >= before ? after - before : 0u;
        }

        [[nodiscard]] std::optional<std::string> FindCommandLineEngineConfigPath(
            const std::span<const std::string_view> args,
            const std::string_view flag)
        {
            if (flag.empty())
            {
                return std::nullopt;
            }

            const std::string equalsPrefix = std::string{flag} + "=";
            for (std::size_t index = 1; index < args.size(); ++index)
            {
                const std::string_view arg = args[index];
                if (arg == flag)
                {
                    if (index + 1 < args.size() && !args[index + 1].empty())
                    {
                        return std::string{args[index + 1]};
                    }
                    return std::nullopt;
                }
                if (arg.starts_with(equalsPrefix))
                {
                    const std::string_view value = arg.substr(equalsPrefix.size());
                    if (!value.empty())
                    {
                        return std::string{value};
                    }
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> FindEnvironmentEngineConfigPath(
            const std::string_view variableName)
        {
            if (variableName.empty())
            {
                return std::nullopt;
            }
            const char* value = std::getenv(std::string{variableName}.c_str());
            if (value == nullptr || value[0] == '\0')
            {
                return std::nullopt;
            }
            return std::string{value};
        }

        [[nodiscard]] bool ExistingFilePath(const std::string_view path)
        {
            if (path.empty())
            {
                return false;
            }
            std::error_code ec{};
            return std::filesystem::is_regular_file(std::filesystem::path{path}, ec) && !ec;
        }

        [[nodiscard]] Core::Result DrainAssetImportEvents(
            Assets::AssetService& service,
            const Assets::AssetId asset)
        {
            return service.CompleteCpuLoadAndFlushEvent(asset);
        }

        [[nodiscard]] Core::ErrorCode NormalizeImportError(
            const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::Success
                ? Core::ErrorCode::Unknown
                : error;
        }

        [[nodiscard]] bool IsTextureUploadDeferral(
            const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::DeviceNotOperational
                || error == Core::ErrorCode::ResourceBusy;
        }

        [[nodiscard]] std::string FileNameFromPath(const std::string_view path)
        {
            if (path.empty())
            {
                return {};
            }

            const std::size_t slash = path.find_last_of("/\\");
            const std::size_t begin = slash == std::string_view::npos
                ? 0u
                : slash + 1u;
            if (begin >= path.size())
            {
                return {};
            }
            return std::string(path.substr(begin));
        }

        template <typename Component>
        void CopySerializableComponent(const entt::registry& sourceRaw,
                                       entt::registry& destinationRaw,
                                       const ECS::EntityHandle source,
                                       const ECS::EntityHandle destination)
        {
            if (const auto* component = sourceRaw.try_get<Component>(source))
            {
                destinationRaw.emplace_or_replace<Component>(
                    destination,
                    *component);
            }
        }

        template <typename Component>
        void CopySerializableTag(const entt::registry& sourceRaw,
                                 entt::registry& destinationRaw,
                                 const ECS::EntityHandle source,
                                 const ECS::EntityHandle destination)
        {
            if (sourceRaw.all_of<Component>(source))
            {
                destinationRaw.emplace_or_replace<Component>(destination);
            }
        }

        void CopySerializableHierarchy(
            const entt::registry& sourceRaw,
            entt::registry& destinationRaw,
            const std::unordered_map<ECS::EntityHandle, ECS::EntityHandle>& remap,
            const ECS::EntityHandle source,
            const ECS::EntityHandle destination)
        {
            namespace ECSC = ECS::Components;

            const auto* hierarchy =
                sourceRaw.try_get<ECSC::Hierarchy::Component>(source);
            if (hierarchy == nullptr ||
                hierarchy->Parent == ECS::InvalidEntityHandle)
            {
                return;
            }

            const auto parent = remap.find(hierarchy->Parent);
            if (parent == remap.end())
            {
                return;
            }

            destinationRaw.emplace_or_replace<ECSC::Hierarchy::Component>(
                destination,
                ECSC::Hierarchy::Component{.Parent = parent->second});
        }

        void SnapshotSerializableScene(const ECS::Scene::Registry& source,
                                       ECS::Scene::Registry& destination)
        {
            namespace ECSC = ECS::Components;
            namespace GS = ECS::Components::GeometrySources;
            namespace G = Graphics::Components;
            namespace Sel = ECS::Components::Selection;

            const entt::registry& sourceRaw = source.Raw();
            entt::registry& destinationRaw = destination.Raw();

            std::vector<ECS::EntityHandle> entities;
            entities.reserve(sourceRaw.storage<entt::entity>()->size());
            for (const entt::entity entity : sourceRaw.view<entt::entity>())
            {
                entities.push_back(entity);
            }
            std::sort(entities.begin(),
                      entities.end(),
                      [](const ECS::EntityHandle lhs,
                         const ECS::EntityHandle rhs)
                      {
                          return static_cast<std::uint32_t>(lhs) <
                                 static_cast<std::uint32_t>(rhs);
                      });

            std::unordered_map<ECS::EntityHandle, ECS::EntityHandle> remap;
            remap.reserve(entities.size());
            for (const ECS::EntityHandle sourceEntity : entities)
            {
                remap.emplace(sourceEntity, destination.Create());
            }

            for (const ECS::EntityHandle sourceEntity : entities)
            {
                const ECS::EntityHandle destinationEntity =
                    remap.at(sourceEntity);

                CopySerializableComponent<ECSC::MetaData>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::StableId>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableTag<Sel::SelectableTag>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Transform::Component>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableHierarchy(
                    sourceRaw,
                    destinationRaw,
                    remap,
                    sourceEntity,
                    destinationEntity);

                CopySerializableComponent<G::RenderSurface>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<G::RenderEdges>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<G::RenderPoints>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<G::VisualizationConfig>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<G::VisualizationLaneOverrides>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);

                CopySerializableComponent<GS::Vertices>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<GS::Edges>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<GS::Halfedges>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<GS::Faces>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<GS::Nodes>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableTag<GS::HasMeshTopology>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableTag<GS::HasGraphTopology>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);

                CopySerializableComponent<ProgressivePresentationBindings>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);

                CopySerializableTag<ECSC::Lights::LightTag>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Lights::DirectionalLight>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Lights::PointLight>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Lights::SpotLight>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Lights::AmbientLight>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableTag<ECSC::Shadows::CasterTag>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Collider::Component>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::RigidBody::Component>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::SpatialDebugBinding>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::AssetInstance::Source>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
            }
        }

        [[nodiscard]] std::string GeometryEntityName(
            const std::string_view path,
            const Assets::AssetPayloadKind kind)
        {
            std::string name = FileNameFromPath(path);
            if (!name.empty())
            {
                return name;
            }
            name = "Imported";
            name += Assets::DebugNameForAssetPayloadKind(kind);
            return name;
        }

        [[nodiscard]] Graphics::Components::VisualizationConfig
            ImportedGeometryVisualization() noexcept
        {
            Graphics::Components::VisualizationConfig visualization{};
            visualization.Source =
                Graphics::Components::VisualizationConfig::ColorSource::UniformColor;
            visualization.Color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
            return visualization;
        }

        [[nodiscard]] Graphics::Components::VisualizationConfig
            ImportedMeshVisualization() noexcept
        {
            Graphics::Components::VisualizationConfig visualization =
                ImportedGeometryVisualization();
            visualization.Source =
                Graphics::Components::VisualizationConfig::ColorSource::Material;
            return visualization;
        }

        struct DecodedMeshImport
        {
            Geometry::MeshIO::MeshIOResult Payload{};
        };

        struct DecodedGraphImport
        {
            Geometry::GraphIO::GraphIOResult Payload{};
        };

        struct DecodedPointCloudImport
        {
            Geometry::PointCloudIO::PointCloudIOResult Payload{};
        };

        using DecodedGeometryImportPayload =
            std::variant<DecodedMeshImport, DecodedGraphImport, DecodedPointCloudImport>;

        struct DecodedGeometryImport
        {
            std::string Path{};
            Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
            DecodedGeometryImportPayload Payload{};
        };

        struct DroppedGeometryImportState
        {
            RuntimeAssetIngestHandle IngestHandle{};
            RuntimeAssetImportRequest Request{};
            std::optional<DecodedGeometryImport> Decoded{};
            Core::ErrorCode Error{Core::ErrorCode::Unknown};
        };

        using DecodedModelTextureImportPayload =
            std::variant<Assets::AssetModelScenePayload,
                         Assets::AssetTexture2DPayload>;

        struct DecodedModelTextureImport
        {
            std::string Path{};
            Assets::AssetPayloadKind PayloadKind{
                Assets::AssetPayloadKind::Unknown};
            DecodedModelTextureImportPayload Payload{};
        };

        struct DroppedModelTextureImportState
        {
            RuntimeAssetIngestHandle IngestHandle{};
            RuntimeAssetImportRequest Request{};
            std::optional<DecodedModelTextureImport> Decoded{};
            Core::ErrorCode Error{Core::ErrorCode::Unknown};
        };

        struct QueuedSceneLoadState
        {
            std::string Path{};
            StreamingTaskHandle Task{};
            ECS::Scene::Registry LoadedScene{};
            std::optional<SceneDeserializationResult> Result{};
            Core::ErrorCode Error{Core::ErrorCode::Unknown};
        };

        struct QueuedSceneSaveState
        {
            std::string Path{};
            StreamingTaskHandle Task{};
            ECS::Scene::Registry Snapshot{};
            std::optional<SceneSerializationResult> Result{};
            Core::ErrorCode Error{Core::ErrorCode::Unknown};
        };

        struct GeometryImportBounds
        {
            glm::vec3 Min{0.0f};
            glm::vec3 Max{0.0f};
            bool Valid{false};
        };

        struct MaterializedGeometryImport
        {
            RuntimeAssetImportResult Result{};
            std::optional<GeometryImportBounds> Bounds{};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        };

        struct DirectMeshGeneratedTextureResult
        {
            Assets::AssetId NormalTexture{};
            std::uint64_t GeneratedTextureAssetsCreated{0u};
            std::uint64_t GeneratedTextureUploadRequests{0u};
        };

        struct DirectMeshPostProcessState
        {
            std::string Path{};
            Geometry::MeshIO::MeshIOResult Payload{};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::optional<RuntimeMeshMaterializationResult> Materialized{};
            std::optional<Assets::AssetTexture2DPayload> GeneratedNormalTexture{};
        };

        [[nodiscard]] RuntimeAssetIngestRequest MakeRuntimeAssetIngestRequest(
            const RuntimeAssetImportRequest& request,
            const RuntimeAssetIngestSource source,
            const Assets::AssetId existingAsset = {})
        {
            return RuntimeAssetIngestRequest{
                .Source = source,
                .Path = request.Path,
                .PayloadKind = request.PayloadKind,
                .ExistingAsset = existingAsset,
            };
        }

        [[nodiscard]] RuntimeAssetIngestResult ToRuntimeAssetIngestResult(
            const RuntimeAssetImportResult& result) noexcept
        {
            return RuntimeAssetIngestResult{
                .PayloadKind = result.PayloadKind,
                .Asset = result.Asset,
                .PrimitiveEntitiesCreated =
                    static_cast<std::uint32_t>(result.PrimitiveEntitiesCreated),
                .EmbeddedTextureAssetsCreated =
                    static_cast<std::uint32_t>(result.EmbeddedTextureAssetsCreated),
                .GeneratedTextureAssetsCreated =
                    static_cast<std::uint32_t>(result.GeneratedTextureAssetsCreated),
                .TextureUploadRequests =
                    static_cast<std::uint32_t>(result.TextureUploadRequests),
                .MaterializedModelScene = result.MaterializedModelScene,
                .RequestedTextureUpload = result.RequestedTextureUpload,
            };
        }

        [[nodiscard]] RuntimeAssetIngestDiagnostic DiagnosticForImportError(
            const Core::ErrorCode error) noexcept
        {
            switch (error)
            {
            case Core::ErrorCode::FileNotFound:
                return RuntimeAssetIngestDiagnostic::MissingFile;
            case Core::ErrorCode::AssetUnsupportedFormat:
                return RuntimeAssetIngestDiagnostic::UnsupportedExtension;
            case Core::ErrorCode::AssetLoaderMissing:
                return RuntimeAssetIngestDiagnostic::CallbackFailed;
            case Core::ErrorCode::AssetDecodeFailed:
                return RuntimeAssetIngestDiagnostic::DecodeFailed;
            case Core::ErrorCode::AssetInvalidData:
                return RuntimeAssetIngestDiagnostic::MaterializationFailed;
            case Core::ErrorCode::InvalidPath:
                return RuntimeAssetIngestDiagnostic::MissingPath;
            case Core::ErrorCode::ResourceBusy:
                return RuntimeAssetIngestDiagnostic::DuplicateActiveRequest;
            default:
                break;
            }
            return RuntimeAssetIngestDiagnostic::DecodeFailed;
        }

        [[nodiscard]] RuntimeAssetIngestDiagnostic DiagnosticForImportError(
            const Core::ErrorCode error,
            const RuntimeAssetIngestSource source) noexcept
        {
            if (source == RuntimeAssetIngestSource::Reimport &&
                (error == Core::ErrorCode::ResourceNotFound ||
                 error == Core::ErrorCode::TypeMismatch ||
                 error == Core::ErrorCode::AssetTypeMismatch ||
                 error == Core::ErrorCode::InvalidArgument))
            {
                return RuntimeAssetIngestDiagnostic::InvalidReimportTarget;
            }
            return DiagnosticForImportError(error);
        }

        [[nodiscard]] Core::ErrorCode ErrorFromIngestTransition(
            const RuntimeAssetIngestTransition& transition) noexcept
        {
            if (transition.Error != Core::ErrorCode::Success)
                return transition.Error;
            switch (transition.Diagnostic)
            {
            case RuntimeAssetIngestDiagnostic::None:
                return Core::ErrorCode::Success;
            case RuntimeAssetIngestDiagnostic::MissingPath:
                return Core::ErrorCode::InvalidPath;
            case RuntimeAssetIngestDiagnostic::MissingFile:
                return Core::ErrorCode::FileNotFound;
            case RuntimeAssetIngestDiagnostic::DuplicateActiveRequest:
                return Core::ErrorCode::ResourceBusy;
            case RuntimeAssetIngestDiagnostic::UnknownHandle:
                return Core::ErrorCode::ResourceNotFound;
            default:
                return Core::ErrorCode::InvalidState;
            }
        }

        [[nodiscard]] bool StreamingTaskStateCanCancel(
            const StreamingTaskState state) noexcept
        {
            switch (state)
            {
            case StreamingTaskState::Pending:
            case StreamingTaskState::Ready:
            case StreamingTaskState::Running:
            case StreamingTaskState::WaitingForReadback:
                return true;
            case StreamingTaskState::WaitingForMainThreadApply:
            case StreamingTaskState::WaitingForGpuUpload:
            case StreamingTaskState::Complete:
            case StreamingTaskState::Failed:
            case StreamingTaskState::Cancelled:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool QueueStageCanUseStreamingCancellation(
            const RuntimeAssetImportQueueStage stage) noexcept
        {
            switch (stage)
            {
            case RuntimeAssetImportQueueStage::DecodeQueued:
            case RuntimeAssetImportQueueStage::Decoding:
                return true;
            case RuntimeAssetImportQueueStage::Queued:
            case RuntimeAssetImportQueueStage::Routing:
            case RuntimeAssetImportQueueStage::MainThreadApply:
            case RuntimeAssetImportQueueStage::GpuUpload:
            case RuntimeAssetImportQueueStage::Complete:
            case RuntimeAssetImportQueueStage::Failed:
            case RuntimeAssetImportQueueStage::Cancelled:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool CreatesOrChangesScene(
            const RuntimeAssetImportResult& result) noexcept
        {
            return result.PrimitiveEntitiesCreated > 0u ||
                   result.MaterializedModelScene;
        }

        [[nodiscard]] bool RequestsGpuUpload(
            const RuntimeAssetImportResult& result) noexcept
        {
            return result.RequestedTextureUpload ||
                   result.TextureUploadRequests > 0u ||
                   result.GeneratedTextureUploadRequests > 0u;
        }

        [[nodiscard]] bool IsModelTextureImportPayload(
            const Assets::AssetPayloadKind payloadKind) noexcept
        {
            return payloadKind == Assets::AssetPayloadKind::ModelScene ||
                   payloadKind == Assets::AssetPayloadKind::Texture2D;
        }

        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
        MaterializeDecodedModelSceneImport(
            Assets::AssetService& assetService,
            AssetModelSceneHandoff& handoff,
            const RuntimeAssetImportRequest& request,
            const Assets::AssetId existingAsset,
            Assets::AssetModelScenePayload decoded)
        {
            auto payload =
                std::make_shared<Assets::AssetModelScenePayload>(
                    std::move(decoded));
            const AssetModelSceneHandoffDiagnostics before =
                handoff.GetDiagnostics();
            if (existingAsset.IsValid())
            {
                Core::Result reloaded =
                    assetService.Reload<Assets::AssetModelScenePayload>(
                        existingAsset,
                        [payload](std::string_view,
                                  Assets::AssetId)
                            -> Core::Expected<Assets::AssetModelScenePayload>
                        {
                            return *payload;
                        });
                if (!reloaded.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        reloaded.error());
                }

                if (Core::Result drained =
                        DrainAssetImportEvents(assetService, existingAsset);
                    !drained.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        drained.error());
                }
                const AssetModelSceneHandoffDiagnostics after =
                    handoff.GetDiagnostics();
                if (after.ModelSceneMaterializeFailures >
                        before.ModelSceneMaterializeFailures &&
                    after.LastFailedAsset == existingAsset)
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        NormalizeImportError(after.LastError));
                }

                return RuntimeAssetImportResult{
                    .Asset = existingAsset,
                    .PayloadKind = Assets::AssetPayloadKind::ModelScene,
                    .PrimitiveEntitiesCreated =
                        Delta(after.PrimitiveEntitiesCreated,
                              before.PrimitiveEntitiesCreated),
                    .EmbeddedTextureAssetsCreated =
                        Delta(after.EmbeddedTextureAssetsCreated,
                              before.EmbeddedTextureAssetsCreated),
                    .GeneratedTextureAssetsCreated =
                        Delta(after.GeneratedTextureAssetsCreated,
                              before.GeneratedTextureAssetsCreated),
                    .TextureUploadRequests =
                        Delta(after.EmbeddedTextureUploadRequests,
                              before.EmbeddedTextureUploadRequests) +
                        Delta(after.GeneratedTextureUploadRequests,
                              before.GeneratedTextureUploadRequests),
                    .GeneratedTextureUploadRequests =
                        Delta(after.GeneratedTextureUploadRequests,
                              before.GeneratedTextureUploadRequests),
                    .MaterializedModelScene =
                        after.ModelSceneMaterializeSuccesses >
                            before.ModelSceneMaterializeSuccesses,
                };
            }

            auto asset = assetService.Load<Assets::AssetModelScenePayload>(
                request.Path,
                [payload](std::string_view,
                          Assets::AssetId)
                    -> Core::Expected<Assets::AssetModelScenePayload>
                {
                    return *payload;
                });
            if (!asset.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(asset.error());
            }

            if (Core::Result drained =
                    DrainAssetImportEvents(assetService, *asset);
                !drained.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(drained.error());
            }
            if (handoff.FindRecord(*asset) == nullptr)
            {
                if (Core::Result materialized =
                        handoff.MaterializeReadyModelScene(*asset);
                    !materialized.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        materialized.error());
                }
            }

            const AssetModelSceneHandoffDiagnostics after =
                handoff.GetDiagnostics();
            if (after.ModelSceneMaterializeFailures >
                    before.ModelSceneMaterializeFailures &&
                after.LastFailedAsset == *asset)
            {
                return Core::Err<RuntimeAssetImportResult>(
                    NormalizeImportError(after.LastError));
            }

            return RuntimeAssetImportResult{
                .Asset = *asset,
                .PayloadKind = Assets::AssetPayloadKind::ModelScene,
                .PrimitiveEntitiesCreated =
                    Delta(after.PrimitiveEntitiesCreated,
                          before.PrimitiveEntitiesCreated),
                .EmbeddedTextureAssetsCreated =
                    Delta(after.EmbeddedTextureAssetsCreated,
                          before.EmbeddedTextureAssetsCreated),
                .GeneratedTextureAssetsCreated =
                    Delta(after.GeneratedTextureAssetsCreated,
                          before.GeneratedTextureAssetsCreated),
                .TextureUploadRequests =
                    Delta(after.EmbeddedTextureUploadRequests,
                          before.EmbeddedTextureUploadRequests) +
                    Delta(after.GeneratedTextureUploadRequests,
                          before.GeneratedTextureUploadRequests),
                .GeneratedTextureUploadRequests =
                    Delta(after.GeneratedTextureUploadRequests,
                          before.GeneratedTextureUploadRequests),
                .MaterializedModelScene =
                    after.ModelSceneMaterializeSuccesses >
                        before.ModelSceneMaterializeSuccesses,
            };
        }

        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
        MaterializeDecodedTextureImport(
            Assets::AssetService& assetService,
            Graphics::GpuAssetCache& gpuAssetCache,
            AssetModelTextureHandoff& handoff,
            const RuntimeAssetImportRequest& request,
            const Assets::AssetId existingAsset,
            Assets::AssetTexture2DPayload decoded)
        {
            auto payload =
                std::make_shared<Assets::AssetTexture2DPayload>(
                    std::move(decoded));
            const AssetModelTextureHandoffDiagnostics before =
                handoff.GetDiagnostics();
            if (existingAsset.IsValid())
            {
                Core::Result reloaded =
                    assetService.Reload<Assets::AssetTexture2DPayload>(
                        existingAsset,
                        [payload](std::string_view,
                                  Assets::AssetId)
                            -> Core::Expected<Assets::AssetTexture2DPayload>
                        {
                            return *payload;
                        });
                if (!reloaded.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        reloaded.error());
                }

                if (Core::Result drained =
                        DrainAssetImportEvents(assetService, existingAsset);
                    !drained.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        drained.error());
                }
                const AssetModelTextureHandoffDiagnostics after =
                    handoff.GetDiagnostics();
                if (after.TextureUploadFailures > before.TextureUploadFailures &&
                    after.LastFailedAsset == existingAsset)
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        NormalizeImportError(after.LastError));
                }

                return RuntimeAssetImportResult{
                    .Asset = existingAsset,
                    .PayloadKind = Assets::AssetPayloadKind::Texture2D,
                    .TextureUploadRequests =
                        Delta(after.TextureUploadRequests,
                              before.TextureUploadRequests),
                    .RequestedTextureUpload =
                        after.TextureUploadRequests > before.TextureUploadRequests,
                };
            }

            auto asset = assetService.Load<Assets::AssetTexture2DPayload>(
                request.Path,
                [payload](std::string_view,
                          Assets::AssetId)
                    -> Core::Expected<Assets::AssetTexture2DPayload>
                {
                    return *payload;
                });
            if (!asset.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(asset.error());
            }

            if (Core::Result drained =
                    DrainAssetImportEvents(assetService, *asset);
                !drained.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(drained.error());
            }
            AssetModelTextureHandoffDiagnostics after =
                handoff.GetDiagnostics();
            const bool uploadWasAlreadyHandled =
                after.TextureUploadRequests > before.TextureUploadRequests ||
                after.TextureUploadDeferrals > before.TextureUploadDeferrals ||
                after.TextureUploadFailures > before.TextureUploadFailures;

            if (!uploadWasAlreadyHandled &&
                gpuAssetCache.GetState(*asset) ==
                    Graphics::GpuAssetState::NotRequested)
            {
                if (Core::Result uploaded = handoff.UploadReadyTexture(*asset);
                    !uploaded.has_value())
                {
                    if (!IsTextureUploadDeferral(uploaded.error()))
                    {
                        return Core::Err<RuntimeAssetImportResult>(
                            uploaded.error());
                    }
                }
            }

            after = handoff.GetDiagnostics();
            if (after.TextureUploadFailures > before.TextureUploadFailures &&
                after.LastFailedAsset == *asset)
            {
                return Core::Err<RuntimeAssetImportResult>(
                    NormalizeImportError(after.LastError));
            }

            return RuntimeAssetImportResult{
                .Asset = *asset,
                .PayloadKind = Assets::AssetPayloadKind::Texture2D,
                .TextureUploadRequests =
                    Delta(after.TextureUploadRequests,
                          before.TextureUploadRequests),
                .RequestedTextureUpload =
                    after.TextureUploadRequests > before.TextureUploadRequests,
            };
        }

        [[nodiscard]] bool IsFinitePosition(const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        void AccumulateBounds(GeometryImportBounds& bounds,
                              const glm::vec3& position) noexcept
        {
            if (!IsFinitePosition(position))
                return;

            if (!bounds.Valid)
            {
                bounds.Min = position;
                bounds.Max = position;
                bounds.Valid = true;
                return;
            }

            bounds.Min = glm::min(bounds.Min, position);
            bounds.Max = glm::max(bounds.Max, position);
        }

        [[nodiscard]] std::optional<GeometryImportBounds> BoundsFromHalfedgeMesh(
            const Geometry::HalfedgeMesh::Mesh& mesh) noexcept
        {
            GeometryImportBounds bounds{};
            for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vertex{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(vertex) || mesh.IsDeleted(vertex))
                    continue;
                AccumulateBounds(bounds, mesh.Position(vertex));
            }
            if (!bounds.Valid)
                return std::nullopt;
            return bounds;
        }

        [[nodiscard]] std::optional<GeometryImportBounds> BoundsFromGraph(
            const Geometry::Graph::Graph& graph) noexcept
        {
            GeometryImportBounds bounds{};
            for (std::size_t i = 0u; i < graph.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vertex{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (!graph.IsValid(vertex) || graph.IsDeleted(vertex))
                    continue;
                AccumulateBounds(bounds, graph.VertexPosition(vertex));
            }
            if (!bounds.Valid)
                return std::nullopt;
            return bounds;
        }

        [[nodiscard]] std::optional<GeometryImportBounds> BoundsFromCloud(
            const Geometry::PointCloud::Cloud& cloud) noexcept
        {
            GeometryImportBounds bounds{};
            for (const glm::vec3& position : cloud.Positions())
            {
                AccumulateBounds(bounds, position);
            }
            if (!bounds.Valid)
                return std::nullopt;
            return bounds;
        }

        [[nodiscard]] float RadiusForBounds(
            const GeometryImportBounds& bounds) noexcept
        {
            constexpr float kMinimumVisibleRadius = 0.05f;
            const float radius = 0.5f * glm::length(bounds.Max - bounds.Min);
            if (!std::isfinite(radius) || radius <= 0.0f)
                return kMinimumVisibleRadius;
            return std::max(kMinimumVisibleRadius, radius);
        }

        [[nodiscard]] CameraFocusTarget ToCameraFocusTarget(
            const GeometryImportBounds& bounds) noexcept
        {
            return CameraFocusTarget{
                .Center = 0.5f * (bounds.Min + bounds.Max),
                .Radius = RadiusForBounds(bounds),
            };
        }

        void AttachGeometryBounds(entt::registry& raw,
                                  const ECS::EntityHandle entity,
                                  const GeometryImportBounds& bounds)
        {
            if (!bounds.Valid)
                return;

            const glm::vec3 center = 0.5f * (bounds.Min + bounds.Max);
            const glm::vec3 extents = 0.5f * (bounds.Max - bounds.Min);
            const float radius = RadiusForBounds(bounds);

            ECS::Components::Culling::Local::Bounds local{};
            local.LocalBoundingAABB.Min = bounds.Min;
            local.LocalBoundingAABB.Max = bounds.Max;
            local.LocalBoundingSphere.Center = center;
            local.LocalBoundingSphere.Radius = radius;
            raw.emplace_or_replace<ECS::Components::Culling::Local::Bounds>(
                entity,
                local);

            ECS::Components::Culling::World::Bounds world{};
            world.WorldBoundingOBB.Center = center;
            world.WorldBoundingOBB.Extents = extents;
            world.WorldBoundingOBB.Rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
            world.WorldBoundingSphere.Center = center;
            world.WorldBoundingSphere.Radius = radius;
            raw.emplace_or_replace<ECS::Components::Culling::World::Bounds>(
                entity,
                world);
        }

        void FocusMainCameraOnImportedGeometry(
            CameraControllerRegistry& cameraControllers,
            const Core::Config::CameraControllerKind controllerKind,
            const bool cameraEnabled,
            const std::optional<GeometryImportBounds>& bounds)
        {
            if (!cameraEnabled || !bounds.has_value() || !bounds->Valid)
                return;

            ICameraController* controller =
                cameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            if (controller == nullptr)
            {
                cameraControllers.Register(
                    CameraControllerSlot::Main,
                    CreateCameraController(controllerKind));
                controller = cameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            }
            if (controller == nullptr)
                return;

            controller->Focus(ToCameraFocusTarget(*bounds));
            cameraControllers.MarkCameraTransition(CameraControllerSlot::Main);
        }

        [[nodiscard]] std::optional<Assets::AssetTexture2DPayload>
        BakeDirectMeshGeneratedNormalTexturePayload(
            const std::string_view meshPath,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const bool hasResolvedTexcoords)
        {
            if (!hasResolvedTexcoords)
            {
                return std::nullopt;
            }

            MeshAttributeTextureBakeOptions options{};
            options.SourcePropertyName = "v:normal";
            options.Width = 64u;
            options.Height = 64u;
            options.DebugName = "generated-direct-mesh-normal-v:normal";

            MeshAttributeTextureBakeResult bake =
                BakeMeshVertexNormalTexture(mesh, options);
            if (bake.Status != MeshAttributeTextureBakeStatus::Success)
            {
                return std::nullopt;
            }

            bake.Payload.Metadata.SourcePath = std::string{meshPath};
            return std::move(bake.Payload);
        }

        [[nodiscard]] Core::Expected<DirectMeshGeneratedTextureResult>
        RegisterDirectMeshGeneratedNormalTexture(
            Assets::AssetService& assetService,
            Graphics::GpuAssetCache& gpuAssetCache,
            RenderExtractionCache& extraction,
            const std::string_view meshPath,
            const ECS::EntityHandle entity,
            const Assets::AssetTexture2DPayload& payload)
        {
            DirectMeshGeneratedTextureResult result{};
            const std::string generatedPath = BuildGeneratedTextureAssetPath(
                meshPath,
                0u,
                "normal",
                "v:normal");
            auto texture = LoadGeneratedTextureAsset(
                assetService,
                generatedPath,
                payload);
            if (!texture.has_value())
            {
                return Core::Err<DirectMeshGeneratedTextureResult>(texture.error());
            }

            result.NormalTexture = *texture;
            result.GeneratedTextureAssetsCreated = 1u;

            auto upload = RequestTextureAssetUpload(
                assetService,
                gpuAssetCache,
                *texture);
            if (upload.has_value())
            {
                result.GeneratedTextureUploadRequests = 1u;
            }
            else if (!IsTextureUploadDeferral(upload.error()))
            {
                return Core::Err<DirectMeshGeneratedTextureResult>(upload.error());
            }

            extraction.SetMaterialTextureAssetBindings(
                StableEntityLookup::ToRenderId(entity),
                Graphics::MaterialTextureAssetBindings{
                    .Albedo = {},
                    .Normal = *texture,
                    .MetallicRoughness = {},
                    .Emissive = {},
                    .NormalSpace =
                        Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal,
                });

            return result;
        }

        void MarkMeshGeometryDirty(entt::registry& raw, const ECS::EntityHandle entity)
        {
            ECS::Components::DirtyTags::MarkGpuDirty(raw, entity);
            ECS::Components::DirtyTags::MarkVertexPositionsDirty(raw, entity);
            ECS::Components::DirtyTags::MarkFaceTopologyDirty(raw, entity);
            ECS::Components::DirtyTags::MarkEdgeTopologyDirty(raw, entity);
        }

        [[nodiscard]] std::vector<glm::vec3> SnapshotCurrentMeshVertexNormals(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            namespace GS = ECS::Components::GeometrySources;

            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            if (!view.Valid() || view.ActiveDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return {};
            }

            const auto normals =
                view.VertexSource->Properties.Get<glm::vec3>(GS::PropertyNames::kNormal);
            if (!normals)
            {
                return {};
            }

            return std::vector<glm::vec3>(
                normals.Vector().begin(),
                normals.Vector().end());
        }

        [[nodiscard]] bool RestoreMeshVertexNormalsIfCompatible(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const std::vector<glm::vec3>& normals)
        {
            if (normals.empty())
            {
                return false;
            }

            namespace GS = ECS::Components::GeometrySources;
            auto* vertices = raw.try_get<GS::Vertices>(entity);
            if (vertices == nullptr)
            {
                return false;
            }

            auto target = vertices->Properties.Get<glm::vec3>(GS::PropertyNames::kNormal);
            if (!target || target.Vector().size() != normals.size())
            {
                return false;
            }

            target.Vector() = normals;
            return true;
        }

        void QueueDirectMeshPostProcess(
            StreamingExecutor* streamingExecutor,
            Assets::AssetService& assetService,
            Graphics::GpuAssetCache& gpuAssetCache,
            RenderExtractionCache& extraction,
            ECS::Scene::Registry& scene,
            std::string meshPath,
            const Geometry::MeshIO::MeshIOResult& meshPayload,
            const ECS::EntityHandle entity)
        {
            if (streamingExecutor == nullptr || entity == ECS::InvalidEntityHandle)
            {
                return;
            }

            auto state = std::make_shared<DirectMeshPostProcessState>();
            state->Path = std::move(meshPath);
            state->Payload = meshPayload;
            state->Entity = entity;

            const StreamingTaskHandle handle = streamingExecutor->Submit(
                StreamingTaskDesc{
                    .Name = "Runtime.DirectMeshPostProcess." +
                        FileNameFromPath(state->Path),
                    .Kind = Core::Dag::TaskKind::AssetDecode,
                    .Priority = Core::Dag::TaskPriority::Low,
                    .EstimatedCost = 8u,
                    .Execute = [state]() mutable -> StreamingResult
                    {
                        auto materialized = BuildRuntimeHalfedgeMeshMaterialization(
                            state->Payload,
                            RuntimeMeshMaterializationOptions{
                                .AllowDisconnectedRenderableFallback = true,
                            });
                        if (!materialized.has_value())
                        {
                            state->Error = materialized.error();
                            return StreamingResult{
                                StreamingCpuPayloadReady{.PayloadToken = 0u}};
                        }

                        state->GeneratedNormalTexture =
                            BakeDirectMeshGeneratedNormalTexturePayload(
                                state->Path,
                                materialized->Mesh,
                                materialized->Diagnostics.ResolvedTexcoordsValid);
                        state->Materialized = std::move(*materialized);
                        state->Error = Core::ErrorCode::Success;
                        return StreamingResult{
                            StreamingCpuPayloadReady{.PayloadToken = 0u}};
                    },
                    .ApplyOnMainThread = [
                        state,
                        &assetService,
                        &gpuAssetCache,
                        &extraction,
                        &scene](StreamingResult&& result) mutable
                    {
                        if (!result.has_value() ||
                            state->Error != Core::ErrorCode::Success ||
                            !state->Materialized.has_value())
                        {
                            Core::Log::Warn(
                                "[Runtime] Deferred mesh post-process failed: path='{}' error={}",
                                state->Path,
                                Core::Error::ToString(
                                    result.has_value()
                                        ? state->Error
                                        : result.error()));
                            return;
                        }

                        if (!scene.IsValid(state->Entity))
                        {
                            return;
                        }

                        auto& raw = scene.Raw();
                        const std::vector<glm::vec3> currentNormals =
                            SnapshotCurrentMeshVertexNormals(raw, state->Entity);
                        Geometry::HalfedgeMesh::Mesh mesh =
                            std::move(state->Materialized->Mesh);
                        ECS::Components::GeometrySources::PopulateFromMesh(
                            raw,
                            state->Entity,
                            mesh);
                        (void)RestoreMeshVertexNormalsIfCompatible(
                            raw,
                            state->Entity,
                            currentNormals);
                        MarkMeshGeometryDirty(raw, state->Entity);

                        if (state->GeneratedNormalTexture.has_value())
                        {
                            auto generated = RegisterDirectMeshGeneratedNormalTexture(
                                assetService,
                                gpuAssetCache,
                                extraction,
                                state->Path,
                                state->Entity,
                                *state->GeneratedNormalTexture);
                            if (!generated.has_value())
                            {
                                Core::Log::Warn(
                                    "[Runtime] Deferred generated normal texture registration failed: path='{}' error={}",
                                    state->Path,
                                    Core::Error::ToString(generated.error()));
                            }
                        }
                    },
                });

            if (handle.IsValid())
            {
                streamingExecutor->PumpBackground(1u);
                Core::Log::Info(
                    "[Runtime] Queued direct mesh post-process: path='{}'",
                    state->Path);
            }
            else
            {
                Core::Log::Warn(
                    "[Runtime] Direct mesh post-process queue submission failed: path='{}'",
                    state->Path);
            }
        }

        [[nodiscard]] Core::Expected<DecodedGeometryImport> DecodeGeometryImport(
            const RuntimeAssetImportRequest& request)
        {
            auto route = Assets::ResolveAssetImportRoute(
                request.Path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
            if (!route.has_value())
            {
                return Core::Err<DecodedGeometryImport>(route.error());
            }
            if (!Assets::IsGeometryPayloadKind(route->PayloadKind))
            {
                return Core::Err<DecodedGeometryImport>(
                    Core::ErrorCode::AssetUnsupportedFormat);
            }

            Assets::AssetGeometryIOBridge bridge;
            if (Core::Result registered = RegisterPromotedGeometryIOCallbacks(bridge);
                !registered.has_value())
            {
                return Core::Err<DecodedGeometryImport>(registered.error());
            }

            auto decoded = bridge.Import(
                request.Path,
                Assets::AssetImportHint{.PayloadKind = route->PayloadKind});
            if (!decoded.has_value())
            {
                return Core::Err<DecodedGeometryImport>(decoded.error());
            }

            switch (route->PayloadKind)
            {
            case Assets::AssetPayloadKind::Mesh:
            {
                auto meshPayload =
                    decoded->Read<Geometry::MeshIO::MeshIOResult>();
                if (!meshPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        meshPayload.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedMeshImport{
                        .Payload = **meshPayload,
                    },
                };
            }
            case Assets::AssetPayloadKind::Graph:
            {
                auto graphPayload =
                    decoded->Read<Geometry::GraphIO::GraphIOResult>();
                if (!graphPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        graphPayload.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedGraphImport{.Payload = **graphPayload},
                };
            }
            case Assets::AssetPayloadKind::PointCloud:
            {
                auto cloudPayload =
                    decoded->Read<Geometry::PointCloudIO::PointCloudIOResult>();
                if (!cloudPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        cloudPayload.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedPointCloudImport{.Payload = **cloudPayload},
                };
            }
            default:
                break;
            }

            return Core::Err<DecodedGeometryImport>(
                Core::ErrorCode::AssetUnsupportedFormat);
        }

        [[nodiscard]] Core::Expected<MaterializedGeometryImport>
        MaterializeDecodedGeometryImport(
            Assets::AssetService& assetService,
            Graphics::GpuAssetCache& gpuAssetCache,
            RenderExtractionCache& extraction,
            ECS::Scene::Registry& scene,
            StreamingExecutor* streamingExecutor,
            const DecodedGeometryImport& decoded)
        {
            return std::visit(
                [&](const auto& payload) -> Core::Expected<MaterializedGeometryImport>
                {
                    using PayloadT = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<PayloadT, DecodedMeshImport>)
                    {
                        auto asset =
                            assetService.Load<Geometry::MeshIO::MeshIOResult>(
                                decoded.Path,
                                [&payload](std::string_view,
                                           Assets::AssetId)
                                    -> Core::Expected<Geometry::MeshIO::MeshIOResult>
                                {
                                    return payload.Payload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        if (Core::Result drained =
                                DrainAssetImportEvents(assetService, *asset);
                            !drained.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                drained.error());
                        }

                        auto rawMesh = BuildRuntimeHalfedgeMeshGeometryOnly(
                            payload.Payload,
                            RuntimeMeshGeometryOnlyOptions{
                                .AllowDisconnectedRenderableFallback = true,
                            });
                        if (!rawMesh.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                rawMesh.error());
                        }

                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        raw.emplace<ECS::Components::Selection::SelectableTag>(entity);
                        raw.emplace<Graphics::Components::RenderSurface>(
                            entity,
                            Graphics::Components::RenderSurface{
                                .Domain = Graphics::Components::RenderSurface::SourceDomain::Vertex,
                            });
                        raw.emplace<Graphics::Components::VisualizationConfig>(
                            entity,
                            ImportedMeshVisualization());
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromHalfedgeMesh(*rawMesh);
                        if (bounds.has_value())
                        {
                            AttachGeometryBounds(raw, entity, *bounds);
                        }
                        ECS::Components::GeometrySources::PopulateFromMesh(
                            raw,
                            entity,
                            *rawMesh);
                        QueueDirectMeshPostProcess(
                            streamingExecutor,
                            assetService,
                            gpuAssetCache,
                            extraction,
                            scene,
                            decoded.Path,
                            payload.Payload,
                            entity);

                        return MaterializedGeometryImport{
                            .Result = RuntimeAssetImportResult{
                                .Asset = *asset,
                                .PayloadKind = decoded.PayloadKind,
                                .PrimitiveEntitiesCreated = 1u,
                            },
                            .Bounds = bounds,
                            .Entity = entity,
                        };
                    }
                    else if constexpr (std::is_same_v<PayloadT, DecodedGraphImport>)
                    {
                        auto asset =
                            assetService.Load<Geometry::GraphIO::GraphIOResult>(
                                decoded.Path,
                                [&payload](std::string_view,
                                           Assets::AssetId)
                                    -> Core::Expected<Geometry::GraphIO::GraphIOResult>
                                {
                                    return payload.Payload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        if (Core::Result drained =
                                DrainAssetImportEvents(assetService, *asset);
                            !drained.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                drained.error());
                        }

                        Geometry::Graph::Graph graph = payload.Payload.Graph;
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromGraph(graph);
                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        raw.emplace<ECS::Components::Selection::SelectableTag>(entity);
                        raw.emplace<Graphics::Components::RenderEdges>(
                            entity,
                            Graphics::Components::RenderEdges{
                                .Domain = Graphics::Components::RenderEdges::SourceDomain::Vertex,
                            });
                        raw.emplace<Graphics::Components::RenderPoints>(
                            entity,
                            Graphics::Components::RenderPoints{});
                        raw.emplace<Graphics::Components::VisualizationConfig>(
                            entity,
                            ImportedGeometryVisualization());
                        if (bounds.has_value())
                        {
                            AttachGeometryBounds(raw, entity, *bounds);
                        }
                        ECS::Components::GeometrySources::PopulateFromGraph(
                            raw,
                            entity,
                            graph);

                        return MaterializedGeometryImport{
                            .Result = RuntimeAssetImportResult{
                                .Asset = *asset,
                                .PayloadKind = decoded.PayloadKind,
                                .PrimitiveEntitiesCreated = 1u,
                            },
                            .Bounds = bounds,
                            .Entity = entity,
                        };
                    }
                    else
                    {
                        auto asset =
                            assetService.Load<Geometry::PointCloudIO::PointCloudIOResult>(
                                decoded.Path,
                                [&payload](std::string_view,
                                           Assets::AssetId)
                                    -> Core::Expected<Geometry::PointCloudIO::PointCloudIOResult>
                                {
                                    return payload.Payload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        if (Core::Result drained =
                                DrainAssetImportEvents(assetService, *asset);
                            !drained.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                drained.error());
                        }

                        Geometry::PointCloud::Cloud cloud = payload.Payload.Cloud;
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromCloud(cloud);
                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        raw.emplace<ECS::Components::Selection::SelectableTag>(entity);
                        raw.emplace<Graphics::Components::RenderPoints>(
                            entity,
                            Graphics::Components::RenderPoints{});
                        raw.emplace<Graphics::Components::VisualizationConfig>(
                            entity,
                            ImportedGeometryVisualization());
                        if (bounds.has_value())
                        {
                            AttachGeometryBounds(raw, entity, *bounds);
                        }
                        ECS::Components::GeometrySources::PopulateFromCloud(
                            raw,
                            entity,
                            cloud);

                        return MaterializedGeometryImport{
                            .Result = RuntimeAssetImportResult{
                                .Asset = *asset,
                                .PayloadKind = decoded.PayloadKind,
                                .PrimitiveEntitiesCreated = 1u,
                            },
                            .Bounds = bounds,
                            .Entity = entity,
                        };
                    }
                },
                decoded.Payload);
        }

        [[nodiscard]] Core::Expected<Assets::AssetPayloadKind>
        PayloadKindForExistingAsset(
            Assets::AssetService& assetService,
            const Assets::AssetId asset)
        {
            if (!asset.IsValid() || !assetService.IsAlive(asset))
            {
                return Core::Err<Assets::AssetPayloadKind>(
                    Core::ErrorCode::ResourceNotFound);
            }

            const auto meta = assetService.GetMeta(asset);
            if (!meta.has_value())
            {
                return Core::Err<Assets::AssetPayloadKind>(meta.error());
            }

            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Geometry::MeshIO::MeshIOResult>())
            {
                return Assets::AssetPayloadKind::Mesh;
            }
            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Geometry::GraphIO::GraphIOResult>())
            {
                return Assets::AssetPayloadKind::Graph;
            }
            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Geometry::PointCloudIO::PointCloudIOResult>())
            {
                return Assets::AssetPayloadKind::PointCloud;
            }
            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Assets::AssetModelScenePayload>())
            {
                return Assets::AssetPayloadKind::ModelScene;
            }
            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Assets::AssetTexture2DPayload>())
            {
                return Assets::AssetPayloadKind::Texture2D;
            }

            return Core::Err<Assets::AssetPayloadKind>(
                Core::ErrorCode::AssetTypeMismatch);
        }

        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
        ReloadDecodedGeometryImport(
            Assets::AssetService& assetService,
            const Assets::AssetId existingAsset,
            const DecodedGeometryImport& decoded)
        {
            auto reloadResult = std::visit(
                [&](const auto& payload) -> Core::Result
                {
                    using PayloadT = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<PayloadT, DecodedMeshImport>)
                    {
                        const Geometry::MeshIO::MeshIOResult storedPayload =
                            payload.Payload;
                        return assetService.Reload<
                            Geometry::MeshIO::MeshIOResult>(
                            existingAsset,
                            [storedPayload](std::string_view,
                                            Assets::AssetId)
                                -> Core::Expected<Geometry::MeshIO::MeshIOResult>
                            {
                                return storedPayload;
                            });
                    }
                    else if constexpr (std::is_same_v<PayloadT, DecodedGraphImport>)
                    {
                        const Geometry::GraphIO::GraphIOResult storedPayload =
                            payload.Payload;
                        return assetService.Reload<
                            Geometry::GraphIO::GraphIOResult>(
                            existingAsset,
                            [storedPayload](std::string_view,
                                            Assets::AssetId)
                                -> Core::Expected<Geometry::GraphIO::GraphIOResult>
                            {
                                return storedPayload;
                            });
                    }
                    else
                    {
                        const Geometry::PointCloudIO::PointCloudIOResult storedPayload =
                            payload.Payload;
                        return assetService.Reload<
                            Geometry::PointCloudIO::PointCloudIOResult>(
                            existingAsset,
                            [storedPayload](std::string_view,
                                            Assets::AssetId)
                                -> Core::Expected<Geometry::PointCloudIO::PointCloudIOResult>
                            {
                                return storedPayload;
                            });
                    }
                },
                decoded.Payload);

            if (!reloadResult.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(
                    reloadResult.error());
            }

            if (Core::Result drained =
                    DrainAssetImportEvents(assetService, existingAsset);
                !drained.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(drained.error());
            }
            return RuntimeAssetImportResult{
                .Asset = existingAsset,
                .PayloadKind = decoded.PayloadKind,
            };
        }

        [[nodiscard]] std::uint32_t ClampCursorPixel(const float value,
                                                     const std::uint32_t extent) noexcept
        {
            if (extent == 0u || !std::isfinite(value))
                return 0u;
            const float clamped = std::clamp(value, 0.0f, static_cast<float>(extent - 1u));
            return static_cast<std::uint32_t>(clamped);
        }

        [[nodiscard]] bool CursorInsideViewport(const Platform::Input::Context::XY cursor,
                                                const Core::Extent2D viewport) noexcept
        {
            return viewport.Width > 0 &&
                   viewport.Height > 0 &&
                   std::isfinite(cursor.x) &&
                   std::isfinite(cursor.y) &&
                   cursor.x >= 0.0f &&
                   cursor.y >= 0.0f &&
                   cursor.x < static_cast<float>(viewport.Width) &&
                   cursor.y < static_cast<float>(viewport.Height);
        }

        // BUG-026 — platform cursor positions are window (logical) coordinates
        // (GLFW's cursor callback), while picking/gizmo math addresses
        // framebuffer pixels. On HiDPI hosts (content scale != 1) the two
        // differ; scale by the extent ratio so the pick pixel matches what is
        // under the cursor. Degenerate extents pass the cursor through.
        [[nodiscard]] Platform::Input::Context::XY WindowToFramebufferCursor(
            const Platform::Input::Context::XY cursor,
            const Core::Extent2D windowExtent,
            const Core::Extent2D framebufferExtent) noexcept
        {
            if (windowExtent.Width <= 0 || windowExtent.Height <= 0 ||
                framebufferExtent.Width <= 0 || framebufferExtent.Height <= 0)
            {
                return cursor;
            }
            const float scaleX = static_cast<float>(framebufferExtent.Width) /
                                 static_cast<float>(windowExtent.Width);
            const float scaleY = static_cast<float>(framebufferExtent.Height) /
                                 static_cast<float>(windowExtent.Height);
            return Platform::Input::Context::XY{cursor.x * scaleX, cursor.y * scaleY};
        }

        void SubmitViewportSelectionClickForFrame(SelectionController& selection,
                                                  const Platform::Input::Context& input,
                                                  const Core::Extent2D windowExtent,
                                                  const Core::Extent2D viewport,
                                                  const bool imguiCapturesMouse,
                                                  const bool gizmoCapturesMouse) noexcept
        {
            if (imguiCapturesMouse || gizmoCapturesMouse || Core::IsEmpty(viewport) ||
                !input.IsMouseButtonJustPressed(kSelectionMouseButton))
            {
                return;
            }

            const Platform::Input::Context::XY cursor =
                WindowToFramebufferCursor(input.GetMousePosition(), windowExtent, viewport);
            if (!CursorInsideViewport(cursor, viewport))
                return;

            selection.RequestClickPick(ClampCursorPixel(cursor.x, viewport.Width),
                                       ClampCursorPixel(cursor.y, viewport.Height));
        }

        [[nodiscard]] std::uint32_t BuildGizmoModifierMask(
            const Platform::Input::Context& input) noexcept
        {
            std::uint32_t mask = 0u;
            if (input.IsKeyPressed(Platform::Input::Key::LeftShift))
                mask |= static_cast<std::uint32_t>(GizmoModifier::Snap);
            return mask;
        }

        void RebuildSelectedGizmoEntities(
            const SelectionController& selection,
            ECS::Scene::Registry& scene,
            std::vector<ECS::EntityHandle>& outSelected)
        {
            outSelected.clear();
            for (const std::uint32_t stableId : selection.SelectedStableIds())
            {
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableId);
                if (scene.IsValid(entity))
                    outSelected.push_back(entity);
            }
        }

        void DriveGizmoInteractionForFrame(
            GizmoInteraction& gizmo,
            GizmoUndoStack& undo,
            ECS::Scene::Registry& scene,
            const Platform::Input::Context& input,
            const Graphics::CameraViewInput& cameraInput,
            const Core::Extent2D windowExtent,
            const Core::Extent2D viewport,
            const bool imguiCapturesInput,
            std::span<const ECS::EntityHandle> selected)
        {
            if (imguiCapturesInput)
            {
                gizmo.SetModifierMask(0u);
                if (gizmo.IsDragging())
                    gizmo.DragCancel(scene);
                return;
            }

            gizmo.SetModifierMask(BuildGizmoModifierMask(input));
            if (Core::IsEmpty(viewport))
            {
                if (gizmo.IsDragging())
                    gizmo.DragCancel(scene);
                return;
            }

            const Platform::Input::Context::XY cursor =
                WindowToFramebufferCursor(input.GetMousePosition(), windowExtent, viewport);
            const std::uint32_t pixelX = ClampCursorPixel(cursor.x, viewport.Width);
            const std::uint32_t pixelY = ClampCursorPixel(cursor.y, viewport.Height);
            const Graphics::CameraViewSnapshot camera =
                Graphics::BuildCameraViewSnapshot(
                    cameraInput,
                    viewport,
                    Graphics::PickPixelRequest{
                        .X = pixelX,
                        .Y = pixelY,
                        .Pending = true,
                    });
            if (!camera.Valid || !camera.HasPickRay)
            {
                if (!input.IsMouseButtonPressed(kGizmoMouseButton) && gizmo.IsDragging())
                    (void)gizmo.DragCommit(scene, undo);
                return;
            }

            const PickRay ray{
                .Origin = camera.PickRayOrigin,
                .Direction = camera.PickRayDirection,
            };

            if (input.IsMouseButtonJustPressed(kGizmoMouseButton))
            {
                const GizmoHitResult hit = gizmo.HitTest(
                    scene,
                    camera,
                    glm::vec2{cursor.x, cursor.y},
                    viewport,
                    selected);
                if (hit.Hit)
                    (void)gizmo.BeginDrag(scene, hit, ray, selected);
            }
            else if (input.IsMouseButtonPressed(kGizmoMouseButton) && gizmo.IsDragging())
            {
                (void)gizmo.DragTick(scene, ray);
            }
            else if (!input.IsMouseButtonPressed(kGizmoMouseButton) && gizmo.IsDragging())
            {
                (void)gizmo.DragCommit(scene, undo);
            }
        }

    }

    RuntimeDeviceSelection SelectRuntimeDeviceBackend(
        const Core::Config::RenderConfig& config,
        const bool promotedVulkanAvailable) noexcept
    {
        switch (config.Backend)
        {
        case Core::Config::GraphicsBackend::Vulkan:
            if (config.EnablePromotedVulkanDevice && promotedVulkanAvailable)
            {
                return RuntimeDeviceSelection{
                    .UsePromotedVulkanDevice = true,
                    .FallsBackToNullDevice = false,
                };
            }
            return RuntimeDeviceSelection{};
        }
        return RuntimeDeviceSelection{};
    }

    bool ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
        const Core::Config::RenderConfig& config,
        const bool isDeviceOperational) noexcept
    {
        if (config.Backend != Core::Config::GraphicsBackend::Vulkan)
            return false;
        if (!config.EnablePromotedVulkanDevice)
            return false;
        return !isDeviceOperational;
    }

    [[nodiscard]] RuntimeRenderRecipeActivationSource ToRecipeActivationSource(
        const RuntimeConfigControlSource source) noexcept
    {
        switch (source)
        {
        case RuntimeConfigControlSource::AgentCli:
            return RuntimeRenderRecipeActivationSource::AgentCli;
        case RuntimeConfigControlSource::Editor:
            return RuntimeRenderRecipeActivationSource::Editor;
        case RuntimeConfigControlSource::Programmatic:
            return RuntimeRenderRecipeActivationSource::Programmatic;
        case RuntimeConfigControlSource::None:
            return RuntimeRenderRecipeActivationSource::None;
        }
        return RuntimeRenderRecipeActivationSource::None;
    }

    void RecordBootOnlyDifference(std::vector<std::string>& fields,
                                  const bool differs,
                                  std::string field)
    {
        if (differs)
        {
            fields.push_back(std::move(field));
        }
    }

    [[nodiscard]] bool ProgressivePoissonPlaygroundConfigEquals(
        const Core::Config::ProgressivePoissonPlaygroundConfig& lhs,
        const Core::Config::ProgressivePoissonPlaygroundConfig& rhs) noexcept
    {
        return lhs.Dimension == rhs.Dimension &&
               lhs.GridWidth == rhs.GridWidth &&
               lhs.MaxLevels == rhs.MaxLevels &&
               lhs.HashLoadFactor == rhs.HashLoadFactor &&
               lhs.RadiusAlpha == rhs.RadiusAlpha &&
               lhs.RandomizeGridOrigin == rhs.RandomizeGridOrigin &&
               lhs.GridOriginSeed == rhs.GridOriginSeed &&
               lhs.ShuffleWithinLevels == rhs.ShuffleWithinLevels &&
               lhs.ShuffleSeed == rhs.ShuffleSeed &&
               lhs.PrefixCount == rhs.PrefixCount &&
               lhs.Channel == rhs.Channel &&
               lhs.Backend == rhs.Backend &&
               lhs.MeshSurfaceSampleCount == rhs.MeshSurfaceSampleCount &&
               lhs.MeshSurfaceSampleSeed == rhs.MeshSurfaceSampleSeed &&
               lhs.MeshSurfaceMinTriangleArea == rhs.MeshSurfaceMinTriangleArea &&
               lhs.MeshSurfaceInterpolateNormals == rhs.MeshSurfaceInterpolateNormals &&
               lhs.AutoRunOnEdit == rhs.AutoRunOnEdit &&
               lhs.DebounceSeconds == rhs.DebounceSeconds;
    }

    [[nodiscard]] std::vector<std::string> FindBootOnlyEngineConfigDifferences(
        const Core::Config::EngineConfig& current,
        const Core::Config::EngineConfig& candidate)
    {
        std::vector<std::string> fields{};
        RecordBootOnlyDifference(fields,
                                  current.Window.Title != candidate.Window.Title,
                                  "window.title");
        RecordBootOnlyDifference(fields,
                                  current.Window.Width != candidate.Window.Width,
                                  "window.width");
        RecordBootOnlyDifference(fields,
                                  current.Window.Height != candidate.Window.Height,
                                  "window.height");
        RecordBootOnlyDifference(fields,
                                  current.Window.Resizable != candidate.Window.Resizable,
                                  "window.resizable");
        RecordBootOnlyDifference(fields,
                                  current.Window.Backend != candidate.Window.Backend,
                                  "window.backend");
        RecordBootOnlyDifference(fields,
                                  current.Render.Backend != candidate.Render.Backend,
                                  "render.backend");
        RecordBootOnlyDifference(
            fields,
            current.Render.EnablePromotedVulkanDevice !=
                candidate.Render.EnablePromotedVulkanDevice,
            "render.enable_promoted_vulkan_device");
        RecordBootOnlyDifference(fields,
                                  current.Render.EnableValidation !=
                                      candidate.Render.EnableValidation,
                                  "render.enable_validation");
        RecordBootOnlyDifference(fields,
                                  current.Render.EnableVSync !=
                                      candidate.Render.EnableVSync,
                                  "render.enable_vsync");
        RecordBootOnlyDifference(fields,
                                  current.Render.FramesInFlight !=
                                      candidate.Render.FramesInFlight,
                                  "render.frames_in_flight");
        RecordBootOnlyDifference(fields,
                                  current.Render.SynchronousExtraction !=
                                      candidate.Render.SynchronousExtraction,
                                  "render.synchronous_extraction");
        RecordBootOnlyDifference(fields,
                                  current.Simulation.WorkerThreadCount !=
                                      candidate.Simulation.WorkerThreadCount,
                                  "simulation.worker_thread_count");
        RecordBootOnlyDifference(fields,
                                  current.ReferenceScene.Enabled !=
                                      candidate.ReferenceScene.Enabled,
                                  "reference_scene.enabled");
        RecordBootOnlyDifference(fields,
                                  current.ReferenceScene.Selector !=
                                      candidate.ReferenceScene.Selector,
                                  "reference_scene.selector");
        RecordBootOnlyDifference(fields,
                                  current.Camera.Enabled != candidate.Camera.Enabled,
                                  "camera.enabled");
        RecordBootOnlyDifference(fields,
                                  current.Camera.Controller !=
                                      candidate.Camera.Controller,
                                  "camera.controller");
        return fields;
    }

    Core::Config::EngineConfig CreateReferenceEngineConfig()
    {
        Core::Config::EngineConfig config{};
        config.Window.Title = "Modular Vulkan Engine";
        config.Window.Width = 1600;
        config.Window.Height = 900;
        config.Render.Backend = Core::Config::GraphicsBackend::Vulkan;
        config.Render.EnablePromotedVulkanDevice = true;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = true;
        config.Render.FramesInFlight = 2;
        config.ReferenceScene.Enabled = true;
        config.ReferenceScene.Selector = Core::Config::ReferenceSceneSelector::Triangle;
        return config;
    }

    EngineConfigBootResult ResolveEngineConfigForBoot(
        const std::span<const std::string_view> args,
        const EngineConfigBootOptions& options)
    {
        EngineConfigBootResult result{};
        result.Config = CreateReferenceEngineConfig();
        result.LoadResult.Preview.Config = result.Config;
        result.LoadResult.SourceId = "<reference>";

        std::optional<std::string> path =
            FindCommandLineEngineConfigPath(args, options.CliFlag);
        EngineConfigBootSource source = EngineConfigBootSource::CommandLine;

        if (!path.has_value())
        {
            path = FindEnvironmentEngineConfigPath(options.EnvironmentVariable);
            source = EngineConfigBootSource::Environment;
        }

        if (!path.has_value() && ExistingFilePath(options.DefaultConfigPath))
        {
            path = options.DefaultConfigPath;
            source = EngineConfigBootSource::DefaultPath;
        }

        if (!path.has_value())
        {
            return result;
        }

        result.Source = source;
        result.SourcePath = *path;
        result.LoadResult = Core::Config::LoadEngineConfigFile(
            *path,
            result.Config,
            Core::Config::EngineConfigParseOptions{.SourceId = *path});
        result.LoadedFile = Core::Config::IsConfigUsable(result.LoadResult);
        result.UsedReferenceFallback =
            result.LoadResult.State != Core::Config::EngineConfigState::Valid;

        if (result.LoadedFile)
        {
            result.Config = result.LoadResult.Preview.Config;
        }
        return result;
    }

    // ── Construction / destruction ────────────────────────────────────────

    Engine::Engine(Core::Config::EngineConfig config,
                   std::unique_ptr<IApplication> application)
        : m_Config(std::move(config))
        , m_Application(std::move(application))
    {
        if (!m_Application)
            std::terminate();
        m_ConfigControlState.ActiveConfig = m_Config;
    }

    Engine::~Engine()
    {
        if (m_Initialized)
            Shutdown();
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void Engine::Initialize()
    {
        // ── 1. CPU fiber scheduler ────────────────────────────────────────
        // Must be first — all three graphs dispatch through it.
        Core::Tasks::Scheduler::Initialize(m_Config.Simulation.WorkerThreadCount);

        // ── 2. Subsystems ─────────────────────────────────────────────────
        // ARCH-005 / WORKSHOP-002: runtime owns the cross-layer composition
        // between platform window and graphics backend. RHI is platform-
        // neutral, so we fill a backend-agnostic `RHI::DeviceCreateDesc`
        // from the live `IWindow` here.
        m_Window   = Platform::CreateWindow(m_Config.Window);
        if (m_Window && m_Window->ShouldClose())
        {
            Core::Log::Warn(
                "[Runtime] Platform window initialized closed; Engine::Run() will execute zero frames unless the test or caller requests a headless-capable window backend.");
        }
        m_Device   = CreateDevice(m_Config.Render);
        const Platform::Extent2D initialExtent = m_Window->GetFramebufferExtent();
        m_Device->Initialize(RHI::MakeDeviceCreateDesc(
            m_Config.Render,
            initialExtent,
            m_Window->GetNativeHandle()));

        // GRAPHICS-033B: emit the Vulkan-requested-but-not-operational
        // breadcrumb and bump the operational diagnostics counters exactly
        // once per startup when the runtime requested the promoted Vulkan
        // device but the resolved device is not operational. Runtime never
        // aborts on this fallback — see the truth table in
        // `src/graphics/vulkan/README.md`.
        if (ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
                m_Config.Render, m_Device->IsOperational()))
        {
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
            const Backends::Vulkan::VulkanOperationalStatus status =
                Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus(m_Device.get());
            Core::Log::Warn(
                "[Runtime] VulkanRequestedButNotOperational status={} reason={}",
                Backends::Vulkan::ToString(status.Code),
                Backends::Vulkan::ToString(status.Reason));
            Backends::Vulkan::RecordVulkanOperationalFallback(status);
#else
            // Vulkan backend was not compiled into this build; the truth
            // table row resolves to `NotCompiled` with no reason.
            Core::Log::Warn(
                "[Runtime] VulkanRequestedButNotOperational status={} reason={}",
                "NotCompiled",
                "None");
#endif
        }

        m_Renderer = Graphics::CreateRenderer();
        m_Renderer->Initialize(*m_Device);
        m_RendererOperational = m_Device->IsOperational();
        if (!m_Config.Render.DefaultRecipeConfigPath.empty())
        {
            (void)LoadAndApplyRenderRecipeConfigFile(
                m_Config.Render.DefaultRecipeConfigPath,
                RuntimeRenderRecipeActivationSource::StartupConfigFile);
        }

        // ── 2c. Runtime-side Dear ImGui adapter (RUNTIME-090 / GRAPHICS-079) ─
        // Constructed after the Window and Renderer exist. The adapter owns the
        // ImGui context lifecycle and produces exactly one ImGuiOverlayFrame per
        // engine frame into the runtime-owned overlay system; RunFrame brackets
        // the variable tick with BeginFrame/EndFrame (the producer half per
        // GRAPHICS-013CQ). GRAPHICS-079 Slice B hands that same overlay instance
        // to the renderer consumer, so the producer and `ImGuiPass` route share
        // one system without graphics seeing live runtime/editor state.
        m_ImGuiAdapter = std::make_unique<ImGuiAdapter>(*m_Window, m_ImGuiOverlay);
        m_ImGuiAdapter->Initialize();
        if (m_ImGuiEditorCallback)
            m_ImGuiAdapter->SetEditorCallback(m_ImGuiEditorCallback);
        m_Renderer->SetImGuiOverlaySystem(&m_ImGuiOverlay);

        // ── 2d. Render-world pool (GRAPHICS-036C) ─────────────────────────
        // Size the runtime-owned slot pool from the render config: one logical
        // buffer in the default synchronous mode (serial extraction/render,
        // behavior-preserving), or the triple-buffered default when pipelined
        // extraction is requested. The production default remains synchronous;
        // GRAPHICS-036D proves the opt-in render-N-1 path by consuming the
        // previous front while extraction writes the newly acquired back slot.
        m_RenderWorldPool = std::make_unique<RenderWorldPool>(
            m_Config.Render.SynchronousExtraction
                ? 1u
                : RenderWorldPool::kDefaultBuffers);

        // ── 3. CPU task graph (ECS system scheduling) ─────────────────────
        m_FrameGraph = std::make_unique<Core::FrameGraph>();

        // ── 4. Streaming executor (asset IO / geometry processing) ────────
        m_StreamingExecutor = std::make_unique<StreamingExecutor>();
        m_DerivedJobRegistry =
            std::make_unique<DerivedJobRegistry>(*m_StreamingExecutor);

        // ── 5. Asset service ──────────────────────────────────────────────
        m_AssetService = std::make_unique<Assets::AssetService>();

        // ── 5b. GPU asset cache ───────────────────────────────────────────
        // Bridges AssetId to refcounted Buffer/Texture leases.  Subscribes
        // to AssetEventBus for Failed / Reloaded / Destroyed transitions;
        // type-specific bridges drive RequestUpload separately. The cache
        // receives the renderer's `SamplerManager` so RUNTIME-070's fallback
        // texture (and future texture-asset bridges) can resolve sampler
        // descriptors through the deduplicated manager path.
        m_GpuAssetCache = std::make_unique<Graphics::GpuAssetCache>(
            m_Renderer->GetBufferManager(),
            m_Renderer->GetTextureManager(),
            m_Renderer->GetSamplerManager(),
            m_Device->GetTransferQueue());
        m_KMeansGpuJobs = std::make_unique<RuntimeKMeansGpuJobQueue>(
            *m_Device,
            m_Renderer->GetBufferManager(),
            m_Device->GetTransferQueue());
        m_Renderer->SetRuntimeFrameCommandHook(
            [this](RHI::ICommandContext& commandContext)
            {
                if (m_KMeansGpuJobs)
                    m_KMeansGpuJobs->AdvanceGpuWork(commandContext);
            });

        // RUNTIME-070: bootstrap the runtime-owned 4×4 magenta-and-black
        // checkerboard fallback texture exactly once. Skipped when the
        // device is non-operational (e.g. the Null backend) — material
        // resolution then returns `GpuAssetFallbackReason::Unavailable` and
        // shaders route to factor-only shading, matching the documented
        // contract in `src/graphics/assets/README.md`.
        if (m_Device->IsOperational())
        {
            const Graphics::GpuTextureFallbackDesc fallbackDesc =
                BuildFallbackTextureDesc();
            if (auto r = m_GpuAssetCache->InitializeFallbackTexture(fallbackDesc);
                !r.has_value())
            {
                Core::Log::Warn(
                    "[Runtime] GpuAssetCache fallback texture bootstrap failed: error={}; material code will use factor-only fallback.",
                    static_cast<int>(r.error()));
            }
        }

        m_GpuAssetCacheListener = m_AssetService->SubscribeAll(
            [cache = m_GpuAssetCache.get()](Assets::AssetId id, Assets::AssetEvent ev)
            {
                switch (ev)
                {
                case Assets::AssetEvent::Failed:    cache->NotifyFailed(id);    break;
                case Assets::AssetEvent::Reloaded:  cache->NotifyReloaded(id);  break;
                case Assets::AssetEvent::Destroyed: cache->NotifyDestroyed(id); break;
                case Assets::AssetEvent::Ready:     /* no-op: type-specific bridges
                                                       drive RequestUpload */    break;
                }
            });
        // ── 6. ECS scene ──────────────────────────────────────────────────
        m_Scene = std::make_unique<ECS::Scene::Registry>();

        m_AssetModelTextureHandoff = std::make_unique<AssetModelTextureHandoff>(
            *m_AssetService,
            *m_GpuAssetCache);
        m_AssetModelSceneHandoff = std::make_unique<AssetModelSceneHandoff>(
            *m_AssetService,
            *m_GpuAssetCache,
            *m_Scene,
            *m_Renderer);

        // RUNTIME-092 Slice B — attach the runtime-owned stable-entity lookup
        // to the selection authority so render-id resolution flows through the
        // single runtime sidecar (which decodes + validates against the
        // registry) rather than a bare cast. The lookup is rebuilt each frame
        // in RunFrame before the pick-readback drain.
        m_SelectionController.SetStableEntityLookup(&m_StableEntityLookup);

        // ── 6b. Reference scene bootstrap (GRAPHICS-029A/B) ───────────────
        // Opt-in: only fires when EngineConfig::ReferenceScene::Enabled is
        // true. The default-off path leaves m_ReferenceScenePopulation
        // empty so RenderExtraction observes zero candidates. Double-install
        // is rejected via std::terminate to match the registry's
        // GRAPHICS-029 Decision 7 invariant.
        //
        // GRAPHICS-029B: install the production default providers for any
        // selector that does not yet have an explicit registration so the
        // resolve path is always covered without colliding with the strict
        // double-install guard.
        if (m_Config.ReferenceScene.Enabled)
        {
            if (m_ReferenceSceneInstalled)
                std::terminate();

            RegisterDefaultReferenceProvidersIfAbsent(m_ReferenceSceneRegistry);

            IReferenceSceneProvider& provider =
                m_ReferenceSceneRegistry.Resolve(m_Config.ReferenceScene.Selector);
            m_ReferenceScenePopulation = provider.Populate(*m_Scene);
            m_ReferenceCamera = m_ReferenceScenePopulation.Camera;
            m_ReferenceSceneInstalled = true;
        }

        // ── 7. Application ────────────────────────────────────────────────
        m_Application->OnInitialize(*this);

        m_Initialized = true;
        m_Running     = true;
        m_WindowCloseLogged = false;

        m_Window->Listen(
            [this](const Platform::Event& event)
            {
                HandlePlatformEvent(event);
            });
    }

    void Engine::Shutdown()
    {
        if (m_Window)
            m_Window->Listen({});

        // GRAPHICS-079 Slice B — detach the renderer consumer before the adapter
        // shuts the shared overlay system down, so the renderer never observes a
        // borrowed but inactive overlay during the rest of teardown.
        if (m_Renderer)
            m_Renderer->SetImGuiOverlaySystem(nullptr);
        // RUNTIME-090 Slice B — tear the Dear ImGui adapter down while the
        // Window and overlay system it references are still alive. The adapter
        // destructor shuts the overlay system + ImGui context down; the overlay
        // system value member is reusable on a later re-Initialize().
        m_ImGuiAdapter.reset();
        if (m_Renderer)
            m_Renderer->SetRuntimeFrameCommandHook({});
        // Runtime K-Means GPU jobs own direct compute pipeline handles and
        // cache BufferLease values. Detach the hook first, wait for any in-flight
        // frame commands to retire, then drop the leases before renderer/device
        // teardown.
        if (m_KMeansGpuJobs && m_Device)
            m_Device->WaitIdle();
        m_KMeansGpuJobs.reset();

        struct ShutdownHooks final : Core::IShutdownHooks
        {
            Engine& Owner;
            bool& Running;
            bool& Initialized;
            std::unique_ptr<IApplication>& Application;
            std::unique_ptr<Platform::IWindow>& Window;
            std::unique_ptr<RHI::IDevice>& Device;
            std::unique_ptr<Graphics::IRenderer>& Renderer;
            std::unique_ptr<Core::FrameGraph>& FrameGraph;
            std::unique_ptr<StreamingExecutor>& StreamingExecutorPtr;
            std::unique_ptr<DerivedJobRegistry>& DerivedJobRegistryPtr;
            std::unique_ptr<Assets::AssetService>& AssetService;
            std::unique_ptr<Graphics::GpuAssetCache>& GpuAssetCache;
            std::unique_ptr<AssetModelTextureHandoff>& AssetModelTextureHandoffPtr;
            std::unique_ptr<AssetModelSceneHandoff>& AssetModelSceneHandoffPtr;
            Assets::AssetEventBus::ListenerToken& GpuAssetCacheListener;
            std::unique_ptr<ECS::Scene::Registry>& Scene;
            ReferenceSceneRegistry& ReferenceRegistry;
            ReferenceScenePopulation& ReferencePopulation;
            std::optional<Graphics::CameraViewInput>& ReferenceCameraSeed;
            CameraControllerRegistry& CameraControllers;
            bool& ReferenceInstalled;
            Core::Config::ReferenceSceneSelector ReferenceSelector;
            bool ReferenceEnabled;

            ShutdownHooks(Engine& owner,
                          bool& running,
                          bool& initialized,
                          std::unique_ptr<IApplication>& application,
                          std::unique_ptr<Platform::IWindow>& window,
                          std::unique_ptr<RHI::IDevice>& device,
                          std::unique_ptr<Graphics::IRenderer>& renderer,
                          std::unique_ptr<Core::FrameGraph>& frameGraph,
                          std::unique_ptr<StreamingExecutor>& streamingExecutor,
                          std::unique_ptr<DerivedJobRegistry>& derivedJobRegistry,
                          std::unique_ptr<Assets::AssetService>& assetService,
                          std::unique_ptr<Graphics::GpuAssetCache>& gpuAssetCache,
                          std::unique_ptr<AssetModelTextureHandoff>& assetModelTextureHandoff,
                          std::unique_ptr<AssetModelSceneHandoff>& assetModelSceneHandoff,
                          Assets::AssetEventBus::ListenerToken& gpuAssetCacheListener,
                          std::unique_ptr<ECS::Scene::Registry>& scene,
                          ReferenceSceneRegistry& referenceRegistry,
                          ReferenceScenePopulation& referencePopulation,
                          std::optional<Graphics::CameraViewInput>& referenceCameraSeed,
                          CameraControllerRegistry& cameraControllers,
                          bool& referenceInstalled,
                          Core::Config::ReferenceSceneSelector referenceSelector,
                          bool referenceEnabled)
                : Owner(owner)
                , Running(running)
                , Initialized(initialized)
                , Application(application)
                , Window(window)
                , Device(device)
                , Renderer(renderer)
                , FrameGraph(frameGraph)
                , StreamingExecutorPtr(streamingExecutor)
                , DerivedJobRegistryPtr(derivedJobRegistry)
                , AssetService(assetService)
                , GpuAssetCache(gpuAssetCache)
                , AssetModelTextureHandoffPtr(assetModelTextureHandoff)
                , AssetModelSceneHandoffPtr(assetModelSceneHandoff)
                , GpuAssetCacheListener(gpuAssetCacheListener)
                , Scene(scene)
                , ReferenceRegistry(referenceRegistry)
                , ReferencePopulation(referencePopulation)
                , ReferenceCameraSeed(referenceCameraSeed)
                , CameraControllers(cameraControllers)
                , ReferenceInstalled(referenceInstalled)
                , ReferenceSelector(referenceSelector)
                , ReferenceEnabled(referenceEnabled)
            {
            }

            void StopRunning() override { Running = false; }
            void WaitDeviceIdle() override
            {
                if (Device)
                    Device->WaitIdle();
            }
            void ShutdownApplication() override
            {
                if (Application)
                    Application->OnShutdown(Owner);
            }
            void ShutdownStreaming() override
            {
                if (StreamingExecutorPtr)
                    StreamingExecutorPtr->ShutdownAndDrain();
            }
            void DestroyScene() override
            {
                // The model-scene handoff borrows the scene and renderer, so
                // detach it before provider teardown or wholesale scene reset.
                AssetModelSceneHandoffPtr.reset();

                // Reference scene teardown (GRAPHICS-029A/B): route entity
                // destruction through the same provider that authored them
                // before the scene registry is wholesale destroyed, and
                // clear the cached camera seed so a re-Initialize loop does
                // not republish a stale reference camera.
                if (ReferenceEnabled && ReferenceInstalled && Scene)
                {
                    if (IReferenceSceneProvider* provider =
                            ReferenceRegistry.ResolveOrNull(ReferenceSelector))
                    {
                        provider->Teardown(*Scene, ReferencePopulation.Entities);
                    }
                    ReferencePopulation = ReferenceScenePopulation{};
                    ReferenceInstalled = false;
                }
                ReferenceCameraSeed.reset();
                CameraControllers = CameraControllerRegistry{};
                Scene.reset();
            }
            void DestroyAssets() override
            {
                // Unsubscribe before destroying the cache so a late event
                // flush cannot reach a freed cache.  The cache is destroyed
                // before the renderer (which owns Buffer/Texture managers)
                // so leases unwind through live managers.
                AssetModelTextureHandoffPtr.reset();
                if (AssetService &&
                    GpuAssetCacheListener != Assets::AssetEventBus::InvalidToken)
                {
                    AssetService->UnsubscribeAll(GpuAssetCacheListener);
                    GpuAssetCacheListener = Assets::AssetEventBus::InvalidToken;
                }
                GpuAssetCache.reset();
                AssetService.reset();
            }
            void DestroyStreamingState() override
            {
                DerivedJobRegistryPtr.reset();
                StreamingExecutorPtr.reset();
            }
            void DestroyFrameGraph() override { FrameGraph.reset(); }
            void ShutdownRenderer() override
            {
                if (Renderer)
                {
                    Owner.m_RenderExtraction.Shutdown(*Renderer);
                    Renderer->Shutdown();
                    Renderer.reset();
                }
            }
            void ShutdownDevice() override
            {
                if (Device)
                {
                    Device->Shutdown();
                    Device.reset();
                }
            }
            void DestroyWindow() override { Window.reset(); }
            void ShutdownScheduler() override
            {
                // Shut down the fiber scheduler last — worker threads must exit cleanly
                // before any other thread-local storage or allocators are destroyed.
                Core::Tasks::Scheduler::Shutdown();
            }
            void MarkUninitialized() override { Initialized = false; }
        };

        ShutdownHooks hooks(*this,
                            m_Running,
                            m_Initialized,
                            m_Application,
                            m_Window,
                            m_Device,
                            m_Renderer,
                            m_FrameGraph,
                            m_StreamingExecutor,
                            m_DerivedJobRegistry,
                            m_AssetService,
                            m_GpuAssetCache,
                            m_AssetModelTextureHandoff,
                            m_AssetModelSceneHandoff,
                            m_GpuAssetCacheListener,
                            m_Scene,
                            m_ReferenceSceneRegistry,
                            m_ReferenceScenePopulation,
                            m_ReferenceCamera,
                            m_CameraControllers,
                            m_ReferenceSceneInstalled,
                            m_Config.ReferenceScene.Selector,
                            m_Config.ReferenceScene.Enabled);
        Core::ExecuteShutdownContract(hooks);
    }

    // ── Main loop ─────────────────────────────────────────────────────────

    void Engine::Run()
    {
        while (m_Running && m_Window != nullptr && !m_Window->ShouldClose())
            RunFrame();

        if (m_Running && m_Window != nullptr && m_Window->ShouldClose())
            RequestExitFromWindowClose("native-poll");
    }

    void Engine::RunFrame()
    {
        RuntimeFrameContext frameContext{};
        RuntimeFramePacingDiagnostics pacing{};
        pacing.Valid = true;
        pacing.FrameIndex = m_FrameIndex;
        const auto framePacingBegin = std::chrono::steady_clock::now();
        const auto publishPacingSample = [&]()
        {
            if (m_ImGuiAdapter)
            {
                const ImGuiAdapterDiagnostics& imgui = m_ImGuiAdapter->GetDiagnostics();
                pacing.ImGuiEditorCallbackMicros = imgui.LastEditorCallbackMicros;
                pacing.ImGuiDrawDataCopyMicros = imgui.LastDrawDataCopyMicros;
                pacing.ImGuiDrawListCount = imgui.LastDrawListCount;
                pacing.ImGuiVertexCount = imgui.LastVertexCount;
                pacing.ImGuiIndexCount = imgui.LastIndexCount;
                pacing.ImGuiCommandCount = imgui.LastCommandCount;
                pacing.ImGuiFontAtlasCopyCount = imgui.FontAtlasCopyCount;
                pacing.ImGuiFontAtlasReuseCount = imgui.FontAtlasReuseCount;
                pacing.ImGuiFontAtlasCopied = imgui.LastFrameFontAtlasCopied;
                pacing.ImGuiFrameUsedUserTexture = imgui.LastFrameUsedUserTexture;
                pacing.ImGuiFontAtlasByteCount = imgui.LastFontAtlasByteCount;
                pacing.ImGuiFontAtlasCopyBytes = imgui.LastFrameFontAtlasCopyBytes;
                pacing.ImGuiVertexCopyBytes = imgui.LastFrameVertexCopyBytes;
                pacing.ImGuiIndexCopyBytes = imgui.LastFrameIndexCopyBytes;
                pacing.ImGuiCommandCopyBytes = imgui.LastFrameCommandCopyBytes;
                pacing.ImGuiOverlayCopyBytes = imgui.LastFrameOverlayCopyBytes;
            }
            if (m_Renderer)
            {
                const Graphics::RenderGraphFrameStats& stats =
                    m_Renderer->GetLastRenderGraphStats();
                pacing.RenderGraphCompileMicros = stats.Compile.TimeMicros;
                pacing.RenderGraphExecuteMicros = stats.Execute.TimeMicros;
            }
            pacing.TotalMicros = ElapsedMicros(framePacingBegin);
            m_LastFramePacingDiagnostics = pacing;
        };

        // ── Phase 1: Platform ─────────────────────────────────────────────
        PlatformFrameHooks platformHooks{*m_Window};
        const auto platformBegin = std::chrono::steady_clock::now();
        const Core::PlatformFrameResult platformResult =
            Core::ExecutePlatformBeginFrameContract(platformHooks,
                                                    kIdleSleepSeconds);
        pacing.PlatformBeginMicros = ElapsedMicros(platformBegin);
        pacing.PlatformContinueFrame = platformResult.ContinueFrame;
        if (platformResult.ShouldClose)
        {
            RequestExitFromWindowClose("platform-poll");
            publishPacingSample();
            return;
        }
        if (!m_Running)
        {
            publishPacingSample();
            return;
        }

        m_FrameClock.BeginFrame();

        if (!platformResult.ContinueFrame)
        {
            m_FrameClock.Resample();
            publishPacingSample();
            return;
        }

        // Swapchain resize: drain GPU, resize resources, then proceed normally.
        const auto resizeBegin = std::chrono::steady_clock::now();
        if (m_Window->WasResized())
        {
            const auto extent = m_Window->GetFramebufferExtent();
            if (extent.Width > 0 && extent.Height > 0)
            {
                m_Device->WaitIdle();
                m_Device->Resize(static_cast<unsigned>(extent.Width),
                                 static_cast<unsigned>(extent.Height));
                m_Renderer->Resize(static_cast<unsigned>(extent.Width),
                                   static_cast<unsigned>(extent.Height));
            }
            m_Window->AcknowledgeResize();
        }
        pacing.ResizeMicros = ElapsedMicros(resizeBegin);

        OperationalTransitionHooks operationalHooks(*m_Device, *m_Renderer, m_RendererOperational);
        const auto operationalBegin = std::chrono::steady_clock::now();
        (void)Core::ExecuteOperationalTransitionContract(operationalHooks);
        pacing.OperationalTransitionMicros = ElapsedMicros(operationalBegin);

        // ── Phase 2: Fixed-step simulation + CPU task graph ───────────────
        // Each tick: app adds FrameGraph passes → Engine compiles and executes
        // the ECS system DAG → reset for next tick.

        const double frameDt = m_FrameClock.FrameDeltaClamped(m_MaxFrameDelta);
        frameContext.FrameDeltaSeconds = frameDt;
        m_Accumulator += frameDt;

        const auto fixedStepBegin = std::chrono::steady_clock::now();
        RunFixedStepSimulationTicks(*this,
                                    *m_Application,
                                    *m_FrameGraph,
                                    *m_Scene,
                                    m_Accumulator,
                                    m_FixedDt,
                                    m_MaxSubSteps);
        pacing.FixedStepMicros = ElapsedMicros(fixedStepBegin);

        const double alpha = m_Accumulator / m_FixedDt;
        frameContext.FixedStepAlpha = alpha;

        // ── RUNTIME-090 Slice B: open the Dear ImGui frame ────────────────
        // BeginFrame runs after Window::PollEvents (Phase 1) and the
        // minimize/resize early returns, immediately before the variable tick,
        // so the editor hook and any ImGui draws issued during OnVariableTick
        // run inside the NewFrame()/Render() scope. Minimized frames return
        // before this point, so a NewFrame is never left without a matching
        // Render() in EndFrame.
        const auto imguiBegin = std::chrono::steady_clock::now();
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->BeginFrame(frameDt);
        pacing.ImGuiBeginMicros = ElapsedMicros(imguiBegin);

        // ── Phase 3: Variable tick ────────────────────────────────────────
        const auto variableTickBegin = std::chrono::steady_clock::now();
        m_Application->OnVariableTick(*this, alpha, frameDt);
        pacing.VariableTickMicros = ElapsedMicros(variableTickBegin);

        // ── RUNTIME-090 Slice B: close the Dear ImGui frame ───────────────
        // EndFrame runs after the variable tick and before the render
        // contract's IRenderer::PrepareFrame(): it invokes the editor hook,
        // calls ImGui::Render(), walks ImDrawData, and submits one
        // ImGuiOverlayFrame to the overlay system (per GRAPHICS-013CQ). The
        // renderer consumer is attached in Initialize(); graphics-side
        // draw upload + recorded Pass.ImGui execution remain later GRAPHICS-079
        // slices.
        const auto imguiEnd = std::chrono::steady_clock::now();
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->EndFrame();
        pacing.ImGuiEndMicros = ElapsedMicros(imguiEnd);

        const bool imguiCapturesMouse =
            m_ImGuiAdapter != nullptr && m_ImGuiAdapter->WantsMouseCapture();
        const bool imguiCapturesKeyboard =
            m_ImGuiAdapter != nullptr && m_ImGuiAdapter->WantsKeyboardCapture();
        const bool imguiCapturesInput = imguiCapturesMouse || imguiCapturesKeyboard;

        // ── Phase 4: Build render snapshot ────────────────────────────────
        const auto preRenderSetupBegin = std::chrono::steady_clock::now();
        const Platform::Extent2D viewport = m_Window->GetFramebufferExtent();
        frameContext.RenderInput = Graphics::RenderFrameInput{
            .Alpha    = alpha,
            .Viewport = viewport,
        };
        Graphics::RenderFrameInput& renderInput = frameContext.RenderInput;

        const Platform::IWindow& inputWindow = *m_Window;
        PopulateMainCameraForFrame(m_Config,
                                   m_CameraControllers,
                                   m_ReferenceCamera,
                                   inputWindow,
                                   viewport,
                                   frameDt,
                                   imguiCapturesInput,
                                   renderInput);
        DriveGizmoAndSelectionInputForFrame(m_GizmoInteraction,
                                            m_GizmoUndoStack,
                                            *m_Scene,
                                            m_SelectionController,
                                            inputWindow,
                                            viewport,
                                            imguiCapturesInput,
                                            imguiCapturesMouse,
                                            m_GizmoSelectedEntities,
                                            renderInput.Camera);
        pacing.PreRenderSetupMicros += ElapsedMicros(preRenderSetupBegin);

        // ── BUG-024: pre-render transform flush ───────────────────────────
        // Local-transform mutations made after the fixed-step ECS bundle —
        // Sandbox Editor UI inspector edits (applied inside the ImGui editor
        // hook during EndFrame above), OnVariableTick app mutations, and the
        // GizmoInteraction drag just driven — would otherwise reach render
        // extraction with a stale Transform::WorldMatrix and only become
        // visible one frame late (or never, when no further fixed-step tick
        // runs). Flush TransformHierarchy → BoundsPropagation → RenderSync
        // here, before the transform-gizmo packets are built and before
        // ExtractRenderWorld observes the scene, so the rendered model
        // matrix and the gizmo packets agree with the authored transform in
        // the same frame.
        const auto preRenderFlushBegin = std::chrono::steady_clock::now();
        (void)FlushPreRenderTransformState(*m_Scene);
        pacing.PreRenderTransformFlushMicros =
            ElapsedMicros(preRenderFlushBegin);

        // RUNTIME-116: `F` (focus) reframes the Main camera on the current
        // selection so the selected object(s) are centered and fully visible.
        // It runs *after* the pre-render flush above (BUG-024) so it gathers
        // refreshed `World::Bounds` that already reflect this frame's
        // OnVariableTick / editor-hook / gizmo transform edits, not the stale
        // pre-flush bounds. Edge-triggered and suppressed while Dear ImGui owns
        // the keyboard (e.g. typing in a field). On a successful reframe the
        // camera view is rebuilt so the snapped camera reaches the transform-
        // gizmo packets and render extraction this same frame.
        const auto postFlushSetupBegin = std::chrono::steady_clock::now();
        FocusMainCameraOnSelectionForFrame(m_Config,
                                           m_CameraControllers,
                                           m_SelectionController,
                                           *m_Scene,
                                           inputWindow,
                                           viewport,
                                           imguiCapturesKeyboard,
                                           renderInput);

        const std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos =
            m_GizmoPacketBuilder.Build(*m_Scene,
                                       m_GizmoSelectedEntities,
                                       m_GizmoInteraction.Mode(),
                                       m_GizmoInteraction.Orientation(),
                                       m_GizmoInteraction.Config().AxisLength);
        pacing.PreRenderSetupMicros += ElapsedMicros(postFlushSetupBegin);

        // ── RUNTIME-089 / BUG-026: drain coalesced selection pick ─────────
        const auto selectionPickDrainBegin = std::chrono::steady_clock::now();
        DrainPendingSelectionPickForFrame(m_SelectionController,
                                          m_Renderer->GetSelectionSystem(),
                                          m_InFlightPickContexts,
                                          viewport,
                                          renderInput);
        pacing.SelectionPickDrainMicros =
            ElapsedMicros(selectionPickDrainBegin);

        // ── Phases 5–9: promoted render-frame contract ───────────────────
        RHI::FrameHandle frame{};
        Graphics::RenderWorld renderWorld{};

        // GRAPHICS-036C — the render-world pool slot lifecycle is driven around
        // extraction inside the hook (producer: AcquireBack/PublishFront;
        // consumer: AcquireFront) and the front reference is released after the
        // frame retires below. `frameIndex` stamps the acquired slot so the
        // consumer's frame-age diagnostic reads 0 in the synchronous baseline.
        frameContext.FrameIndex = m_FrameIndex++;

        RuntimeRenderFrameHooks renderHooks(*m_Renderer,
                                            *m_Scene,
                                            m_RenderExtraction,
                                            m_GpuAssetCache.get(),
                                            m_SelectionController,
                                            *m_RenderWorldPool,
                                            m_Config.Render.SynchronousExtraction,
                                            frameContext.ExtractionStats,
                                            frameContext.FrameIndex,
                                            frameContext.PooledFrontSlot,
                                            frame,
                                            renderInput,
                                            transformGizmos,
                                            renderWorld,
                                            &pacing);

        const auto renderContractBegin = std::chrono::steady_clock::now();
        const Core::RenderFrameResult renderResult = Core::ExecuteRenderFrameContract(renderHooks);
        pacing.RenderContractMicros = ElapsedMicros(renderContractBegin);
        pacing.RendererBeganFrame = renderResult.BeganFrame;
        pacing.RendererCompletedFrame = renderResult.CompletedFrame;
        m_LastExtractionStats = frameContext.ExtractionStats;
        if (!renderResult.BeganFrame)
        {
            // BeginFrame failed before extraction ran, so no slot was acquired
            // (PooledFrontSlot stays kInvalidSlot) — nothing to release.
            m_FrameClock.EndFrame();
            publishPacingSample();
            return;
        }

        const std::uint64_t completedGpuValue = renderResult.CompletedGpuValue;
        const auto presentBegin = std::chrono::steady_clock::now();
        m_Device->Present(frame);
        pacing.PresentMicros = ElapsedMicros(presentBegin);

        // ── Phase 10: Maintenance ─────────────────────────────────────────
        TransferHooks transferHooks(*m_Device);
        StreamingHooks streamingHooks(*m_StreamingExecutor,
                                      m_DerivedJobRegistry.get());
        AssetHooks assetHooks(*m_AssetService,
                              m_GpuAssetCache.get(),
                              m_AssetModelSceneHandoff.get(),
                              *m_Device,
                              m_RenderExtraction,
                              *m_Renderer);
        const auto maintenanceBegin = std::chrono::steady_clock::now();
        Core::ExecuteMaintenanceContract(transferHooks, streamingHooks, assetHooks, 8);
        if (m_KMeansGpuJobs)
            m_KMeansGpuJobs->DrainCompletedTransfers();
        pacing.MaintenanceMicros = ElapsedMicros(maintenanceBegin);

        // ── RUNTIME-092 Slice B: refresh the stable-entity lookup ──────────
        // Rebuild the runtime-owned StableId winner-map from the live registry
        // before consuming pick readbacks, so durable-id resolution and the
        // editor/serialization-facing ResolveByStableId/ResolveSelected APIs
        // observe this frame's entity set. Render-id resolution (the path the
        // controller takes for a pick hit) decodes + validates against the live
        // registry directly and does not depend on the map, so a recycled slot
        // is rejected regardless; the rebuild keeps the durable map coherent for
        // the other consumers and is the single per-frame maintenance point.
        const auto readbackBegin = std::chrono::steady_clock::now();
        m_StableEntityLookup.Rebuild(*m_Scene);

        // ── RUNTIME-089 / RUNTIME-093 / BUG-026: completed pick readbacks ──
        DrainCompletedSelectionReadbacksForFrame(m_Renderer->GetSelectionSystem(),
                                                 m_SelectionController,
                                                 *m_Scene,
                                                 m_InFlightPickContexts,
                                                 m_LastRefinedPrimitive,
                                                 m_LastRefinedPrimitiveGeneration);
        pacing.SelectionReadbackMicros = ElapsedMicros(readbackBegin);

        // completedGpuValue is the renderer's per-frame timeline value.  The
        // GpuAssetCache currently retires on the CPU frame counter (which is
        // a conservative proxy for GPU completion); a follow-up may key
        // retirement directly on completedGpuValue for tighter recycling.
        (void)completedGpuValue;

        // ── GRAPHICS-036C: release the pooled front at frame retire ────────
        // The renderer consumed the acquired snapshot this frame (commands are
        // recorded by ExecuteFrame above). Synchronous mode releases the current
        // front; pipelined mode releases the previous front consumed by render-N
        // after extraction-N has already published the new front.
        const auto releaseFrontBegin = std::chrono::steady_clock::now();
        if (frameContext.PooledFrontSlot != RenderWorldPool::kInvalidSlot)
            m_RenderWorldPool->ReleaseFront(frameContext.PooledFrontSlot);
        pacing.ReleaseRenderWorldMicros = ElapsedMicros(releaseFrontBegin);

        // ── Phase 11: Clock EndFrame ──────────────────────────────────────
        m_FrameClock.EndFrame();
        publishPacingSample();
    }

    // ── Query / control ───────────────────────────────────────────────────

    bool Engine::IsRunning() const noexcept { return m_Running; }
    void Engine::RequestExit()      noexcept { m_Running = false; }

    void Engine::RequestExitFromWindowClose(const std::string_view source)
    {
        if (!m_WindowCloseLogged)
        {
            Core::Log::Info(
                "[Runtime] Window close requested; stopping Engine::Run loop. source={}",
                source);
            m_WindowCloseLogged = true;
        }
        RequestExit();
    }

    Platform::IWindow&    Engine::GetWindow()        noexcept { return *m_Window;        }
    RHI::IDevice&         Engine::GetDevice()        noexcept { return *m_Device;        }
    Graphics::IRenderer&  Engine::GetRenderer()      noexcept { return *m_Renderer;      }
    const Core::Config::EngineConfig& Engine::GetEngineConfig() const noexcept
    {
        return m_Config;
    }

    Graphics::RenderRecipeConfigContext Engine::CreateRenderRecipeConfigContext() const
    {
        Core::Extent2D viewport{
            .Width = std::max(m_Config.Window.Width, 1),
            .Height = std::max(m_Config.Window.Height, 1),
        };
        if (m_Window)
        {
            const Platform::Extent2D extent = m_Window->GetFramebufferExtent();
            if (extent.Width > 0 && extent.Height > 0)
            {
                viewport = Core::Extent2D{.Width = extent.Width, .Height = extent.Height};
            }
        }

        const Graphics::RenderFrameInput recipeInput{
            .Viewport = viewport,
            .Camera = Graphics::CameraViewInput{.Valid = true},
        };
        return Graphics::RenderRecipeConfigContext{
            .Renderer = Graphics::MakeCurrentRendererDescriptor(),
            .BaseRecipe = Graphics::MakeCurrentRendererRecipeDescriptor(),
            .BaseViewOutput =
                Graphics::MakeCurrentRendererViewOutputRecipe(recipeInput),
            .BaseBindings = Graphics::MakeCurrentRendererBindingSet(),
        };
    }

    Graphics::RenderRecipeConfigLoadResult Engine::PreviewRenderRecipeConfigDocument(
        const std::string_view document,
        std::string sourceId) const
    {
        return Graphics::PreviewRenderRecipeConfig(
            document,
            CreateRenderRecipeConfigContext(),
            Graphics::RenderRecipeConfigParseOptions{
                .SourceId = std::move(sourceId),
            });
    }

    Graphics::RenderRecipeConfigLoadResult Engine::LoadRenderRecipeConfigPreviewFile(
        std::string path) const
    {
        const std::string sourceId = path;
        return Graphics::LoadRenderRecipeConfigFile(
            path,
            CreateRenderRecipeConfigContext(),
            Graphics::RenderRecipeConfigParseOptions{
                .SourceId = sourceId,
            });
    }

    RuntimeRenderRecipeApplyResult Engine::ActivateRenderRecipeConfigDocument(
        const std::string_view document,
        std::string sourceId,
        const RuntimeRenderRecipeActivationSource source)
    {
        const Graphics::RenderRecipeConfigLoadResult loadResult =
            PreviewRenderRecipeConfigDocument(document, std::move(sourceId));
        return ApplyRenderRecipeConfigPreview(loadResult, source);
    }

    RuntimeRenderRecipeApplyResult Engine::ApplyRenderRecipeConfigPreview(
        const Graphics::RenderRecipeConfigLoadResult& loadResult,
        const RuntimeRenderRecipeActivationSource source)
    {
        RuntimeRenderRecipeApplyResult result{
            .Source = source,
            .LoadResult = loadResult,
        };

        if (!m_Renderer)
        {
            result.Status = RuntimeRenderRecipeApplyStatus::MissingRenderer;
            m_RenderRecipeState.LastApply = result;
            m_RenderRecipeState.HasLastApply = true;
            return result;
        }

        if (!Graphics::IsConfigUsable(loadResult))
        {
            m_Renderer->ClearActiveFrameRecipeOverride();
            m_RenderRecipeState.ActiveOverride.reset();
            m_RenderRecipeState.ActiveConfig = Graphics::RenderRecipeConfigLoadResult{};
            m_RenderRecipeState.HasActiveConfig = false;
            m_RenderRecipeState.ActiveSource =
                RuntimeRenderRecipeActivationSource::None;
            result.Status = RuntimeRenderRecipeApplyStatus::Rejected;
            m_RenderRecipeState.LastApply = result;
            m_RenderRecipeState.HasLastApply = true;
            return result;
        }

        Graphics::FrameRecipeOverride recipeOverride{
            .Recipe = loadResult.Preview.Recipe,
            .DisabledExtensionSlots = loadResult.Preview.DisabledExtensionSlots,
            .SourceId = loadResult.SourceId,
        };
        m_Renderer->SetActiveFrameRecipeOverride(std::make_optional(recipeOverride));
        m_RenderRecipeState.ActiveOverride = recipeOverride;
        m_RenderRecipeState.ActiveConfig = loadResult;
        m_RenderRecipeState.HasActiveConfig = true;
        m_RenderRecipeState.ActiveSource = source;
        result.Status = RuntimeRenderRecipeApplyStatus::Applied;
        result.RendererOverrideInstalled = true;
        m_RenderRecipeState.LastApply = result;
        m_RenderRecipeState.HasLastApply = true;
        return result;
    }

    RuntimeRenderRecipeApplyResult Engine::LoadAndApplyRenderRecipeConfigFile(
        std::string path,
        const RuntimeRenderRecipeActivationSource source)
    {
        const std::string sourceId = path;
        const Graphics::RenderRecipeConfigLoadResult loadResult =
            Graphics::LoadRenderRecipeConfigFile(
                path,
                CreateRenderRecipeConfigContext(),
                Graphics::RenderRecipeConfigParseOptions{
                    .SourceId = sourceId,
                });
        return ApplyRenderRecipeConfigPreview(loadResult, source);
    }

    void Engine::ClearActiveRenderRecipeOverride() noexcept
    {
        if (m_Renderer)
        {
            m_Renderer->ClearActiveFrameRecipeOverride();
        }
        m_RenderRecipeState.ActiveOverride.reset();
        m_RenderRecipeState.ActiveConfig = Graphics::RenderRecipeConfigLoadResult{};
        m_RenderRecipeState.HasActiveConfig = false;
        m_RenderRecipeState.ActiveSource = RuntimeRenderRecipeActivationSource::None;
    }

    const RuntimeRenderRecipeState& Engine::GetRenderRecipeState() const noexcept
    {
        return m_RenderRecipeState;
    }

    Core::Config::EngineConfigLoadResult Engine::PreviewEngineConfigControlDocument(
        const std::string_view document,
        std::string sourceId) const
    {
        return Core::Config::PreviewEngineConfig(
            document,
            m_Config,
            Core::Config::EngineConfigParseOptions{
                .SourceId = std::move(sourceId),
            });
    }

    Core::Config::EngineConfigLoadResult Engine::LoadEngineConfigControlFile(
        std::string path) const
    {
        const std::string sourceId = path;
        return Core::Config::LoadEngineConfigFile(
            path,
            m_Config,
            Core::Config::EngineConfigParseOptions{
                .SourceId = sourceId,
            });
    }

    RuntimeEngineConfigApplyResult Engine::ApplyEngineConfigHotSubset(
        const Core::Config::EngineConfigLoadResult& loadResult,
        const RuntimeConfigControlSource source)
    {
        RuntimeEngineConfigApplyResult result{
            .Source = source,
            .LoadResult = loadResult,
        };

        if (!Core::Config::IsConfigUsable(loadResult))
        {
            result.Status = RuntimeEngineConfigApplyStatus::Rejected;
            m_ConfigControlState.LastApply = result;
            m_ConfigControlState.HasLastApply = true;
            return result;
        }

        const Core::Config::EngineConfig& candidate = loadResult.Preview.Config;
        result.RejectedBootOnlyFields =
            FindBootOnlyEngineConfigDifferences(m_Config, candidate);
        if (!result.RejectedBootOnlyFields.empty())
        {
            result.Status = RuntimeEngineConfigApplyStatus::Rejected;
            m_ConfigControlState.LastApply = result;
            m_ConfigControlState.HasLastApply = true;
            return result;
        }

        const bool recipePathChanged =
            m_Config.Render.DefaultRecipeConfigPath !=
            candidate.Render.DefaultRecipeConfigPath;
        const bool progressivePoissonChanged =
            !ProgressivePoissonPlaygroundConfigEquals(
                m_Config.Sandbox.ProgressivePoisson,
                candidate.Sandbox.ProgressivePoisson);
        result.DefaultRecipeConfigPathChanged = recipePathChanged;
        result.SandboxProgressivePoissonChanged = progressivePoissonChanged;
        if (!recipePathChanged && !progressivePoissonChanged)
        {
            result.Status = RuntimeEngineConfigApplyStatus::NoChange;
            m_ConfigControlState.ActiveConfig = m_Config;
            m_ConfigControlState.LastApply = result;
            m_ConfigControlState.HasLastApply = true;
            return result;
        }

        if (recipePathChanged && !candidate.Render.DefaultRecipeConfigPath.empty())
        {
            const Graphics::RenderRecipeConfigLoadResult recipeLoadResult =
                LoadRenderRecipeConfigPreviewFile(candidate.Render.DefaultRecipeConfigPath);
            result.RecipeApply = RuntimeRenderRecipeApplyResult{
                .Source = ToRecipeActivationSource(source),
                .LoadResult = recipeLoadResult,
            };
            if (!Graphics::IsConfigUsable(recipeLoadResult))
            {
                result.RecipeApply.Status =
                    RuntimeRenderRecipeApplyStatus::Rejected;
                result.Status = RuntimeEngineConfigApplyStatus::Rejected;
                m_ConfigControlState.LastApply = result;
                m_ConfigControlState.HasLastApply = true;
                return result;
            }

            result.RecipeApply = ApplyRenderRecipeConfigPreview(
                recipeLoadResult,
                ToRecipeActivationSource(source));
            if (!result.RecipeApply.Succeeded())
            {
                result.Status = RuntimeEngineConfigApplyStatus::Rejected;
                m_ConfigControlState.LastApply = result;
                m_ConfigControlState.HasLastApply = true;
                return result;
            }
        }

        if (recipePathChanged)
        {
            m_Config.Render.DefaultRecipeConfigPath =
                candidate.Render.DefaultRecipeConfigPath;
            if (candidate.Render.DefaultRecipeConfigPath.empty())
            {
                ClearActiveRenderRecipeOverride();
            }
        }
        if (progressivePoissonChanged)
        {
            m_Config.Sandbox.ProgressivePoisson =
                candidate.Sandbox.ProgressivePoisson;
        }
        m_ConfigControlState.ActiveConfig = m_Config;
        result.Status = RuntimeEngineConfigApplyStatus::Applied;
        result.EngineConfigApplied = true;
        m_ConfigControlState.LastApply = result;
        m_ConfigControlState.HasLastApply = true;
        return result;
    }

    RuntimeEngineConfigApplyResult Engine::LoadAndApplyEngineConfigHotSubsetFile(
        std::string path,
        const RuntimeConfigControlSource source)
    {
        const Core::Config::EngineConfigLoadResult loadResult =
            LoadEngineConfigControlFile(std::move(path));
        return ApplyEngineConfigHotSubset(loadResult, source);
    }

    const RuntimeEngineConfigControlState& Engine::GetEngineConfigControlState()
        const noexcept
    {
        return m_ConfigControlState;
    }

    Assets::AssetService& Engine::GetAssetService()  noexcept { return *m_AssetService;  }
    Graphics::GpuAssetCache& Engine::GetGpuAssetCache() noexcept { return *m_GpuAssetCache; }
    ECS::Scene::Registry& Engine::GetScene()         noexcept { return *m_Scene;         }
    GizmoInteraction& Engine::GetGizmoInteraction() noexcept { return m_GizmoInteraction; }
    const GizmoInteraction& Engine::GetGizmoInteraction() const noexcept { return m_GizmoInteraction; }
    GizmoUndoStack& Engine::GetGizmoUndoStack() noexcept { return m_GizmoUndoStack; }
    const GizmoUndoStack& Engine::GetGizmoUndoStack() const noexcept { return m_GizmoUndoStack; }

    const RenderWorldPool& Engine::GetRenderWorldPool() const noexcept { return *m_RenderWorldPool; }
    const RuntimeRenderExtractionStats& Engine::GetLastRenderExtractionStats() const noexcept
    {
        return m_LastExtractionStats;
    }

    const RuntimeFramePacingDiagnostics&
    Engine::GetLastFramePacingDiagnostics() const noexcept
    {
        return m_LastFramePacingDiagnostics;
    }

    std::optional<Graphics::MaterialTextureAssetBindings>
    Engine::GetMaterialTextureAssetBindingsForTest(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_RenderExtraction.GetMaterialTextureAssetBindings(stableEntityId);
    }

    Core::Expected<RuntimeAssetImportResult> Engine::ImportAssetFromPath(
        RuntimeAssetImportRequest request)
    {
        auto result = ImportAssetFromPathWithIngest(
            std::move(request),
            RuntimeAssetIngestSource::ManualImport,
            {});
        if (result.has_value() && CreatesOrChangesScene(*result))
        {
            (void)m_EditorCommandHistory.MarkDirty("Import Asset");
        }
        return result;
    }

    Core::Expected<RuntimeQueuedAssetImport> Engine::QueueModelTextureImport(
        RuntimeAssetImportRequest request)
    {
        return QueueModelTextureImportWithIngest(
            std::move(request),
            RuntimeAssetIngestSource::ManualImport,
            {});
    }

    Core::Expected<RuntimeAssetImportResult> Engine::ReimportAsset(
        RuntimeAssetReimportRequest request)
    {
        RuntimeAssetImportRequest importRequest{
            .PayloadKind = request.PayloadKind,
        };

        if (m_AssetService && request.Asset.IsValid() &&
            m_AssetService->IsAlive(request.Asset))
        {
            auto path = m_AssetService->GetPath(request.Asset);
            if (path.has_value())
            {
                importRequest.Path = std::move(*path);
            }
            else
            {
                importRequest.Path = "<invalid-reimport-target>";
            }
            if (importRequest.PayloadKind == Assets::AssetPayloadKind::Unknown)
            {
                auto payloadKind =
                    PayloadKindForExistingAsset(*m_AssetService, request.Asset);
                if (payloadKind.has_value())
                {
                    importRequest.PayloadKind = *payloadKind;
                }
            }
        }
        else if (request.Asset.IsValid())
        {
            importRequest.Path = "<invalid-reimport-target>";
        }

        auto result = ImportAssetFromPathWithIngest(
            std::move(importRequest),
            RuntimeAssetIngestSource::Reimport,
            request.Asset);
        if (result.has_value() && CreatesOrChangesScene(*result))
        {
            (void)m_EditorCommandHistory.MarkDirty("Reimport Asset");
        }
        return result;
    }

    const std::optional<RuntimeAssetImportEvent>& Engine::GetLastAssetImportEvent()
        const noexcept
    {
        return m_LastAssetImportEvent;
    }

    std::vector<RuntimeAssetIngestRecord>
    Engine::GetAssetIngestRecordsForTest() const
    {
        return m_AssetIngestStateMachine.SnapshotAll();
    }

    RuntimeAssetImportQueueSnapshot Engine::GetAssetImportQueueSnapshot() const
    {
        RuntimeAssetImportQueueSnapshot snapshot =
            m_AssetIngestStateMachine.SnapshotQueue();

        for (RuntimeAssetImportQueueEntry& entry : snapshot.Entries)
        {
            if (entry.TerminalStatus != RuntimeAssetImportQueueTerminalStatus::None)
            {
                entry.CanCancel = false;
                entry.CancelDisabledReason =
                    "Import has already reached a terminal state.";
                continue;
            }

            const auto taskIt = std::find_if(
                m_AssetImportStreamingTasks.begin(),
                m_AssetImportStreamingTasks.end(),
                [&entry](const RuntimeAssetImportStreamingTask& task)
                {
                    return task.Ingest == entry.Operation;
                });

            if (taskIt == m_AssetImportStreamingTasks.end() ||
                !taskIt->Streaming.IsValid() ||
                !m_StreamingExecutor)
            {
                entry.CanCancel = false;
                entry.CancelDisabledReason =
                    "Import is running synchronously or has no cancellable streaming task.";
                continue;
            }

            const StreamingTaskState streamingState =
                m_StreamingExecutor->GetState(taskIt->Streaming);
            entry.CanCancel =
                QueueStageCanUseStreamingCancellation(entry.Stage) &&
                StreamingTaskStateCanCancel(streamingState);
            if (!entry.CanCancel)
            {
                entry.CancelDisabledReason =
                    "Import can no longer be cancelled before main-thread apply.";
            }
            else
            {
                entry.CancelDisabledReason.clear();
            }
        }

        return snapshot;
    }

    std::size_t Engine::ClearCompletedAssetImports()
    {
        return m_AssetIngestStateMachine.ClearCompletedQueueEntries();
    }

    Core::Result Engine::CancelAssetImport(
        const RuntimeAssetIngestHandle operation)
    {
        const std::optional<RuntimeAssetIngestRecord> record =
            m_AssetIngestStateMachine.Snapshot(operation);
        if (!record.has_value())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }
        if (IsTerminal(record->Phase))
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto taskIt = std::find_if(
            m_AssetImportStreamingTasks.begin(),
            m_AssetImportStreamingTasks.end(),
            [operation](const RuntimeAssetImportStreamingTask& task)
            {
                return task.Ingest == operation;
            });
        if (taskIt == m_AssetImportStreamingTasks.end() ||
            !taskIt->Streaming.IsValid() ||
            !m_StreamingExecutor)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        const StreamingTaskState state =
            m_StreamingExecutor->GetState(taskIt->Streaming);
        if (!StreamingTaskStateCanCancel(state))
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_StreamingExecutor->Cancel(taskIt->Streaming);
        RuntimeAssetIngestTransition cancelled =
            m_AssetIngestStateMachine.Cancel(operation);
        const bool cancelledRecord =
            cancelled.Mutated &&
            cancelled.Diagnostic == RuntimeAssetIngestDiagnostic::Cancelled;
        if (!cancelledRecord)
        {
            return Core::Err(ErrorFromIngestTransition(cancelled));
        }

        RecordAssetImportEvent(
            RuntimeAssetImportRequest{
                .Path = record->Request.Path,
                .PayloadKind = record->Request.PayloadKind,
            },
            Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
            cancelled.Diagnostic);
        return Core::Ok();
    }

    void Engine::HandlePlatformEvent(const Platform::Event& event)
    {
        if (std::holds_alternative<Platform::WindowCloseEvent>(event))
        {
            RequestExitFromWindowClose("platform-event");
            return;
        }

        if (const auto* dropped = std::get_if<Platform::WindowDropEvent>(&event))
        {
            HandleWindowDropEvent(*dropped);
        }
    }

    void Engine::DispatchPlatformEventForTest(const Platform::Event& event)
    {
        HandlePlatformEvent(event);
    }

    void Engine::HandleWindowDropEvent(const Platform::WindowDropEvent& event)
    {
        Core::Log::Info("[Runtime] File drop received: path_count={}",
                        event.Paths.size());
        ImportDroppedFilePaths(event.Paths);
    }

    void Engine::ImportDroppedFilePaths(std::span<const std::string> paths)
    {
        for (const std::string& path : paths)
        {
            if (path.empty())
            {
                Core::Log::Warn(
                    "[Runtime] Dropped file path ignored: empty path.");
                continue;
            }

            const Assets::AssetRouteDiagnostic diagnostic =
                Assets::DiagnoseAssetImportRoute(
                    path,
                    Assets::AssetRouteOperation::Import,
                    Assets::AssetImportHint{
                        .PayloadKind = Assets::AssetPayloadKind::Unknown,
                    });
            if (diagnostic.Status == Assets::AssetRouteStatus::AmbiguousPayloadKind)
            {
                const Assets::AssetFileFormatInfo* format =
                    Assets::FindAssetFileFormat(path);
                std::vector<Assets::AssetPayloadKind> geometryPayloads{};
                if (format != nullptr)
                {
                    for (const Assets::AssetPayloadKind payloadKind :
                         format->ImportPayloads)
                    {
                        if (!Assets::IsGeometryPayloadKind(payloadKind))
                            continue;
                        geometryPayloads.push_back(payloadKind);
                    }
                }
                if (!geometryPayloads.empty())
                {
                    Core::Log::Info(
                        "[Runtime] Dropped ambiguous geometry file routed to deferred import: path='{}' candidate_count={}",
                        path,
                        geometryPayloads.size());
                    QueueDroppedGeometryImport(path, std::move(geometryPayloads));
                    continue;
                }
            }

            auto route = Assets::ResolveAssetImportRoute(
                path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{
                    .PayloadKind = Assets::AssetPayloadKind::Unknown,
                });
            if (route.has_value() &&
                Assets::IsGeometryPayloadKind(route->PayloadKind))
            {
                Core::Log::Info(
                    "[Runtime] Dropped geometry file routed to deferred import: path='{}' payload={}",
                    path,
                    Assets::DebugNameForAssetPayloadKind(route->PayloadKind));
                QueueDroppedGeometryImport(path, {route->PayloadKind});
                continue;
            }
            if (route.has_value() &&
                (route->PayloadKind == Assets::AssetPayloadKind::ModelScene ||
                 route->PayloadKind == Assets::AssetPayloadKind::Texture2D))
            {
                Core::Log::Info(
                    "[Runtime] Dropped model/texture file routed to deferred import: path='{}' payload={}",
                    path,
                    Assets::DebugNameForAssetPayloadKind(route->PayloadKind));
                QueueDroppedModelTextureImport(path, route->PayloadKind);
                continue;
            }

            Core::Log::Info(
                "[Runtime] Dropped file routed to synchronous import: path='{}' route_status={} route_error={}",
                path,
                Assets::DebugNameForAssetRouteStatus(diagnostic.Status),
                Core::Error::ToString(diagnostic.Error));
            auto result = ImportAssetFromPathWithIngest(
                RuntimeAssetImportRequest{
                    .Path = path,
                    .PayloadKind = Assets::AssetPayloadKind::Unknown,
                },
                RuntimeAssetIngestSource::DroppedFile,
                {});
            if (result.has_value() && CreatesOrChangesScene(*result))
            {
                (void)m_EditorCommandHistory.MarkDirty("Import Asset");
            }
        }
    }

    void Engine::QueueDroppedGeometryImport(
        std::string path,
        std::vector<Assets::AssetPayloadKind> payloadKinds)
    {
        RuntimeAssetImportRequest request{
            .Path = path,
            .PayloadKind = payloadKinds.empty()
                ? Assets::AssetPayloadKind::Unknown
                : payloadKinds.front(),
        };
        RuntimeAssetIngestTransition submit =
            m_AssetIngestStateMachine.Submit(
                MakeRuntimeAssetIngestRequest(
                    request,
                    RuntimeAssetIngestSource::DroppedFile));
        if (!submit.Succeeded())
        {
            Core::Log::Warn(
                "[Runtime] Dropped geometry import rejected by ingest state machine: path='{}' payload={} diagnostic={} error={}",
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                DebugNameForRuntimeAssetIngestDiagnostic(submit.Diagnostic),
                Core::Error::ToString(ErrorFromIngestTransition(submit)));
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(submit)),
                submit.Diagnostic);
            return;
        }

        if (!m_Initialized ||
            !m_StreamingExecutor ||
            !m_AssetService ||
            !m_Scene ||
            path.empty() ||
            payloadKinds.empty())
        {
            RuntimeAssetIngestTransition failed =
                m_AssetIngestStateMachine.FailCallback(
                    submit.Handle,
                    Core::ErrorCode::InvalidState);
            Core::Log::Warn(
                "[Runtime] Dropped geometry import rejected before queueing: path='{}' payload={} error={}",
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                Core::Error::ToString(Core::ErrorCode::InvalidState));
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
                failed.Diagnostic);
            return;
        }

        const Assets::AssetRouteDiagnostic routeDiagnostic =
            Assets::DiagnoseAssetImportRoute(
                path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        RuntimeAssetIngestTransition routeResolved =
            m_AssetIngestStateMachine.ResolveRoute(
                submit.Handle,
                routeDiagnostic);
        if (!routeResolved.Succeeded())
        {
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(routeResolved)),
                routeResolved.Diagnostic);
            return;
        }

        RuntimeAssetIngestTransition decodeQueued =
            m_AssetIngestStateMachine.QueueDecode(submit.Handle);
        if (!decodeQueued.Succeeded())
        {
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decodeQueued)),
                decodeQueued.Diagnostic);
            return;
        }

        RuntimeAssetIngestTransition decoding =
            m_AssetIngestStateMachine.MarkDecoding(submit.Handle);
        if (!decoding.Succeeded())
        {
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decoding)),
                decoding.Diagnostic);
            return;
        }

        auto state = std::make_shared<DroppedGeometryImportState>();
        state->IngestHandle = submit.Handle;
        state->Request = request;
        const std::size_t candidateCount = payloadKinds.size();

        const StreamingTaskHandle handle = m_StreamingExecutor->Submit(
            StreamingTaskDesc{
                .Name = "Runtime.ImportDroppedGeometry." +
                    FileNameFromPath(path),
                .Kind = Core::Dag::TaskKind::AssetDecode,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = 4u,
                .Execute = [
                    state,
                    path = std::move(path),
                    payloadKinds = std::move(payloadKinds)]() mutable -> StreamingResult
                {
                    Core::ErrorCode lastError = Core::ErrorCode::Unknown;
                    for (const Assets::AssetPayloadKind payloadKind : payloadKinds)
                    {
                        RuntimeAssetImportRequest request{
                            .Path = path,
                            .PayloadKind = payloadKind,
                        };
                        auto decoded = DecodeGeometryImport(request);
                        state->Request = request;
                        if (decoded.has_value())
                        {
                            state->Decoded = std::move(*decoded);
                            state->Error = Core::ErrorCode::Success;
                            return StreamingResult{
                                StreamingCpuPayloadReady{.PayloadToken = 0u}};
                        }
                        lastError = decoded.error();
                    }

                    state->Error = lastError;
                    return StreamingResult{
                        StreamingCpuPayloadReady{.PayloadToken = 0u}};
                },
                .ApplyOnMainThread = [this, state](StreamingResult&& streamingResult) mutable
                {
                    Core::Expected<RuntimeAssetImportResult> result =
                        Core::Err<RuntimeAssetImportResult>(
                            streamingResult.has_value()
                                ? state->Error
                                : streamingResult.error());
                    RuntimeAssetIngestDiagnostic eventDiagnostic =
                        DiagnosticForImportError(result.error());

                    if (!streamingResult.has_value() || !state->Decoded.has_value())
                    {
                        RuntimeAssetIngestTransition failed =
                            result.error() == Core::ErrorCode::FileNotFound
                                ? m_AssetIngestStateMachine.MarkMissingFile(
                                      state->IngestHandle)
                                : m_AssetIngestStateMachine.FailDecode(
                                      state->IngestHandle,
                                      state->IngestHandle.Generation,
                                      result.error());
                        eventDiagnostic = failed.Diagnostic;
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            eventDiagnostic);
                        return;
                    }

                    RuntimeAssetIngestTransition decodeComplete =
                        m_AssetIngestStateMachine.CompleteDecode(
                            state->IngestHandle,
                            state->IngestHandle.Generation);
                    if (!decodeComplete.Succeeded())
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            ErrorFromIngestTransition(decodeComplete));
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            decodeComplete.Diagnostic);
                        return;
                    }

                    RuntimeAssetIngestTransition applying =
                        m_AssetIngestStateMachine.BeginApply(state->IngestHandle);
                    if (!applying.Succeeded())
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            ErrorFromIngestTransition(applying));
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            applying.Diagnostic);
                        return;
                    }

                    auto materialized = MaterializeDecodedGeometryImport(
                        *m_AssetService,
                        *m_GpuAssetCache,
                        m_RenderExtraction,
                        *m_Scene,
                        m_StreamingExecutor.get(),
                        *state->Decoded);
                    if (materialized.has_value())
                    {
                        FocusMainCameraOnImportedGeometry(
                            m_CameraControllers,
                            m_Config.Camera.Controller,
                            m_Config.Camera.Enabled,
                            materialized->Bounds);
                        (void)m_SelectionController.SetSelectedEntity(
                            *m_Scene,
                            materialized->Entity);
                        result = materialized->Result;
                        if (RequestsGpuUpload(*result))
                        {
                            RuntimeAssetIngestTransition upload =
                                m_AssetIngestStateMachine.BeginGpuUpload(
                                    state->IngestHandle);
                            eventDiagnostic = upload.Diagnostic;
                            if (!upload.Succeeded())
                            {
                                result = Core::Err<RuntimeAssetImportResult>(
                                    ErrorFromIngestTransition(upload));
                            }
                        }
                        RuntimeAssetIngestTransition complete =
                            result.has_value()
                                ? m_AssetIngestStateMachine.CompleteApply(
                                      state->IngestHandle,
                                      state->IngestHandle.Generation,
                                      ToRuntimeAssetIngestResult(*result))
                                : RuntimeAssetIngestTransition{};
                        if (result.has_value())
                        {
                            eventDiagnostic = complete.Diagnostic;
                            if (!complete.Succeeded())
                            {
                                result = Core::Err<RuntimeAssetImportResult>(
                                    ErrorFromIngestTransition(complete));
                            }
                            else if (CreatesOrChangesScene(*result))
                            {
                                (void)m_EditorCommandHistory.MarkDirty("Import Asset");
                            }
                        }
                    }
                    else
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            materialized.error());
                        RuntimeAssetIngestTransition failed =
                            m_AssetIngestStateMachine.FailApply(
                                state->IngestHandle,
                                state->IngestHandle.Generation,
                                materialized.error());
                        eventDiagnostic = failed.Diagnostic;
                    }
                    RecordAssetImportEvent(
                        state->Request,
                        result,
                        eventDiagnostic);
                },
            });

        if (!handle.IsValid())
        {
            RuntimeAssetImportRequest request{
                .Path = std::move(state->Request.Path),
                .PayloadKind = state->Request.PayloadKind,
            };
            Core::Log::Warn(
                "[Runtime] Dropped geometry import queue submission failed: path='{}' payload={} error={}",
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                Core::Error::ToString(Core::ErrorCode::InvalidState));
            RuntimeAssetIngestTransition failed =
                m_AssetIngestStateMachine.FailCallback(
                    state->IngestHandle,
                    Core::ErrorCode::InvalidState);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
                failed.Diagnostic);
            return;
        }

        m_AssetImportStreamingTasks.push_back(RuntimeAssetImportStreamingTask{
            .Ingest = state->IngestHandle,
            .Streaming = handle,
        });

        Core::Log::Info(
            "[Runtime] Queued dropped geometry import: path='{}' payload={} candidate_count={}",
            state->Request.Path,
            Assets::DebugNameForAssetPayloadKind(state->Request.PayloadKind),
            candidateCount);
    }

    Core::Expected<RuntimeQueuedAssetImport>
    Engine::QueueModelTextureImportWithIngest(
        RuntimeAssetImportRequest request,
        const RuntimeAssetIngestSource source,
        const Assets::AssetId existingAsset)
    {
        auto route = Assets::ResolveAssetImportRoute(
            request.Path,
            Assets::AssetRouteOperation::Import,
            Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        if (!route.has_value())
        {
            return Core::Err<RuntimeQueuedAssetImport>(route.error());
        }
        if (!IsModelTextureImportPayload(route->PayloadKind))
        {
            return Core::Err<RuntimeQueuedAssetImport>(
                Core::ErrorCode::AssetTypeMismatch);
        }
        request.PayloadKind = route->PayloadKind;

        const Assets::AssetRouteDiagnostic routeDiagnostic =
            Assets::DiagnoseAssetImportRoute(
                request.Path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        RuntimeAssetIngestTransition submit =
            m_AssetIngestStateMachine.Submit(
                MakeRuntimeAssetIngestRequest(
                    request,
                    source,
                    existingAsset));
        if (!submit.Succeeded())
        {
            Core::Log::Warn(
                "[Runtime] Model/texture import rejected by ingest state machine: source={} path='{}' payload={} diagnostic={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                DebugNameForRuntimeAssetIngestDiagnostic(submit.Diagnostic),
                Core::Error::ToString(ErrorFromIngestTransition(submit)));
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(submit)),
                submit.Diagnostic);
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(submit));
        }

        if (!m_Initialized ||
            !m_StreamingExecutor ||
            !m_AssetService ||
            !m_GpuAssetCache ||
            !m_AssetModelTextureHandoff ||
            !m_AssetModelSceneHandoff ||
            !m_Scene ||
            request.Path.empty() ||
            !IsModelTextureImportPayload(request.PayloadKind))
        {
            RuntimeAssetIngestTransition failed =
                m_AssetIngestStateMachine.FailCallback(
                    submit.Handle,
                    Core::ErrorCode::InvalidState);
            Core::Log::Warn(
                "[Runtime] Model/texture import rejected before queueing: source={} path='{}' payload={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                Core::Error::ToString(Core::ErrorCode::InvalidState));
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
                failed.Diagnostic);
            return Core::Err<RuntimeQueuedAssetImport>(
                Core::ErrorCode::InvalidState);
        }

        RuntimeAssetIngestTransition routeResolved =
            m_AssetIngestStateMachine.ResolveRoute(
                submit.Handle,
                routeDiagnostic);
        if (!routeResolved.Succeeded())
        {
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(routeResolved)),
                routeResolved.Diagnostic);
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(routeResolved));
        }

        RuntimeAssetIngestTransition decodeQueued =
            m_AssetIngestStateMachine.QueueDecode(submit.Handle);
        if (!decodeQueued.Succeeded())
        {
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decodeQueued)),
                decodeQueued.Diagnostic);
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(decodeQueued));
        }

        RuntimeAssetIngestTransition decoding =
            m_AssetIngestStateMachine.MarkDecoding(submit.Handle);
        if (!decoding.Succeeded())
        {
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decoding)),
                decoding.Diagnostic);
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(decoding));
        }

        auto state = std::make_shared<DroppedModelTextureImportState>();
        state->IngestHandle = submit.Handle;
        state->Request = request;

        const StreamingTaskHandle handle = m_StreamingExecutor->Submit(
            StreamingTaskDesc{
                .Name = "Runtime.ImportModelTexture." +
                    FileNameFromPath(request.Path),
                .Kind = Core::Dag::TaskKind::AssetDecode,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = 4u,
                .Execute = [
                    state,
                    path = request.Path,
                    payloadKind = request.PayloadKind]() mutable -> StreamingResult
                {
                    Assets::AssetModelTextureIOBridge bridge;
                    if (Core::Result registered =
                            RegisterPromotedModelTextureIOCallbacks(bridge);
                        !registered.has_value())
                    {
                        state->Error = registered.error();
                        return StreamingResult{
                            StreamingCpuPayloadReady{.PayloadToken = 0u}};
                    }

                    Core::IO::FileIOBackend backend;
                    if (payloadKind == Assets::AssetPayloadKind::ModelScene)
                    {
                        auto decoded = bridge.ImportModelScene(path, backend);
                        if (!decoded.has_value())
                        {
                            state->Error = decoded.error();
                            return StreamingResult{
                                StreamingCpuPayloadReady{.PayloadToken = 0u}};
                        }

                        state->Decoded = DecodedModelTextureImport{
                            .Path = path,
                            .PayloadKind = payloadKind,
                            .Payload = std::move(*decoded),
                        };
                    }
                    else
                    {
                        auto decoded = bridge.ImportTexture2D(path, backend);
                        if (!decoded.has_value())
                        {
                            state->Error = decoded.error();
                            return StreamingResult{
                                StreamingCpuPayloadReady{.PayloadToken = 0u}};
                        }

                        state->Decoded = DecodedModelTextureImport{
                            .Path = path,
                            .PayloadKind = payloadKind,
                            .Payload = std::move(*decoded),
                        };
                    }

                    state->Error = Core::ErrorCode::Success;
                    return StreamingResult{
                        StreamingCpuPayloadReady{.PayloadToken = 0u}};
                },
                .ApplyOnMainThread = [this, state, existingAsset](StreamingResult&& streamingResult) mutable
                {
                    Core::Expected<RuntimeAssetImportResult> result =
                        Core::Err<RuntimeAssetImportResult>(
                            streamingResult.has_value()
                                ? state->Error
                                : streamingResult.error());
                    RuntimeAssetIngestDiagnostic eventDiagnostic =
                        DiagnosticForImportError(result.error());

                    if (!streamingResult.has_value() || !state->Decoded.has_value())
                    {
                        RuntimeAssetIngestTransition failed{};
                        if (result.error() == Core::ErrorCode::FileNotFound)
                        {
                            failed = m_AssetIngestStateMachine.MarkMissingFile(
                                state->IngestHandle);
                        }
                        else if (result.error() == Core::ErrorCode::AssetLoaderMissing)
                        {
                            failed = m_AssetIngestStateMachine.FailCallback(
                                state->IngestHandle,
                                result.error());
                        }
                        else
                        {
                            failed = m_AssetIngestStateMachine.FailDecode(
                                state->IngestHandle,
                                state->IngestHandle.Generation,
                                result.error());
                        }
                        eventDiagnostic = failed.Diagnostic;
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            eventDiagnostic);
                        return;
                    }

                    RuntimeAssetIngestTransition decodeComplete =
                        m_AssetIngestStateMachine.CompleteDecode(
                            state->IngestHandle,
                            state->IngestHandle.Generation);
                    if (!decodeComplete.Succeeded())
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            ErrorFromIngestTransition(decodeComplete));
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            decodeComplete.Diagnostic);
                        return;
                    }

                    RuntimeAssetIngestTransition applying =
                        m_AssetIngestStateMachine.BeginApply(state->IngestHandle);
                    if (!applying.Succeeded())
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            ErrorFromIngestTransition(applying));
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            applying.Diagnostic);
                        return;
                    }

                    if (state->Decoded->PayloadKind ==
                        Assets::AssetPayloadKind::ModelScene)
                    {
                        result = MaterializeDecodedModelSceneImport(
                            *m_AssetService,
                            *m_AssetModelSceneHandoff,
                            state->Request,
                            existingAsset,
                            std::move(std::get<Assets::AssetModelScenePayload>(
                                state->Decoded->Payload)));
                    }
                    else
                    {
                        result = MaterializeDecodedTextureImport(
                            *m_AssetService,
                            *m_GpuAssetCache,
                            *m_AssetModelTextureHandoff,
                            state->Request,
                            existingAsset,
                            std::move(std::get<Assets::AssetTexture2DPayload>(
                                state->Decoded->Payload)));
                    }

                    if (result.has_value())
                    {
                        if (RequestsGpuUpload(*result))
                        {
                            RuntimeAssetIngestTransition upload =
                                m_AssetIngestStateMachine.BeginGpuUpload(
                                    state->IngestHandle);
                            eventDiagnostic = upload.Diagnostic;
                            if (!upload.Succeeded())
                            {
                                result = Core::Err<RuntimeAssetImportResult>(
                                    ErrorFromIngestTransition(upload));
                            }
                        }

                        RuntimeAssetIngestTransition complete =
                            result.has_value()
                                ? m_AssetIngestStateMachine.CompleteApply(
                                      state->IngestHandle,
                                      state->IngestHandle.Generation,
                                      ToRuntimeAssetIngestResult(*result))
                                : RuntimeAssetIngestTransition{};
                        if (result.has_value())
                        {
                            eventDiagnostic = complete.Diagnostic;
                            if (!complete.Succeeded())
                            {
                                result = Core::Err<RuntimeAssetImportResult>(
                                    ErrorFromIngestTransition(complete));
                            }
                            else if (CreatesOrChangesScene(*result))
                            {
                                (void)m_EditorCommandHistory.MarkDirty("Import Asset");
                            }
                        }
                    }
                    else
                    {
                        RuntimeAssetIngestTransition failed =
                            m_AssetIngestStateMachine.FailApply(
                                state->IngestHandle,
                                state->IngestHandle.Generation,
                                result.error());
                        eventDiagnostic = failed.Diagnostic;
                    }

                    RecordAssetImportEvent(
                        state->Request,
                        result,
                        eventDiagnostic);
                },
            });

        if (!handle.IsValid())
        {
            RuntimeAssetImportRequest queuedRequest{
                .Path = state->Request.Path,
                .PayloadKind = state->Request.PayloadKind,
            };
            Core::Log::Warn(
                "[Runtime] Model/texture import queue submission failed: source={} path='{}' payload={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                queuedRequest.Path,
                Assets::DebugNameForAssetPayloadKind(queuedRequest.PayloadKind),
                Core::Error::ToString(Core::ErrorCode::InvalidState));
            RuntimeAssetIngestTransition failed =
                m_AssetIngestStateMachine.FailCallback(
                    state->IngestHandle,
                    Core::ErrorCode::InvalidState);
            RecordAssetImportEvent(
                queuedRequest,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
                failed.Diagnostic);
            return Core::Err<RuntimeQueuedAssetImport>(
                Core::ErrorCode::InvalidState);
        }

        m_AssetImportStreamingTasks.push_back(RuntimeAssetImportStreamingTask{
            .Ingest = state->IngestHandle,
            .Streaming = handle,
        });

        Core::Log::Info(
            "[Runtime] Queued model/texture import: source={} path='{}' payload={}",
            DebugNameForRuntimeAssetIngestSource(source),
            state->Request.Path,
            Assets::DebugNameForAssetPayloadKind(state->Request.PayloadKind));

        return RuntimeQueuedAssetImport{
            .Operation = state->IngestHandle,
            .PayloadKind = state->Request.PayloadKind,
        };
    }

    void Engine::QueueDroppedModelTextureImport(
        std::string path,
        const Assets::AssetPayloadKind payloadKind)
    {
        (void)QueueModelTextureImportWithIngest(
            RuntimeAssetImportRequest{
                .Path = std::move(path),
                .PayloadKind = payloadKind,
            },
            RuntimeAssetIngestSource::DroppedFile,
            {});
    }

    Core::Expected<RuntimeAssetImportResult>
    Engine::ImportAssetFromPathWithIngest(
        RuntimeAssetImportRequest request,
        const RuntimeAssetIngestSource source,
        const Assets::AssetId existingAsset)
    {
        RuntimeAssetIngestTransition submit =
            m_AssetIngestStateMachine.Submit(
                MakeRuntimeAssetIngestRequest(request, source, existingAsset));
        if (!submit.Succeeded())
        {
            Core::Expected<RuntimeAssetImportResult> result =
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(submit));
            RecordAssetImportEvent(request, result, submit.Diagnostic);
            return result;
        }

        if (source == RuntimeAssetIngestSource::Reimport)
        {
            Core::ErrorCode targetError = Core::ErrorCode::Success;
            if (!existingAsset.IsValid())
            {
                targetError = Core::ErrorCode::InvalidArgument;
            }
            else if (!m_AssetService || !m_AssetService->IsAlive(existingAsset))
            {
                targetError = Core::ErrorCode::ResourceNotFound;
            }
            else if (request.Path == "<invalid-reimport-target>")
            {
                targetError = Core::ErrorCode::ResourceNotFound;
            }
            else if (request.PayloadKind == Assets::AssetPayloadKind::Unknown)
            {
                targetError = Core::ErrorCode::AssetTypeMismatch;
            }

            if (targetError != Core::ErrorCode::Success)
            {
                RuntimeAssetIngestTransition failed =
                    m_AssetIngestStateMachine.MarkInvalidReimportTarget(
                        submit.Handle,
                        targetError);
                Core::Expected<RuntimeAssetImportResult> result =
                    Core::Err<RuntimeAssetImportResult>(targetError);
                RecordAssetImportEvent(request, result, failed.Diagnostic);
                return result;
            }
        }

        const Assets::AssetRouteDiagnostic routeDiagnostic =
            Assets::DiagnoseAssetImportRoute(
                request.Path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        RuntimeAssetIngestTransition routeResolved =
            m_AssetIngestStateMachine.ResolveRoute(
                submit.Handle,
                routeDiagnostic);
        if (!routeResolved.Succeeded())
        {
            Core::Expected<RuntimeAssetImportResult> result =
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(routeResolved));
            RecordAssetImportEvent(
                request,
                result,
                routeResolved.Diagnostic);
            return result;
        }

        RuntimeAssetIngestTransition decodeQueued =
            m_AssetIngestStateMachine.QueueDecode(submit.Handle);
        if (!decodeQueued.Succeeded())
        {
            Core::Expected<RuntimeAssetImportResult> result =
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decodeQueued));
            RecordAssetImportEvent(
                request,
                result,
                decodeQueued.Diagnostic);
            return result;
        }

        RuntimeAssetIngestTransition decoding =
            m_AssetIngestStateMachine.MarkDecoding(submit.Handle);
        if (!decoding.Succeeded())
        {
            Core::Expected<RuntimeAssetImportResult> result =
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decoding));
            RecordAssetImportEvent(request, result, decoding.Diagnostic);
            return result;
        }

        Core::Expected<RuntimeAssetImportResult> result =
            ImportAssetFromPathImpl(request, existingAsset);
        RuntimeAssetIngestDiagnostic eventDiagnostic =
            result.has_value()
                ? RuntimeAssetIngestDiagnostic::None
                : DiagnosticForImportError(result.error(), source);
        if (!result.has_value())
        {
            RuntimeAssetIngestTransition failed{};
            switch (eventDiagnostic)
            {
            case RuntimeAssetIngestDiagnostic::MissingFile:
                failed = m_AssetIngestStateMachine.MarkMissingFile(submit.Handle);
                break;
            case RuntimeAssetIngestDiagnostic::InvalidReimportTarget:
                failed = m_AssetIngestStateMachine.MarkInvalidReimportTarget(
                    submit.Handle,
                    result.error());
                break;
            case RuntimeAssetIngestDiagnostic::CallbackFailed:
                failed = m_AssetIngestStateMachine.FailCallback(
                    submit.Handle,
                    result.error());
                break;
            case RuntimeAssetIngestDiagnostic::MaterializationFailed:
            {
                RuntimeAssetIngestTransition decodeComplete =
                    m_AssetIngestStateMachine.CompleteDecode(
                        submit.Handle,
                        submit.Handle.Generation);
                if (!decodeComplete.Succeeded())
                {
                    failed = decodeComplete;
                    break;
                }
                RuntimeAssetIngestTransition applying =
                    m_AssetIngestStateMachine.BeginApply(submit.Handle);
                if (!applying.Succeeded())
                {
                    failed = applying;
                    break;
                }
                failed = m_AssetIngestStateMachine.FailApply(
                    submit.Handle,
                    submit.Handle.Generation,
                    result.error());
                break;
            }
            default:
                failed = m_AssetIngestStateMachine.FailDecode(
                    submit.Handle,
                    submit.Handle.Generation,
                    result.error());
                break;
            }
            eventDiagnostic = failed.Diagnostic;
            RecordAssetImportEvent(request, result, eventDiagnostic);
            return result;
        }

        RuntimeAssetIngestTransition decodeComplete =
            m_AssetIngestStateMachine.CompleteDecode(
                submit.Handle,
                submit.Handle.Generation);
        if (!decodeComplete.Succeeded())
        {
            result = Core::Err<RuntimeAssetImportResult>(
                ErrorFromIngestTransition(decodeComplete));
            RecordAssetImportEvent(
                request,
                result,
                decodeComplete.Diagnostic);
            return result;
        }

        RuntimeAssetIngestTransition applying =
            m_AssetIngestStateMachine.BeginApply(submit.Handle);
        if (!applying.Succeeded())
        {
            result = Core::Err<RuntimeAssetImportResult>(
                ErrorFromIngestTransition(applying));
            RecordAssetImportEvent(request, result, applying.Diagnostic);
            return result;
        }

        RuntimeAssetIngestTransition complete =
            RequestsGpuUpload(*result)
                ? m_AssetIngestStateMachine.BeginGpuUpload(submit.Handle)
                : RuntimeAssetIngestTransition{};
        if (RequestsGpuUpload(*result) && !complete.Succeeded())
        {
            result = Core::Err<RuntimeAssetImportResult>(
                ErrorFromIngestTransition(complete));
            RecordAssetImportEvent(request, result, complete.Diagnostic);
            return result;
        }
        complete = m_AssetIngestStateMachine.CompleteApply(
            submit.Handle,
            submit.Handle.Generation,
            ToRuntimeAssetIngestResult(*result));
        eventDiagnostic = complete.Diagnostic;
        if (!complete.Succeeded())
        {
            result = Core::Err<RuntimeAssetImportResult>(
                ErrorFromIngestTransition(complete));
        }
        RecordAssetImportEvent(request, result, eventDiagnostic);
        return result;
    }

    void Engine::RecordAssetImportEvent(
        const RuntimeAssetImportRequest& request,
        const Core::Expected<RuntimeAssetImportResult>& result,
        const RuntimeAssetIngestDiagnostic ingestDiagnostic)
    {
        RuntimeAssetImportEvent event{};
        event.Sequence = ++m_AssetImportEventSequence;
        event.Path = request.Path;
        event.RequestedPayloadKind = request.PayloadKind;
        event.Error = result.has_value()
            ? Core::ErrorCode::Success
            : result.error();
        event.IngestDiagnostic = ingestDiagnostic;
        if (result.has_value())
        {
            event.Result = *result;
            Core::Log::Info(
                "[Runtime] Asset import succeeded: path='{}' requested_payload={} result_payload={} ingest_diagnostic={} primitive_entities={} embedded_textures={} generated_textures={} texture_upload_requests={} generated_texture_upload_requests={} materialized_model_scene={} requested_texture_upload={}",
                event.Path,
                Assets::DebugNameForAssetPayloadKind(event.RequestedPayloadKind),
                Assets::DebugNameForAssetPayloadKind(result->PayloadKind),
                DebugNameForRuntimeAssetIngestDiagnostic(event.IngestDiagnostic),
                result->PrimitiveEntitiesCreated,
                result->EmbeddedTextureAssetsCreated,
                result->GeneratedTextureAssetsCreated,
                result->TextureUploadRequests,
                result->GeneratedTextureUploadRequests,
                result->MaterializedModelScene,
                result->RequestedTextureUpload);
        }
        else
        {
            Core::Log::Warn(
                "[Runtime] Asset import failed: path='{}' requested_payload={} ingest_diagnostic={} error={}",
                event.Path,
                Assets::DebugNameForAssetPayloadKind(event.RequestedPayloadKind),
                DebugNameForRuntimeAssetIngestDiagnostic(event.IngestDiagnostic),
                Core::Error::ToString(event.Error));
        }
        m_LastAssetImportEvent = std::move(event);
    }

    void Engine::RecordSceneFileEvent(RuntimeSceneFileEvent event)
    {
        event.Sequence = ++m_SceneFileEventSequence;
        const char* operationName = "None";
        switch (event.Operation)
        {
        case RuntimeSceneFileOperation::Load:
            operationName = "Load";
            break;
        case RuntimeSceneFileOperation::Save:
            operationName = "Save";
            break;
        case RuntimeSceneFileOperation::None:
            break;
        }

        if (event.Succeeded())
        {
            Core::Log::Info(
                "[Runtime] Scene file operation succeeded: operation={} path='{}'",
                operationName,
                event.Path);
        }
        else
        {
            Core::Log::Warn(
                "[Runtime] Scene file operation failed: operation={} path='{}' error={}",
                operationName,
                event.Path,
                Core::Error::ToString(event.Error));
        }
        m_LastSceneFileEvent = std::move(event);
    }

    Core::Expected<RuntimeAssetImportResult> Engine::ImportAssetFromPathImpl(
        RuntimeAssetImportRequest request,
        const Assets::AssetId existingAsset)
    {
        if (!m_Initialized ||
            !m_AssetService ||
            !m_GpuAssetCache ||
            !m_AssetModelTextureHandoff ||
            !m_AssetModelSceneHandoff)
        {
            return Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState);
        }
        if (request.Path.empty())
        {
            return Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidPath);
        }

        auto route = Assets::ResolveAssetImportRoute(
            request.Path,
            Assets::AssetRouteOperation::Import,
            Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        if (!route.has_value())
        {
            return Core::Err<RuntimeAssetImportResult>(route.error());
        }
        if (Assets::IsGeometryPayloadKind(route->PayloadKind))
        {
            auto decoded = DecodeGeometryImport(
                RuntimeAssetImportRequest{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                });
            if (!decoded.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(decoded.error());
            }

            if (existingAsset.IsValid())
            {
                return ReloadDecodedGeometryImport(
                    *m_AssetService,
                    existingAsset,
                    *decoded);
            }

            auto materialized = MaterializeDecodedGeometryImport(
                *m_AssetService,
                *m_GpuAssetCache,
                m_RenderExtraction,
                *m_Scene,
                m_StreamingExecutor.get(),
                *decoded);
            if (!materialized.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(
                    materialized.error());
            }
            FocusMainCameraOnImportedGeometry(
                m_CameraControllers,
                m_Config.Camera.Controller,
                m_Config.Camera.Enabled,
                materialized->Bounds);
            (void)m_SelectionController.SetSelectedEntity(*m_Scene,
                                                          materialized->Entity);
            return materialized->Result;
        }
        if (route->PayloadKind != Assets::AssetPayloadKind::ModelScene &&
            route->PayloadKind != Assets::AssetPayloadKind::Texture2D)
        {
            return Core::Err<RuntimeAssetImportResult>(
                Core::ErrorCode::AssetUnsupportedFormat);
        }

        Assets::AssetModelTextureIOBridge bridge;
        if (Core::Result registered =
                RegisterPromotedModelTextureIOCallbacks(bridge);
            !registered.has_value())
        {
            return Core::Err<RuntimeAssetImportResult>(registered.error());
        }

        Core::IO::FileIOBackend backend;
        if (route->PayloadKind == Assets::AssetPayloadKind::ModelScene)
        {
            auto decoded = bridge.ImportModelScene(request.Path, backend);
            if (!decoded.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(decoded.error());
            }

            return MaterializeDecodedModelSceneImport(
                *m_AssetService,
                *m_AssetModelSceneHandoff,
                request,
                existingAsset,
                std::move(*decoded));
        }

        auto decoded = bridge.ImportTexture2D(request.Path, backend);
        if (!decoded.has_value())
        {
            return Core::Err<RuntimeAssetImportResult>(decoded.error());
        }

        return MaterializeDecodedTextureImport(
            *m_AssetService,
            *m_GpuAssetCache,
            *m_AssetModelTextureHandoff,
            request,
            existingAsset,
            std::move(*decoded));
    }

    void Engine::ClearSceneRuntimeState()
    {
        if (m_Renderer)
            m_RenderExtraction.ClearSceneState(*m_Renderer);
        if (m_Scene)
            m_SelectionController.ClearSceneState(*m_Scene);
        m_LastRefinedPrimitive.reset();
        ++m_LastRefinedPrimitiveGeneration;
    }

    Core::Expected<SceneSerializationResult> Engine::SaveSceneToPath(
        std::string path)
    {
        if (!m_Initialized || !m_Scene)
            return Core::Err<SceneSerializationResult>(Core::ErrorCode::InvalidState);
        if (path.empty())
            return Core::Err<SceneSerializationResult>(Core::ErrorCode::InvalidPath);

        Core::IO::FileIOBackend backend;
        auto saved = SaveSceneDocument(*m_Scene, path, backend);
        if (saved.has_value())
            m_EditorCommandHistory.MarkSaved(path);
        return saved;
    }

    Core::Expected<RuntimeQueuedSceneFileOperation>
    Engine::QueueSceneSaveToPath(std::string path)
    {
        if (!m_Initialized || !m_Scene || !m_StreamingExecutor)
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }
        if (path.empty())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidPath);
        }

        auto state = std::make_shared<QueuedSceneSaveState>();
        state->Path = std::move(path);
        SnapshotSerializableScene(*m_Scene, state->Snapshot);

        const StreamingTaskHandle handle = m_StreamingExecutor->Submit(
            StreamingTaskDesc{
                .Name = "Runtime.SceneSave." + FileNameFromPath(state->Path),
                .Kind = Core::Dag::TaskKind::AssetDecode,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = 3u,
                .Execute = [state]() mutable -> StreamingResult
                {
                    Core::IO::FileIOBackend backend;
                    auto saved = SaveSceneDocument(
                        state->Snapshot,
                        state->Path,
                        backend);
                    if (!saved.has_value())
                    {
                        state->Error = saved.error();
                        return StreamingResult{
                            StreamingCpuPayloadReady{.PayloadToken = 0u}};
                    }

                    state->Result = *saved;
                    state->Error = Core::ErrorCode::Success;
                    return StreamingResult{
                        StreamingCpuPayloadReady{.PayloadToken = 0u}};
                },
                .ApplyOnMainThread = [this, state](StreamingResult&& result) mutable
                {
                    Core::Expected<SceneSerializationResult> saved =
                        Core::Err<SceneSerializationResult>(
                            result.has_value() ? state->Error : result.error());

                    if (result.has_value() &&
                        state->Error == Core::ErrorCode::Success &&
                        state->Result.has_value())
                    {
                        if (!m_Initialized)
                        {
                            saved = Core::Err<SceneSerializationResult>(
                                Core::ErrorCode::InvalidState);
                        }
                        else
                        {
                            saved = *state->Result;
                            m_EditorCommandHistory.MarkSaved(state->Path);
                        }
                    }

                    RuntimeSceneFileEvent event{
                        .Operation = RuntimeSceneFileOperation::Save,
                        .Task = state->Task,
                        .Path = state->Path,
                        .Error = saved.has_value()
                            ? Core::ErrorCode::Success
                            : saved.error(),
                    };
                    if (saved.has_value())
                    {
                        event.SaveResult = *saved;
                    }
                    RecordSceneFileEvent(std::move(event));
                },
            });

        if (!handle.IsValid())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }

        state->Task = handle;
        Core::Log::Info(
            "[Runtime] Queued scene save: path='{}'",
            state->Path);
        return RuntimeQueuedSceneFileOperation{
            .Task = handle,
            .Operation = RuntimeSceneFileOperation::Save,
        };
    }

    Core::Expected<SceneDeserializationResult> Engine::LoadSceneFromPath(
        std::string path)
    {
        if (!m_Initialized || !m_Scene)
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidState);
        if (path.empty())
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidPath);

        Core::IO::FileIOBackend backend;
        ECS::Scene::Registry loadedScene;
        auto loaded = LoadSceneDocument(loadedScene, path, backend);
        if (!loaded.has_value())
            return Core::Err<SceneDeserializationResult>(loaded.error());

        ClearSceneRuntimeState();
        m_Scene->Clear();
        m_Scene->Raw() = std::move(loadedScene.Raw());
        m_StableEntityLookup.Rebuild(*m_Scene);
        m_EditorCommandHistory.ResetDocument(path);
        return loaded;
    }

    Core::Expected<RuntimeQueuedSceneFileOperation>
    Engine::QueueSceneLoadFromPath(std::string path)
    {
        if (!m_Initialized || !m_Scene || !m_StreamingExecutor)
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }
        if (path.empty())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidPath);
        }

        auto state = std::make_shared<QueuedSceneLoadState>();
        state->Path = std::move(path);

        const StreamingTaskHandle handle = m_StreamingExecutor->Submit(
            StreamingTaskDesc{
                .Name = "Runtime.SceneLoad." + FileNameFromPath(state->Path),
                .Kind = Core::Dag::TaskKind::AssetDecode,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = 4u,
                .Execute = [state]() mutable -> StreamingResult
                {
                    Core::IO::FileIOBackend backend;
                    auto loaded = LoadSceneDocument(
                        state->LoadedScene,
                        state->Path,
                        backend);
                    if (!loaded.has_value())
                    {
                        state->Error = loaded.error();
                        return StreamingResult{
                            StreamingCpuPayloadReady{.PayloadToken = 0u}};
                    }

                    state->Result = *loaded;
                    state->Error = Core::ErrorCode::Success;
                    return StreamingResult{
                        StreamingCpuPayloadReady{.PayloadToken = 0u}};
                },
                .ApplyOnMainThread = [this, state](StreamingResult&& result) mutable
                {
                    Core::Expected<SceneDeserializationResult> loaded =
                        Core::Err<SceneDeserializationResult>(
                            result.has_value() ? state->Error : result.error());

                    if (result.has_value() &&
                        state->Error == Core::ErrorCode::Success &&
                        state->Result.has_value())
                    {
                        if (!m_Initialized || !m_Scene)
                        {
                            loaded = Core::Err<SceneDeserializationResult>(
                                Core::ErrorCode::InvalidState);
                        }
                        else
                        {
                            loaded = *state->Result;
                            ClearSceneRuntimeState();
                            m_Scene->Clear();
                            m_Scene->Raw() = std::move(state->LoadedScene.Raw());
                            m_StableEntityLookup.Rebuild(*m_Scene);
                            m_EditorCommandHistory.ResetDocument(state->Path);
                        }
                    }

                    RuntimeSceneFileEvent event{
                        .Operation = RuntimeSceneFileOperation::Load,
                        .Task = state->Task,
                        .Path = state->Path,
                        .Error = loaded.has_value()
                            ? Core::ErrorCode::Success
                            : loaded.error(),
                    };
                    if (loaded.has_value())
                    {
                        event.LoadResult = *loaded;
                    }
                    RecordSceneFileEvent(std::move(event));
                },
            });

        if (!handle.IsValid())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }

        state->Task = handle;
        Core::Log::Info(
            "[Runtime] Queued scene load: path='{}'",
            state->Path);
        return RuntimeQueuedSceneFileOperation{
            .Task = handle,
            .Operation = RuntimeSceneFileOperation::Load,
        };
    }

    const std::optional<RuntimeSceneFileEvent>&
    Engine::GetLastSceneFileEvent() const noexcept
    {
        return m_LastSceneFileEvent;
    }

    Core::Result Engine::NewSceneDocument()
    {
        if (!m_Initialized || !m_Scene)
            return Core::Err(Core::ErrorCode::InvalidState);

        ClearSceneRuntimeState();
        m_Scene->Clear();
        m_StableEntityLookup.Clear();
        m_EditorCommandHistory.ResetDocument();
        return Core::Ok();
    }

    Core::Result Engine::CloseSceneDocument()
    {
        return NewSceneDocument();
    }

    SelectionController&  Engine::GetSelectionController() noexcept { return m_SelectionController; }
    EditorCommandHistory& Engine::GetEditorCommandHistory() noexcept
    {
        return m_EditorCommandHistory;
    }
    const EditorCommandHistory& Engine::GetEditorCommandHistory() const noexcept
    {
        return m_EditorCommandHistory;
    }
    const std::optional<PrimitiveSelectionResult>&
    Engine::GetLastRefinedPrimitiveSelection() const noexcept { return m_LastRefinedPrimitive; }
    std::uint64_t
    Engine::GetLastRefinedPrimitiveSelectionGeneration() const noexcept
    {
        return m_LastRefinedPrimitiveGeneration;
    }
    Core::FrameGraph&     Engine::GetFrameGraph()    noexcept { return *m_FrameGraph;    }

    ReferenceSceneRegistry& Engine::GetReferenceSceneRegistry() noexcept
    {
        return m_ReferenceSceneRegistry;
    }

    bool Engine::IsReferenceSceneInstalled() const noexcept
    {
        return m_ReferenceSceneInstalled;
    }

    const std::optional<Graphics::CameraViewInput>& Engine::GetReferenceCameraSeed() const noexcept
    {
        return m_ReferenceCamera;
    }

    CameraControllerRegistry& Engine::GetCameraControllerRegistry() noexcept
    {
        return m_CameraControllers;
    }

    void Engine::SetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId,
        const MeshPrimitiveViewSettings settings)
    {
        if (!m_Scene)
        {
            return;
        }

        namespace G = Graphics::Components;
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(stableEntityId);
        entt::registry& raw = m_Scene->Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
        {
            return;
        }

        if (settings.EnableEdgeView)
        {
            raw.emplace_or_replace<G::RenderEdges>(entity);
        }
        else if (raw.all_of<G::RenderEdges>(entity))
        {
            raw.remove<G::RenderEdges>(entity);
        }

        if (settings.EnableVertexView)
        {
            G::RenderPoints points =
                raw.all_of<G::RenderPoints>(entity)
                    ? raw.get<G::RenderPoints>(entity)
                    : G::RenderPoints{};
            points.Type = ToRenderPointType(settings.VertexRenderMode);
            points.SizeSource = settings.VertexPointRadiusPx;
            raw.emplace_or_replace<G::RenderPoints>(entity, points);
        }
        else if (raw.all_of<G::RenderPoints>(entity))
        {
            raw.remove<G::RenderPoints>(entity);
        }

        m_RenderExtraction.ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    void Engine::ClearMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) noexcept
    {
        if (m_Scene)
        {
            namespace G = Graphics::Components;
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            entt::registry& raw = m_Scene->Raw();
            if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
            {
                raw.remove<G::RenderEdges, G::RenderPoints>(entity);
            }
        }
        m_RenderExtraction.ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    MeshPrimitiveViewSettings Engine::GetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) const noexcept
    {
        MeshPrimitiveViewSettings settings{};
        if (!m_Scene)
        {
            return settings;
        }

        namespace G = Graphics::Components;
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(stableEntityId);
        const entt::registry& raw = m_Scene->Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
        {
            return settings;
        }

        settings.EnableEdgeView = raw.all_of<G::RenderEdges>(entity);
        if (const auto* points = raw.try_get<G::RenderPoints>(entity))
        {
            settings.EnableVertexView = true;
            settings.VertexRenderMode = ToMeshVertexViewRenderMode(points->Type);
            if (const auto* uniform = std::get_if<float>(&points->SizeSource))
            {
                settings.VertexPointRadiusPx = *uniform;
            }
        }
        return settings;
    }

    void Engine::SetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId,
        RenderExtractionCache::VisualizationAdapterBinding binding)
    {
        m_RenderExtraction.SetVisualizationAdapterBinding(
            stableEntityId,
            std::move(binding));
    }

    void Engine::ClearVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) noexcept
    {
        m_RenderExtraction.ClearVisualizationAdapterBinding(stableEntityId);
    }

    std::optional<RenderExtractionCache::VisualizationAdapterBinding>
    Engine::GetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_RenderExtraction.GetVisualizationAdapterBinding(stableEntityId);
    }

    std::uint64_t
    Engine::GetVisualizationAdapterBindingRevision() const noexcept
    {
        return m_RenderExtraction.GetVisualizationAdapterBindingRevision();
    }

    RuntimeKMeansGpuJobSubmission Engine::SubmitKMeansGpuJob(
        RuntimeKMeansGpuJobRequest request)
    {
        if (!m_KMeansGpuJobs)
        {
            return RuntimeKMeansGpuJobSubmission{
                .Status = RuntimeKMeansGpuJobStatus::GpuUnavailable,
                .GpuStatus = KMeansGpuStatus::DeviceUnavailable,
                .Diagnostic = "Runtime K-Means GPU job queue is unavailable.",
            };
        }
        return m_KMeansGpuJobs->Submit(std::move(request));
    }

    std::optional<RuntimeKMeansGpuJobResult>
    Engine::ConsumeCompletedKMeansGpuJob()
    {
        if (!m_KMeansGpuJobs)
            return std::nullopt;
        return m_KMeansGpuJobs->ConsumeCompleted();
    }

    DerivedJobHandle Engine::SubmitDerivedJob(DerivedJobDesc desc)
    {
        if (!m_DerivedJobRegistry)
            return {};
        return m_DerivedJobRegistry->Submit(std::move(desc));
    }

    void Engine::CancelDerivedJob(const DerivedJobHandle handle)
    {
        if (m_DerivedJobRegistry)
            m_DerivedJobRegistry->Cancel(handle);
    }

    DerivedJobQueueSnapshot Engine::GetDerivedJobQueueSnapshot() const
    {
        if (!m_DerivedJobRegistry)
            return {};
        return m_DerivedJobRegistry->SnapshotAll();
    }

    void Engine::SetImGuiEditorCallback(std::function<void()> callback)
    {
        m_ImGuiEditorCallback = std::move(callback);
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->SetEditorCallback(m_ImGuiEditorCallback);
    }

    const ImGuiAdapter& Engine::GetImGuiAdapter() const noexcept
    {
        return *m_ImGuiAdapter;
    }
}
