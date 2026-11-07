module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

module Intrinsic.Plugin.DensityWeights;

import Geometry.PointCloud.Kernels;
import Geometry.PointLBVH;
import Geometry.SupportRadius;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PointCloudEditing;

namespace Intrinsic::Plugins::DensityWeights
{
    namespace
    {
        constexpr const char* kLbvhLimits =
            "LBVH requires the spatial cache, at most 2^24 points, and coordinates/expanded radius within 1e18.";
        constexpr const char* kTooFewSamples = "Density weights require at least one live sample.";

        // The plugin's runs; freed by Shutdown, which discards the GPU previews still waiting.
        R::PluginRunBook<Run> g_Runs{};

        R::ScalarAnalysisBackend BackendOf(const DensityWeightBackend backend)
        {
            switch (backend)
            {
            case DensityWeightBackend::CpuKDTree: return R::ScalarAnalysisBackend::CpuReference;
            case DensityWeightBackend::CpuLBVH: return R::ScalarAnalysisBackend::CpuIndex;
            case DensityWeightBackend::VulkanLBVH: break;
            }
            return R::ScalarAnalysisBackend::Gpu;
        }

        // The method as the removed engine operation named it (texts, the GPU kernel's method 2); `r` carries the
        // resolved radii (none yet: a readiness without the radius-dependent limits).
        R::ScalarAnalysisRequest Request(const Config& c, const Result& r = {})
        {
            return {.StableEntityId = c.StableEntityId,
                    .Positions = c.Positions,
                    .Output = c.Weights,
                    .Params = {.Method = 2,
                               .Capacity = c.GpuRadiusCapacity,
                               .Kernel = std::uint32_t(c.Kernel),
                               .Inverse = std::uint32_t(c.Mode),
                               .QueryRadius = r.QueryRadius,
                               .SupportRadius = r.SupportRadius},
                    .Noun = "Density-weight",
                    .Method = "Density weights",
                    .History = "Estimate density weights",
                    .MinimumLiveCount = 1u,
                    .TooFewSamples = kTooFewSamples};
        }

        std::string Invalid(const Config& c)
        {
            const auto validation =
                R::ValidateDerivedSection<kSection>(R::SerializeDerivedSection<kSection>(c), {}, kSectionName);
            if (validation.Usable()) return {};
            return validation.Diagnostics.empty() ? "Invalid density weight settings."
                                                  : validation.Diagnostics.front().Message;
        }

        // The support radius (0 derives it from `points`) and its conservative query radius into `r`; false with why.
        bool Resolve(const Config& c, std::span<const glm::vec3> points, Result& r, std::string& why)
        {
            namespace SR = Geometry::SupportRadius;
            auto support = c.SupportRadius;
            if (!(support > 0))
            {
                // The LOP-family row of docs/architecture/support-radius-policy.md. The Vulkan candidate capacity is
                // the budget for every backend, so the automatic radius does not depend on the backend.
                const auto a = SR::Analyze(points, std::nullopt,
                                           {.NeighborRank = 16, .Quantile = SR::CoverageQuantile::P75,
                                            .RadiusMultiplier = 1.25, .MinimumNeighborRank = 4},
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
            if (c.Backend != DensityWeightBackend::CpuKDTree && *radius > Geometry::PointLBVH::CoordinateLimit)
            {
                why = kLbvhLimits;
                return false;
            }
            r.SupportRadius = support;
            r.QueryRadius = *radius;
            return true;
        }

        void Fold(Result& r, const R::ScalarAnalysisState& s)
        {
            r.Message = s.Message;
            r.SlotCount = s.SlotCount;
            r.LiveCount = s.LiveCount;
            if (!s.Output.Name.empty()) r.Weights = s.Output; // its resolved domain
            r.ActualBackend = s.GpuQueryBatches ? "vulkan_lbvh" : "";
            r.GpuInputUploadBytes = s.GpuInputUploadBytes;
            r.GpuInputCacheHits = s.GpuInputCacheHits;
            r.CpuStageReadbackBytes = s.CpuStageReadbackBytes;
            r.GpuQueryBatches = s.GpuQueryBatches;
            r.IndexReused = s.IndexReused;
            r.MinWeight = float(s.Statistics.Minimum);
            r.MaxWeight = float(s.Statistics.Maximum);
            r.MaximumNeighbors = s.Statistics.MaximumNeighbors;
            r.Diagnostics.QueryCount = r.LiveCount;
            r.Diagnostics.NeighborContributionCount = s.Statistics.Contributions;
            r.Diagnostics.UsedSuppliedNeighborhoods = true;
        }

        // A CPU run's state; the worker touches it only inside Work, the main thread everywhere else.
        struct Work
        {
            Config Cfg{};
            R::PointCloudEditingService* Service{nullptr};
            std::shared_ptr<R::PointEdit> Edit{};
            std::vector<glm::vec3> Points{};
            std::vector<float> Weights{};
            Result Current{};
            bool Failed{false}, Invalid{false};
            std::shared_ptr<Run> Shared{};
        };

        // Worker: an automatic radius, the radius candidates (the KD-tree's own, or complete rows of the cached LBVH,
        // self excluded, as the removed engine operation queried them) and the weights.
        void Compute(Work& w)
        {
            const auto started = std::chrono::steady_clock::now();
            auto& r = w.Current;
            const auto fail = [&](std::string message)
            {
                w.Failed = true;
                r.Message = std::move(message);
            };
            if (!(r.SupportRadius > 0) && !Resolve(w.Cfg, w.Points, r, r.Message))
            {
                w.Failed = w.Invalid = true;
                return;
            }
            Kernels::DensityWeightResult result;
            if (w.Cfg.Backend == DensityWeightBackend::CpuKDTree)
                result = Kernels::ComputeDensityWeights(w.Points, r.SupportRadius, w.Cfg.Kernel, w.Cfg.Mode);
            else
            {
                const auto* index = w.Service->CpuIndex(*w.Edit);
                if (index == nullptr) return fail("The density weight CPU index is unavailable.");
                std::vector<std::uint32_t> offsets{0u}, indices;
                const auto capacity = std::uint32_t(std::max<std::size_t>(1, w.Points.size() - 1));
                for (std::uint32_t i = 0; i < w.Points.size(); ++i)
                {
                    const auto found = index->Radius(w.Points[i], r.QueryRadius, capacity, i, true);
                    if (found.Neighbors.size() != found.TotalCount) return fail("Incomplete CPU radius candidates.");
                    if (found.Neighbors.size() > std::numeric_limits<std::uint32_t>::max() - indices.size())
                        return fail("Required radius support exceeds the packed neighborhood range.");
                    const auto begin = indices.size();
                    for (const auto& neighbor : found.Neighbors) indices.push_back(neighbor.Index);
                    std::sort(indices.begin() + std::ptrdiff_t(begin), indices.end());
                    r.MaximumNeighbors = std::max(r.MaximumNeighbors, found.Neighbors.size());
                    offsets.push_back(std::uint32_t(indices.size()));
                }
                result = Kernels::ComputeDensityWeightsFromNeighbors(w.Points, {offsets, indices}, r.SupportRadius,
                                                                     w.Cfg.Kernel, w.Cfg.Mode);
            }
            if (!result.Succeeded())
                return fail("Density weights failed: " + std::string(Kernels::DebugName(result.Status)));
            const auto [minimum, maximum] = std::minmax_element(result.Weights.begin(), result.Weights.end());
            r.MinWeight = *minimum;
            r.MaxWeight = *maximum;
            r.Diagnostics = result.Diagnostics;
            w.Weights = std::move(result.Weights);
            r.CpuComputeMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }

        // Main thread: the run's completion.
        bool Complete(Work& w, const bool applied)
        {
            auto& run = *w.Shared;
            if (run.Final) return false;
            auto& r = w.Current;
            if (applied && !w.Failed && !w.Service->SessionCurrent(run.Session))
            {
                // The service no longer works in the run's editor session (a release or a switch of its workspace,
                // or a detach): nothing of it may land in another one.
                r.Status = "cancelled";
                r.Message = "The editor session of the density weight run ended; nothing was written.";
            }
            else if (!applied || w.Failed || !w.Service->Current(*w.Edit))
            {
                // The job gate's check before publication, as the removed engine operation's: a cancel, or an input or
                // the output changed while the run went.
                r.Status = w.Invalid ? "invalid" : w.Failed ? "failed" : !w.Service->Current(*w.Edit) ? "stale"
                                                                                                    : "cancelled";
                if (!w.Failed)
                    r.Message = "Density weights was cancelled or its source became stale; nothing was applied.";
            }
            else
            {
                const std::vector<std::vector<float>> columns{std::move(w.Weights)};
                const auto status = w.Service->PublishScalars(*w.Edit, columns, "Compute compact density weights");
                switch (status)
                {
                case R::PointPublishStatus::Applied:
                    r.Status = "applied";
                    r.WrittenCount = r.LiveCount;
                    r.Message = "Density weights computed with " + r.ActualBackend +
                        " radius candidates and CPU double-precision kernel reduction; support radius " +
                        std::format("{:.4g}.", r.SupportRadius);
                    break;
                case R::PointPublishStatus::Stale:
                    r.Status = "stale";
                    r.Message = "Density-weight input or output changed before publication.";
                    break;
                case R::PointPublishStatus::Unrepresentable:
                    r.Status = "invalid";
                    r.Message = "Output values are not exactly representable in the selected scalar storage.";
                    break;
                case R::PointPublishStatus::Rejected:
                    r.Status = "failed";
                    r.Message = "Density publication rejected by history checks.";
                    break;
                }
            }
            if (run.Handle.IsValid()) R::ReportPluginJobOutcome(run.Handle, R::PluginJobDomain::Cpu, r.Message);
            g_Runs.Finish(run, r);
            // The job's callables keep `w` until the host reaps them: release the copies and the index lease now.
            w.Edit.reset();
            w.Points = std::vector<glm::vec3>{};
            w.Weights = std::vector<float>{};
            return r.Status == "applied";
        }

        // The capture both paths start from, with the method's own checks and texts after the service's.
        std::shared_ptr<R::PointEdit> CaptureInput(const Config& config, R::PointCloudEditingService& service,
                                                   R::PointEditView& view, std::string& why)
        {
            auto edit = service.Capture({.StableEntityId = config.StableEntityId,
                                         .Positions = config.Positions,
                                         .ScalarOutputs = {config.Weights},
                                         .MinimumLiveCount = 0u,
                                         .Noun = "Density-weight"},
                                        why);
            if (edit == nullptr) return nullptr;
            view = service.View(*edit);
            if (view.LiveCount < 1u)
            {
                why = kTooFewSamples;
                return nullptr;
            }
            return edit;
        }

        // A CPU run: the capture, a given radius checked at once, one plugin job.
        void StartCpu(const std::shared_ptr<Run>& run, const Config& config, R::PointCloudEditingService& service)
        {
            auto fail = [&](std::string status, std::string message)
            {
                auto result = run->Pending;
                result.Status = std::move(status);
                result.Message = std::move(message);
                g_Runs.Finish(*run, std::move(result));
            };
            std::string diagnostic;
            R::PointEditView view;
            auto w = std::make_shared<Work>(Work{.Cfg = config, .Service = &service, .Shared = run});
            w->Edit = CaptureInput(config, service, view, diagnostic);
            if (w->Edit == nullptr) return fail("invalid", diagnostic);
            if (config.Backend == DensityWeightBackend::CpuLBVH && (!view.ValidLbvh || view.LiveCount > (1u << 24)))
                return fail("invalid", kLbvhLimits);
            w->Current = run->Pending;
            if (config.SupportRadius > 0 && !Resolve(config, view.Points, w->Current, diagnostic))
                return fail("invalid", diagnostic);
            if (config.Backend == DensityWeightBackend::CpuLBVH)
            {
                if (!service.AcquireIndex(*w->Edit, diagnostic)) return fail("invalid", diagnostic);
                view = service.View(*w->Edit);
            }
            w->Points.assign(view.Points.begin(), view.Points.end());
            w->Current.ActualBackend = ToString(config.Backend);
            w->Current.Weights = view.ScalarOutputs.front();
            w->Current.SlotCount = view.SlotCount;
            w->Current.LiveCount = view.LiveCount;
            w->Current.IndexReused = view.IndexReused;
            run->Pending = w->Current;
            std::string refusal;
            if (!R::SubmitPluginRunStage(*run, {.Label = "Density weights",
                                                .Output = R::PluginJobOutput{.StableEntityId = config.StableEntityId,
                                                                             .Property = w->Current.Weights},
                                                .Domain = R::PluginJobDomain::Cpu,
                                                .Work = [w](const std::function<bool()>&) { Compute(*w); },
                                                .Completion = [w](const bool applied) { return Complete(*w, applied); }},
                                         refusal))
                fail("busy", refusal.empty() ? "The density weight run was not queued." : refusal);
        }

        // A Vulkan run: the radius-independent limits refuse before an automatic radius is derived on this thread.
        // shortcut: the frame-thread analysis is bounded by Vulkan's 2^20-point admission; move it into a worker, as
        // K-Means seeds, if large clouds stall the frame.
        R::ScalarAnalysis StartGpu(Run& run, const Config& config, R::PointCloudEditingService& service,
                                   const bool autoAccept, R::ScalarAnalysisRefusal& refusal)
        {
            auto& r = run.Pending;
            if (!(config.SupportRadius > 0))
            {
                refusal.Message = service.ScalarAnalysisReadiness(Request(config), R::ScalarAnalysisBackend::Gpu, true);
                if (!refusal.Message.empty()) return {};
                R::PointEditView view;
                const auto edit = CaptureInput(config, service, view, refusal.Message);
                if (edit == nullptr || !Resolve(config, view.Points, r, refusal.Message)) return {};
            }
            else if (std::vector<glm::vec3> none; !Resolve(config, none, r, refusal.Message))
                return {};
            return service.StartScalarAnalysis(Request(config, r), autoAccept, refusal);
        }
    }

    const char* ToString(const DensityWeightBackend backend) noexcept
    {
        // The tokens are string literals, so their views end in a NUL.
        return std::size_t(backend) < std::size(kBackendTokens) ? kBackendTokens[std::size_t(backend)].data() : "invalid";
    }

    std::string Preview(const Config& config)
    {
        if (auto invalid = Invalid(config); !invalid.empty()) return invalid;
        auto* service = R::PluginService<R::PointCloudEditingService>();
        if (service == nullptr) return "The point-cloud editing service is unavailable.";
        Result radii;
        if (config.SupportRadius > 0)
            if (std::string why; !Resolve(config, {}, radii, why)) return why;
        return service->ScalarAnalysisReadiness(Request(config, radii), BackendOf(config.Backend));
    }

    std::shared_ptr<Run> Start(const Config& config, const bool autoAccept)
    {
        auto run = std::make_shared<Run>();
        run->Pending = {.Status = "pending", .Message = "Density weights queued.", .RequestedBackend = config.Backend,
                        .Weights = config.Weights};
        auto fail = [&](std::string status, std::string message, const bool busy = false)
        {
            auto result = run->Pending;
            result.Status = std::move(status);
            result.Message = std::move(message);
            run->Final = std::move(result);
            return g_Runs.Keep(run, busy);
        };
        auto* service = R::PluginService<R::PointCloudEditingService>();
        if (service == nullptr) return fail("invalid", "The point-cloud editing service is unavailable.");
        run->Session = service->CurrentSession();
        if (auto invalid = Invalid(config); !invalid.empty()) return fail("invalid", std::move(invalid));
        if (config.Backend != DensityWeightBackend::VulkanLBVH)
        {
            StartCpu(run, config, *service);
            return g_Runs.Keep(run, run->Final && run->Final->Status == "busy");
        }
        R::ScalarAnalysisRefusal refusal;
        run->Analysis = StartGpu(*run, config, *service, autoAccept, refusal);
        if (!run->Analysis)
            return fail(refusal.Busy ? "busy" : refusal.Failed ? "failed" : "invalid", std::move(refusal.Message), refusal.Busy);
        g_Runs.Keep(run);
        Refresh(*run);
        return run;
    }

    void Refresh(Run& run) { R::RefreshScalarPluginRun(g_Runs, run, Fold); }

    void Accept(Run& run) { R::AcceptScalarPluginRun(g_Runs, run, Fold); }

    bool Cancel(Run& run) { return R::CancelScalarPluginRun(g_Runs, run, Fold); }

    std::shared_ptr<Run> Latest() { return R::LatestScalarPluginRun(g_Runs); }

    std::span<const std::shared_ptr<Run>> Active() { return g_Runs.Active(); }

    void Dismiss(const std::shared_ptr<Run>& run) { g_Runs.Dismiss(run); }

    void ShutdownRuns() noexcept { g_Runs.Shutdown(); }
}
