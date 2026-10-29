// Kernel density on the operation runtime (RUNTIME-349, ADR 0031 Decision 8): its definition and entry
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
module Extrinsic.Runtime.KernelDensityConfig;
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
    template std::string SerializeDerivedSection<kKernelDensitySection>(const KernelDensityConfig&);
    template Core::Config::EngineConfigSectionValidationResult ValidateDerivedSection<kKernelDensitySection>(
        std::string_view, std::string_view, std::string_view);
    template std::optional<KernelDensityConfig> GetDerivedSection<kKernelDensitySection>(
        const Core::Config::EngineConfig&);
    template void SetDerivedSection<kKernelDensitySection>(Core::Config::EngineConfig&, const KernelDensityConfig&);
    template Core::Config::EngineConfigSectionRegistration MakeDerivedSectionRegistration<kKernelDensitySection>();
}

// The backend's name is the row's token, so the codec and the owner's names cannot drift apart; it
// keeps global-module attachment, as its interface declares it.
extern "C++"
{
    namespace Extrinsic::Runtime
    {
        const char* ToString(KernelDensityBackend b) noexcept
        {
            const auto i = std::to_underlying(b);
            return i < std::size(kKernelDensityBackend) ? kKernelDensityBackend[i].data() : "invalid";
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
                return ValidateDerivedSection<kKernelDensitySection>(
                    SerializeDerivedSection<kKernelDensitySection>(c), {}, kKernelDensityConfigSectionName);
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
            ValidateDerivedSection<kKernelDensitySection>(
                SerializeDerivedSection<kKernelDensitySection>(config), {}, kKernelDensityConfigSectionName),
            sourceId.empty() ? std::string{kKernelDensityConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate)
            {
                SetDerivedSection<kKernelDensitySection>(candidate, config);
            });
    }
    std::optional<KernelDensityConfig> GetEditorKernelDensityConfig(const EditorProcessingCommands& commands)
    {
        const Core::Config::EngineConfig* active = ActiveOperationConfig(commands);
        return active ? GetDerivedSection<kKernelDensitySection>(*active) : std::nullopt;
    }
} // namespace Extrinsic::Runtime
