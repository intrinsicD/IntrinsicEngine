// Typed density/spacing commands and copied results; execution services have a separate owner.
module;
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
#include <optional>
#include <string>
#include <glm/vec3.hpp>
export module Extrinsic.Runtime.PointFieldOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.PointScalarTransaction;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.KernelDensityConfig;
export import Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    struct EditorKernelDensityResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        KernelDensityBackend RequestedBackend{KernelDensityBackend::CpuOctree};
        GeometryPropertyRef Density{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        float UsedBandwidth{}, MeanDensity{}, MinDensity{}, MaxDensity{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    struct EditorPointSpacingResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        PointSpacingBackend RequestedBackend{PointSpacingBackend::CpuOctree};
        GeometryPropertyRef Radii{};
        std::string ActualBackend{}, Message{};
        std::size_t SlotCount{}, LiveCount{}, WrittenCount{};
        float MeanRadius{}, MinRadius{}, MaxRadius{};
        glm::vec3 Centroid{};
        float AverageSpacing{}, MinSpacing{}, MaxSpacing{}, BoundingBoxDiagonal{};
        std::uint64_t GpuInputUploadBytes{}, GpuInputCacheHits{}, CpuStageReadbackBytes{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    // RUNTIME-348: the results come from the session's operation result store (a copy for the
    // frame) and are dismissed through it; the operation runtime writes them.
    struct EditorPointFieldPreparedFrame
    {
        EditorProcessingCommands Commands{};
        OperationResultSnapshot Results{};
        OperationResultDismiss DismissResult{};
    };

    [[nodiscard]] EditorPointFieldPreparedFrame PrepareEditorPointFieldFrame(const EditorWorkspaceAttachment&);

    // Apply returns immediate outcomes directly. onComplete receives the terminal
    // outcome of a newly queued job while its attachment remains active, or the
    // inline outcome when no job lane exists (RUNTIME-348). Pending for an already
    // active output observes that job and registers no additional callback. Every
    // outcome but that duplicate refusal is stored in the session's operation results.
    // Configured Apply follows the same delivery contract.
    // Start*Transaction: null handle with the reason in the result when refused; an active job on
    // the same output answers Pending with the shared "already has an active" message.
    [[nodiscard]] EditorPointScalarTransactionHandle StartEditorKernelDensityTransaction(
        const EditorProcessingCommands&,const KernelDensityConfig&,EditorKernelDensityResult&,
        std::function<void(EditorKernelDensityResult)> onComplete={});
    [[nodiscard]] EditorPointScalarTransactionHandle MakeEditorKernelDensityTransactionForTest(
        const EditorProcessingCommands&,const KernelDensityConfig&,std::vector<float>,Graphics::GpuPropertyResidency&, const EditorPointScalarTransactionSnapshot& diagnostics = {});
    [[nodiscard]] ActionReadiness PreviewEditorKernelDensityCommand(const EditorProcessingCommands&, const KernelDensityConfig&);
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorKernelDensityInputCatalog(const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorKernelDensityResult ApplyEditorKernelDensityCommand(const EditorProcessingCommands&, const KernelDensityConfig&, std::function<void(EditorKernelDensityResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorKernelDensityConfig(const EditorProcessingCommands&, const KernelDensityConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<KernelDensityConfig> GetEditorKernelDensityConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorKernelDensityResult ApplyEditorConfiguredKernelDensity(const EditorProcessingCommands&, std::function<void(EditorKernelDensityResult)> onComplete = {});

    [[nodiscard]] EditorPointScalarTransactionHandle StartEditorPointSpacingTransaction(
        const EditorProcessingCommands&,const PointSpacingConfig&,EditorPointSpacingResult&,
        std::function<void(EditorPointSpacingResult)> onComplete={});
    [[nodiscard]] EditorPointScalarTransactionHandle MakeEditorPointSpacingTransactionForTest(
        const EditorProcessingCommands&,const PointSpacingConfig&,std::vector<float>,Graphics::GpuPropertyResidency&, const EditorPointScalarTransactionSnapshot& diagnostics = {});
    [[nodiscard]] ActionReadiness PreviewEditorPointSpacingCommand(const EditorProcessingCommands&, const PointSpacingConfig&);
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorPointSpacingInputCatalog(const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorPointSpacingResult ApplyEditorPointSpacingCommand(const EditorProcessingCommands&, const PointSpacingConfig&, std::function<void(EditorPointSpacingResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPointSpacingConfig(const EditorProcessingCommands&, const PointSpacingConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<PointSpacingConfig> GetEditorPointSpacingConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorPointSpacingResult ApplyEditorConfiguredPointSpacing(const EditorProcessingCommands&, std::function<void(EditorPointSpacingResult)> onComplete = {});
}
