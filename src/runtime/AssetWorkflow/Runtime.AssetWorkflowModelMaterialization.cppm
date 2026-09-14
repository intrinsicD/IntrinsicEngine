// Declares workflow-owned model materialization and copied import diagnostics.
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

export module Extrinsic.Runtime.AssetWorkflowModelMaterialization;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Runtime.AssetWorkflowTextureResidency;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;

// IRenderer is globally attached in its owning module; only a reference is used here.
extern "C++" { namespace Extrinsic::Graphics { class IRenderer; } }

export namespace Extrinsic::Runtime
{
    struct AssetWorkflowModelMaterializationOptions
    {
        AssetWorkflowTextureResidencyOptions TextureOptions{};
        bool RequestEmbeddedTextureUploads{true};
        bool ResolveMaterialTextureBindings{true};
        bool GenerateMissingNormalTextures{true};
        bool GenerateMissingAlbedoTextures{true};
        bool ProgressiveRawGeometryFirst{false};
        // Optional execution service for the progressive enrichment chain
        // (UV atlas / vertex normals, then the dependent texture bakes).
        // Null in every non-test composition today; when null the enrichment
        // jobs are simply not queued.
        JobService* ProgressiveJobs{nullptr};
        WorldHandle World{DefaultWorldHandle};
        std::uint64_t BindingEpoch{0u};
        std::function<bool()> BindingValid{};
        TextureBakeService* TextureBake{nullptr};
        std::string GeneratedNormalPropertyName{"v:normal"};
        std::string GeneratedAlbedoPropertyName{"v:color"};
        // Zero resolves each automatic bake to the materialized UV atlas
        // extent. A fixed nonzero extent remains available to callers that
        // deliberately request a resample.
        std::uint32_t GeneratedTextureWidth{0u};
        std::uint32_t GeneratedTextureHeight{0u};
        std::uint32_t GeneratedTexturePaddingTexels{2u};
    };

    struct AssetWorkflowModelMaterializationDiagnostics
    {
        std::uint64_t ReadyEventsObserved{0};
        std::uint64_t ModelSceneReadyEvents{0};
        std::uint64_t ModelSceneMaterializeRequests{0};
        std::uint64_t ModelSceneMaterializeSuccesses{0};
        std::uint64_t ModelSceneMaterializeFailures{0};
        std::uint64_t NodeEntitiesCreated{0};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t EmbeddedTextureUploadRequests{0};
        std::uint64_t EmbeddedTextureUploadDeferrals{0};
        std::uint64_t EmbeddedTextureUploadFailures{0};
        std::uint64_t GeneratedTextureBakeFailures{0};
        std::uint64_t GeneratedNormalTextureBakeFailures{0};
        std::uint64_t GeneratedAlbedoTextureBakeFailures{0};
        std::uint64_t ProgressiveRawPrimitiveEntitiesPublished{0};
        std::uint64_t GeometryPresentationRecipesCreated{0};
        std::uint64_t ProgressiveUvAtlasJobsQueued{0};
        std::uint64_t ProgressiveNormalJobsQueued{0};
        std::uint64_t ProgressiveTextureBakeJobsQueued{0};
        std::uint64_t AuthoredUvPrimitives{0};
        std::uint64_t GeneratedUvAtlasPrimitives{0};
        std::uint64_t InvalidAuthoredUvPrimitives{0};
        std::uint64_t UvAtlasFailures{0};
        // Vertices an indexed GPU upload duplicates to carry UV
        // seams. The materialized mesh itself keeps its source topology.
        std::uint64_t UvAtlasGpuSplitVertices{0};
        std::uint64_t LastUvAtlasChartCount{0};
        std::uint64_t LastUvAtlasWidth{0};
        std::uint64_t LastUvAtlasHeight{0};
        std::uint64_t MaterialInstancesCreated{0};
        std::uint64_t DefaultLitMaterialInstancesCreated{0};
        std::uint64_t MaterialLessPrimitivesAssignedDefaultLit{0};
        std::uint64_t MaterialTextureBindingsResolved{0};
        std::uint64_t MaterialTextureBindingFailures{0};
        std::uint64_t MaterialTextureBindingUploadDeferrals{0};
        std::uint64_t MaterialTextureBindingReloadInvalidations{0};
        std::uint64_t MaterialTextureBindingReresolveRequests{0};
        std::uint64_t MaterialTextureBindingReresolveSuccesses{0};
        std::uint64_t MaterialTextureBindingReresolveFailures{0};
        std::uint64_t NonModelSceneReadyEvents{0};
        Assets::AssetId LastFailedAsset{};
        Core::ErrorCode LastError{Core::ErrorCode::Success};
    };

    struct AssetWorkflowModelMaterialRecord
    {
        std::uint32_t MaterialIndex{Assets::kInvalidAssetModelIndex};
        Graphics::MaterialTextureAssetBindings TextureBindings{};
        std::uint32_t MaterialSlot{Graphics::kDefaultMaterialSlotIndex};
        bool HasMaterialSlot{false};
        bool TextureBindingsResolved{false};
    };

    struct AssetWorkflowModelPrimitiveRecord
    {
        ECS::EntityHandle Entity{};
        std::uint32_t NodeIndex{Assets::kInvalidAssetModelIndex};
        std::uint32_t PrimitiveIndex{Assets::kInvalidAssetModelIndex};
        std::uint32_t GeometryPayloadIndex{Assets::kInvalidAssetModelIndex};
        std::uint32_t MaterialIndex{Assets::kInvalidAssetModelIndex};
        std::uint32_t MaterialSlot{Graphics::kDefaultMaterialSlotIndex};
        bool HasMaterialSlot{false};
    };

    struct AssetWorkflowModelNodeRecord
    {
        ECS::EntityHandle Entity{};
        std::uint32_t NodeIndex{Assets::kInvalidAssetModelIndex};
    };

    struct AssetWorkflowModelMaterializationRecord
    {
        Assets::AssetId ModelAsset{};
        std::vector<Assets::AssetId> EmbeddedTextureAssets{};
        std::vector<AssetWorkflowModelMaterialRecord> Materials{};
        std::vector<AssetWorkflowModelNodeRecord> Nodes{};
        std::vector<AssetWorkflowModelPrimitiveRecord> Primitives{};
    };

    class AssetWorkflowModelMaterializer
    {
    public:
        AssetWorkflowModelMaterializer(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            ECS::Scene::Registry& scene,
            Graphics::IRenderer& renderer,
            AssetWorkflowModelMaterializationOptions options = {});
        ~AssetWorkflowModelMaterializer();

        AssetWorkflowModelMaterializer(const AssetWorkflowModelMaterializer&) = delete;
        AssetWorkflowModelMaterializer& operator=(const AssetWorkflowModelMaterializer&) = delete;
        AssetWorkflowModelMaterializer(AssetWorkflowModelMaterializer&&) = delete;
        AssetWorkflowModelMaterializer& operator=(AssetWorkflowModelMaterializer&&) = delete;

        [[nodiscard]] bool IsSubscribed() const noexcept;
        [[nodiscard]] AssetWorkflowModelMaterializationDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] const AssetWorkflowModelMaterializationRecord* FindRecord(
            Assets::AssetId modelAsset) const noexcept;

        [[nodiscard]] Core::Result MaterializeReadyModelScene(Assets::AssetId modelAsset);
        [[nodiscard]] Core::Expected<std::uint64_t> ResolvePendingMaterialTextureBindings();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
