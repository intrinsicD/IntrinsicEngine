// Point-set commands and copied results for progressive Poisson sampling. It
// consumes only the shared processing context, chooses a CPU/GPU backend with
// truthful requested/actual/fallback reporting, and publishes same-domain
// properties on the originating element domain without topology changes. It uses
// the canonical serialized playground config. (The bilateral point filter that
// shared this family is a plugin now.)
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
export module Extrinsic.Runtime.PointSetOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    [[nodiscard]] const char*
    DebugNameForProgressivePoissonChannel(ProgressivePoissonPlaygroundChannel channel) noexcept;

    [[nodiscard]] const char*
    DebugNameForProgressivePoissonBackend(ProgressivePoissonPlaygroundBackend backend) noexcept;

    struct EditorProgressivePoissonCommand
    {
        std::uint32_t StableEntityId{0u};
        ProgressivePoissonPlaygroundConfig Config{};
    };

    struct EditorProgressivePoissonResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        ProgressivePoissonPlaygroundChannel Channel{ProgressivePoissonPlaygroundChannel::Level};
        std::uint32_t InputCount{0u};
        std::uint32_t AcceptedCount{0u};
        std::uint32_t PrefixCount{0u};
        std::uint32_t LevelCount{0u};
        ProgressivePoissonPlaygroundBackend RequestedBackend{
            ProgressivePoissonPlaygroundBackend::CpuReference};
        ProgressivePoissonPlaygroundBackend ActualBackend{
            ProgressivePoissonPlaygroundBackend::CpuReference};
        std::string RequestedBackendId{};
        std::string RequestedBackendDisplayName{};
        std::string BackendId{};
        std::string BackendDisplayName{};
        bool FellBackToCpu{false};
        std::string BackendFallbackReason{};
        std::vector<std::uint32_t> LevelAcceptedCounts{};
        float BaseRadius{0.0f};
        float UsedAlpha{0.0f};
        bool AlphaDefaulted{false};
        bool ClampedGridWidth{false};
        bool ClampedMaxLevels{false};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    enum class EditorPointSetResultSlot : std::uint8_t { ProgressivePoisson };
    // Private workspace bindings borrow incomplete containers so sibling features
    // need not import these method records.
    extern "C++"
    {
        struct EditorPointSetResultSinks
        {
            std::function<void(EditorPointSetResultSlot)> DismissResult{};
            std::function<void(EditorProgressivePoissonResult)> ProgressivePoisson{};
        };
        struct EditorPointSetResultsSnapshot
        {
            std::optional<EditorProgressivePoissonResult> LastProgressivePoissonResult{};
        };
    }
    struct EditorPointSetPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorPointSetResultSinks ResultSinks{};
        EditorPointSetResultsSnapshot Results{};
    };
    [[nodiscard]] EditorPointSetPreparedFrame PrepareEditorPointSetFrame(const EditorWorkspaceAttachment&);

    // Admission uses metadata only; apply checks finite values before submission.
    [[nodiscard]] ActionReadiness PreviewEditorProgressivePoissonCommand(
        const EditorProcessingCommands&, const EditorProgressivePoissonCommand&);
    [[nodiscard]] EditorProgressivePoissonResult ApplyEditorProgressivePoissonCommand(
        const EditorProcessingCommands&, const EditorProgressivePoissonCommand&,
        std::function<void(EditorProgressivePoissonResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorProgressivePoissonConfig(
        const EditorProcessingCommands&, const ProgressivePoissonPlaygroundConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<ProgressivePoissonPlaygroundConfig>
    GetEditorProgressivePoissonConfig(const EditorProcessingCommands&);
}
