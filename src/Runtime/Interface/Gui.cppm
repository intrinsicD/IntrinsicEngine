module;
#include <functional>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp" // For VkCommandBuffer

export module Interface:GUI;

import RHI;
import Core;

export namespace Interface::GUI
{
    using UIPanelCallback = std::function<void(void)>;
    using UIMenuCallback = std::function<void(void)>;

    // Initializes ImGui, GLFW backend, and Vulkan backend (Dynamic Rendering)
    void Init(Core::Windowing::Window& window, 
              RHI::VulkanDevice& device, 
              RHI::VulkanSwapchain& swapchain,
              VkInstance instance,
              VkQueue graphicsQueue);

    void Shutdown();

    void BeginFrame();

    void EndFrame();

    void DrawGUI();
    
    // Records the ImGui draw commands into the provided command buffer.
    // Must be called inside a RenderPass (or BeginRendering).
    void Render(VkCommandBuffer cmd);

    void RegisterPanel(std::string name, UIPanelCallback callback, bool isClosable = true, int flags = 0);

    void RemovePanel(const std::string& name);

    void RegisterMainMenuBar(std::string name, UIMenuCallback callback);

    // Returns true if ImGui is using the mouse (hovering a window, dragging a slider)
    [[nodiscard]] bool WantCaptureMouse();

    // Returns true if ImGui is using the keyboard (typing in a text box)
    [[nodiscard]] bool WantCaptureKeyboard();

    bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f);
}