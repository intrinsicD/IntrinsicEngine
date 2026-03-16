module;
#include <cstring>
#include <limits>
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
        // Each buffer holds 8 bytes: EntityID (uint32) + PrimitiveID (uint32).
        m_PickReadbackBuffers.resize(config.MaxFramesInFlight);
        m_PickReadbackRequestFrame.resize(config.MaxFramesInFlight, 0);

        for (uint32_t i = 0; i < config.MaxFramesInFlight; ++i)
        {
            m_PickReadbackBuffers[i] = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                2 * sizeof(uint32_t), // Two pixels: EntityID + PrimitiveID
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

        // Reset the readback slot up front so "no PrimitiveID copied" cannot be
        // misinterpreted as a valid zero-based primitive index.
        if (frameIndex < m_PickReadbackBuffers.size())
        {
            if (RHI::VulkanBuffer* buf = m_PickReadbackBuffers[frameIndex].get())
            {
                const uint32_t clear[2] = {0u, std::numeric_limits<uint32_t>::max()};
                buf->Write(clear, sizeof(clear), 0);
            }
        }

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
                    uint32_t readback[2] = {0, 0};
                    buf->Read<uint32_t>(readback, 2);

                    // Entity ID 0 is background/nothing. PrimitiveID uses
                    // UINT_MAX as the explicit invalid sentinel so primitive 0
                    // remains selectable.
                    m_LastPickResult.HasHit = (readback[0] != 0);
                    m_LastPickResult.EntityID = readback[0];
                    m_LastPickResult.PrimitiveID = readback[1];

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
