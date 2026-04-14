module;

#include <chrono>
#include <memory>
#include <thread>
#include <utility>

module Extrinsic.Runtime.Engine;

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Graphics.Renderer;

namespace Extrinsic::Runtime
{
    namespace
    {
        std::unique_ptr<RHI::IDevice> CreateDevice(const Core::Config::RenderConfig& config)
        {
            switch (config.Backend)
            {
            case Core::Config::GraphicsBackend::Vulkan:
                return Backends::Vulkan::CreateVulkanDevice();
            }

            std::terminate();
        }
    }

    Engine::Engine(Core::Config::EngineConfig config, std::unique_ptr<IApplication> application)
        : m_Config(std::move(config))
          , m_Application(std::move(application))
    {
        if (!m_Application)
        {
            std::terminate();
        }
    }

    Engine::~Engine()
    {
        if (m_Initialized)
        {
            Shutdown();
        }
    }

    void Engine::Initialize()
    {
        m_Window = Platform::CreateWindow(m_Config.Window);
        m_Device = CreateDevice(m_Config.Render);
        m_Device->Initialize(*m_Window, m_Config.Render);

        m_Renderer = Graphics::CreateRenderer();
        m_Renderer->Initialize(*m_Device);

        m_Application->OnInitialize(*this);

        m_Initialized = true;
        m_Running = true;
    }

    void Engine::Run()
    {
        using Clock = std::chrono::steady_clock;

        auto previousTime = Clock::now();

        while (m_Running && !m_Window->ShouldClose())
        {
            const auto currentTime = Clock::now();
            const std::chrono::duration<double> delta = currentTime - previousTime;
            previousTime = currentTime;

            m_Window->PollEvents();
            HandleResize();

            if (m_Window->IsMinimized())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            Tick(delta.count());
        }
    }

    void Engine::Shutdown()
    {
        m_Running = false;

        if (m_Device)
        {
            m_Device->WaitIdle();
        }

        if (m_Application)
        {
            m_Application->OnShutdown(*this);
        }

        if (m_Renderer)
        {
            m_Renderer->Shutdown();
            m_Renderer.reset();
        }

        if (m_Device)
        {
            m_Device->Shutdown();
            m_Device.reset();
        }

        m_Window.reset();
        m_Initialized = false;
    }

    void Engine::HandleResize()
    {
        if (!m_Window->WasResized())
        {
            return;
        }

        const auto extent = m_Window->GetExtent();
        if (extent.Width == 0 || extent.Height == 0)
        {
            return;
        }

        m_Device->WaitIdle();
        m_Device->Resize(extent.Width, extent.Height);
        m_Renderer->Resize(extent.Width, extent.Height);
        m_Window->AcknowledgeResize();
    }

    void Engine::Tick(double deltaSeconds)
    {
        m_Application->OnUpdate(*this, deltaSeconds);

        RHI::FrameHandle frame{};
        if (!m_Device->BeginFrame(frame))
        {
            return;
        }

        m_Renderer->RenderFrame(frame);
        m_Device->EndFrame(frame);
        m_Device->Present(frame);
    }

    bool Engine::IsRunning() const noexcept
    {
        return m_Running;
    }

    void Engine::RequestExit() noexcept
    {
        m_Running = false;
    }

    Platform::IWindow& Engine::GetWindow() noexcept
    {
        return *m_Window;
    }

    RHI::IDevice& Engine::GetDevice() noexcept
    {
        return *m_Device;
    }

    Graphics::IRenderer& Engine::GetRenderer() noexcept
    {
        return *m_Renderer;
    }
}
