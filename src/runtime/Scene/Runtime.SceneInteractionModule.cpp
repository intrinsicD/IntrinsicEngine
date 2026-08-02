module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.SceneInteractionModule;

import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Platform.Input;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::uint64_t ElapsedInteractionMicros(
            const std::chrono::steady_clock::time_point start) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count());
        }

        constexpr int kGizmoMouseButton = 0;
        constexpr int kSelectionMouseButton = 0;

        [[nodiscard]] std::uint32_t ClampCursorPixel(
            const float value,
            const std::uint32_t extent) noexcept
        {
            if (extent == 0u || !std::isfinite(value))
                return 0u;
            const float clamped =
                std::clamp(value, 0.0f, static_cast<float>(extent - 1u));
            return static_cast<std::uint32_t>(clamped);
        }

        [[nodiscard]] bool CursorInsideViewport(
            const Platform::Input::Context::XY cursor,
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

        [[nodiscard]] Platform::Input::Context::XY
        WindowToFramebufferCursor(
            const Platform::Input::Context::XY cursor,
            const Core::Extent2D windowExtent,
            const Core::Extent2D framebufferExtent) noexcept
        {
            if (windowExtent.Width <= 0 || windowExtent.Height <= 0 ||
                framebufferExtent.Width <= 0 ||
                framebufferExtent.Height <= 0)
            {
                return cursor;
            }
            const float scaleX =
                static_cast<float>(framebufferExtent.Width) /
                static_cast<float>(windowExtent.Width);
            const float scaleY =
                static_cast<float>(framebufferExtent.Height) /
                static_cast<float>(windowExtent.Height);
            return Platform::Input::Context::XY{
                cursor.x * scaleX,
                cursor.y * scaleY,
            };
        }

        void SubmitViewportSelectionClickForFrame(
            SelectionController& selection,
            const Platform::Input::Context& input,
            const Core::Extent2D windowExtent,
            const Core::Extent2D viewport,
            const bool imguiCapturesMouse,
            const bool gizmoCapturesMouse) noexcept
        {
            if (imguiCapturesMouse || gizmoCapturesMouse ||
                Core::IsEmpty(viewport) ||
                !input.IsMouseButtonJustPressed(kSelectionMouseButton))
            {
                return;
            }

            const Platform::Input::Context::XY cursor =
                WindowToFramebufferCursor(
                    input.GetMousePosition(),
                    windowExtent,
                    viewport);
            if (!CursorInsideViewport(cursor, viewport))
                return;

            selection.RequestClickPick(
                ClampCursorPixel(cursor.x, viewport.Width),
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
            for (const std::uint32_t stableId :
                 selection.SelectedStableIds())
            {
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableId);
                if (scene.IsValid(entity))
                    outSelected.push_back(entity);
            }
        }

        void DriveGizmoInteractionForFrame(
            GizmoInteraction& gizmo,
            ECS::Scene::Registry& scene,
            const WorldHandle world,
            EditorCommandHistory* const history,
            const Platform::Input::Context& input,
            const Graphics::CameraViewInput& cameraInput,
            const Core::Extent2D windowExtent,
            const Core::Extent2D viewport,
            const bool imguiCapturesInput,
            std::span<const ECS::EntityHandle> selected)
        {
            if (!world.IsValid() || history == nullptr ||
                imguiCapturesInput)
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
                WindowToFramebufferCursor(
                    input.GetMousePosition(),
                    windowExtent,
                    viewport);
            const std::uint32_t pixelX =
                ClampCursorPixel(cursor.x, viewport.Width);
            const std::uint32_t pixelY =
                ClampCursorPixel(cursor.y, viewport.Height);
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
                if (!input.IsMouseButtonPressed(kGizmoMouseButton) &&
                    gizmo.IsDragging())
                {
                    (void)gizmo.DragCommit(scene, world, *history);
                }
                return;
            }

            const PickRay ray{
                .Origin = camera.PickRayOrigin,
                .Direction = camera.PickRayDirection,
            };

            if (input.IsMouseButtonJustPressed(kGizmoMouseButton))
            {
                const GizmoHitResult hit =
                    gizmo.HitTest(scene,
                                  camera,
                                  glm::vec2{cursor.x, cursor.y},
                                  viewport,
                                  selected);
                if (hit.Hit)
                    (void)gizmo.BeginDrag(scene, hit, ray, selected);
            }
            else if (input.IsMouseButtonPressed(kGizmoMouseButton) &&
                     gizmo.IsDragging())
            {
                (void)gizmo.DragTick(scene, ray);
            }
            else if (!input.IsMouseButtonPressed(kGizmoMouseButton) &&
                     gizmo.IsDragging())
            {
                (void)gizmo.DragCommit(scene, world, *history);
            }
        }

        [[nodiscard]] std::optional<PickReadbackContext>
        BuildPickReadbackContextForFrame(
            const Graphics::RenderFrameInput& renderInput,
            const Platform::Extent2D& viewport)
        {
            const Graphics::CameraViewSnapshot pickCamera =
                Graphics::BuildCameraViewSnapshot(
                    renderInput.Camera,
                    viewport,
                    renderInput.Pick);
            if (!pickCamera.Valid)
                return std::nullopt;

            const std::uint32_t viewportWidth =
                viewport.Width > 0
                    ? static_cast<std::uint32_t>(viewport.Width)
                    : 0u;
            const std::uint32_t viewportHeight =
                viewport.Height > 0
                    ? static_cast<std::uint32_t>(viewport.Height)
                    : 0u;
            PickReadbackContext context{};
            context.InverseViewProjection =
                pickCamera.InverseViewProjection;
            context.ViewportWidth = viewportWidth;
            context.ViewportHeight = viewportHeight;
            context.HasWorldRay = pickCamera.HasPickRay;
            context.WorldRayOrigin = pickCamera.PickRayOrigin;
            context.WorldRayDirection = pickCamera.PickRayDirection;
            const float projectionScaleY =
                std::abs(renderInput.Camera.Projection[1][1]);
            if (projectionScaleY > 0.000001f &&
                viewportHeight > 0u)
            {
                context.WorldUnitsPerPixelAtUnitDepth =
                    2.0f /
                    (projectionScaleY *
                     static_cast<float>(viewportHeight));
            }
            context.OrthographicProjection =
                IsOrthographicProjection(
                    renderInput.Camera.Projection);
            return context;
        }
    }

    struct SceneInteractionModule::Impl
    {
        struct State
        {
            struct InFlightPickContext
            {
                std::uint64_t Sequence{0u};
                WorldHandle World{};
                std::uint64_t InteractionEpoch{0u};
                std::optional<PickReadbackContext> Context{};
            };

            WorldRegistry* Worlds{nullptr};
            Platform::IWindow* Window{nullptr};
            Graphics::IRenderer* Renderer{nullptr};
            RenderExtractionCache* Extraction{nullptr};
            SceneDocumentModule* Documents{nullptr};
            EditorCommandHistory* History{nullptr};

            SelectionController Selection{};
            StableEntityLookup Lookup{};
            StableEntityLookupSceneBinding LookupBinding{};
            GizmoInteraction Gizmo{};
            TransformGizmoRenderPacketBuilder GizmoPacketBuilder{};
            std::vector<ECS::EntityHandle> GizmoSelectedEntities{};
            std::vector<InFlightPickContext> InFlightPickContexts{};
            std::optional<PrimitiveSelectionResult> LastRefinedPrimitive{};
            std::uint64_t LastRefinedPrimitiveGeneration{0u};

            WorldHandle BoundWorld{};
            ECS::Scene::Registry* BoundRegistry{nullptr};
            std::uint64_t InteractionEpoch{1u};

            Graphics::RenderFrameInput* FrameRenderInput{nullptr};
            Platform::Extent2D FrameViewport{};
            WorldHandle FrameWorld{};
            std::uint64_t FrameEpoch{0u};

            SceneReplacementParticipantHandle DocumentParticipant{};
            bool AcceptingCallbacks{true};
            RuntimeSceneInteractionRenderSnapshot RenderSnapshot{};

            void AdvanceEpoch() noexcept
            {
                ++InteractionEpoch;
                if (InteractionEpoch == 0u)
                    InteractionEpoch = 1u;
            }

            void ClearFrameBorrow() noexcept
            {
                FrameRenderInput = nullptr;
                FrameViewport = {};
                FrameWorld = {};
                FrameEpoch = 0u;
            }

            void PublishEmpty(const WorldHandle world)
            {
                RenderSnapshot.World = world;
                RenderSnapshot.SelectedRenderIds.clear();
                RenderSnapshot.HasHovered = false;
                RenderSnapshot.HoveredRenderId = 0u;
                RenderSnapshot.GizmoDrawPackets.clear();
                if (Extraction == nullptr)
                    return;
                Extraction->SubmitSceneInteractionSnapshot(
                    RenderSnapshot);
            }

            void ClearWorldBoundState()
            {
                // BoundRegistry is cleared only while it is still known-live:
                // world retirement and document replacement notify before
                // destroying/clearing the outgoing registry.
                const GizmoConfig gizmoConfig = Gizmo.Config();
                const GizmoMode gizmoMode = Gizmo.Mode();
                const GizmoOrientation gizmoOrientation =
                    Gizmo.Orientation();
                if (BoundRegistry != nullptr && Gizmo.IsDragging())
                    Gizmo.DragCancel(*BoundRegistry);
                Gizmo = GizmoInteraction{gizmoConfig};
                Gizmo.SetMode(gizmoMode);
                Gizmo.SetOrientation(gizmoOrientation);
                GizmoSelectedEntities.clear();
                GizmoPacketBuilder =
                    TransformGizmoRenderPacketBuilder{};
                if (BoundRegistry != nullptr)
                    Selection.ClearSceneState(*BoundRegistry);
                InFlightPickContexts.clear();
                LastRefinedPrimitive.reset();
                ++LastRefinedPrimitiveGeneration;
                Selection.SetStableEntityLookup(nullptr);
                LookupBinding.Disconnect();
                Lookup.Clear();
                ClearFrameBorrow();
                PublishEmpty(BoundWorld);
                BoundWorld = {};
                BoundRegistry = nullptr;
                AdvanceEpoch();
            }

            void BindTo(
                const WorldHandle world,
                ECS::Scene::Registry* const registry)
            {
                BoundWorld = world;
                BoundRegistry = registry;
                if (BoundWorld.IsValid() &&
                    BoundRegistry != nullptr)
                {
                    LookupBinding.Rebuild(
                        Lookup, BoundRegistry);
                    Selection.SetStableEntityLookup(&Lookup);
                }
                PublishEmpty(BoundWorld);
            }

            void ResetAndBindTo(
                const WorldHandle world,
                ECS::Scene::Registry* const registry)
            {
                ClearWorldBoundState();
                BindTo(world, registry);
            }

            [[nodiscard]] bool ValidateBinding()
            {
                if (!AcceptingCallbacks || Worlds == nullptr)
                    return false;

                const WorldHandle active = Worlds->ActiveWorld();
                ECS::Scene::Registry* const registry =
                    Worlds->Get(active);
                if (active != BoundWorld ||
                    registry != BoundRegistry)
                {
                    ResetAndBindTo(active, registry);
                }
                return BoundWorld.IsValid() &&
                       BoundRegistry != nullptr &&
                       Worlds->ActiveWorld() == BoundWorld &&
                       Worlds->Get(BoundWorld) == BoundRegistry;
            }

            void RunViewportInput(
                RuntimeViewportInputHookContext& context)
            {
                if (!ValidateBinding() ||
                    context.ActiveWorldHandle != BoundWorld)
                {
                    ClearFrameBorrow();
                    return;
                }

                FrameRenderInput = &context.RenderInput;
                FrameViewport = context.Viewport;
                FrameWorld = BoundWorld;
                FrameEpoch = InteractionEpoch;

                if (Window == nullptr)
                    return;

                RebuildSelectedGizmoEntities(
                    Selection,
                    *BoundRegistry,
                    GizmoSelectedEntities);
                const Platform::IWindow& inputWindow = *Window;
                const Platform::Input::Context& input =
                    inputWindow.GetInput();
                const Platform::Extent2D windowExtent =
                    inputWindow.GetWindowExtent();
                DriveGizmoInteractionForFrame(
                    Gizmo,
                    *BoundRegistry,
                    BoundWorld,
                    History,
                    input,
                    context.RenderInput.Camera,
                    windowExtent,
                    context.Viewport,
                    context.EditorCapture.CapturesViewportInput(),
                    GizmoSelectedEntities);
                SubmitViewportSelectionClickForFrame(
                    Selection,
                    input,
                    windowExtent,
                    context.Viewport,
                    context.EditorCapture.CapturedMouse ||
                        context.EditorCapture.WidgetsActive,
                    Gizmo.IsDragging());
            }

            void RunBeforeExtraction(
                RuntimeFrameHookContext& context)
            {
                const bool valid =
                    ValidateBinding() &&
                    context.ActiveWorldHandle == BoundWorld &&
                    &context.ActiveWorld == BoundRegistry;
                if (!valid)
                {
                    ClearFrameBorrow();
                    return;
                }

                const auto pickBegin =
                    std::chrono::steady_clock::now();
                if (Renderer != nullptr &&
                    FrameRenderInput != nullptr &&
                    FrameWorld == BoundWorld &&
                    FrameEpoch == InteractionEpoch)
                {
                    const std::optional<PendingSelectionPick> pick =
                        Selection.ConsumePendingPick();
                    if (pick.has_value())
                    {
                        FrameRenderInput->HasPendingPick = true;
                        FrameRenderInput->Pick =
                            Graphics::PickPixelRequest{
                                .X = pick->PixelX,
                                .Y = pick->PixelY,
                                .Pending = true,
                                .Sequence = pick->Sequence,
                            };
                        Renderer->GetSelectionSystem().RequestPick(
                            Graphics::PickRequest{
                                .PixelX = pick->PixelX,
                                .PixelY = pick->PixelY,
                            });

                        constexpr std::size_t
                            kMaxInFlightPickContexts = 32u;
                        if (InFlightPickContexts.size() >=
                            kMaxInFlightPickContexts)
                        {
                            (void)Selection.DiscardInFlightPick(
                                InFlightPickContexts.front().Sequence);
                            InFlightPickContexts.erase(
                                InFlightPickContexts.begin());
                        }
                        InFlightPickContexts.push_back(
                            InFlightPickContext{
                                .Sequence = pick->Sequence,
                                .World = BoundWorld,
                                .InteractionEpoch =
                                    InteractionEpoch,
                                .Context =
                                    BuildPickReadbackContextForFrame(
                                        *FrameRenderInput,
                                        FrameViewport),
                            });
                    }
                }
                context.Pacing.SelectionPickDrainMicros +=
                    ElapsedInteractionMicros(pickBegin);

                const auto packetBegin =
                    std::chrono::steady_clock::now();
                const std::span<const
                    Graphics::TransformGizmoRenderPacket> packets =
                    GizmoPacketBuilder.Build(
                        *BoundRegistry,
                        GizmoSelectedEntities,
                        Gizmo.Mode(),
                        Gizmo.Orientation(),
                        Gizmo.Config().AxisLength);

                RenderSnapshot.World = BoundWorld;
                RenderSnapshot.SelectedRenderIds.assign(
                    Selection.SelectedStableIds().begin(),
                    Selection.SelectedStableIds().end());
                RenderSnapshot.HasHovered = Selection.HasHovered();
                RenderSnapshot.HoveredRenderId =
                    Selection.HoveredStableId();
                RenderSnapshot.GizmoDrawPackets.assign(
                    packets.begin(), packets.end());
                if (Extraction != nullptr)
                {
                    Extraction->SubmitSceneInteractionSnapshot(
                        RenderSnapshot);
                }
                context.Pacing.PreRenderSetupMicros +=
                    ElapsedInteractionMicros(packetBegin);
                ClearFrameBorrow();
            }

            void RunMaintenance(
                RuntimeFrameHookContext& context)
            {
                if (!ValidateBinding() ||
                    context.ActiveWorldHandle != BoundWorld ||
                    &context.ActiveWorld != BoundRegistry ||
                    Renderer == nullptr)
                {
                    return;
                }

                const auto begin =
                    std::chrono::steady_clock::now();
                Graphics::SelectionSystem& selectionSystem =
                    Renderer->GetSelectionSystem();
                while (const std::optional<
                           Graphics::PickReadbackResult> result =
                           selectionSystem.PopPickResult())
                {
                    // Never forward a zero/unknown sequence into the
                    // controller's standalone convenience fallback.
                    if (result->Sequence == 0u)
                        continue;

                    const auto contextIt = std::find_if(
                        InFlightPickContexts.begin(),
                        InFlightPickContexts.end(),
                        [sequence = result->Sequence](
                            const InFlightPickContext& entry)
                        { return entry.Sequence == sequence; });
                    if (contextIt == InFlightPickContexts.end())
                        continue;

                    InFlightPickContext pickContext =
                        std::move(*contextIt);
                    InFlightPickContexts.erase(contextIt);
                    if (pickContext.World != BoundWorld ||
                        pickContext.InteractionEpoch !=
                            InteractionEpoch)
                    {
                        (void)Selection.DiscardInFlightPick(
                            result->Sequence);
                        continue;
                    }

                    const bool consumed = result->Hit
                        ? Selection.ConsumeHit(
                              *BoundRegistry,
                              result->StableEntityId,
                              result->Sequence)
                        : Selection.ConsumeNoHit(
                              *BoundRegistry,
                              result->Sequence);
                    if (!consumed)
                        continue;

                    LastRefinedPrimitive =
                        RefinePickReadbackResult(
                            *BoundRegistry,
                            *result,
                            pickContext.Context
                                ? &*pickContext.Context
                                : nullptr);
                    ++LastRefinedPrimitiveGeneration;
                }
                context.Pacing.SelectionReadbackMicros +=
                    ElapsedInteractionMicros(begin);
            }

            void BeforeDocumentReplace(
                const SceneReplacementContext& context)
            {
                if (!AcceptingCallbacks)
                    return;
                if (BoundWorld != context.World ||
                    BoundRegistry != &context.Registry)
                {
                    (void)ValidateBinding();
                }
                if (BoundWorld == context.World &&
                    BoundRegistry == &context.Registry)
                {
                    ClearWorldBoundState();
                }
            }

            void AfterDocumentReplace(
                const SceneReplacementContext& context)
            {
                if (!AcceptingCallbacks)
                    return;
                BindTo(context.World, &context.Registry);
            }

            void AnnounceShutdown()
            {
                if (!AcceptingCallbacks)
                    return;

                ClearWorldBoundState();
                AcceptingCallbacks = false;
                if (Documents != nullptr &&
                    DocumentParticipant.IsValid())
                {
                    (void)Documents->
                        UnregisterReplacementParticipant(
                            DocumentParticipant);
                }
                DocumentParticipant = {};

                // Provider lifetimes are still live at announcement. Drop all
                // borrows now so reverse name-sorted module shutdown cannot
                // observe a destroyed provider.
                Documents = nullptr;
                History = nullptr;
                Window = nullptr;
                Renderer = nullptr;
                Extraction = nullptr;
                Worlds = nullptr;
            }
        };

        std::shared_ptr<State> Shared{};
        KernelEventSubscription ActiveWorldChangedSubscription{};
        KernelEventSubscription WorldDestroyedSubscription{};
        KernelEventSubscription ShutdownSubscription{};
        bool ModulePublished{false};
        bool SelectionPublished{false};
    };

    SceneInteractionModule::SceneInteractionModule()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    SceneInteractionModule::~SceneInteractionModule()
    {
        if (m_Impl)
            m_Impl->Shared.reset();
    }

    std::string_view SceneInteractionModule::Name() const noexcept
    {
        return "Runtime.SceneInteractionModule";
    }

    Core::Result SceneInteractionModule::OnRegister(
        EngineSetup& setup)
    {
        if (!m_Impl ||
            m_Impl->Shared ||
            m_Impl->ModulePublished ||
            m_Impl->SelectionPublished ||
            m_Impl->ActiveWorldChangedSubscription.IsValid() ||
            m_Impl->WorldDestroyedSubscription.IsValid() ||
            m_Impl->ShutdownSubscription.IsValid() ||
            setup.Services().Phase() !=
                ServiceRegistryPhase::Registration ||
            setup.Services().Find<SceneInteractionModule>() !=
                nullptr ||
            setup.Services().Find<SelectionController>() !=
                nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl->Shared = std::make_shared<Impl::State>();
        m_Impl->Shared->Worlds = &setup.Worlds();
        m_Impl->Shared->BindTo(
            setup.Worlds().ActiveWorld(),
            setup.Worlds().Get(
                setup.Worlds().ActiveWorld()));

        if (Core::Result provided =
                setup.Services().Provide<SceneInteractionModule>(
                    *this, Name());
            !provided.has_value())
        {
            m_Impl->Shared.reset();
            return provided;
        }
        m_Impl->ModulePublished = true;

        if (Core::Result provided =
                setup.Services().Provide<SelectionController>(
                    m_Impl->Shared->Selection, Name());
            !provided.has_value())
        {
            (void)setup.Services().Withdraw<
                SceneInteractionModule>(*this);
            m_Impl->ModulePublished = false;
            m_Impl->Shared.reset();
            return provided;
        }
        m_Impl->SelectionPublished = true;

        const std::weak_ptr<Impl::State> weakState =
            m_Impl->Shared;
        m_Impl->ActiveWorldChangedSubscription =
            setup.Subscribe<ActiveWorldChanged>(
                [weakState](const ActiveWorldChanged&)
                {
                    if (const auto state = weakState.lock())
                        (void)state->ValidateBinding();
                });
        m_Impl->WorldDestroyedSubscription =
            setup.Subscribe<WorldWillBeDestroyed>(
                [weakState](
                    const WorldWillBeDestroyed& event)
                {
                    if (const auto state = weakState.lock();
                        state &&
                        event.World == state->BoundWorld)
                    {
                        state->ClearWorldBoundState();
                    }
                });
        m_Impl->ShutdownSubscription =
            setup.Subscribe<RuntimeShutdownAnnounced>(
                [weakState](const RuntimeShutdownAnnounced&)
                {
                    if (const auto state = weakState.lock())
                        state->AnnounceShutdown();
                });

        Core::Result viewportRegistered =
            setup.RegisterViewportInputHook(
                [weakState](
                    RuntimeViewportInputHookContext& context)
                {
                    if (const auto state = weakState.lock())
                        state->RunViewportInput(context);
                });
        Core::Result extractionRegistered =
            setup.RegisterFrameHook(
                FramePhase::BeforeExtraction,
                [weakState](RuntimeFrameHookContext& context)
                {
                    if (const auto state = weakState.lock())
                        state->RunBeforeExtraction(context);
                });
        Core::Result maintenanceRegistered =
            setup.RegisterFrameHook(
                FramePhase::Maintenance,
                [weakState](RuntimeFrameHookContext& context)
                {
                    if (const auto state = weakState.lock())
                        state->RunMaintenance(context);
                });

        if (!m_Impl->ActiveWorldChangedSubscription.IsValid() ||
            !m_Impl->WorldDestroyedSubscription.IsValid() ||
            !m_Impl->ShutdownSubscription.IsValid() ||
            !viewportRegistered.has_value() ||
            !extractionRegistered.has_value() ||
            !maintenanceRegistered.has_value())
        {
            RuntimeModuleShutdownContext context{
                .Commands = setup.Commands(),
                .Events = setup.Events(),
                .Jobs = setup.Jobs(),
                .Worlds = setup.Worlds(),
                .Services = setup.Services(),
            };
            OnShutdown(context);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        return Core::Ok();
    }

    Core::Result SceneInteractionModule::OnResolve(
        EngineSetup& setup)
    {
        if (!m_Impl ||
            !m_Impl->Shared ||
            !m_Impl->ModulePublished ||
            !m_Impl->SelectionPublished ||
            setup.Services().Find<SceneInteractionModule>() !=
                this ||
            setup.Services().Find<SelectionController>() !=
                &m_Impl->Shared->Selection)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto& state = *m_Impl->Shared;
        state.Window =
            setup.Services().Find<Platform::IWindow>();
        state.Renderer =
            setup.Services().Find<Graphics::IRenderer>();
        state.Extraction =
            setup.Services().Find<RenderExtractionCache>();
        if (state.Window == nullptr ||
            state.Renderer == nullptr ||
            state.Extraction == nullptr)
        {
            RuntimeModuleShutdownContext context{
                .Commands = setup.Commands(),
                .Events = setup.Events(),
                .Jobs = setup.Jobs(),
                .Worlds = setup.Worlds(),
                .Services = setup.Services(),
            };
            OnShutdown(context);
            return Core::Err(
                Core::ErrorCode::ResourceNotFound);
        }

        state.Documents =
            setup.Services().Find<SceneDocumentModule>();
        state.History =
            setup.Services().Find<EditorCommandHistory>();
        if (state.Documents != nullptr)
        {
            const std::weak_ptr<Impl::State> weakState =
                m_Impl->Shared;
            auto participant =
                state.Documents->
                    RegisterReplacementParticipant(
                        SceneReplacementParticipantDesc{
                            .Name =
                                "Runtime.SceneInteractionModule",
                            .BeforeReplace =
                                [weakState](
                                    const SceneReplacementContext&
                                        context)
                                {
                                    if (const auto locked =
                                            weakState.lock())
                                    {
                                        locked->
                                            BeforeDocumentReplace(
                                                context);
                                    }
                                },
                            .AfterReplace =
                                [weakState](
                                    const SceneReplacementContext&
                                        context)
                                {
                                    if (const auto locked =
                                            weakState.lock())
                                    {
                                        locked->
                                            AfterDocumentReplace(
                                                context);
                                    }
                                },
                        });
            if (!participant.has_value())
            {
                const Core::ErrorCode error =
                    participant.error();
                RuntimeModuleShutdownContext context{
                    .Commands = setup.Commands(),
                    .Events = setup.Events(),
                    .Jobs = setup.Jobs(),
                    .Worlds = setup.Worlds(),
                    .Services = setup.Services(),
                };
                OnShutdown(context);
                return Core::Err(error);
            }
            state.DocumentParticipant = *participant;
        }

        (void)state.ValidateBinding();
        state.PublishEmpty(state.BoundWorld);
        return Core::Ok();
    }

    void SceneInteractionModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        if (!m_Impl)
            return;

        if (m_Impl->Shared)
            m_Impl->Shared->AnnounceShutdown();

        if (m_Impl->ActiveWorldChangedSubscription
                .IsValid())
        {
            context.Events.Unsubscribe(
                m_Impl->
                    ActiveWorldChangedSubscription);
        }
        if (m_Impl->WorldDestroyedSubscription.IsValid())
        {
            context.Events.Unsubscribe(
                m_Impl->WorldDestroyedSubscription);
        }
        if (m_Impl->ShutdownSubscription.IsValid())
        {
            context.Events.Unsubscribe(
                m_Impl->ShutdownSubscription);
        }
        m_Impl->ActiveWorldChangedSubscription = {};
        m_Impl->WorldDestroyedSubscription = {};
        m_Impl->ShutdownSubscription = {};

        if (m_Impl->Shared &&
            m_Impl->SelectionPublished)
        {
            (void)context.Services.Withdraw<
                SelectionController>(
                    m_Impl->Shared->Selection);
        }
        m_Impl->SelectionPublished = false;
        if (m_Impl->ModulePublished)
        {
            (void)context.Services.Withdraw<
                SceneInteractionModule>(*this);
        }
        m_Impl->ModulePublished = false;
        m_Impl->Shared.reset();
    }

    std::optional<ECS::EntityHandle>
    SceneInteractionModule::ResolveEntityByStableId(
        const ECS::Components::StableId id)
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state || !state->ValidateBinding())
            return std::nullopt;
        return state->Lookup.ResolveByStableId(
            *state->BoundRegistry, id);
    }

    const StableEntityLookupDiagnostics&
    SceneInteractionModule::LookupDiagnostics() const noexcept
    {
        return m_Impl->Shared->Lookup.GetDiagnostics();
    }

    GizmoInteraction&
    SceneInteractionModule::Interaction() noexcept
    {
        return m_Impl->Shared->Gizmo;
    }

    const GizmoInteraction&
    SceneInteractionModule::Interaction() const noexcept
    {
        return m_Impl->Shared->Gizmo;
    }

    const std::optional<PrimitiveSelectionResult>&
    SceneInteractionModule::LastRefinedPrimitive()
        const noexcept
    {
        return m_Impl->Shared->LastRefinedPrimitive;
    }

    std::uint64_t
    SceneInteractionModule::
        LastRefinedPrimitiveGeneration() const noexcept
    {
        return m_Impl->Shared->LastRefinedPrimitiveGeneration;
    }
}
