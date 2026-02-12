module;
#include <cstring>
#include <memory>
#include <optional>
#include <vector>
#include "RHI.Vulkan.hpp"

module Graphics:Interaction.Impl;

import :Interaction;
import RHI;
import Core.Hash;

using namespace Core::Hash;

namespace Graphics
{
    InteractionSystem::InteractionSystem(const Config& config, std::shared_ptr<RHI::VulkanDevice> device)
        : m_Device(std::move(device))
    {
        // Allocate readback buffers (one per frame in flight).
        // Each buffer holds 4 bytes (RGBA8 or UINT32) for the picked entity ID.
        m_PickReadbackBuffers.resize(config.MaxFramesInFlight);
        m_PickReadbackRequestFrame.resize(config.MaxFramesInFlight, 0);

        for (uint32_t i = 0; i < config.MaxFramesInFlight; ++i)
        {
            m_PickReadbackBuffers[i] = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                sizeof(uint32_t), // One pixel, 4 bytes
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_TO_CPU // Mapped for CPU readback
            );
        }
    }

    InteractionSystem::~InteractionSystem()
    {
        m_PickReadbackBuffers.clear();
        m_PickReadbackRequestFrame.clear();
    }

    void InteractionSystem::RequestPick(uint32_t x, uint32_t y, uint32_t frameIndex, uint64_t globalFrame)
    {
        if (m_PendingPick.Pending)
        {
            // Already have a pending pick request this frame.
            return;
        }

        m_PendingPick.Pending = true;
        m_PendingPick.X = x;
        m_PendingPick.Y = y;
        m_PendingPick.RequestFrameIndex = frameIndex;
        m_PendingPick.RequestGlobalFrame = globalFrame;

        // Mark this frame slot as "waiting for readback".
        m_PickReadbackRequestFrame[frameIndex] = globalFrame;
    }

    void InteractionSystem::ProcessReadbacks(uint64_t completedGlobalFrame)
    {
        // Check all frame slots to see if any have completed on the GPU.
        for (uint32_t i = 0; i < m_PickReadbackRequestFrame.size(); ++i)
        {
            const uint64_t reqFrame = m_PickReadbackRequestFrame[i];
            
            // If there is a request for this slot AND the GPU has finished that frame...
            if (reqFrame != 0 && reqFrame <= completedGlobalFrame)
            {
                // Readback is ready!
                RHI::VulkanBuffer* buf = m_PickReadbackBuffers[i].get();
                if (buf)
                {
                    uint32_t pixelValue = 0;
                    buf->Read<uint32_t>(&pixelValue, 1);

                    // Entity ID 0 is usually "background/nothing".
                    m_LastPickResult.HasHit = (pixelValue != 0);
                    m_LastPickResult.EntityID = pixelValue;

                    // Queue for consumption
                    m_HasPendingConsumedResult = true;
                    m_PendingConsumedResult = m_LastPickResult;
                }

                // Clear the request.
                m_PickReadbackRequestFrame[i] = 0;
                
                // If this was the *current* pending pick request, clear that too.
                if (m_PendingPick.RequestGlobalFrame == reqFrame)
                {
                    m_PendingPick.Pending = false;
                }
            }
        }
    }

    RHI::VulkanBuffer* InteractionSystem::GetReadbackBuffer(uint32_t frameIndex) const
    {
        if (frameIndex < m_PickReadbackBuffers.size())
        {
            return m_PickReadbackBuffers[frameIndex].get();
        }
        return nullptr;
    }

    std::optional<InteractionSystem::PickResultGpu> InteractionSystem::TryConsumePickResult()
    {
        if (m_HasPendingConsumedResult)
        {
            m_HasPendingConsumedResult = false;
            return m_PendingConsumedResult;
        }
        return std::nullopt;
    }
}
