module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Runtime.SelectedMeshTextureBake;

import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.RHI.Device;
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
        PropertyRasterGpu,
    };

    enum class SelectedMeshTextureBakeStorage : std::uint8_t
    {
        Auto,
        RawFloat,
        EncodedRgba,
    };

    enum class BakedPropertyTextureState : std::uint8_t
    {
        Pending,
        Ready,
        Failed,
    };

    enum class BakedPropertyNormalSpace : std::uint8_t
    {
        Object,
        World,
    };

    struct BakedPropertyTextureConsumer
    {
        std::string PresentationKey{};
        ProgressiveSlotSemantic Semantic{ProgressiveSlotSemantic::Albedo};
        Graphics::Colormap::Type Colormap{
            Graphics::Colormap::Type::Viridis};
    };

    struct BakedPropertyTextureRecord
    {
        std::string OutputName{};
        ProgressiveGeometryDomain SourceDomain{
            ProgressiveGeometryDomain::MeshVertex};
        std::string SourcePropertyName{};
        ProgressivePropertyValueKind ValueKind{
            ProgressivePropertyValueKind::Unknown};
        std::string TexcoordPropertyName{"v:texcoord"};
        SelectedMeshTextureBakeStorage Storage{
            SelectedMeshTextureBakeStorage::Auto};
        MeshAttributeTextureBakeEncoder Encoder{
            MeshAttributeTextureBakeEncoder::Auto};
        Graphics::Colormap::Type EncodingColormap{
            Graphics::Colormap::Type::Viridis};
        BakedPropertyNormalSpace NormalSpace{
            BakedPropertyNormalSpace::Object};
        Assets::AssetId Texture{};
        std::vector<BakedPropertyTextureConsumer> Consumers{};
        std::size_t ExpectedElementCount{0u};
        std::uint64_t SourceGeneration{0u};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint64_t Generation{0u};
        BakedPropertyTextureState State{BakedPropertyTextureState::Pending};
        std::string Diagnostic{};
    };

    struct BakedPropertyTextures
    {
        std::vector<BakedPropertyTextureRecord> Records{};
        std::uint64_t Generation{1u};
    };

    struct BakedPropertyTextureRepresentation
    {
        SelectedMeshTextureBakeStorage Storage{
            SelectedMeshTextureBakeStorage::RawFloat};
        MeshAttributeTextureBakeEncoder Encoder{
            MeshAttributeTextureBakeEncoder::Auto};
    };

    [[nodiscard]] BakedPropertyTextureRepresentation
        ResolveBakedPropertyTextureRepresentation(
            ProgressivePropertyValueKind valueKind,
            SelectedMeshTextureBakeStorage requestedStorage,
            MeshAttributeTextureBakeEncoder requestedEncoder,
            std::span<const BakedPropertyTextureConsumer> consumers) noexcept;

    [[nodiscard]] bool IsBakedPropertyTextureRepresentationCompatible(
        ProgressivePropertyValueKind valueKind,
        SelectedMeshTextureBakeStorage storage,
        MeshAttributeTextureBakeEncoder encoder) noexcept;

    [[nodiscard]] bool IsBakedPropertyTextureConsumerCompatible(
        const BakedPropertyTextureConsumer& consumer,
        ProgressivePropertyValueKind valueKind,
        SelectedMeshTextureBakeStorage storage,
        MeshAttributeTextureBakeEncoder encoder) noexcept;

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
        std::string OutputName{};
        SelectedMeshTextureBakeStorage Storage{
            SelectedMeshTextureBakeStorage::Auto};
        Graphics::Colormap::Type EncodingColormap{
            Graphics::Colormap::Type::Viridis};
        BakedPropertyNormalSpace NormalSpace{
            BakedPropertyNormalSpace::Object};
        std::vector<BakedPropertyTextureConsumer> Consumers{};
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
        std::string OutputName{};
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
        std::uint64_t BindingEpoch{0u};
        Assets::AssetService* AssetService{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        DerivedJobRegistry* DerivedJobs{nullptr};
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{nullptr};
        const RHI::IDevice* ObjectSpaceNormalBakeDevice{nullptr};
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
        std::string OutputName{};
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
