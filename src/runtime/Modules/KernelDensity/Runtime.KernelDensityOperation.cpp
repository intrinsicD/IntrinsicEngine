// Kernel density, declared once (RUNTIME-349, ADR 0031 Decision 8): its config section derived from the
// bound rows, its definition on the operation runtime, and its entry points.
module;
#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/vec3.hpp>
#include <entt/entity/registry.hpp>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.KernelDensityConfig;
import Extrinsic.Runtime.OperationRuntime;
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.PointCloud.Utils;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "Config/internal/Runtime.DerivedConfigSection.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr Geometry::PropertyValueKind kFloatKind[]{Geometry::PropertyValueKind::Float};
        constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
        constexpr std::string_view kKernelDensityBackend[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
        constexpr std::string_view kKernelDensityBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
        constexpr std::array kKernelDensityRows{
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
                .Description =
                    "Zero selects automatic bandwidth; positive values must remain positive in float storage.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max()),
                .Refusal = "Positive bandwidth must remain positive in float storage."}),
            Bind<&KernelDensityConfig::Positions>({.Name = "positions",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Position and density bindings must have distinct names on the same element domain.",
                .RefKinds = kVec3Kind}),
            Bind<&KernelDensityConfig::Density>({.Name = "density",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Density.",
                .RefKinds = kFloatKind,
                .AnyScalar = true}),
        };
        constexpr auto kKernelDensityFields = SpecsOf(kKernelDensityRows);
    }
    extern "C++" std::span<const BoundField<KernelDensityConfig>> KernelDensityConfigRows() noexcept
    {
        return kKernelDensityRows;
    }
}

// The section's exported functions keep global-module attachment, as their interface declares them.
extern "C++"
{
    namespace Extrinsic::Runtime
    {
        namespace
        {
            constexpr ConfigDetail::DerivedConfigSection<KernelDensityConfig> kKernelDensitySection{
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
                },
                .Validate = &ValidateKernelDensityConfigSection};
            static_assert(ConfigDetail::IsDerivable(kKernelDensitySection));
        // Rows paired with a different table, by name or by type, are refused (the check's witnesses).
            constexpr auto kKernelDensityRenamed = []
            {
                auto fields = kKernelDensityFields;
                fields[1].Name = "other";
                return fields;
            }();
            constexpr auto kKernelDensityRetyped = []
            {
                auto fields = kKernelDensityFields;
                fields[1].Type = ConfigFieldType::Float;
                return fields;
            }();
            static_assert(!ConfigDetail::IsDerivable(
                ConfigDetail::DerivedConfigSection<KernelDensityConfig>{.Rows = kKernelDensityRows,
                    .Fields = kKernelDensityRenamed,
                    .Validate = &ValidateKernelDensityConfigSection}));
            static_assert(!ConfigDetail::IsDerivable(
                ConfigDetail::DerivedConfigSection<KernelDensityConfig>{.Rows = kKernelDensityRows,
                    .Fields = kKernelDensityRetyped,
                    .Validate = &ValidateKernelDensityConfigSection}));
        }
    // The row's tokens, so the codec and the owner's names cannot drift apart.
        const char* ToString(KernelDensityBackend b) noexcept
        {
            const auto i = std::to_underlying(b);
            return i < std::size(kKernelDensityBackend) ? kKernelDensityBackend[i].data() : "invalid";
        }
        std::string SerializeKernelDensityConfig(const KernelDensityConfig& c)
        {
            return ConfigDetail::SerializeDerivedConfig(kKernelDensitySection, c);
        }
        Core::Config::EngineConfigSectionValidationResult ValidateKernelDensityConfigSection(
            std::string_view payload, std::string_view, std::string_view subject)
        {
            return ConfigDetail::ValidateDerivedConfig(kKernelDensitySection, payload, subject);
        }
        std::optional<KernelDensityConfig> GetKernelDensityConfig(const Core::Config::EngineConfig& c)
        {
            return ConfigDetail::GetDerivedConfig(kKernelDensitySection, c);
        }
        void SetKernelDensityConfig(Core::Config::EngineConfig& c, const KernelDensityConfig& value)
        {
            ConfigDetail::SetDerivedConfig(kKernelDensitySection, c, value);
        }
        std::span<const ConfigFieldSpec> KernelDensityConfigFieldSpecs() noexcept
        {
            return kKernelDensityFields;
        }
        Core::Config::EngineConfigSectionRegistration MakeKernelDensityConfigSectionRegistration()
        {
            return ConfigDetail::MakeDerivedRegistration(kKernelDensitySection);
        }
    }
}

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace PC = Geometry::PointCloud;
        using GeometryProcessingDetail::OperationRuntime;
        using GeometryProcessingDetail::PointSamplesInput;

        struct KernelDensityDefinition
        {
            using Operation = KernelDensityOperation;
            using Params = KernelDensityConfig;
            using Result = EditorKernelDensityResult;
            using Backend = KernelDensityBackend;
            static constexpr std::string_view Noun{"Density"}, Method{"Kernel density"},
                History{"Estimate kernel density"}, Evaluation{"CPU bandwidth/Gaussian evaluation"},
                Failure{
                    "Kernel density failed: invalid neighborhoods or unrepresentable float kernel values/bandwidth."};
            static constexpr std::size_t MinimumK = 2; // Query min(n,max(k,2)+1) including self.
            static GeometryPropertyRef& Output(Params& c)
            {
                return c.Density;
            }
            static Result InitialResult(const Params& c)
            {
                return {.RequestedBackend = c.Backend, .Density = c.Density};
            }
            static Core::Config::EngineConfigSectionValidationResult Validate(const Params& c)
            {
                return ValidateKernelDensityConfigSection(
                    SerializeKernelDensityConfig(c), {}, kKernelDensityConfigSectionName);
            }
            static Graphics::PointScalarGpuParams GpuParams(const Params& c)
            {
                return {.Method = 0, .K = c.KNeighbors, .Bandwidth = c.Bandwidth};
            }
            static void FoldGpuStats(Result& r, const Graphics::PointScalarGpuStats& s)
            {
                r.UsedBandwidth = s.Bandwidth;
                r.MeanDensity = s.Mean;
                r.MinDensity = s.Minimum;
                r.MaxDensity = s.Maximum;
            }
            static std::optional<std::vector<float>> Compute(const Params& c, const PointSamplesInput input, Result& r)
            {
                const PC::KDEParams params{.KNeighbors = c.KNeighbors, .Bandwidth = c.Bandwidth};
                auto analysis = input.Neighbors
                    ? PC::EstimateKernelDensityFromNeighbors(input.Points, *input.Neighbors, params)
                    : PC::EstimateKernelDensity(input.Points, params);
                if (!analysis || analysis->Densities.size() != input.Points.size())
                    return std::nullopt;
                r.UsedBandwidth = analysis->UsedBandwidth;
                r.MeanDensity = analysis->MeanDensity;
                r.MinDensity = analysis->MinDensity;
                r.MaxDensity = analysis->MaxDensity;
                return std::move(analysis->Densities);
            }
        };
        using KernelDensityRuntime = OperationRuntime<KernelDensityDefinition>;
    }
    EditorPointScalarTransactionHandle StartEditorKernelDensityTransaction(const EditorProcessingCommands& commands,
        const KernelDensityConfig& config,
        EditorKernelDensityResult& result,
        std::function<void(EditorKernelDensityResult)> sink)
    {
        return KernelDensityRuntime::StartTransaction(commands, config, result, std::move(sink));
    }
    EditorPointScalarTransactionHandle MakeEditorKernelDensityTransactionForTest(
        const EditorProcessingCommands& commands,
        const KernelDensityConfig& config,
        std::vector<float> values,
        Graphics::GpuPropertyResidency& residency,
        const EditorPointScalarTransactionSnapshot& diagnostics)
    {
        return KernelDensityRuntime::MakeTransactionForTest(
            commands, config, std::move(values), residency, diagnostics);
    }
    ActionReadiness PreviewEditorKernelDensityCommand(
        const EditorProcessingCommands& commands, const KernelDensityConfig& config)
    {
        return KernelDensityRuntime::Preview(commands, config);
    }
    GeometryPropertyCatalogSnapshot GetEditorKernelDensityInputCatalog(
        const EditorProcessingCommands& commands, std::uint32_t id)
    {
        return KernelDensityRuntime::InputCatalog(commands, id);
    }
    EditorKernelDensityResult ApplyEditorKernelDensityCommand(const EditorProcessingCommands& commands,
        const KernelDensityConfig& config,
        std::function<void(EditorKernelDensityResult)> onComplete)
    {
        return KernelDensityRuntime::Apply(commands, config, std::move(onComplete));
    }
    EditorKernelDensityResult ApplyEditorConfiguredKernelDensity(
        const EditorProcessingCommands& commands, std::function<void(EditorKernelDensityResult)> onComplete)
    {
        return KernelDensityRuntime::ApplyConfigured(
            commands, GetEditorKernelDensityConfig(commands), std::move(onComplete));
    }
    RuntimeEngineConfigApplyResult ApplyEditorKernelDensityConfig(
        const EditorProcessingCommands& commands, const KernelDensityConfig& config, std::string sourceId)
    {
        return ApplyOperationConfig(commands,
            ValidateKernelDensityConfigSection(
                SerializeKernelDensityConfig(config), {}, kKernelDensityConfigSectionName),
            sourceId.empty() ? std::string{kKernelDensityConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate)
            {
                SetKernelDensityConfig(candidate, config);
            });
    }
    std::optional<KernelDensityConfig> GetEditorKernelDensityConfig(const EditorProcessingCommands& commands)
    {
        const Core::Config::EngineConfig* active = ActiveOperationConfig(commands);
        return active ? GetKernelDensityConfig(*active) : std::nullopt;
    }
} // namespace Extrinsic::Runtime
