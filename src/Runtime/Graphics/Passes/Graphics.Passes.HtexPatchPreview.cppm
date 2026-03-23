module;

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>

export module Graphics.Passes.HtexPatchPreview;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.Components;

import Core.Hash;

import Geometry.HalfedgeMesh;
import Geometry.HtexPatch;
import Geometry.Properties;

import RHI.Buffer;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;

import ECS;

using namespace Core::Hash;

export namespace Graphics::Passes
{
    struct HtexPatchPreviewDebugState
    {
        bool Initialized = false;
        bool HasMesh = false;
        bool PreviewImageReady = false;
        bool UsedKMeansColors = false;
        bool AtlasRebuiltThisFrame = false;
        bool AtlasUploadQueuedThisFrame = false;
        uint32_t LastMeshEntity = 0;
        uint32_t LastPatchCount = 0;
        uint32_t LastAtlasWidth = 0;
        uint32_t LastAtlasHeight = 0;
    };

    struct PreviewKMeansData
    {
        Geometry::Property<uint32_t> Labels{};
        Geometry::Property<glm::vec4> Colors{};
        std::span<const glm::vec3> Centroids{};

        [[nodiscard]] bool HasAny() const noexcept
        {
            return static_cast<bool>(Labels) || static_cast<bool>(Colors);
        }

        [[nodiscard]] bool HasCentroidField() const noexcept
        {
            return static_cast<bool>(Labels) && !Centroids.empty();
        }
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
            RGResourceHandle Target{};
        };

        struct FinalizePassData
        {
            RGResourceHandle Target{};
        };

        RHI::VulkanDevice* m_Device = nullptr;
        std::unique_ptr<RHI::VulkanImage> m_PreviewImages[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_StagingBuffers[FRAMES];
        std::array<uint64_t, FRAMES> m_UploadedAtlasRevision{};
        uint32_t m_StagingCapacity = 0;
        HtexPatchPreviewDebugState m_DebugState{};

        struct CachedPreviewAtlas
        {
            std::vector<glm::vec4> Pixels{};
            uint64_t Signature = 0;
            uint64_t Revision = 0;
            uint32_t Width = 1;
            uint32_t Height = 1;
            bool Built = false;
            bool Valid = false;
        };

        CachedPreviewAtlas m_CachedAtlas{};

        struct PendingPreviewBake;
        std::shared_ptr<PendingPreviewBake> m_PendingBake{};

        [[nodiscard]] static bool BuildPreviewAtlas(const Geometry::Halfedge::Mesh& mesh,
                                                    const PreviewKMeansData& kmeansData,
                                                    std::span<const Geometry::HtexPatch::HalfedgePatchMeta> patches,
                                                     std::vector<glm::vec4>& outPixels,
                                                     uint32_t& outWidth,
                                                     uint32_t& outHeight);

        [[nodiscard]] static bool IsInterestingMeshEntity(const entt::registry& reg, entt::entity entity);
        [[nodiscard]] static std::optional<entt::entity> FindSourceMeshEntity(const entt::registry& reg);
        [[nodiscard]] static uint64_t ComputePreviewAtlasSignature(
            const Geometry::Halfedge::Mesh& mesh,
            const PreviewKMeansData& kmeansData,
            std::span<const Geometry::HtexPatch::HalfedgePatchMeta> patches) noexcept;
    };
}
