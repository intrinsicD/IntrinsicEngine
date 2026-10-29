// Persisted canonical-domain spacing/radius controls and neighborhood backend selection.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <glm/vec3.hpp>
export module Extrinsic.Runtime.PointSpacingConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
// RUNTIME-349: the operation's results and entry points (the editor's processing surface).
export import Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.EditorCommon;
export import Extrinsic.Runtime.PointScalarTransaction;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.ECS.Components.GeometrySources;
export namespace Extrinsic::Runtime
{
    extern "C++" [[nodiscard]] std::span<const ConfigFieldSpec> PointSpacingConfigFieldSpecs() noexcept;
    inline constexpr std::string_view kPointSpacingConfigSectionName="sandbox.point_spacing";
    inline constexpr std::string_view kPointSpacingConfigSectionSchemaId="intrinsic.runtime.sandbox.point_spacing";
    enum class PointSpacingBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    extern "C++" [[nodiscard]] const char* ToString(PointSpacingBackend) noexcept;
    struct PointSpacingConfig
    {
        std::uint32_t StableEntityId{};
        PointSpacingBackend Backend{PointSpacingBackend::CpuOctree};
        GeometryPropertyRef Positions{.Domain = GeometryElementDomain::Unknown,
            .Name = std::string{ECS::Components::GeometrySources::PropertyNames::kPosition},
            .ValueKind = Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Radii{.Domain=GeometryElementDomain::Unknown,.Name="radii",.ValueKind=Geometry::PropertyValueKind::Float};
        std::uint32_t KNeighbors{6}, GpuQueryBatchSize{4096};
        float ScaleFactor{1};
    };
    extern "C++" [[nodiscard]] std::string SerializePointSpacingConfig(const PointSpacingConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidatePointSpacingConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    extern "C++" [[nodiscard]] std::optional<PointSpacingConfig> GetPointSpacingConfig(const Core::Config::EngineConfig&);
    extern "C++" void SetPointSpacingConfig(Core::Config::EngineConfig&,const PointSpacingConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakePointSpacingConfigSectionRegistration();
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
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied || Status == EditorCommandStatus::NoChange;
        }
    };
    // The operation's identity for its stored results (RUNTIME-349, ADR 0031 Decision 7).
    struct PointSpacingOperation
    {
        using Params = PointSpacingConfig;
        using Result = EditorPointSpacingResult;
    };
    // The declared fields bound to the record's members, for generated panels and agent rows.
    extern "C++" [[nodiscard]] std::span<const BoundField<PointSpacingConfig>> PointSpacingConfigRows() noexcept;
    // Apply returns immediate outcomes directly. onComplete receives the terminal
    // outcome of a newly queued job while its attachment remains active, or the
    // inline outcome when no job lane exists (RUNTIME-348). Pending for an already
    // active output observes that job and registers no additional callback. Every
    // outcome but that duplicate refusal is stored in the session's operation results.
    // Configured Apply follows the same delivery contract.
    // Start*Transaction: null handle with the reason in the result when refused; an active job on
    // the same output answers Pending with the shared "already has an active" message.
    [[nodiscard]] EditorPointScalarTransactionHandle StartEditorPointSpacingTransaction(const EditorProcessingCommands&,
        const PointSpacingConfig&,
        EditorPointSpacingResult&,
        std::function<void(EditorPointSpacingResult)> onComplete = {});
    [[nodiscard]] EditorPointScalarTransactionHandle MakeEditorPointSpacingTransactionForTest(
        const EditorProcessingCommands&,
        const PointSpacingConfig&,
        std::vector<float>,
        Graphics::GpuPropertyResidency&,
        const EditorPointScalarTransactionSnapshot& diagnostics = {});
    [[nodiscard]] ActionReadiness PreviewEditorPointSpacingCommand(
        const EditorProcessingCommands&, const PointSpacingConfig&);
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorPointSpacingInputCatalog(
        const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorPointSpacingResult ApplyEditorPointSpacingCommand(const EditorProcessingCommands&,
        const PointSpacingConfig&,
        std::function<void(EditorPointSpacingResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorPointSpacingConfig(
        const EditorProcessingCommands&, const PointSpacingConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<PointSpacingConfig> GetEditorPointSpacingConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorPointSpacingResult ApplyEditorConfiguredPointSpacing(
        const EditorProcessingCommands&, std::function<void(EditorPointSpacingResult)> onComplete = {});
}
