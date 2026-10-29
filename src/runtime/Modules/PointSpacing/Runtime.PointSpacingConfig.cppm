// Persisted canonical-domain spacing/radius controls and neighborhood backend selection.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <glm/vec3.hpp>
export module Extrinsic.Runtime.PointSpacingConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.DerivedConfigSections;
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
    // RUNTIME-351: the section declared once (rows bound to the record, the codec derived from them);
    // callers use the generic functions of Extrinsic.Runtime.DerivedConfigSections with kPointSpacingSection.
    inline constexpr Geometry::PropertyValueKind kPointSpacingFloatKind[]{Geometry::PropertyValueKind::Float};
    inline constexpr Geometry::PropertyValueKind kPointSpacingVec3Kind[]{Geometry::PropertyValueKind::Vec3};
    inline constexpr std::string_view kPointSpacingBackend[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
    inline constexpr std::string_view kPointSpacingBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
    inline constexpr std::array kPointSpacingRows{
        Bind<&PointSpacingConfig::StableEntityId>(
            {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."}),
        Bind<&PointSpacingConfig::KNeighbors>(
            {.Name = "k_neighbors", .Type = ConfigFieldType::UInt, .Description = "K neighbors."}),
        Bind<&PointSpacingConfig::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
            .Type = ConfigFieldType::UInt,
            .Description = "Gpu query batch size.",
            .Min = 1,
            .Max = 16384,
            .Refusal = "GPU query batch size must be 1..16384."}),
        Bind<&PointSpacingConfig::Backend>({.Name = "backend",
            .Type = ConfigFieldType::StringEnum,
            .Description = "Backend.",
            .EnumNames = kPointSpacingBackendNames,
            .EnumValues = kPointSpacingBackend,
            .Refusal = "Unknown radii backend."}),
        Bind<&PointSpacingConfig::ScaleFactor>({.Name = "scale_factor",
            .Type = ConfigFieldType::Float,
            .Description = "Nonnegative radius scale factor; positive values must remain positive in float storage.",
            .Min = 0,
            .Max = double(std::numeric_limits<float>::max()),
            .Refusal = "Positive scale factor must remain positive in float storage."}),
        Bind<&PointSpacingConfig::Positions>({.Name = "positions",
            .Type = ConfigFieldType::PropertyRef,
            .Description = "Position and radii bindings must have distinct names on the same element domain.",
            .RefKinds = kPointSpacingVec3Kind}),
        Bind<&PointSpacingConfig::Radii>({.Name = "radii",
            .Type = ConfigFieldType::PropertyRef,
            .Description = "Radii.",
            .RefKinds = kPointSpacingFloatKind,
            .AnyScalar = true}),
    };
    inline constexpr auto kPointSpacingFields = SpecsOf(kPointSpacingRows);
    inline constexpr DerivedConfigSection<PointSpacingConfig> kPointSpacingSection{
        .Name = kPointSpacingConfigSectionName,
        .SchemaId = kPointSpacingConfigSectionSchemaId,
        .Title = "PointSpacing",
        .Rows = kPointSpacingRows,
        .Fields = kPointSpacingFields,
        .ObjectError = "Point spacing config must be an object.",
        .UnknownFieldPrefix = "Unknown radii field: ",
        .Check = [](const PointSpacingConfig& c) -> std::string_view
        {
            return c.Positions.Domain != c.Radii.Domain || c.Positions.Name == c.Radii.Name
                ? "Position and radii must be distinct properties on the same domain."
                : "";
        }};
    // The section's codec is instantiated once, in the operation's implementation unit.
    extern template std::string SerializeDerivedSection<kPointSpacingSection>(const PointSpacingConfig&);
    extern template Core::Config::EngineConfigSectionValidationResult ValidateDerivedSection<kPointSpacingSection>(
        std::string_view, std::string_view, std::string_view);
    extern template std::optional<PointSpacingConfig> GetDerivedSection<kPointSpacingSection>(
        const Core::Config::EngineConfig&);
    extern template void SetDerivedSection<kPointSpacingSection>(
        Core::Config::EngineConfig&, const PointSpacingConfig&);
    extern template Core::Config::EngineConfigSectionRegistration
    MakeDerivedSectionRegistration<kPointSpacingSection>();
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
