module;

#include <cstdint>
#include <optional>
#include <string>

export module Extrinsic.Runtime.SelectedMeshTextureBake;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.WorldHandle;

export namespace Extrinsic::Runtime
{
    enum class SelectedMeshTextureBakeStatus : std::uint8_t
    {
        Success,
        Scheduled,
        NonOperationalBackend,
        MissingScene,
        MissingAssetService,
        StaleEntity,
        NonMeshSelection,
        MissingProgressiveBindings,
        MissingPresentation,
        MissingSlot,
        UnsupportedSourceDomain,
        UnsupportedTargetSemantic,
        IncompatibleTargetSlot,
        InvalidResolution,
        InvalidRange,
        MissingProperty,
        UnsupportedPropertyType,
        MismatchedPropertyCount,
        MissingTexcoords,
        NonFiniteTexcoord,
        NonFinitePropertyValue,
        DegenerateAllTriangles,
        DegenerateUvTriangles,
        ZeroCoverageBake,
        BakeFailed,
        AssetLoadFailed,
        CommandFailed,
        JobSubmitFailed,
        StaleCompletion,
    };

    enum class SelectedMeshTextureBakeExecutionMode : std::uint8_t
    {
        Synchronous,
        DerivedJob,
        ObjectSpaceNormalBakeQueue,
    };

    struct SelectedMeshTextureBakeRequest
    {
        std::uint32_t StableEntityId{0u};
        ProgressiveGeometryDomain SourceDomain{ProgressiveGeometryDomain::MeshVertex};
        std::string SourcePropertyName{};
        ProgressivePropertyValueKind ExpectedValueKind{ProgressivePropertyValueKind::Any};
        MeshAttributeTextureBakeEncoder Encoder{MeshAttributeTextureBakeEncoder::Auto};
        MeshAttributeTextureBakeValueKind ValueKind{MeshAttributeTextureBakeValueKind::Auto};
        MeshAttributeTextureBakeRangePolicy RangePolicy{MeshAttributeTextureBakeRangePolicy::AutoFinite};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::string TexcoordPropertyName{"v:texcoord"};
        std::uint32_t Width{64u};
        std::uint32_t Height{64u};
        Assets::AssetTextureColorSpace ColorSpace{Assets::AssetTextureColorSpace::Unknown};
        Assets::AssetTexturePixelFormat PixelFormat{Assets::AssetTexturePixelFormat::Unknown};
        ProgressiveRenderLane TargetLane{ProgressiveRenderLane::Surface};
        std::string TargetPresentationKey{};
        ProgressiveSlotSemantic TargetSemantic{ProgressiveSlotSemantic::Albedo};
        ProgressiveGeneratedOutputPolicy GeneratedPolicy{
            ProgressiveGeneratedOutputPolicy::DeterministicChildAsset};
        std::string GeneratedKey{};
        Assets::AssetId ExistingGeneratedTexture{};
        bool BindGeneratedTexture{true};
        bool PreferDerivedJob{false};
        std::uint64_t SourceGeneration{0u};
        std::uint64_t DirtyStamp{0u};
    };

    struct SelectedMeshTextureBakeBuildResult
    {
        SelectedMeshTextureBakeStatus Status{SelectedMeshTextureBakeStatus::Success};
        MeshAttributeTextureBakeRequest BakeRequest{};
        ProgressivePropertyResolution PropertyResolution{};
        std::size_t ExpectedElementCount{0u};
        std::string GeneratedAssetPath{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SelectedMeshTextureBakeStatus::Success;
        }
    };

    struct SelectedMeshTextureBakeContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        WorldHandle World{DefaultWorldHandle};
        Assets::AssetService* AssetService{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        DerivedJobRegistry* DerivedJobs{nullptr};
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{nullptr};
        bool ObjectSpaceNormalBakeGraphicsBackendOperational{false};
    };

    struct SelectedMeshTextureBakeResult
    {
        SelectedMeshTextureBakeStatus Status{SelectedMeshTextureBakeStatus::Success};
        MeshAttributeTextureBakeStatus BakeStatus{MeshAttributeTextureBakeStatus::Success};
        MeshAttributeTextureBakeDiagnostics BakeDiagnostics{};
        Assets::AssetId GeneratedTexture{};
        DerivedJobHandle Job{};
        SelectedMeshTextureBakeExecutionMode ExecutionMode{
            SelectedMeshTextureBakeExecutionMode::Synchronous};
        bool BoundGeneratedTexture{false};
        bool PreviousOutputRetained{false};
        std::uint64_t BindingGeneration{0u};
        std::string GeneratedAssetPath{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SelectedMeshTextureBakeStatus::Success ||
                   Status == SelectedMeshTextureBakeStatus::Scheduled;
        }
    };

    [[nodiscard]] const char* DebugNameForSelectedMeshTextureBakeStatus(
        SelectedMeshTextureBakeStatus status) noexcept;

    [[nodiscard]] SelectedMeshTextureBakeBuildResult BuildSelectedMeshTextureBakeRequest(
        const ECS::Scene::Registry& scene,
        const SelectedMeshTextureBakeRequest& request);

    [[nodiscard]] SelectedMeshTextureBakeResult ApplySelectedMeshTextureBakeCommand(
        const SelectedMeshTextureBakeContext& context,
        const SelectedMeshTextureBakeRequest& request);
}
