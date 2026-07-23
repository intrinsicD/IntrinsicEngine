module;

#include <cstdint>
#include <memory>

module Extrinsic.Runtime.Engine;

import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderWorldPool;

#include "Runtime.RenderExtractionService.Internal.hpp"

namespace Extrinsic::Runtime
{
    void RenderExtractionService::ConfigurePool(
        const bool synchronousExtraction)
    {
        m_Pool = std::make_unique<RenderWorldPool>(
            synchronousExtraction ? 1u : RenderWorldPool::kDefaultBuffers);
    }

    RenderExtractionCache& RenderExtractionService::Cache() noexcept
    {
        return m_Cache;
    }

    const RenderExtractionCache& RenderExtractionService::Cache() const noexcept
    {
        return m_Cache;
    }

    RenderWorldPool& RenderExtractionService::Pool() noexcept
    {
        return *m_Pool;
    }

    const RenderWorldPool& RenderExtractionService::Pool() const noexcept
    {
        return *m_Pool;
    }

    std::uint64_t RenderExtractionService::CurrentFrameIndex() const noexcept
    {
        return m_FrameIndex;
    }

    std::uint64_t RenderExtractionService::ConsumeFrameIndex() noexcept
    {
        return m_FrameIndex++;
    }

    void RenderExtractionService::ReleaseFrontSlot(
        const std::uint32_t slot) noexcept
    {
        if (slot != RenderWorldPool::kInvalidSlot && m_Pool != nullptr)
        {
            m_Pool->ReleaseFront(slot);
        }
    }

    void RenderExtractionService::Shutdown(Graphics::IRenderer& renderer)
    {
        m_Cache.Shutdown(renderer);
    }

}
