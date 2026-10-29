// Point spacing, declared once (RUNTIME-349, ADR 0031 Decision 8): its config section derived from the
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
module Extrinsic.Runtime.PointSpacingConfig;
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
        constexpr std::string_view kPointSpacingBackend[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
        constexpr std::string_view kPointSpacingBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
        constexpr std::array kPointSpacingRows{
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
                .Description =
                    "Nonnegative radius scale factor; positive values must remain positive in float storage.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max()),
                .Refusal = "Positive scale factor must remain positive in float storage."}),
            Bind<&PointSpacingConfig::Positions>({.Name = "positions",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Position and radii bindings must have distinct names on the same element domain.",
                .RefKinds = kVec3Kind}),
            Bind<&PointSpacingConfig::Radii>({.Name = "radii",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Radii.",
                .RefKinds = kFloatKind,
                .AnyScalar = true}),
        };
        constexpr auto kPointSpacingFields = SpecsOf(kPointSpacingRows);
    }
    extern "C++" std::span<const BoundField<PointSpacingConfig>> PointSpacingConfigRows() noexcept
    {
        return kPointSpacingRows;
    }
}

// The section's exported functions keep global-module attachment, as their interface declares them.
extern "C++"
{
    namespace Extrinsic::Runtime
    {
        namespace
        {
            constexpr ConfigDetail::DerivedConfigSection<PointSpacingConfig> kPointSpacingSection{
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
                },
                .Validate = &ValidatePointSpacingConfigSection};
            static_assert(ConfigDetail::IsDerivable(kPointSpacingSection));
        }
    // The row's tokens, so the codec and the owner's names cannot drift apart.
        const char* ToString(PointSpacingBackend b) noexcept
        {
            const auto i = std::to_underlying(b);
            return i < std::size(kPointSpacingBackend) ? kPointSpacingBackend[i].data() : "invalid";
        }
        std::string SerializePointSpacingConfig(const PointSpacingConfig& c)
        {
            return ConfigDetail::SerializeDerivedConfig(kPointSpacingSection, c);
        }
        Core::Config::EngineConfigSectionValidationResult ValidatePointSpacingConfigSection(
            std::string_view payload, std::string_view, std::string_view subject)
        {
            return ConfigDetail::ValidateDerivedConfig(kPointSpacingSection, payload, subject);
        }
        std::optional<PointSpacingConfig> GetPointSpacingConfig(const Core::Config::EngineConfig& c)
        {
            return ConfigDetail::GetDerivedConfig(kPointSpacingSection, c);
        }
        void SetPointSpacingConfig(Core::Config::EngineConfig& c, const PointSpacingConfig& value)
        {
            ConfigDetail::SetDerivedConfig(kPointSpacingSection, c, value);
        }
        std::span<const ConfigFieldSpec> PointSpacingConfigFieldSpecs() noexcept
        {
            return kPointSpacingFields;
        }
        Core::Config::EngineConfigSectionRegistration MakePointSpacingConfigSectionRegistration()
        {
            return ConfigDetail::MakeDerivedRegistration(kPointSpacingSection);
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

        struct PointSpacingDefinition
        {
            using Operation = PointSpacingOperation;
            using Params = PointSpacingConfig;
            using Result = EditorPointSpacingResult;
            using Backend = PointSpacingBackend;
            static constexpr std::string_view Noun{"Radii"}, Method{"Point spacing"},
                History{"Estimate point spacing and radii"}, Evaluation{"CPU spacing/radius evaluation"},
                Failure{"Point spacing failed: invalid neighborhoods or unrepresentable float distances/radii."};
            static constexpr std::size_t MinimumK = 1; // Query min(n,max(k,1)+1) including self.
            static GeometryPropertyRef& Output(Params& c)
            {
                return c.Radii;
            }
            static Result InitialResult(const Params& c)
            {
                return {.RequestedBackend = c.Backend, .Radii = c.Radii};
            }
            static Core::Config::EngineConfigSectionValidationResult Validate(const Params& c)
            {
                return ValidatePointSpacingConfigSection(
                    SerializePointSpacingConfig(c), {}, kPointSpacingConfigSectionName);
            }
            static Graphics::PointScalarGpuParams GpuParams(const Params& c)
            {
                return {.Method = 1, .K = c.KNeighbors, .Scale = c.ScaleFactor};
            }
            static void FoldGpuStats(Result& r, const Graphics::PointScalarGpuStats& s)
            {
                r.MeanRadius = s.Mean;
                r.MinRadius = s.Minimum;
                r.MaxRadius = s.Maximum;
                r.AverageSpacing = s.AverageSpacing;
                r.MinSpacing = s.MinSpacing;
                r.MaxSpacing = s.MaxSpacing;
                r.BoundingBoxDiagonal = s.Diagonal;
                r.Centroid = {s.CentroidX, s.CentroidY, s.CentroidZ};
            }
            static std::optional<std::vector<float>> Compute(const Params& c, const PointSamplesInput input, Result& r)
            {
                const PC::RadiusEstimationParams params{.KNeighbors = c.KNeighbors, .ScaleFactor = c.ScaleFactor};
                auto analysis = input.Neighbors ? PC::EstimateRadiiFromNeighbors(input.Points, *input.Neighbors, params)
                                                : PC::EstimateRadii(input.Points, params);
                if (!analysis || analysis->Radii.size() != input.Points.size())
                    return std::nullopt;
                r.MeanRadius = analysis->AverageRadius;
                r.MinRadius = analysis->MinRadius;
                r.MaxRadius = analysis->MaxRadius;
                r.Centroid = analysis->Statistics.Centroid;
                r.AverageSpacing = analysis->Statistics.AverageSpacing;
                r.MinSpacing = analysis->Statistics.MinSpacing;
                r.MaxSpacing = analysis->Statistics.MaxSpacing;
                r.BoundingBoxDiagonal = analysis->Statistics.BoundingBoxDiagonal;
                return std::move(analysis->Radii);
            }
        };
        using PointSpacingRuntime = OperationRuntime<PointSpacingDefinition>;
    }
    EditorPointScalarTransactionHandle StartEditorPointSpacingTransaction(const EditorProcessingCommands& commands,
        const PointSpacingConfig& config,
        EditorPointSpacingResult& result,
        std::function<void(EditorPointSpacingResult)> sink)
    {
        return PointSpacingRuntime::StartTransaction(commands, config, result, std::move(sink));
    }
    EditorPointScalarTransactionHandle MakeEditorPointSpacingTransactionForTest(
        const EditorProcessingCommands& commands,
        const PointSpacingConfig& config,
        std::vector<float> values,
        Graphics::GpuPropertyResidency& residency,
        const EditorPointScalarTransactionSnapshot& diagnostics)
    {
        return PointSpacingRuntime::MakeTransactionForTest(commands, config, std::move(values), residency, diagnostics);
    }
    ActionReadiness PreviewEditorPointSpacingCommand(
        const EditorProcessingCommands& commands, const PointSpacingConfig& config)
    {
        return PointSpacingRuntime::Preview(commands, config);
    }
    GeometryPropertyCatalogSnapshot GetEditorPointSpacingInputCatalog(
        const EditorProcessingCommands& commands, std::uint32_t id)
    {
        return PointSpacingRuntime::InputCatalog(commands, id);
    }
    EditorPointSpacingResult ApplyEditorPointSpacingCommand(const EditorProcessingCommands& commands,
        const PointSpacingConfig& config,
        std::function<void(EditorPointSpacingResult)> onComplete)
    {
        return PointSpacingRuntime::Apply(commands, config, std::move(onComplete));
    }
    EditorPointSpacingResult ApplyEditorConfiguredPointSpacing(
        const EditorProcessingCommands& commands, std::function<void(EditorPointSpacingResult)> onComplete)
    {
        return PointSpacingRuntime::ApplyConfigured(
            commands, GetEditorPointSpacingConfig(commands), std::move(onComplete));
    }
    RuntimeEngineConfigApplyResult ApplyEditorPointSpacingConfig(
        const EditorProcessingCommands& commands, const PointSpacingConfig& config, std::string sourceId)
    {
        return ApplyOperationConfig(commands,
            ValidatePointSpacingConfigSection(SerializePointSpacingConfig(config), {}, kPointSpacingConfigSectionName),
            sourceId.empty() ? std::string{kPointSpacingConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate)
            {
                SetPointSpacingConfig(candidate, config);
            });
    }
    std::optional<PointSpacingConfig> GetEditorPointSpacingConfig(const EditorProcessingCommands& commands)
    {
        const Core::Config::EngineConfig* active = ActiveOperationConfig(commands);
        return active ? GetPointSpacingConfig(*active) : std::nullopt;
    }
} // namespace Extrinsic::Runtime
