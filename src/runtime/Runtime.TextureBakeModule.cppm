module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.TextureBakeModule;

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    export enum class PropertyTextureBakeStatus : std::uint8_t
    {
        Success,
        Scheduled,
        NonOperationalBackend,
        MissingScene,
        MissingAssetService,
        StaleEntity,
        NonMeshSource,
        InvalidResolution,
        InvalidPadding,
        InvalidRange,
        MissingProperty,
        UnsupportedPropertyType,
        MismatchedPropertyCount,
        UnsupportedSourceDomain,
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

    export enum class PropertyTextureBakeExecutionMode : std::uint8_t
    {
        PropertyRasterGpu,
    };

    export enum class PropertyTextureBakeStorage : std::uint8_t
    {
        Auto,
        RawFloat,
        EncodedRgba,
    };

    export enum class PropertyTextureBakeEncoding : std::uint8_t
    {
        Auto,
        ScalarColormap,
        LinearScalar,
        LabelPalette,
        Vector2,
        Vector3,
        Normal,
        RgbaColor,
    };

    export enum class PropertyTextureBakeRangePolicy : std::uint8_t
    {
        AutoFinite,
        Manual,
    };

    export enum class PropertyTextureBakeOutputState : std::uint8_t
    {
        Pending,
        Ready,
        Failed,
    };

    export struct PropertyTextureBakeRepresentation
    {
        PropertyTextureBakeStorage Storage{
            PropertyTextureBakeStorage::RawFloat};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Auto};
    };

    export [[nodiscard]] PropertyTextureBakeRepresentation
        ResolvePropertyTextureBakeRepresentation(
            Geometry::PropertyValueKind valueKind,
            PropertyTextureBakeStorage requestedStorage,
            PropertyTextureBakeEncoding requestedEncoding) noexcept;

    export [[nodiscard]] bool IsPropertyTextureBakeRepresentationCompatible(
        Geometry::PropertyValueKind valueKind,
        PropertyTextureBakeStorage storage,
        PropertyTextureBakeEncoding encoding) noexcept;

    export struct PropertyTextureBakeRequest
    {
        WorldHandle World{DefaultWorldHandle};
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Source{};
        GeometryPropertyRef Texcoords{
            .Domain = GeometryElementDomain::MeshVertex,
            .Name = "v:texcoord",
            .ValueKind = Geometry::PropertyValueKind::Vec2,
        };
        std::uint64_t ExpectedSourceGeneration{0u};
        std::uint64_t ExpectedPropertyGeneration{0u};
        PropertyTextureBakeStorage Storage{
            PropertyTextureBakeStorage::Auto};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Auto};
        PropertyTextureBakeRangePolicy RangePolicy{
            PropertyTextureBakeRangePolicy::AutoFinite};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        Graphics::Colormap::Type EncodingColormap{
            Graphics::Colormap::Type::Viridis};
        std::uint32_t Width{64u};
        std::uint32_t Height{64u};
        std::uint32_t PaddingTexels{0u};
        std::string OutputName{};
        Assets::AssetId ExistingGeneratedTexture{};
    };

    export struct PropertyTextureBakeResult
    {
        PropertyTextureBakeStatus Status{
            PropertyTextureBakeStatus::Success};
        PropertyTextureBakeExecutionMode ExecutionMode{
            PropertyTextureBakeExecutionMode::PropertyRasterGpu};
        PropertyTextureBakeOutputState State{
            PropertyTextureBakeOutputState::Pending};
        Assets::AssetId GeneratedTexture{};
        JobToken Job{};
        std::uint64_t Generation{0u};
        std::uint64_t SourceGeneration{0u};
        std::string GeneratedAssetPath{};
        std::string OutputName{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == PropertyTextureBakeStatus::Success ||
                   Status == PropertyTextureBakeStatus::Scheduled;
        }
    };

    export struct PropertyTextureBakeRecord
    {
        std::string OutputName{};
        GeometryPropertyRef Source{};
        GeometryPropertyRef Texcoords{};
        PropertyTextureBakeStorage Storage{
            PropertyTextureBakeStorage::Auto};
        PropertyTextureBakeEncoding Encoding{
            PropertyTextureBakeEncoding::Auto};
        Graphics::Colormap::Type EncodingColormap{
            Graphics::Colormap::Type::Viridis};
        Assets::AssetId Texture{};
        std::size_t ExpectedElementCount{0u};
        std::uint64_t SourceGeneration{0u};
        std::uint64_t PropertyGeneration{0u};
        float RangeMin{0.0f};
        float RangeMax{1.0f};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t PaddingTexels{0u};
        std::uint64_t Generation{0u};
        PropertyTextureBakeOutputState State{
            PropertyTextureBakeOutputState::Pending};
        std::string Diagnostic{};
    };

    export struct PropertyTextureBakeOutputs
    {
        std::vector<PropertyTextureBakeRecord> Records{};
        std::uint64_t Generation{1u};
    };

    export [[nodiscard]] const char* DebugNameForPropertyTextureBakeStatus(
        PropertyTextureBakeStatus status) noexcept;

    // Transitional caller-owned binding vocabulary. It is deliberately
    // separate from PropertyTextureBakeRequest/Result and is removed in
    // RUNTIME-191 Slice C once every caller processes generic output records.
    export enum class PropertyTextureNormalSpace : std::uint8_t
    {
        Object,
        World,
    };

    export struct TextureBakeConsumerBinding
    {
        std::string PresentationKey{};
        GeometryPresentationSlotSemantic Semantic{
            GeometryPresentationSlotSemantic::Albedo};
        Graphics::Colormap::Type Colormap{
            Graphics::Colormap::Type::Viridis};
        PropertyTextureNormalSpace NormalSpace{
            PropertyTextureNormalSpace::Object};
    };

    export [[nodiscard]] PropertyTextureBakeRepresentation
        ResolvePropertyTextureBakeRepresentation(
            Geometry::PropertyValueKind valueKind,
            PropertyTextureBakeStorage requestedStorage,
            PropertyTextureBakeEncoding requestedEncoding,
            std::span<const TextureBakeConsumerBinding> consumers) noexcept;

    export [[nodiscard]] bool IsPropertyTextureBakeConsumerCompatible(
        const TextureBakeConsumerBinding& consumer,
        Geometry::PropertyValueKind valueKind,
        PropertyTextureBakeStorage storage,
        PropertyTextureBakeEncoding encoding) noexcept;

    export struct TextureBakeBindingChanged
    {
        WorldHandle World{};
        std::uint64_t BindingEpoch{0u};
    };

    export struct TextureBakeModuleStats
    {
        std::uint64_t BakeRequests{0u};
        std::uint64_t BakeRequestsAccepted{0u};
        std::uint64_t BakeRequestsRejected{0u};
        std::uint64_t BindingChanges{0u};
    };

    export enum class TextureBakeMutationStatus : std::uint8_t
    {
        Success,
        MissingScene,
        StaleEntity,
        MissingTexture,
        DuplicateName,
        InvalidName,
        IncompatibleConsumer,
        AssetDestroyFailed,
        CommandFailed,
    };

    export struct TextureBakeMutationResult
    {
        TextureBakeMutationStatus Status{
            TextureBakeMutationStatus::Success};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == TextureBakeMutationStatus::Success;
        }
    };

    export struct TextureBakeConsumerUpdateRequest
    {
        std::uint32_t StableEntityId{0u};
        std::string OutputName{};
        std::vector<TextureBakeConsumerBinding> Consumers{};
    };

    export struct TextureBakeConsumerSnapshot
    {
        std::string OutputName{};
        std::vector<TextureBakeConsumerBinding> Consumers{};
    };

    export struct TextureBakeSnapshot
    {
        std::vector<PropertyTextureBakeRecord> Textures{};
        std::vector<TextureBakeConsumerSnapshot> ConsumerBindings{};
        bool GpuOperational{false};
        std::string Diagnostic{};
    };

    export class TextureBakeService
    {
    public:
        TextureBakeService();
        ~TextureBakeService();

        TextureBakeService(const TextureBakeService&) = delete;
        TextureBakeService& operator=(const TextureBakeService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] PropertyTextureBakeResult Bake(
            const PropertyTextureBakeRequest& request);
        [[nodiscard]] TextureBakeModuleStats Stats() const noexcept;
        [[nodiscard]] TextureBakeSnapshot Snapshot(
            std::uint32_t stableEntityId) const;
        [[nodiscard]] TextureBakeMutationResult Rename(
            std::uint32_t stableEntityId,
            std::string_view currentName,
            std::string_view newName);
        [[nodiscard]] TextureBakeMutationResult Remove(
            std::uint32_t stableEntityId,
            std::string_view outputName);
        [[nodiscard]] TextureBakeMutationResult SetConsumers(
            const TextureBakeConsumerUpdateRequest& request);

    private:
        friend class TextureBakeModule;

        void Bind(
            ECS::Scene::Registry* scene,
            WorldHandle world,
            std::uint64_t bindingEpoch,
            Assets::AssetService* assets,
            EditorCommandHistory* history,
            JobService* jobs,
            RHI::IDevice* device,
            Graphics::GpuAssetCache* gpuAssets,
            Graphics::IRenderer* renderer,
            RenderExtractionCache* extraction,
            TextureBakeModuleStats* stats) noexcept;
        void SetTarget(WorldHandle world,
                       std::uint64_t bindingEpoch,
                       ECS::Scene::Registry* scene) noexcept;
        void SetCommandHistory(EditorCommandHistory* history) noexcept;
        void DetachTargets(WorldHandle world,
                           std::uint64_t bindingEpoch,
                           bool destroyGeneratedAssets) noexcept;
        void DestroySceneAssets(ECS::Scene::Registry& scene) noexcept;
        void RestoreReadyBindings() noexcept;
        [[nodiscard]] GpuQueueParticipantHandle
            RegisterGpuQueueParticipant(JobService& jobs);
        void Unbind() noexcept;

        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };

    export class TextureBakeModule final : public IRuntimeModule
    {
    public:
        TextureBakeModule();
        ~TextureBakeModule() override;

        TextureBakeModule(const TextureBakeModule&) = delete;
        TextureBakeModule& operator=(const TextureBakeModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
