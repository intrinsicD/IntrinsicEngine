module;

// 1. Force ImGui to expose Dynamic Rendering fields
#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING

#include "RHI/RHI.Vulkan.hpp"

#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h> // Required for glfwGetInstanceProcAddress
#include <vector>
#include <filesystem>

module Runtime.Interface.GUI;
import Core.Logging;
import Core.Filesystem;

namespace Runtime::Interface::GUI
{
    struct RegisteredPanel
    {
        std::string Name;
        UIPanelCallback Callback;
        bool IsClosable = true;
        bool IsOpen = true; // Track state
        int Flags = 0;
    };

    struct RegisteredMenu
    {
        std::string Name;
        UIMenuCallback Callback;
    };

    static std::vector<RegisteredPanel> s_Panels;
    static std::vector<RegisteredMenu> s_Menus;
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
        ImGuiIO& io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui::StyleColorsDark();

        // --- HIGH DPI SCALING START ---

        // 1. Detect Monitor Scale from GLFW
        float x_scale = 1.0f, y_scale = 1.0f;
        glfwGetWindowContentScale((GLFWwindow*)window.GetNativeHandle(), &x_scale, &y_scale);

        // On some Linux configs (Wayland), GLFW might return 1.0 even on HiDPI.
        // You can optionally override this if x_scale == 1.0f but you know it's wrong.
        if (x_scale > 1.0f)
        {
            Core::Log::Info("High DPI Detected: Scale Factor {}", x_scale);

            // 2. Scale UI Elements (Padding, Rounding, Spacing)
            ImGui::GetStyle().ScaleAllSizes(x_scale);
        }

        // 3. Load Scaled Font
        // IMPORTANT: Use a TTF file. The default bitmap font looks terrible when scaled.
        float baseFontSize = 16.0f;
        float scaledFontSize = baseFontSize * x_scale;

        // Use your filesystem helper or a relative path
        std::string fontPath = Core::Filesystem::GetAssetPath("assets/fonts/Roboto-Medium.ttf");

        if (std::filesystem::exists(fontPath))
        {
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), scaledFontSize);
            Core::Log::Info("Loaded custom font at size {}", scaledFontSize);
        }
        else
        {
            // Fallback: Scale the default ugly font (better than tiny text)
            Core::Log::Warn("Custom font not found at '{}'. UI text may look blurry.", fontPath);
            ImFontConfig fontConfig;
            fontConfig.SizePixels = scaledFontSize;
            io.Fonts->AddFontDefault(&fontConfig);
        }
        // --- HIGH DPI SCALING END ---

        // 2. Create Descriptor Pool
        VkDescriptorPoolSize pool_sizes[] =
        {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
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

        ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char* function_name, void* vulkan_instance)
        {
            return vkGetInstanceProcAddr(reinterpret_cast<VkInstance>(vulkan_instance), function_name);
        }, instance);

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

    void DrawGUI()
    {
        // 1. MAIN MENU BAR
        if (ImGui::BeginMainMenuBar())
        {
            // A. Execute User Registered Menus (File, Edit, etc.)
            for (const auto& menu : s_Menus)
            {
                if (menu.Callback) menu.Callback();
            }

            // B. Automatic "Panels" Menu (To re-open closed windows)
            if (ImGui::BeginMenu("Panels"))
            {
                for (auto& panel : s_Panels)
                {
                    // Checkbox to toggle visibility
                    if (ImGui::MenuItem(panel.Name.c_str(), nullptr, &panel.IsOpen))
                    {
                        // Logic handled by bool reference
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // 2. PANELS (WINDOWS)
        for (auto it = s_Panels.begin(); it != s_Panels.end(); )
        {
            if (!it->IsOpen && it->IsClosable)
            {
                // If closed, we skip drawing, but we KEEP it in the vector
                // so the "Panels" menu above can re-enable it.
                ++it;
                continue;
            }

            bool* pOpen = it->IsClosable ? &it->IsOpen : nullptr;

            if (ImGui::Begin(it->Name.c_str(), pOpen, it->Flags))
            {
                if (it->Callback) it->Callback();
            }
            ImGui::End();

            ++it;
        }
    }

    void Render(VkCommandBuffer cmd)
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }

    void RegisterPanel(std::string name, UIPanelCallback callback, bool isClosable, int flags)
    {
        // Check if panel exists to update it (re-open it if it was closed)
        for (auto& panel : s_Panels)
        {
            if (panel.Name == name)
            {
                panel.Callback = std::move(callback);
                panel.IsClosable = isClosable;
                panel.Flags = flags;
                panel.IsOpen = true; // Re-open
                return;
            }
        }

        // Add new
        s_Panels.push_back({std::move(name), std::move(callback), isClosable, true, flags});
    }

    void RemovePanel(const std::string& name)
    {
        std::erase_if(s_Panels, [&](const RegisteredPanel& p) { return p.Name == name; });
    }

    void RegisterMainMenuBar(std::string name, UIMenuCallback callback)
    {
        // Simple append - ImGui menus merge automatically if they have the same name (e.g. "File")
        s_Menus.push_back({std::move(name), std::move(callback)});
    }

    bool WantCaptureMouse()
    {
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool WantCaptureKeyboard()
    {
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float columnWidth)
    {
        bool changed = false;
        ImGui::PushID(label.c_str());

        // Simple columns layout
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);

        ImGui::Text("%s", label.c_str());
        ImGui::NextColumn();

        // 3 floats
        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

        // X
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        if (ImGui::Button("X")) { values.x = resetValue; changed = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Y
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
        if (ImGui::Button("Y")) { values.y = resetValue; changed = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // Z
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
        if (ImGui::Button("Z")) { values.z = resetValue; changed = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::PopItemWidth();

        ImGui::Columns(1);
        ImGui::PopID();

        return changed;
    }
}
