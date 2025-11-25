module;

// 1. Force ImGui to expose Dynamic Rendering fields
#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING

#include "RHI/RHI.Vulkan.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h> // Required for glfwGetInstanceProcAddress
#include <vector>

module Runtime.Interface.GUI;
import Core.Logging;

namespace Runtime::Interface::GUI
{
    static VkDescriptorPool s_DescriptorPool = VK_NULL_HANDLE;
    static RHI::VulkanDevice* s_Device = nullptr;

    void Init(Core::Windowing::Window& window,
              RHI::VulkanDevice& device,
              RHI::VulkanSwapchain& swapchain,
              VkInstance instance,
              VkQueue graphicsQueue)
    {
        s_Device = &device;

        // 1. Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui::StyleColorsDark();

        // 2. Create Descriptor Pool
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(device.GetLogicalDevice(), &pool_info, nullptr, &s_DescriptorPool) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create ImGui Descriptor Pool");
            return;
        }

        // 3. Init GLFW Backend
        ImGui_ImplGlfw_InitForVulkan((GLFWwindow*)window.GetNativeHandle(), false);

        // 4. Init Vulkan Backend
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.ApiVersion = VK_API_VERSION_1_3;
        init_info.Instance = instance;
        init_info.PhysicalDevice = device.GetPhysicalDevice();
        init_info.Device = device.GetLogicalDevice();
        init_info.QueueFamily = device.GetQueueIndices().GraphicsFamily.value();
        init_info.Queue = graphicsQueue;
        init_info.DescriptorPool = s_DescriptorPool;
        init_info.MinImageCount = 2;
        init_info.ImageCount = (uint32_t)swapchain.GetImages().size();

        // API Fix for v1.91+
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        // Dynamic Rendering Setup
        init_info.UseDynamicRendering = true;

        VkFormat color_format = swapchain.GetImageFormat();
        VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {};
        pipeline_rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipeline_rendering_create_info.colorAttachmentCount = 1;
        pipeline_rendering_create_info.pColorAttachmentFormats = &color_format;

        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;

        // Loader Fix: Use GLFW's loader to bypass potential Volk/Static-Linking symbol conflicts
        ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char* function_name, void* vulkan_instance) {
            if (!function_name) return (PFN_vkVoidFunction)nullptr;
            VkInstance inst = reinterpret_cast<VkInstance>(vulkan_instance);
            return glfwGetInstanceProcAddress(inst, function_name);
        }, reinterpret_cast<void*>(instance));

        // Init
        ImGui_ImplVulkan_Init(&init_info);

        Core::Log::Info("ImGui Initialized.");
    }

    void Shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        if (s_DescriptorPool && s_Device)
        {
            vkDestroyDescriptorPool(s_Device->GetLogicalDevice(), s_DescriptorPool, nullptr);
        }
    }

    void BeginFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void Render(VkCommandBuffer cmd)
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }
}