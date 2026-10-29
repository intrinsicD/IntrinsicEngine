module;
#include <string_view>
#include <functional>
#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <glm/vec3.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.PointScalarAnalysis;
import Geometry.PointCloud.Utils;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Geometry.Properties;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
#include "Editor/Operations/Runtime.OperationRuntime.hpp"

// Kernel density and point spacing each publish one kNN-derived scalar per live sample. Each is a
// definition the operation runtime drives (RUNTIME-347): names, config, statistics, self-neighbor
// floor and a pure Compute; capture, index admission, jobs and publication are the runtime's.
namespace Extrinsic::Runtime
{
    namespace
    {
        namespace PC = Geometry::PointCloud;
        using GeometryProcessingDetail::OperationRuntime;
        using GeometryProcessingDetail::PointSamplesInput;

        struct KernelDensityOperation
        {
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
                r.UsedBandwidth = analysis->UsedBandwidth; r.MeanDensity = analysis->MeanDensity;
                r.MinDensity = analysis->MinDensity; r.MaxDensity = analysis->MaxDensity;
                return std::move(analysis->Densities);
            }
        };
        struct PointSpacingOperation
        {
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
                r.MeanRadius = analysis->AverageRadius; r.MinRadius = analysis->MinRadius;
                r.MaxRadius = analysis->MaxRadius; r.Centroid = analysis->Statistics.Centroid;
                r.AverageSpacing = analysis->Statistics.AverageSpacing;
                r.MinSpacing = analysis->Statistics.MinSpacing;
                r.MaxSpacing = analysis->Statistics.MaxSpacing;
                r.BoundingBoxDiagonal = analysis->Statistics.BoundingBoxDiagonal;
                return std::move(analysis->Radii);
            }
        };
        using KernelDensityRuntime = OperationRuntime<KernelDensityOperation>;
        using PointSpacingRuntime = OperationRuntime<PointSpacingOperation>;
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
        const KernelDensityConfig& config, std::function<void(EditorKernelDensityResult)> onComplete)
    {
        return KernelDensityRuntime::Apply(commands, config, std::move(onComplete));
    }
    EditorKernelDensityResult ApplyEditorConfiguredKernelDensity(
        const EditorProcessingCommands& commands, std::function<void(EditorKernelDensityResult)> onComplete)
    {
        return KernelDensityRuntime::ApplyConfigured(
            commands, GetEditorKernelDensityConfig(commands), std::move(onComplete));
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
        const PointSpacingConfig& config, std::function<void(EditorPointSpacingResult)> onComplete)
    {
        return PointSpacingRuntime::Apply(commands, config, std::move(onComplete));
    }
    EditorPointSpacingResult ApplyEditorConfiguredPointSpacing(
        const EditorProcessingCommands& commands, std::function<void(EditorPointSpacingResult)> onComplete)
    {
        return PointSpacingRuntime::ApplyConfigured(
            commands, GetEditorPointSpacingConfig(commands), std::move(onComplete));
    }
} // namespace Extrinsic::Runtime
