module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Extrinsic.Runtime.AssetWorkflowModule;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Core.IOBackend;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    export enum class AssetImportStage : std::uint8_t
    {
        Route,
        Decode,
        CpuMaterialize,
        EcsAuthor,
        Postprocess,
        GpuResidency,
        Complete,
    };

    export struct ImportAuthoringRecipe
    {
        bool AuthorRenderableComponents{true};
        bool AuthorSelectableIdentity{true};
    };

    export enum class AssetImportPostprocessPolicy : std::uint8_t
    {
        None,
        PrepareRenderableGeometry,
    };

    export struct AssetImportCompletionRecipe
    {
        bool SelectFirstCreatedEntity{true};
        bool FocusCameraOnCreatedGeometry{true};
    };

    export struct AssetImportRecipe
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        RuntimeAssetIngestSource Source{RuntimeAssetIngestSource::ManualImport};
        Assets::AssetId ExistingAsset{};
        ImportAuthoringRecipe Authoring{};
        AssetImportPostprocessPolicy Postprocess{
            AssetImportPostprocessPolicy::PrepareRenderableGeometry};
        AssetImportCompletionRecipe Completion{};
    };

    export struct AssetImportExecutionIdentity
    {
        RuntimeAssetIngestHandle Request{};
        WorldHandle World{};
        std::uint64_t BindingGeneration{0u};
        std::uint64_t CancellationGeneration{0u};

        [[nodiscard]] friend bool operator==(
            const AssetImportExecutionIdentity&,
            const AssetImportExecutionIdentity&) noexcept = default;
    };

    export struct AssetImportRouteResult
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{
            Assets::AssetPayloadKind::Unknown};
    };

    export struct AssetImportDecodeResult
    {
        Assets::AssetPayloadKind PayloadKind{
            Assets::AssetPayloadKind::Unknown};
        std::size_t OwnedValueCount{0u};
    };

    export struct AssetImportCpuMaterializationResult
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{
            Assets::AssetPayloadKind::Unknown};
        std::uint64_t PrimitiveCount{0u};
        std::uint64_t EmbeddedTextureCount{0u};
    };

    export struct AssetImportEcsAuthorResult
    {
        std::uint64_t CreatedEntityCount{0u};
    };

    export struct AssetImportPostprocessResult
    {
        AssetImportPostprocessPolicy Policy{
            AssetImportPostprocessPolicy::None};
        bool Requested{false};
    };

    export struct AssetImportGpuResidencyResult
    {
        std::uint64_t RequestCount{0u};
        bool Requested{false};
    };

    export struct AssetImportCompletionResult
    {
        bool SelectFirstCreatedEntity{false};
        bool FocusCameraOnCreatedGeometry{false};
    };

    export using AssetImportStagePayload = std::variant<
        std::monostate,
        AssetImportRouteResult,
        AssetImportDecodeResult,
        AssetImportCpuMaterializationResult,
        AssetImportEcsAuthorResult,
        AssetImportPostprocessResult,
        AssetImportGpuResidencyResult,
        AssetImportCompletionResult>;

    export struct AssetImportStageResult
    {
        AssetImportExecutionIdentity Identity{};
        AssetImportStage Stage{AssetImportStage::Route};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        RuntimeAssetIngestDiagnostic Diagnostic{
            RuntimeAssetIngestDiagnostic::None};
        AssetImportStagePayload Payload{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Error == Core::ErrorCode::Success;
        }
    };

    export struct AssetImportStageTrace
    {
        AssetImportExecutionIdentity Identity{};
        std::vector<AssetImportStageResult> Results{};
        bool Terminal{false};
    };

    export [[nodiscard]] inline bool AssetImportStagePayloadMatches(
        const AssetImportStage stage,
        const AssetImportStagePayload& payload) noexcept
    {
        switch (stage)
        {
        case AssetImportStage::Route:
            return std::holds_alternative<AssetImportRouteResult>(payload);
        case AssetImportStage::Decode:
            return std::holds_alternative<AssetImportDecodeResult>(payload);
        case AssetImportStage::CpuMaterialize:
            return std::holds_alternative<
                AssetImportCpuMaterializationResult>(payload);
        case AssetImportStage::EcsAuthor:
            return std::holds_alternative<AssetImportEcsAuthorResult>(payload);
        case AssetImportStage::Postprocess:
            return std::holds_alternative<AssetImportPostprocessResult>(payload);
        case AssetImportStage::GpuResidency:
            return std::holds_alternative<AssetImportGpuResidencyResult>(payload);
        case AssetImportStage::Complete:
            return std::holds_alternative<AssetImportCompletionResult>(payload);
        }
        return false;
    }

    export [[nodiscard]] inline Core::Result ValidateAssetImportRecipe(
        const AssetImportRecipe& recipe) noexcept
    {
        if (recipe.Path.empty() ||
            recipe.PayloadKind == Assets::AssetPayloadKind::Unknown)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        const bool isReimport =
            recipe.Source == RuntimeAssetIngestSource::Reimport;
        if (isReimport != recipe.ExistingAsset.IsValid())
            return Core::Err(Core::ErrorCode::InvalidArgument);

        if (recipe.PayloadKind != Assets::AssetPayloadKind::Texture2D &&
            (!recipe.Authoring.AuthorRenderableComponents ||
             !recipe.Authoring.AuthorSelectableIdentity))
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        return Core::Ok();
    }

    export [[nodiscard]] inline Core::Result AppendAssetImportStageResult(
        AssetImportStageTrace& trace,
        AssetImportStageResult result)
    {
        constexpr std::array<AssetImportStage, 7> orderedStages{
            AssetImportStage::Route,
            AssetImportStage::Decode,
            AssetImportStage::CpuMaterialize,
            AssetImportStage::EcsAuthor,
            AssetImportStage::Postprocess,
            AssetImportStage::GpuResidency,
            AssetImportStage::Complete,
        };

        if (trace.Terminal ||
            trace.Results.size() >= orderedStages.size() ||
            result.Identity != trace.Identity ||
            result.Stage != orderedStages[trace.Results.size()])
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        if (result.Succeeded())
        {
            if (result.Diagnostic != RuntimeAssetIngestDiagnostic::None ||
                !AssetImportStagePayloadMatches(
                    result.Stage,
                    result.Payload))
                return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        else if (result.Diagnostic == RuntimeAssetIngestDiagnostic::None)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        const bool terminal =
            !result.Succeeded() ||
            result.Stage == AssetImportStage::Complete;
        trace.Results.push_back(std::move(result));
        trace.Terminal = terminal;
        return Core::Ok();
    }

    export struct RuntimeAssetImportRequest
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export struct RuntimeAssetReimportRequest
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export using RuntimeIOBackendFactory =
        std::function<std::unique_ptr<Core::IO::IIOBackend>()>;

    export struct RuntimeAssetImportResult
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t TextureUploadRequests{0};
        bool MaterializedModelScene{false};
        bool RequestedTextureUpload{false};
    };

    export struct RuntimeAssetImportEvent
    {
        std::uint64_t Sequence{0};
        std::string Path{};
        Assets::AssetPayloadKind RequestedPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        RuntimeAssetIngestDiagnostic IngestDiagnostic{
            RuntimeAssetIngestDiagnostic::None};
        std::optional<RuntimeAssetImportResult> Result{};
        std::optional<AssetImportStageTrace> StageTrace{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Result.has_value() &&
                Error == Core::ErrorCode::Success;
        }
    };

    export struct RuntimeQueuedAssetImport
    {
        RuntimeAssetIngestHandle Operation{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export class AssetWorkflowModule final
        : public IRuntimeModule
        , public Core::IAssetFrameHooks
    {
    public:
        AssetWorkflowModule();
        ~AssetWorkflowModule() override;

        AssetWorkflowModule(const AssetWorkflowModule&) = delete;
        AssetWorkflowModule& operator=(const AssetWorkflowModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueAssetImport(AssetImportRecipe recipe);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
            ImportAssetFromPath(RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueModelTextureImport(RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueGeometryImport(RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
            ReimportAsset(RuntimeAssetReimportRequest request);
        [[nodiscard]] const std::optional<RuntimeAssetImportEvent>&
            GetLastAssetImportEvent() const noexcept;
        [[nodiscard]] std::vector<RuntimeAssetIngestRecord>
            GetAssetIngestRecordsForTest() const;
        void SetModelTextureImportIOBackendFactoryForTest(
            RuntimeIOBackendFactory factory);
        void SetQueuedGeometryImportBeforeDecodeHookForTest(
            std::function<void(const RuntimeAssetImportRequest&)> hook);
        [[nodiscard]] RuntimeAssetImportQueueSnapshot
            GetAssetImportQueueSnapshot() const;
        [[nodiscard]] TextureBakeService*
            GetTextureBakeServiceForTest() const noexcept;
        [[nodiscard]] std::size_t ClearCompletedAssetImports();
        [[nodiscard]] Core::Result CancelAssetImport(
            RuntimeAssetIngestHandle operation);
        void CancelActiveAssetImportsForShutdown();
        void ImportDroppedFilePaths(std::span<const std::string> paths);

    private:
        void TickAssets() override;

        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
