// Typed density/spacing commands and copied results; execution services have a separate owner.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <glm/vec3.hpp>
export module Extrinsic.Runtime.PointFieldOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.KernelDensityConfig;
export import Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.EngineConfigControl;
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
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied || Status==EditorCommandStatus::NoChange; }
    };
    enum class EditorPointFieldResultSlot : std::uint8_t { KernelDensity, PointSpacing };
    // C++ linkage permits incomplete borrowed declarations in private workspace
    // bindings without making sibling feature modules import this family.
    extern "C++"
    {
        struct EditorPointFieldResultSinks
        {
            std::function<void(EditorPointFieldResultSlot)> DismissResult{};
            std::function<void(EditorKernelDensityResult)> KernelDensity{};
            std::function<void(EditorPointSpacingResult)> PointSpacing{};
        };
        struct EditorPointFieldResultsSnapshot
        {
            std::optional<EditorKernelDensityResult> LastKernelDensityResult{};
            std::optional<EditorPointSpacingResult> LastPointSpacingResult{};
        };
    }
    struct EditorPointFieldPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorPointFieldResultSinks ResultSinks{};
        EditorPointFieldResultsSnapshot Results{};
    };

    [[nodiscard]] EditorPointFieldPreparedFrame PrepareEditorPointFieldFrame(const EditorWorkspaceAttachment&);

    // Apply returns immediate outcomes directly. onComplete receives only the
    // terminal outcome of a newly queued job while its attachment remains active.
    // Pending for an already active output observes that job and registers no
    // additional callback. Configured Apply follows the same delivery contract.
    [[nodiscard]] ActionReadiness PreviewEditorKernelDensityCommand(const EditorProcessingCommands&, const KernelDensityConfig&);
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorKernelDensityInputCatalog(const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorKernelDensityResult ApplyEditorKernelDensityCommand(const EditorProcessingCommands&, const KernelDensityConfig&, std::function<void(EditorKernelDensityResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorKernelDensityConfig(const EditorProcessingCommands&, const KernelDensityConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<KernelDensityConfig> GetEditorKernelDensityConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorKernelDensityResult ApplyEditorConfiguredKernelDensity(const EditorProcessingCommands&, std::function<void(EditorKernelDensityResult)> onComplete = {});

    [[nodiscard]] ActionReadiness PreviewEditorPointSpacingCommand(const EditorProcessingCommands&, const PointSpacingConfig&);
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorPointSpacingInputCatalog(const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorPointSpacingResult ApplyEditorPointSpacingCommand(const EditorProcessingCommands&, const PointSpacingConfig&, std::function<void(EditorPointSpacingResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPointSpacingConfig(const EditorProcessingCommands&, const PointSpacingConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<PointSpacingConfig> GetEditorPointSpacingConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorPointSpacingResult ApplyEditorConfiguredPointSpacing(const EditorProcessingCommands&, std::function<void(EditorPointSpacingResult)> onComplete = {});
}
