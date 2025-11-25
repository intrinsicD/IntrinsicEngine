module;
#include "RHI/RHI.Vulkan.hpp" // For VkCommandBuffer

export module Runtime.Interface.GUI;

import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Core.Window;

export namespace Runtime::Interface::GUI
{
    // Initializes ImGui, GLFW backend, and Vulkan backend (Dynamic Rendering)
    void Init(Core::Windowing::Window& window, 
              RHI::VulkanDevice& device, 
              RHI::VulkanSwapchain& swapchain,
              VkInstance instance,
              VkQueue graphicsQueue);

    void Shutdown();

    void BeginFrame();
    
    // Records the ImGui draw commands into the provided command buffer.
    // Must be called inside a RenderPass (or BeginRendering).
    void Render(VkCommandBuffer cmd);

    // Returns true if ImGui is using the mouse (hovering a window, dragging a slider)
    [[nodiscard]] bool WantCaptureMouse();

    // Returns true if ImGui is using the keyboard (typing in a text box)
    [[nodiscard]] bool WantCaptureKeyboard();
}