// Construction of new geometry from a point set: Hoppe surface reconstruction
// and kNN graph building. Unlike same-domain point methods these create an
// owning entity (`OutputEntityId`) with its own sources, materialized assets and
// selection, so they are a separate execution contract with their own undo.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
export module Extrinsic.Runtime.PointConstructionOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.PointConstructionConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    struct EditorPointConstructionResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        PointConstructionMethod Method{PointConstructionMethod::Hoppe};
        PointConstructionBackend RequestedBackend{PointConstructionBackend::CpuReference};
        std::string ActualBackend{}, Message{};
        // Stable id of the created entity; zero until publication succeeds.
        std::uint32_t OutputEntityId{};
        std::size_t InputCount{}, QueryCount{}, OutputVertexCount{}, OutputEdgeCount{}, OutputFaceCount{};
        bool IndexReused{};
        std::size_t GpuQueryBatches{};
        double GpuNeighborhoodMilliseconds{}, CpuComputeMilliseconds{};
        [[nodiscard]] bool Succeeded() const noexcept { return Status==EditorCommandStatus::Applied; }
    };
    struct EditorPointConstructionReadiness
    {
        bool Ready{};
        std::string Diagnostic{};
        PointConstructionConfig Resolved{};
    };
    enum class EditorPointConstructionResultSlot : std::uint8_t { PointConstruction };
    // Private workspace bindings borrow incomplete containers so sibling features
    // need not import these method records.
    extern "C++"
    {
        struct EditorPointConstructionResultSinks
        {
            std::function<void(EditorPointConstructionResultSlot)> DismissResult{};
            std::function<void(EditorPointConstructionResult)> PointConstruction{};
        };
        struct EditorPointConstructionResultsSnapshot
        {
            std::optional<EditorPointConstructionResult> LastPointConstructionResult{};
        };
    }
    struct EditorPointConstructionPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorPointConstructionResultSinks ResultSinks{};
        EditorPointConstructionResultsSnapshot Results{};
    };
    [[nodiscard]] EditorPointConstructionPreparedFrame PrepareEditorPointConstructionFrame(const EditorWorkspaceAttachment&);

    // Apply returns immediate outcomes directly. onComplete receives only the
    // terminal outcome of a newly queued job while its attachment remains active.
    // Pending for an already active output observes that job and registers no
    // additional callback. Configured Apply follows the same delivery contract.
    [[nodiscard]] EditorPointConstructionReadiness PreviewEditorPointConstructionCommand(const EditorProcessingCommands&, const PointConstructionConfig&);
    [[nodiscard]] EditorPointConstructionResult ApplyEditorPointConstructionCommand(const EditorProcessingCommands&, const PointConstructionConfig&, std::function<void(EditorPointConstructionResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPointConstructionConfig(const EditorProcessingCommands&, const PointConstructionConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<PointConstructionConfig> GetEditorPointConstructionConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorPointConstructionResult ApplyEditorConfiguredPointConstruction(const EditorProcessingCommands&, std::function<void(EditorPointConstructionResult)> onComplete = {});
}
