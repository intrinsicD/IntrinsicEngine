module;

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

module Extrinsic.Runtime.Engine;

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

    const RuntimeRenderExtractionStats&
    RenderExtractionService::LastStats() const noexcept
    {
        return m_LastStats;
    }

    void RenderExtractionService::PublishLastStats(
        const RuntimeRenderExtractionStats& stats) noexcept
    {
        m_LastStats = stats;
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

    void RenderExtractionService::ClearMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) noexcept
    {
        m_Cache.ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    std::optional<Graphics::MaterialTextureAssetBindings>
    RenderExtractionService::GetMaterialTextureAssetBindingsForTest(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_Cache.GetMaterialTextureAssetBindings(stableEntityId);
    }

    void RenderExtractionService::SetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId,
        RenderExtractionCache::VisualizationAdapterBinding binding)
    {
        m_Cache.SetVisualizationAdapterBinding(stableEntityId, std::move(binding));
    }

    void RenderExtractionService::ClearVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) noexcept
    {
        m_Cache.ClearVisualizationAdapterBinding(stableEntityId);
    }

    std::optional<RenderExtractionCache::VisualizationAdapterBinding>
    RenderExtractionService::GetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_Cache.GetVisualizationAdapterBinding(stableEntityId);
    }

    std::uint64_t
    RenderExtractionService::GetVisualizationAdapterBindingRevision() const noexcept
    {
        return m_Cache.GetVisualizationAdapterBindingRevision();
    }
}
