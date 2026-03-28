module;
#include <functional>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp" // For VkCommandBuffer

export module Interface:GUI;

import RHI.Device;
import RHI.Swapchain;
import Core.Window;
import Core.Telemetry;

export namespace Interface::GUI
{
    using UIPanelCallback = std::function<void()>;
    using UIMenuCallback = std::function<void()>;
    using UIOverlayCallback = std::function<void()>;

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

    // Registers or updates a panel. On first registration, closable panels honor
    // defaultOpen; subsequent registrations preserve the user's current open state.
    void RegisterPanel(std::string name,
                       UIPanelCallback callback,
                       bool isClosable = true,
                       int flags = 0,
                       bool defaultOpen = true);

    // Explicitly re-opens a previously registered panel from menus/tools.
    void OpenPanel(const std::string& name);

    void RemovePanel(const std::string& name);

    void RegisterMainMenuBar(std::string name, UIMenuCallback callback);

    // Registers a draw callback executed every ImGui frame after all panel windows.
    // The callback is responsible for issuing any immediate-mode ImGui/ImGuizmo draws it needs.
    void RegisterOverlay(std::string name, UIOverlayCallback callback);

    // Unregisters a previously added overlay callback.
    void RemoveOverlay(const std::string& name);

    // Returns true when a GUI frame has been started (BeginFrame) but not yet
    // completed (Render or EndFrame).  Used by the renderer to issue cleanup
    // (EndFrame) when swapchain acquire fails after GUI generation ran.
    [[nodiscard]] bool IsFrameActive();

    // Returns true if ImGui is using the mouse (hovering a window, dragging a slider)
    [[nodiscard]] bool WantCaptureMouse();

    // Returns true if ImGui is using the keyboard (typing in a text box)
    [[nodiscard]] bool WantCaptureKeyboard();

    bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f);

    // Registers a sampled image for use with ImGui::Image(). Returns an ImTextureID that remains valid
    // until explicitly removed via RemoveTexture().
    //
    // Note: The image view must remain alive while the texture is registered.
    [[nodiscard]] void* AddTexture(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);

    // Unregisters a previously added ImGui texture.
    void RemoveTexture(void* textureId);
}