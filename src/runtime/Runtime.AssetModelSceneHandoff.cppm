module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.AssetModelSceneHandoff;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.WorldHandle;

export namespace Extrinsic::Runtime
{
    struct AssetModelSceneHandoffOptions
    {
        AssetModelTextureHandoffOptions TextureOptions{};
        bool RequestEmbeddedTextureUploads{true};
        bool RequestGeneratedTextureUploads{true};
        bool ResolveMaterialTextureBindings{true};
        bool GenerateMissingNormalTextures{true};
        bool GenerateMissingAlbedoTextures{true};
        bool ProgressiveRawGeometryFirst{false};
        DerivedJobRegistry* ProgressiveJobs{nullptr};
        WorldHandle World{DefaultWorldHandle};
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{nullptr};
        bool ObjectSpaceNormalBakeGraphicsBackendOperational{false};
        std::string GeneratedNormalPropertyName{"v:normal"};
        std::string GeneratedAlbedoPropertyName{"v:color"};
        std::uint32_t GeneratedTextureWidth{64u};
        std::uint32_t GeneratedTextureHeight{64u};
    };

    struct AssetModelSceneHandoffDiagnostics
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
        std::uint64_t GeneratedTextureAssetsCreated{0};
        std::uint64_t GeneratedTextureUploadRequests{0};
        std::uint64_t GeneratedTextureUploadDeferrals{0};
        std::uint64_t GeneratedTextureUploadFailures{0};
        std::uint64_t GeneratedTextureBakeFailures{0};
        std::uint64_t GeneratedNormalTextureBakeFailures{0};
        std::uint64_t GeneratedAlbedoTextureBakeFailures{0};
        std::uint64_t ProgressiveRawPrimitiveEntitiesPublished{0};
        std::uint64_t ProgressivePresentationBindingsCreated{0};
        std::uint64_t ProgressiveUvAtlasJobsQueued{0};
        std::uint64_t ProgressiveNormalJobsQueued{0};
        std::uint64_t ProgressiveTextureBakeJobsQueued{0};
        std::uint64_t AuthoredUvPrimitives{0};
        std::uint64_t GeneratedUvAtlasPrimitives{0};
        std::uint64_t InvalidAuthoredUvPrimitives{0};
        std::uint64_t UvAtlasFailures{0};
        std::uint64_t UvAtlasSeamSplitVertices{0};
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

    struct AssetModelSceneMaterialRecord
    {
        std::uint32_t MaterialIndex{Assets::kInvalidAssetModelIndex};
        Graphics::MaterialTextureAssetBindings TextureBindings{};
        std::uint32_t MaterialSlot{Graphics::kDefaultMaterialSlotIndex};
        bool HasMaterialSlot{false};
        bool TextureBindingsResolved{false};
    };

    struct AssetModelScenePrimitiveRecord
    {
        ECS::EntityHandle Entity{};
        std::uint32_t NodeIndex{Assets::kInvalidAssetModelIndex};
        std::uint32_t PrimitiveIndex{Assets::kInvalidAssetModelIndex};
        std::uint32_t GeometryPayloadIndex{Assets::kInvalidAssetModelIndex};
        std::uint32_t MaterialIndex{Assets::kInvalidAssetModelIndex};
        std::uint32_t MaterialSlot{Graphics::kDefaultMaterialSlotIndex};
        bool HasMaterialSlot{false};
    };

    struct AssetModelSceneNodeRecord
    {
        ECS::EntityHandle Entity{};
        std::uint32_t NodeIndex{Assets::kInvalidAssetModelIndex};
    };

    struct AssetModelSceneHandoffRecord
    {
        Assets::AssetId ModelAsset{};
        std::vector<Assets::AssetId> EmbeddedTextureAssets{};
        std::vector<Assets::AssetId> GeneratedTextureAssets{};
        std::vector<AssetModelSceneMaterialRecord> Materials{};
        std::vector<AssetModelSceneNodeRecord> Nodes{};
        std::vector<AssetModelScenePrimitiveRecord> Primitives{};
    };

    struct AssetModelSceneHandoffState
    {
        AssetModelSceneHandoffRecord Record{};
        std::vector<Graphics::MaterialSystem::MaterialLease> MaterialLeases{};

        // Lazily-created neutral lit StandardPBR material bound to imported
        // primitives that carry no authored material, so they shade instead of
        // falling back to the unlit DefaultDebugSurface (slot 0). Slot 0 stays
        // reserved for genuine missing/invalid bindings (GRAPHICS-031).
        Graphics::MaterialSystem::MaterialLease DefaultLitMaterialLease{};
        std::uint32_t DefaultLitMaterialSlot{Graphics::kDefaultMaterialSlotIndex};
        bool HasDefaultLitMaterial{false};
    };

    [[nodiscard]] std::string BuildEmbeddedTextureAssetPath(
        std::string_view modelPath,
        std::uint32_t imageIndex,
        const Assets::AssetTexture2DPayload& image);

    [[nodiscard]] Core::Expected<Assets::AssetId> LoadEmbeddedTextureAsset(
        Assets::AssetService& service,
        std::string_view modelPath,
        std::uint32_t imageIndex,
        const Assets::AssetTexture2DPayload& image);

    [[nodiscard]] std::string BuildGeneratedTextureAssetPath(
        std::string_view modelPath,
        std::uint32_t materialIndex,
        std::string_view semantic,
        std::string_view propertyName);

    [[nodiscard]] Core::Expected<Assets::AssetId> LoadGeneratedTextureAsset(
        Assets::AssetService& service,
        std::string_view assetPath,
        const Assets::AssetTexture2DPayload& texture);

    [[nodiscard]] Core::Expected<AssetModelSceneHandoffState> MaterializeModelSceneAsset(
        Assets::AssetService& service,
        Graphics::GpuAssetCache& cache,
        ECS::Scene::Registry& scene,
        Graphics::MaterialSystem& materials,
        Assets::AssetId modelAsset,
        const AssetModelSceneHandoffOptions& options = {},
        AssetModelSceneHandoffDiagnostics* diagnostics = nullptr);

    class AssetModelSceneHandoff
    {
    public:
        AssetModelSceneHandoff(
            Assets::AssetService& service,
            Graphics::GpuAssetCache& cache,
            ECS::Scene::Registry& scene,
            Graphics::IRenderer& renderer,
            AssetModelSceneHandoffOptions options = {});
        ~AssetModelSceneHandoff();

        AssetModelSceneHandoff(const AssetModelSceneHandoff&) = delete;
        AssetModelSceneHandoff& operator=(const AssetModelSceneHandoff&) = delete;
        AssetModelSceneHandoff(AssetModelSceneHandoff&&) = delete;
        AssetModelSceneHandoff& operator=(AssetModelSceneHandoff&&) = delete;

        [[nodiscard]] bool IsSubscribed() const noexcept;
        [[nodiscard]] AssetModelSceneHandoffDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] const AssetModelSceneHandoffRecord* FindRecord(
            Assets::AssetId modelAsset) const noexcept;

        [[nodiscard]] Core::Result MaterializeReadyModelScene(Assets::AssetId modelAsset);
        [[nodiscard]] Core::Expected<std::uint64_t> ResolvePendingMaterialTextureBindings();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
