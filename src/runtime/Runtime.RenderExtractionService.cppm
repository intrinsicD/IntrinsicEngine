module;

#include <cstdint>
#include <memory>
#include <optional>

export module Extrinsic.Runtime.RenderExtractionService;

import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Renderer;
export import Extrinsic.Runtime.RenderExtraction;
export import Extrinsic.Runtime.RenderWorldPool;

namespace Extrinsic::Runtime
{
    export class RenderExtractionService
    {
    public:
        RenderExtractionService() = default;

        RenderExtractionService(const RenderExtractionService&) = delete;
        RenderExtractionService& operator=(const RenderExtractionService&) = delete;

        void ConfigurePool(bool synchronousExtraction);

        [[nodiscard]] RenderExtractionCache& Cache() noexcept;
        [[nodiscard]] const RenderExtractionCache& Cache() const noexcept;

        [[nodiscard]] RenderWorldPool& Pool() noexcept;
        [[nodiscard]] const RenderWorldPool& Pool() const noexcept;

        [[nodiscard]] const RuntimeRenderExtractionStats& LastStats() const noexcept;
        void PublishLastStats(const RuntimeRenderExtractionStats& stats) noexcept;

        [[nodiscard]] std::uint64_t CurrentFrameIndex() const noexcept;
        [[nodiscard]] std::uint64_t ConsumeFrameIndex() noexcept;

        void ReleaseFrontSlot(std::uint32_t slot) noexcept;
        void Shutdown(Graphics::IRenderer& renderer);

        void ClearMeshPrimitiveViewSettings(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<Graphics::MaterialTextureAssetBindings>
            GetMaterialTextureAssetBindingsForTest(std::uint32_t stableEntityId) const noexcept;

        void SetVisualizationAdapterBinding(
            std::uint32_t stableEntityId,
            RenderExtractionCache::VisualizationAdapterBinding binding);
        void ClearVisualizationAdapterBinding(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<RenderExtractionCache::VisualizationAdapterBinding>
            GetVisualizationAdapterBinding(std::uint32_t stableEntityId) const noexcept;
        [[nodiscard]] std::uint64_t
            GetVisualizationAdapterBindingRevision() const noexcept;

    private:
        RenderExtractionCache m_Cache{};
        std::unique_ptr<RenderWorldPool> m_Pool{};
        RuntimeRenderExtractionStats m_LastStats{};
        std::uint64_t m_FrameIndex{0u};
    };
}
