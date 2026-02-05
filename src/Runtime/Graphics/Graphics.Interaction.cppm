module;
#include <vector>
#include <memory>
#include <optional>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

export module Graphics:Interaction;

import RHI;
import Core;

export namespace Graphics
{
    // ---------------------------------------------------------------------
    // Interaction System
    // ---------------------------------------------------------------------
    // Handles mouse picking (GPU readback), debug selection state, and
    // related interaction logic.
    class InteractionSystem
    {
    public:
        struct Config
        {
            uint32_t MaxFramesInFlight = 2; // Default to double buffering
        };

        explicit InteractionSystem(const Config& config, std::shared_ptr<RHI::VulkanDevice> device);
        ~InteractionSystem();

        // -----------------------------------------------------------------
        // Picking API
        // -----------------------------------------------------------------
        void RequestPick(uint32_t x, uint32_t y, uint32_t frameIndex, uint64_t globalFrame);
        
        struct PickResultGpu
        {
            bool HasHit = false;
            uint32_t EntityID = 0;
        };

        // Call this at the start of the frame to check for completed readbacks.
        void ProcessReadbacks(uint64_t completedGlobalFrame);

        [[nodiscard]] PickResultGpu GetLastPickResult() const { return m_LastPickResult; }
        [[nodiscard]] std::optional<PickResultGpu> TryConsumePickResult();

        // Returns the buffer to write picking data INTO for the current frame.
        [[nodiscard]] RHI::VulkanBuffer* GetReadbackBuffer(uint32_t frameIndex) const;

        // -----------------------------------------------------------------
        // Debug View / Selection State
        // -----------------------------------------------------------------
        struct DebugViewState
        {
            bool Enabled = false;
            Core::Hash::StringID SelectedResource = Core::Hash::StringID("PickID");
            bool ShowInViewport = false;
            bool DisableCulling = false;
            float DepthNear = 0.1f;
            float DepthFar = 1000.0f;
        };

        [[nodiscard]] const DebugViewState& GetDebugViewState() const { return m_DebugView; }
        [[nodiscard]] DebugViewState& GetDebugViewStateMut() { return m_DebugView; } // Mutable access for UI
        
        void SetDebugViewSelectedResource(Core::Hash::StringID name) { m_DebugView.SelectedResource = name; }
        void SetDebugViewShowInViewport(bool show) { m_DebugView.ShowInViewport = show; }

        // Helper: Check if a specific frame has a pending pick request
        struct PendingPick
        {
            bool Pending = false;
            uint32_t X = 0;
            uint32_t Y = 0;
            uint32_t RequestFrameIndex = 0;
            uint64_t RequestGlobalFrame = 0;
        };
        
        [[nodiscard]] const PendingPick& GetPendingPick() const { return m_PendingPick; }

    private:
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        
        // Picking State
        PendingPick m_PendingPick;
        std::vector<std::unique_ptr<RHI::VulkanBuffer>> m_PickReadbackBuffers;
        // For each frame slot: the global frame number when a pick was recorded.
        std::vector<uint64_t> m_PickReadbackRequestFrame;
        
        PickResultGpu m_LastPickResult{};
        bool m_HasPendingConsumedResult = false;
        PickResultGpu m_PendingConsumedResult{};

        // Debug State
        DebugViewState m_DebugView{};
    };
}
