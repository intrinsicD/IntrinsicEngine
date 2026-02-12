module;

// 1. Force ImGui to expose Dynamic Rendering fields
#define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING

#include "RHI.Vulkan.hpp"

#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h> // Required for glfwGetInstanceProcAddress
#include <vector>
#include <filesystem>

// Add this line to manually declare the missing function if it's not in the header but present in the object file (unlikely after 2025 change)
// OR, more likely, we need to use ImGui_ImplVulkan_AddTexture manually for fonts if the automatic system fails.
// But the stack trace shows automatic system is TRYING to work (ImGui_ImplVulkan_UpdateTexture).

module Interface:GUI.Impl;
import :GUI;
import Core;
import RHI;

namespace Interface::GUI
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
    static bool s_ShowTelemetryPanel = false;

    static float ToMs(uint64_t ns) { return static_cast<float>(static_cast<double>(ns) / 1'000'000.0); }

    // Simple horizontal bar for time in ms (clamped to a target window).
    static void DrawTimeBar(const char* label, float ms, float targetMs)
    {
        const float clamped = std::min(ms, targetMs);
        const float frac = (targetMs > 0.0f) ? (clamped / targetMs) : 0.0f;

        ImGui::TextUnformatted(label);
        ImGui::SameLine();

        // Reserve a nice wide bar.
        const float barWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x - 80.0f);
        const ImVec2 size(barWidth, 0.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
        ImGui::ProgressBar(frac, size, nullptr);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::Text("%.2f ms", ms);
    }

    // Helper to draw performance/telemetry panel
    static void DrawTelemetryPanel()
    {
        ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Performance", &s_ShowTelemetryPanel))
        {
            auto& telemetry = Core::Telemetry::TelemetrySystem::Get();
            const auto& stats = telemetry.GetFrameStats(0);

            const double avgMs = telemetry.GetAverageFrameTimeMs(60);
            const double avgFps = telemetry.GetAverageFPS(60);

            const float cpuMs = ToMs(stats.CpuTimeNs);
            const float gpuMs = ToMs(stats.GpuTimeNs);
            const float frameMs = ToMs(stats.FrameTimeNs);

            // -----------------------------------------------------------------
            // Header (big FPS + key counters)
            // -----------------------------------------------------------------
            {
                ImGui::PushFont(ImGui::GetFont());
                ImGui::Text("%.1f FPS", avgFps);
                ImGui::PopFont();

                ImGui::SameLine();
                ImGui::TextDisabled("(avg %.2f ms)", avgMs);

                ImGui::Separator();

                ImGui::TextDisabled("Frame #");
                ImGui::SameLine();
                ImGui::Text("%lu", stats.FrameNumber);

                ImGui::SameLine();
                ImGui::TextDisabled("  Draw");
                ImGui::SameLine();
                ImGui::Text("%u", stats.DrawCalls);

                ImGui::SameLine();
                ImGui::TextDisabled("  Tris");
                ImGui::SameLine();
                ImGui::Text("%u", stats.TriangleCount);
            }

            ImGui::Spacing();

            // -----------------------------------------------------------------
            // Frame budget bars (CPU/GPU)
            // -----------------------------------------------------------------
            {
                // Common budgets
                constexpr float kBudget60 = 16.6667f;
                constexpr float kBudget30 = 33.3333f;

                ImGui::TextDisabled("Frame Budget");

                // Use 16.6 ms as primary target, but clamp to 33ms for visibility.
                const float targetMs = kBudget60;
                const float clampMs = kBudget30;

                DrawTimeBar("CPU", std::min(cpuMs, clampMs), targetMs);
                DrawTimeBar("GPU", std::min(gpuMs, clampMs), targetMs);
                DrawTimeBar("Total", std::min(frameMs, clampMs), targetMs);
            }

            ImGui::Separator();

            // -----------------------------------------------------------------
            // Graphs: CPU and GPU history
            // -----------------------------------------------------------------
            {
                static float cpuHistoryMs[120] = {};
                static float gpuHistoryMs[120] = {};
                static int historyIdx = 0;

                cpuHistoryMs[historyIdx] = cpuMs;
                gpuHistoryMs[historyIdx] = gpuMs;
                historyIdx = (historyIdx + 1) % 120;

                float maxMs = 33.3f;
                for (int i = 0; i < 120; ++i)
                {
                    maxMs = std::max(maxMs, cpuHistoryMs[i]);
                    maxMs = std::max(maxMs, gpuHistoryMs[i]);
                }

                ImGui::TextDisabled("CPU/GPU Frame Time (ms)");

                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.25f, 0.65f, 1.0f, 1.0f));
                ImGui::PlotLines("CPU", cpuHistoryMs, 120, historyIdx, nullptr, 0.0f, maxMs * 1.1f, ImVec2(0, 70));
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.90f, 0.55f, 0.15f, 1.0f));
                ImGui::PlotLines("GPU", gpuHistoryMs, 120, historyIdx, nullptr, 0.0f, maxMs * 1.1f, ImVec2(0, 70));
                ImGui::PopStyleColor();
            }

            ImGui::Separator();

            // -----------------------------------------------------------------
            // CPU timing categories
            // -----------------------------------------------------------------
            if (ImGui::TreeNodeEx("CPU Timing Breakdown", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static char filterBuf[64] = {};
                ImGui::TextDisabled("Filter");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputText("##TimingFilter", filterBuf, sizeof(filterBuf));

                auto categories = telemetry.GetCategoriesSortedByTime();

                // Show only the top N by default (avoids overwhelming the panel).
                static int topN = 20;
                ImGui::SameLine();
                ImGui::TextDisabled("Top");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::SliderInt("##TopN", &topN, 5, 50, "%d", ImGuiSliderFlags_AlwaysClamp);

                const bool hasFilter = filterBuf[0] != '\0';

                if (ImGui::BeginTable("TimingTable", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX,
                    ImVec2(0.0f, 160.0f)))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Total (ms)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                    ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("%Frame", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableHeadersRow();

                    int shown = 0;
                    for (const auto* cat : categories)
                    {
                        if (!cat || cat->CallCount == 0) continue;

                        const char* name = cat->Name ? cat->Name : "<unnamed>";
                        if (hasFilter)
                        {
                            if (strstr(name, filterBuf) == nullptr)
                                continue;
                        }

                        if (!hasFilter && shown >= topN)
                            break;

                        const float totalMs = static_cast<float>(cat->TotalMs());
                        const float avgCatMs = static_cast<float>(cat->AverageMs());
                        const float pctFrame = (cpuMs > 0.0f) ? (totalMs / cpuMs) * 100.0f : 0.0f;

                        ImGui::TableNextRow();

                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(name);

                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", totalMs);

                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", avgCatMs);

                        ImGui::TableNextColumn();
                        ImGui::Text("%u", cat->CallCount);

                        ImGui::TableNextColumn();
                        ImGui::Text("%.1f", pctFrame);

                        ++shown;
                    }

                    ImGui::EndTable();
                }

                ImGui::TreePop();
            }
        }
        ImGui::End();
    }

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
        std::string fontPath = Core::Filesystem::GetAssetPath("fonts/Roboto-Medium.ttf");

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

        // Upload Fonts
        // This is necessary because we are using dynamic rendering and managing our own headers.
        // It creates the font texture and uploads it to the GPU.
        // Note: As of ImGui 1.91+, ImGui_ImplVulkan_CreateFontsTexture is removed.
        // The backend handles font upload lazily in ImGui_ImplVulkan_RenderDrawData -> ImGui_ImplVulkan_UpdateTexture.
        // However, we must ensure the command pool is ready if we wanted to do it early, but we don't need to anymore.

        // ImGui_ImplVulkan_CreateFontsTexture(); // REMOVED

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

    void EndFrame()
    {
        ImGui::EndFrame();
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
                ImGui::Separator();
                ImGui::MenuItem("Performance", nullptr, &s_ShowTelemetryPanel);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // 2. BUILT-IN TELEMETRY PANEL
        if (s_ShowTelemetryPanel)
        {
            DrawTelemetryPanel();
        }

        // 3. USER PANELS (WINDOWS)
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

    void* AddTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
    {
        // ImGui's Vulkan backend returns an opaque ImTextureID.
        return ImGui_ImplVulkan_AddTexture(sampler, imageView, imageLayout);
    }

    void RemoveTexture(void* textureId)
    {
        if (!textureId) return;
        ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(textureId));
    }
}
