// Point spacing on the operation runtime (RUNTIME-349, ADR 0031 Decision 8): its definition and entry
// points; its config section is declared in the interface (RUNTIME-351).
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
module Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.OperationRuntime;
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.PointCloud.Utils;

namespace Extrinsic::Runtime
{
    template std::string SerializeDerivedSection<kPointSpacingSection>(const PointSpacingConfig&);
    template Core::Config::EngineConfigSectionValidationResult ValidateDerivedSection<kPointSpacingSection>(
        std::string_view, std::string_view, std::string_view);
    template std::optional<PointSpacingConfig> GetDerivedSection<kPointSpacingSection>(
        const Core::Config::EngineConfig&);
    template void SetDerivedSection<kPointSpacingSection>(Core::Config::EngineConfig&, const PointSpacingConfig&);
    template Core::Config::EngineConfigSectionRegistration MakeDerivedSectionRegistration<kPointSpacingSection>();
}

// The backend's name is the row's token, so the codec and the owner's names cannot drift apart; it
// keeps global-module attachment, as its interface declares it.
extern "C++"
{
    namespace Extrinsic::Runtime
    {
        const char* ToString(PointSpacingBackend b) noexcept
        {
            const auto i = std::to_underlying(b);
            return i < std::size(kPointSpacingBackend) ? kPointSpacingBackend[i].data() : "invalid";
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
                return ValidateDerivedSection<kPointSpacingSection>(
                    SerializeDerivedSection<kPointSpacingSection>(c), {}, kPointSpacingConfigSectionName);
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
            ValidateDerivedSection<kPointSpacingSection>(
                SerializeDerivedSection<kPointSpacingSection>(config), {}, kPointSpacingConfigSectionName),
            sourceId.empty() ? std::string{kPointSpacingConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate)
            {
                SetDerivedSection<kPointSpacingSection>(candidate, config);
            });
    }
    std::optional<PointSpacingConfig> GetEditorPointSpacingConfig(const EditorProcessingCommands& commands)
    {
        const Core::Config::EngineConfig* active = ActiveOperationConfig(commands);
        return active ? GetDerivedSection<kPointSpacingSection>(*active) : std::nullopt;
    }
} // namespace Extrinsic::Runtime
