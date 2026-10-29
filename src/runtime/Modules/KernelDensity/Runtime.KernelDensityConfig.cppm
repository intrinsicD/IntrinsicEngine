// Persisted canonical-domain local Gaussian density controls and backend selection.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
export module Extrinsic.Runtime.KernelDensityConfig;
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
    extern "C++" [[nodiscard]] std::span<const ConfigFieldSpec> KernelDensityConfigFieldSpecs() noexcept;
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
    extern "C++" [[nodiscard]] std::string SerializeKernelDensityConfig(const KernelDensityConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateKernelDensityConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    extern "C++" [[nodiscard]] std::optional<KernelDensityConfig> GetKernelDensityConfig(const Core::Config::EngineConfig&);
    extern "C++" void SetKernelDensityConfig(Core::Config::EngineConfig&,const KernelDensityConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeKernelDensityConfigSectionRegistration();
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
    // The declared fields bound to the record's members, for generated panels and agent rows.
    extern "C++" [[nodiscard]] std::span<const BoundField<KernelDensityConfig>> KernelDensityConfigRows() noexcept;
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
