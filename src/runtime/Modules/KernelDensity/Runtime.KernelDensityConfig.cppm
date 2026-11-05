// Persisted canonical-domain local Gaussian density controls and backend selection.
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
export module Extrinsic.Runtime.KernelDensityConfig;
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
    inline constexpr std::string_view kKernelDensityConfigSectionName="sandbox.kernel_density";
    inline constexpr std::string_view kKernelDensityConfigSectionSchemaId="intrinsic.runtime.sandbox.kernel_density";
    enum class KernelDensityBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    extern "C++" [[nodiscard]] const char* ToString(KernelDensityBackend) noexcept;
    struct KernelDensityConfig
    {
        std::uint32_t StableEntityId{};
        KernelDensityBackend Backend{KernelDensityBackend::CpuOctree};
        GeometryPropertyRef Positions{.Domain = GeometryElementDomain::Unknown,
            .Name = std::string{ECS::Components::GeometrySources::PropertyNames::kPosition},
            .ValueKind = Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Density{.Domain=GeometryElementDomain::Unknown,.Name="density",.ValueKind=Geometry::PropertyValueKind::Float};
        std::uint32_t KNeighbors{15}, GpuQueryBatchSize{4096};
        float Bandwidth{0};
    };
    // RUNTIME-351: the section declared once (rows bound to the record, the codec derived from them);
    // callers use the generic functions of Extrinsic.Runtime.DerivedConfigSections with kKernelDensitySection.
    inline constexpr Geometry::PropertyValueKind kKernelDensityFloatKind[]{Geometry::PropertyValueKind::Float};
    inline constexpr Geometry::PropertyValueKind kKernelDensityVec3Kind[]{Geometry::PropertyValueKind::Vec3};
    inline constexpr std::string_view kKernelDensityBackend[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
    inline constexpr std::string_view kKernelDensityBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
    inline constexpr std::array kKernelDensityRows{
        Bind<&KernelDensityConfig::StableEntityId>(
            {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."}),
        Bind<&KernelDensityConfig::KNeighbors>(
            {.Name = "k_neighbors", .Type = ConfigFieldType::UInt, .Description = "K neighbors."}),
        Bind<&KernelDensityConfig::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
            .Type = ConfigFieldType::UInt,
            .Description = "Gpu query batch size.",
            .Min = 1,
            .Max = 16384,
            .Refusal = "GPU query batch size must be 1..16384."}),
        Bind<&KernelDensityConfig::Backend>({.Name = "backend",
            .Type = ConfigFieldType::StringEnum,
            .Description = "Backend.",
            .EnumNames = kKernelDensityBackendNames,
            .EnumValues = kKernelDensityBackend,
            .Refusal = "Unknown density backend."}),
        Bind<&KernelDensityConfig::Bandwidth>({.Name = "bandwidth",
            .Type = ConfigFieldType::Float,
            .Description = "Zero selects automatic bandwidth; positive values must remain positive in float storage.",
            .Min = 0,
            .Max = double(std::numeric_limits<float>::max()),
            .Refusal = "Positive bandwidth must remain positive in float storage."}),
        Bind<&KernelDensityConfig::Positions>({.Name = "positions",
            .Type = ConfigFieldType::PropertyRef,
            .Description = "Position and density bindings must have distinct names on the same element domain.",
            .RefKinds = kKernelDensityVec3Kind}),
        Bind<&KernelDensityConfig::Density>({.Name = "density",
            .Type = ConfigFieldType::PropertyRef,
            .Description = "Density.",
            .RefKinds = kKernelDensityFloatKind,
            .AnyScalar = true}),
    };
    inline constexpr auto kKernelDensityFields = SpecsOf(kKernelDensityRows);
    inline constexpr DerivedConfigSection<KernelDensityConfig> kKernelDensitySection{
        .Name = kKernelDensityConfigSectionName,
        .SchemaId = kKernelDensityConfigSectionSchemaId,
        .Title = "KernelDensity",
        .Rows = kKernelDensityRows,
        .Fields = kKernelDensityFields,
        .ObjectError = "Kernel density config must be an object.",
        .UnknownFieldPrefix = "Unknown density field: ",
        .Check = [](const KernelDensityConfig& c) -> std::string_view
        {
            return c.Positions.Domain != c.Density.Domain || c.Positions.Name == c.Density.Name
                ? "Position and density must be distinct properties on the same domain."
                : "";
        }};
    // The section's codec is instantiated once, in the operation's implementation unit.
    extern template std::string SerializeDerivedSection<kKernelDensitySection>(const KernelDensityConfig&);
    extern template Core::Config::EngineConfigSectionValidationResult ValidateDerivedSection<kKernelDensitySection>(
        std::string_view, std::string_view, std::string_view);
    extern template std::optional<KernelDensityConfig> GetDerivedSection<kKernelDensitySection>(
        const Core::Config::EngineConfig&);
    extern template void SetDerivedSection<kKernelDensitySection>(
        Core::Config::EngineConfig&, const KernelDensityConfig&);
    extern template Core::Config::EngineConfigSectionRegistration
    MakeDerivedSectionRegistration<kKernelDensitySection>();
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
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied || Status == EditorCommandStatus::NoChange;
        }
    };
    // The operation's identity for its stored results (RUNTIME-349, ADR 0031 Decision 7).
    struct KernelDensityOperation
    {
        using Params = KernelDensityConfig;
        using Result = EditorKernelDensityResult;
    };
    // Apply returns immediate outcomes directly. onComplete receives the terminal
    // outcome of a newly queued job while its attachment remains active, or the
    // inline outcome when no job lane exists (RUNTIME-348). Pending for an already
    // active output observes that job and registers no additional callback. Every
    // outcome but that duplicate refusal is stored in the session's operation results.
    // Configured Apply follows the same delivery contract.
    // Start*Transaction: null handle with the reason in the result when refused; an active job on
    // the same output answers Pending with the shared "already has an active" message.
    [[nodiscard]] EditorPointScalarTransactionHandle StartEditorKernelDensityTransaction(
        const EditorProcessingCommands&,
        const KernelDensityConfig&,
        EditorKernelDensityResult&,
        std::function<void(EditorKernelDensityResult)> onComplete = {});
    [[nodiscard]] EditorPointScalarTransactionHandle MakeEditorKernelDensityTransactionForTest(
        const EditorProcessingCommands&,
        const KernelDensityConfig&,
        std::vector<float>,
        Graphics::GpuPropertyResidency&,
        const EditorPointScalarTransactionSnapshot& diagnostics = {});
    [[nodiscard]] ActionReadiness PreviewEditorKernelDensityCommand(
        const EditorProcessingCommands&, const KernelDensityConfig&);
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorKernelDensityInputCatalog(
        const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorKernelDensityResult ApplyEditorKernelDensityCommand(const EditorProcessingCommands&,
        const KernelDensityConfig&,
        std::function<void(EditorKernelDensityResult)> onComplete = {});
    [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEditorKernelDensityConfig(
        const EditorProcessingCommands&, const KernelDensityConfig&, std::string sourceId = {});
    [[nodiscard]] std::optional<KernelDensityConfig> GetEditorKernelDensityConfig(const EditorProcessingCommands&);
    [[nodiscard]] EditorKernelDensityResult ApplyEditorConfiguredKernelDensity(
        const EditorProcessingCommands&, std::function<void(EditorKernelDensityResult)> onComplete = {});
}
