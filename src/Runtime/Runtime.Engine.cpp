module;
#include <chrono>
#include <queue>
#include <GLFW/glfw3.h>
#include <RHI/RHI.Vulkan.hpp>

module Runtime.Engine;

import Core.Logging;
import Core.Input;
import Core.Window;
import Runtime.RHI.Shader;

namespace Runtime
{
    Engine::Engine(const EngineConfig& config)
    {
        Core::Log::Info("Initializing Engine...");

        // 1. Window
        Core::Windowing::WindowProps props{config.AppName, config.Width, config.Height};
        m_Window = std::make_unique<Core::Windowing::Window>(props);

        Core::Input::Initialize(m_Window->GetNativeHandle());
        m_Window->SetEventCallback([this](const Core::Windowing::Event& e)
        {
            if (e.Type == Core::Windowing::EventType::WindowClose) m_Running = false;
            if (e.Type == Core::Windowing::EventType::KeyPressed && e.KeyCode == 256) m_Running = false;
        });

        // 2. Vulkan Context & Surface
#ifdef NDEBUG
        bool enableValidation = false;
#else
        bool enableValidation = true;
#endif
        RHI::ContextConfig rhiConfig{config.AppName, enableValidation};
        m_Context = std::make_unique<RHI::VulkanContext>(rhiConfig, *m_Window);

        if (!m_Window->CreateSurface(m_Context->GetInstance(), nullptr, &m_Surface))
        {
            Core::Log::Error("FATAL: Failed to create Vulkan Surface");
            std::exit(-1); // Hard fail without exceptions
        }

        // 3. Device
        m_Device = std::make_unique<RHI::VulkanDevice>(*m_Context, m_Surface);

        // 4. Swapchain & Renderer
        m_Swapchain = std::make_unique<RHI::VulkanSwapchain>(*m_Device, *m_Window);
        m_Renderer = std::make_unique<RHI::SimpleRenderer>(*m_Device, *m_Swapchain);

        InitPipeline();
    }

    Engine::~Engine()
    {
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        // Order matters!
        m_Scene.GetRegistry().clear();
        m_RenderSystem.reset();
        m_Pipeline.reset();
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();
        m_Renderer.reset();
        m_Swapchain.reset();
        m_Device.reset();

        vkDestroySurfaceKHR(m_Context->GetInstance(), m_Surface, nullptr);
        m_Context.reset();
        m_Window.reset();
    }

    void Engine::InitPipeline()
    {
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorPool>(*m_Device);

        RHI::ShaderModule vert(*m_Device, "shaders/triangle.vert.spv", RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, "shaders/triangle.frag.spv", RHI::ShaderStage::Fragment);

        RHI::PipelineConfig config{&vert, &frag};
        m_Pipeline = std::make_unique<RHI::GraphicsPipeline>(*m_Device, *m_Swapchain, config,
                                                             m_DescriptorLayout->GetHandle());

        m_RenderSystem = std::make_unique<Graphics::RenderSystem>(
            *m_Device, *m_Swapchain, *m_Renderer, *m_Pipeline);
    }

    void Engine::Run()
    {
        OnStart(); // User setup

        auto lastTime = std::chrono::high_resolution_clock::now();

        std::deque<float> frameTimes;
        const size_t FRAME_SAMPLES = 10;

        while (m_Running && !m_Window->ShouldClose())
        {
            m_Window->OnUpdate();

            if (m_FramebufferResized) {
                m_Renderer->OnResize();
                m_FramebufferResized = false;
            }

            auto currentTime = std::chrono::high_resolution_clock::now();
            float rawDt = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            frameTimes.push_back(rawDt);
            if (frameTimes.size() > FRAME_SAMPLES) frameTimes.pop_front();

            float dt = 0.0f;
            for (float t : frameTimes) dt += t;
            dt /= frameTimes.size();

            // Cap dt to prevent spiral of death during lag spikes
            if (dt > 0.1f) dt = 0.1f;

            OnUpdate(dt); // User Logic

            // Currently RenderSystem::OnUpdate handles the draw, so OnRender is optional hook
            // In future, OnRender might manipulate the RenderGraph
            OnRender();
        }

        vkDeviceWaitIdle(m_Device->GetLogicalDevice());
    }
}
