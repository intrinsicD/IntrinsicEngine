// Compact density weights on the operation runtime (RUNTIME-353): the definition keeps every message,
// status and delivery rule of its former editor copy; the runtime runs the lifecycle.
module;
#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/vec3.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.DensityWeightOperation;
import Extrinsic.Runtime.OperationRuntime;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.PointCloud.Kernels;
import Geometry.PointLBVH;
import Geometry.SupportRadius;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Kernels = Geometry::PointCloud::Kernels;
        using GeometryProcessingDetail::OperationRuntime;
        using GeometryProcessingDetail::OperationWording;
        using GeometryProcessingDetail::PointSamplesInput;

        constexpr const char* kLbvhLimits =
            "LBVH requires the spatial cache, at most 2^24 points, and coordinates/expanded radius within 1e18.";

        struct DensityWeightDefinition
        {
            using Operation = DensityWeightOperation;
            using Params = DensityWeightConfig;
            using Result = EditorDensityWeightResult;
            using Backend = DensityWeightBackend;
            static constexpr Backend ReferenceBackend = Backend::CpuKDTree;
            static constexpr std::uint32_t MinimumLiveCount = 1;
            static constexpr bool InlineDeliversToSink = false, FinalizeFromPending = true;
            static constexpr std::string_view Noun{"Density"}, History{"Compute compact density weights"};
            static OperationWording Wording(OperationWording)
            {
                return {.StaleTarget = "Density-weight target is stale or missing.",
                    .OutputLabel = "Weight",
                    .TooFewSamples = "Density weights require at least one live sample.",
                    .LbvhLimits = kLbvhLimits,
                    .IndexMismatch = "Density index does not match selected samples.",
                    .ConfigUnavailable = "Density-weight config is unavailable.",
                    .JobLabel = "Density weights",
                    .JobName = "Compact density weights",
                    .GpuHistory = "Estimate density weights"};
            }
            static GeometryPropertyRef& Output(Params& c)
            {
                return c.Weights;
            }
            static Result InitialResult(const Params& c)
            {
                return {.RequestedBackend = c.Backend, .Weights = c.Weights};
            }
            static void RefuseInPlace(Result& r, const Params& c)
            {
                r.RequestedBackend = c.Backend;
                r.Weights = c.Weights;
            }
            static Core::Config::EngineConfigSectionValidationResult Validate(const Params& c)
            {
                return ValidateDensityWeightConfigSection(
                    SerializeDensityWeightConfig(c), {}, kDensityWeightConfigSectionName);
            }
            static Graphics::PointScalarGpuParams GpuParams(const Params& c, const Result& r)
            {
                return {.Method = 2,
                    .Capacity = c.GpuRadiusCapacity,
                    .Kernel = std::uint32_t(c.Kernel),
                    .Inverse = std::uint32_t(c.Mode),
                    .QueryRadius = r.QueryRadius,
                    .SupportRadius = r.SupportRadius};
            }
            static void FoldGpuStats(Result& r, const Graphics::PointScalarGpuStats& s)
            {
                r.MinWeight = s.Minimum;
                r.MaxWeight = s.Maximum;
                r.MaximumNeighbors = s.MaximumNeighbors;
                r.Diagnostics.QueryCount = r.LiveCount;
                r.Diagnostics.NeighborContributionCount = s.Contributions;
                r.Diagnostics.UsedSuppliedNeighborhoods = true;
            }
            // Resolves the support radius (0 derives it from the captured positions) and its conservative query radius.
            static bool Resolve(const Params& c, std::span<const glm::vec3> points, Result& r, std::string& why)
            {
                namespace SR = Geometry::SupportRadius;
                auto support = c.SupportRadius;
                if (!(support > 0))
                {
                    // The LOP-family row of docs/architecture/support-radius-policy.md. The Vulkan candidate capacity
                    // is the budget for every backend, so the automatic radius does not depend on the backend.
                    const auto a = SR::Analyze(points,
                        std::nullopt,
                        {.NeighborRank = 16,
                            .Quantile = SR::CoverageQuantile::P75,
                            .RadiusMultiplier = 1.25,
                            .MinimumNeighborRank = 4},
                        {.MaxNeighborsPerSample = c.GpuRadiusCapacity});
                    if (!a.Succeeded())
                    {
                        why = "Automatic support radius failed (" + std::string(SR::DebugName(a.State)) +
                            "); set a positive support radius.";
                        return false;
                    }
                    support = a.Radius;
                }
                const auto radius = Kernels::ConservativeQueryRadius(support);
                if (!radius)
                {
                    why = "Invalid density support radius.";
                    return false;
                }
                if (c.Backend != Backend::CpuKDTree && *radius > Geometry::PointLBVH::CoordinateLimit)
                {
                    why = kLbvhLimits;
                    return false;
                }
                r.SupportRadius = support;
                r.QueryRadius = *radius;
                return true;
            }
            // CPU backends resolve an automatic radius on the job worker; Vulkan dispatches from this thread.
            // shortcut: the frame-thread analysis is bounded by Vulkan's 2^20-point admission; move it into a
            // prepare job, as keypoints do, if large clouds stall the frame.
            static bool Prepare(const Params& c,
                std::span<const glm::vec3> points,
                Result& r,
                bool execute,
                const std::function<bool()>& admit,
                std::string& why)
            {
                const bool analyze = !(c.SupportRadius > 0) && execute && c.Backend == Backend::VulkanLBVH;
                // Radius-independent Vulkan limits refuse before the analysis; the runtime admits the resolved radius.
                if (analyze && !admit())
                    return false;
                return !(c.SupportRadius > 0 || analyze) || Resolve(c, points, r, why);
            }
            static std::optional<std::vector<float>> Compute(const Params& c, const PointSamplesInput input, Result& r)
            {
                if (!(r.SupportRadius > 0) && !Resolve(c, input.Points, r, r.Message))
                {
                    r.Status =
                        EditorCommandStatus::InvalidProcessingParameters; // The status Vulkan reports from Capture.
                    return std::nullopt;
                }
                Kernels::DensityWeightResult result;
                if (c.Backend == Backend::CpuKDTree)
                    result = Kernels::ComputeDensityWeights(input.Points, r.SupportRadius, c.Kernel, c.Mode);
                else
                {
                    GeometryProcessingDetail::PointRadiusSupport rows;
                    if (!GeometryProcessingDetail::QueryPointRadiusRows(input, r.QueryRadius, rows, r.Message))
                        return std::nullopt;
                    r.MaximumNeighbors = rows.MaximumNeighbors;
                    result = Kernels::ComputeDensityWeightsFromNeighbors(
                        input.Points, {rows.Offsets, rows.Indices}, r.SupportRadius, c.Kernel, c.Mode);
                }
                if (!result.Succeeded())
                {
                    r.Message = "Density weights failed: " + std::string(Kernels::DebugName(result.Status));
                    return std::nullopt;
                }
                const auto [minimum, maximum] = std::minmax_element(result.Weights.begin(), result.Weights.end());
                r.MinWeight = *minimum;
                r.MaxWeight = *maximum;
                r.Diagnostics = result.Diagnostics;
                r.Message = "Density weights computed with " + r.ActualBackend +
                    " radius candidates and CPU double-precision kernel reduction; support radius " +
                    std::format("{:.4g}.", r.SupportRadius);
                return std::move(result.Weights);
            }
        };
        using DensityWeightRuntime = OperationRuntime<DensityWeightDefinition>;
    }
    EditorPointScalarTransactionHandle StartEditorDensityWeightTransaction(const EditorProcessingCommands& commands,
        const DensityWeightConfig& config,
        EditorDensityWeightResult& result,
        std::function<void(EditorDensityWeightResult)> sink)
    {
        return DensityWeightRuntime::StartTransaction(commands, config, result, std::move(sink));
    }
    EditorPointScalarTransactionHandle MakeEditorDensityWeightTransactionForTest(
        const EditorProcessingCommands& commands,
        const DensityWeightConfig& config,
        std::vector<float> values,
        Graphics::GpuPropertyResidency& residency,
        const EditorPointScalarTransactionSnapshot& diagnostics)
    {
        return DensityWeightRuntime::MakeTransactionForTest(
            commands, config, std::move(values), residency, diagnostics);
    }
    ActionReadiness PreviewEditorDensityWeightCommand(
        const EditorProcessingCommands& commands, const DensityWeightConfig& config)
    {
        return DensityWeightRuntime::Preview(commands, config);
    }
    EditorDensityWeightResult ApplyEditorDensityWeightCommand(const EditorProcessingCommands& commands,
        const DensityWeightConfig& config,
        std::function<void(EditorDensityWeightResult)> onComplete)
    {
        return DensityWeightRuntime::Apply(commands, config, std::move(onComplete));
    }
    EditorDensityWeightResult ApplyEditorConfiguredDensityWeight(
        const EditorProcessingCommands& commands, std::function<void(EditorDensityWeightResult)> onComplete)
    {
        return DensityWeightRuntime::ApplyConfigured(
            commands, GetEditorDensityWeightConfig(commands), std::move(onComplete));
    }
    RuntimeEngineConfigApplyResult ApplyEditorDensityWeightConfig(
        const EditorProcessingCommands& commands, const DensityWeightConfig& config, std::string sourceId)
    {
        return ApplyOperationConfig(commands,
            DensityWeightDefinition::Validate(config),
            sourceId.empty() ? std::string{kDensityWeightConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate)
            {
                SetDensityWeightConfig(candidate, config);
            });
    }
    std::optional<DensityWeightConfig> GetEditorDensityWeightConfig(const EditorProcessingCommands& commands)
    {
        const Core::Config::EngineConfig* active = ActiveOperationConfig(commands);
        return active ? GetDensityWeightConfig(*active) : std::nullopt;
    }
} // namespace Extrinsic::Runtime
