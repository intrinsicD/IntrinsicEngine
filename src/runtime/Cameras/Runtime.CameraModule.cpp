module;

#include <optional>
#include <string_view>

module Extrinsic.Runtime.CameraModule;

import Extrinsic.Core.Error;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    std::string_view CameraModule::Name() const noexcept
    {
        return "Runtime.CameraModule";
    }

    Core::Result CameraModule::OnRegister(EngineSetup& setup)
    {
        if (m_Published ||
            m_ActiveWorldChangedSubscription.IsValid() ||
            m_WorldDestroyedSubscription.IsValid() ||
            m_Registry.BoundWorld().IsValid() ||
            setup.Services().Phase() !=
                ServiceRegistryPhase::Registration ||
            setup.Services().Find<CameraControllerRegistry>() != nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Registry.ResetForWorld(setup.Worlds().ActiveWorld());
        if (Core::Result provided =
                setup.Services().Provide<CameraControllerRegistry>(
                    m_Registry, Name());
            !provided.has_value())
        {
            m_Registry.ResetForWorld({});
            return provided;
        }
        m_Published = true;

        m_ActiveWorldChangedSubscription =
            setup.Subscribe<ActiveWorldChanged>(
                [this](const ActiveWorldChanged& event)
                {
                    m_Registry.ResetForWorld(event.Current);
                });
        if (!m_ActiveWorldChangedSubscription.IsValid())
        {
            ShutdownAndReset(&setup.Events(), &setup.Services());
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_WorldDestroyedSubscription =
            setup.Subscribe<WorldWillBeDestroyed>(
                [this](const WorldWillBeDestroyed& event)
                {
                    if (event.World == m_Registry.BoundWorld())
                        m_Registry.ResetForWorld({});
                });
        if (!m_WorldDestroyedSubscription.IsValid())
        {
            ShutdownAndReset(&setup.Events(), &setup.Services());
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        if (Core::Result registered =
                setup.RegisterViewportInputHook(
                    [this](RuntimeViewportInputHookContext& context)
                    {
                        RunViewportInput(context);
                    });
            !registered.has_value())
        {
            ShutdownAndReset(&setup.Events(), &setup.Services());
            return registered;
        }

        return Core::Ok();
    }

    Core::Result CameraModule::OnResolve(EngineSetup& setup)
    {
        if (!m_Published ||
            setup.Services().Find<CameraControllerRegistry>() !=
                &m_Registry)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return Core::Ok();
    }

    void CameraModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        ShutdownAndReset(&context.Events, &context.Services);
    }

    void CameraModule::RunViewportInput(
        RuntimeViewportInputHookContext& context)
    {
        if (context.ActiveWorldHandle != m_Registry.BoundWorld())
            m_Registry.ResetForWorld(context.ActiveWorldHandle);

        if (!context.ActiveWorldHandle.IsValid() ||
            !context.Config.Camera.Enabled)
        {
            return;
        }

        ICameraController* controller =
            m_Registry.ResolveOrNull(CameraControllerSlot::Main);
        if (controller == nullptr)
        {
            const std::optional<Graphics::CameraViewInput> worldSeed =
                m_Registry.WorldSeedFor(context.ActiveWorldHandle);
            m_Registry.Register(
                CameraControllerSlot::Main,
                CreateCameraController(
                    context.Config.Camera.Controller,
                    worldSeed.value_or(Graphics::CameraViewInput{})));
            controller =
                m_Registry.ResolveOrNull(CameraControllerSlot::Main);
        }

        if (controller == nullptr)
            return;

        if (!context.EditorCapture.CapturesViewportInput())
        {
            controller->Update(
                context.Input, context.FrameDeltaSeconds);
        }

        context.RenderInput.Camera =
            controller->GetView(context.Viewport);
        context.RenderInput.Camera.ExplicitCameraTransition =
            m_Registry.ConsumeCameraTransition(
                CameraControllerSlot::Main);
    }

    void CameraModule::ShutdownAndReset(
        KernelEventBus* const events,
        ServiceRegistry* const services) noexcept
    {
        if (events != nullptr)
        {
            if (m_ActiveWorldChangedSubscription.IsValid())
                events->Unsubscribe(
                    m_ActiveWorldChangedSubscription);
            if (m_WorldDestroyedSubscription.IsValid())
                events->Unsubscribe(m_WorldDestroyedSubscription);
        }
        m_ActiveWorldChangedSubscription = {};
        m_WorldDestroyedSubscription = {};

        if (services != nullptr && m_Published)
        {
            (void)services->Withdraw<CameraControllerRegistry>(
                m_Registry);
        }
        m_Published = false;
        m_Registry.ResetForWorld({});
    }
}
