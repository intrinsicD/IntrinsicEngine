// Iterative-closest-point alignment of named point bindings through shared processing commands.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
export module Extrinsic.Runtime.RegistrationOperations;
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.RegistrationConfig;
export import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
export namespace Extrinsic::Runtime
{
    using EditorRegistrationCommand = RegistrationConfig;
    [[nodiscard]] const char* DebugNameForEditorICPVariant(EditorICPVariant variant) noexcept;

    struct EditorRegistrationResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        bool HasResult{false};
        RegistrationBackend RequestedBackend{RegistrationBackend::CpuKDTree};
        RegistrationBackend ActualBackend{RegistrationBackend::CpuKDTree};
        bool FellBackToCPU{};
        bool TargetIndexReused{};
        std::string BackendDiagnostic{};
        // The variant the command asked for.
        EditorICPVariant Variant{EditorICPVariant::PointToPoint};
        // The variant the solver actually ran. Runtime preflight rejects a
        // point-to-plane request when it cannot supply valid target normals,
        // preventing `Geometry.Registration`'s point-to-point fallback from
        // becoming silent; successful requested/effective variants agree.
        EditorICPVariant EffectiveVariant{EditorICPVariant::PointToPoint};
        std::size_t SourcePointCount{0u};
        std::size_t TargetPointCount{0u};
        // Target normals resolved, transformed to world space, and handed to
        // the solver. Zero for a point-to-point run.
        std::size_t TargetNormalCount{0u};
        std::size_t IterationsPerformed{0u};
        std::size_t TrajectoryLength{0u};
        std::size_t AppliedStep{0u};
        double FinalRMSE{0.0};
        bool Converged{false};
        std::size_t FinalInlierCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    // Incomplete borrowed containers keep sibling workspace features independent
    // of registration records; prepared frames copy their values.
    extern "C++"
    {
        struct EditorRegistrationResultSinks
        {
            std::function<void()> DismissResult{};
            std::function<void(EditorRegistrationResult)> Registration{};
        };
        struct EditorRegistrationResultsSnapshot
        {
            std::optional<EditorRegistrationResult> LastRegistrationResult{};
        };
    }
    struct EditorRegistrationPreparedFrame
    {
        EditorProcessingCommands Commands{};
        EditorRegistrationResultSinks ResultSinks{};
        EditorRegistrationResultsSnapshot Results{};
    };
    [[nodiscard]] EditorRegistrationPreparedFrame PrepareEditorRegistrationFrame(const EditorWorkspaceAttachment&);

    [[nodiscard]] ActionReadiness PreviewEditorRegistrationCommand(
        const EditorProcessingCommands&, const EditorRegistrationCommand&);
    // ICP needs a solvable correspondence problem, so this catalog keeps only
    // vec3 bindings carrying at least three live finite samples. It is
    // deliberately narrower than the shared point-input catalog.
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorRegistrationInputCatalog(
        const EditorProcessingCommands&, std::uint32_t stableId);
    // Immediate outcomes return directly. Only a newly queued job delivers a
    // terminal callback, while attached. Duplicate Pending requests add no callback.
    [[nodiscard]] EditorRegistrationResult ApplyEditorRegistrationCommand(
        const EditorProcessingCommands&, const EditorRegistrationCommand&,
        std::function<void(EditorRegistrationResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorRegistrationConfig(
        const EditorProcessingCommands&, const RegistrationConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<RegistrationConfig> GetEditorRegistrationConfig(
        const EditorProcessingCommands&);
    [[nodiscard]] EditorRegistrationResult ApplyEditorConfiguredRegistrationCommand(
        const EditorProcessingCommands&, std::function<void(EditorRegistrationResult)> onComplete = {});
}
