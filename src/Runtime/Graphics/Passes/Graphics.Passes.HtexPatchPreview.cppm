module;

#include <memory>
#include <optional>
#include <vector>

#include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>

export module Graphics:Passes.HtexPatchPreview;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import Core.Hash;
import Geometry;
import RHI;
import ECS;

export namespace Graphics::Passes
{
    struct HtexPatchPreviewDebugState
    {
        bool Initialized = false;
        bool HasMesh = false;
        bool PreviewImageReady = false;
        bool UsedKMeansColors = false;
        uint32_t LastMeshEntity = 0;
        uint32_t LastPatchCount = 0;
        uint32_t LastAtlasWidth = 0;
        uint32_t LastAtlasHeight = 0;
    };

    class HtexPatchPreviewPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& globalLayout) override;

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;
        void OnResize(uint32_t width, uint32_t height) override;

        [[nodiscard]] const HtexPatchPreviewDebugState& GetDebugState() const { return m_DebugState; }

    private:
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();
        static constexpr Core::Hash::StringID kPreviewName = "HtexPatchPreview"_id;

        struct UploadPassData
        {
            UploadPassData() : Target(RGResourceHandle{}) {}

            RGResourceHandle Target;
        };

        struct FinalizePassData
        {
            FinalizePassData() : Target(RGResourceHandle{}) {}

            RGResourceHandle Target;
        };

        RHI::VulkanDevice* m_Device = nullptr;
        std::unique_ptr<RHI::VulkanImage> m_PreviewImages[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_StagingBuffers[FRAMES];
        uint32_t m_StagingCapacity = 0;
        RGResourceHandle m_LastPreviewHandle{};
        HtexPatchPreviewDebugState m_DebugState{};

        [[nodiscard]] bool BuildPreviewAtlas(const Geometry::Halfedge::Mesh& mesh,
                                             std::vector<glm::vec4>& outPixels,
                                             uint32_t& outWidth,
                                             uint32_t& outHeight) const;

        [[nodiscard]] static glm::vec4 DecodePackedColor(uint32_t packed) noexcept;
        [[nodiscard]] static glm::vec4 TileColorFromPatch(const Geometry::Halfedge::Mesh& mesh,
                                                          const Geometry::HtexPatch::HalfedgePatchMeta& patch) noexcept;
        [[nodiscard]] static bool IsInterestingMeshEntity(const entt::registry& reg, entt::entity entity);
        [[nodiscard]] static std::optional<entt::entity> FindSourceMeshEntity(const entt::registry& reg);
    };
}
