module;

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.SceneInteractionModule;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.GizmoFrameService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SelectionReadback;
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
    }

    struct SceneInteractionModule::Impl
    {
        struct State
        {
            WorldRegistry* Worlds{nullptr};
            Platform::IWindow* Window{nullptr};
            Graphics::IRenderer* Renderer{nullptr};
            RenderExtractionCache* Extraction{nullptr};
            SceneDocumentModule* Documents{nullptr};

            SelectionController Selection{};
            StableEntityLookup Lookup{};
            StableEntityLookupSceneBinding LookupBinding{};
            SelectionReadbackState Readback{};
            GizmoFrameService Gizmo{};

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
                Gizmo.ClearSceneState(BoundRegistry);
                if (BoundRegistry != nullptr)
                    Selection.ClearSceneState(*BoundRegistry);
                Readback.ClearSceneState();
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

                Gizmo.DriveInputForFrame(
                    GizmoFrameServiceInput{
                        .Scene = *BoundRegistry,
                        .Selection = Selection,
                        .Window = *Window,
                        .Viewport = context.Viewport,
                        .ImGuiCapturesInput =
                            context.EditorCapture
                                .CapturesViewportInput(),
                        .ImGuiCapturesMouse =
                            context.EditorCapture.CapturedMouse ||
                            context.EditorCapture.WidgetsActive,
                        .Camera = context.RenderInput.Camera,
                    });
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
                    Readback.DrainPendingPickForFrame(
                        Selection,
                        Renderer->GetSelectionSystem(),
                        FrameViewport,
                        *FrameRenderInput,
                        BoundWorld,
                        InteractionEpoch);
                }
                context.Pacing.SelectionPickDrainMicros +=
                    ElapsedInteractionMicros(pickBegin);

                const auto packetBegin =
                    std::chrono::steady_clock::now();
                const std::span<const
                    Graphics::TransformGizmoRenderPacket> packets =
                    Gizmo.BuildRenderPackets(*BoundRegistry);

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
                Readback.DrainCompletedReadbacksForFrame(
                    Renderer->GetSelectionSystem(),
                    Selection,
                    *BoundRegistry,
                    BoundWorld,
                    InteractionEpoch);
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
        return m_Impl->Shared->Gizmo.Interaction();
    }

    const GizmoInteraction&
    SceneInteractionModule::Interaction() const noexcept
    {
        return m_Impl->Shared->Gizmo.Interaction();
    }

    GizmoUndoStack&
    SceneInteractionModule::UndoStack() noexcept
    {
        return m_Impl->Shared->Gizmo.UndoStack();
    }

    const GizmoUndoStack&
    SceneInteractionModule::UndoStack() const noexcept
    {
        return m_Impl->Shared->Gizmo.UndoStack();
    }

    const std::optional<PrimitiveSelectionResult>&
    SceneInteractionModule::LastRefinedPrimitive()
        const noexcept
    {
        return m_Impl->Shared->Readback.LastRefinedPrimitive();
    }

    std::uint64_t
    SceneInteractionModule::
        LastRefinedPrimitiveGeneration() const noexcept
    {
        return m_Impl->Shared->Readback
            .LastRefinedPrimitiveGeneration();
    }
}
