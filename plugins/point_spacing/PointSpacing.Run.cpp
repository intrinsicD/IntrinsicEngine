module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

module Intrinsic.Plugin.PointSpacing;

import Geometry.PointCloud.Utils;
import Geometry.PointLBVH;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PointCloudEditing;

namespace Intrinsic::Plugins::PointSpacing
{
    namespace
    {
        namespace PC = Geometry::PointCloud;

        // The plugin's runs; freed by Shutdown, which discards the GPU previews still waiting.
        R::PluginRunBook<Run> g_Runs{};

        R::ScalarAnalysisBackend BackendOf(const SpacingBackend backend)
        {
            switch (backend)
            {
            case SpacingBackend::CpuOctree: return R::ScalarAnalysisBackend::CpuReference;
            case SpacingBackend::CpuLBVH: return R::ScalarAnalysisBackend::CpuIndex;
            case SpacingBackend::VulkanLBVH: break;
            }
            return R::ScalarAnalysisBackend::Gpu;
        }

        // The method as the operation runtime named it (texts, the GPU kernel's method 1).
        R::ScalarAnalysisRequest Request(const Config& c)
        {
            return {.StableEntityId = c.StableEntityId,
                    .Positions = c.Positions,
                    .Output = c.Radii,
                    .Params = {.Method = 1, .K = c.KNeighbors, .Scale = c.ScaleFactor},
                    .Noun = "Radii",
                    .Method = "Point spacing",
                    .History = "Estimate point spacing and radii"};
        }

        std::string Invalid(const Config& c)
        {
            const auto validation =
                R::ValidateDerivedSection<kSection>(R::SerializeDerivedSection<kSection>(c), {}, kSectionName);
            if (validation.Usable()) return {};
            return validation.Diagnostics.empty() ? "Invalid point spacing settings." : validation.Diagnostics.front().Message;
        }

        void FoldStatistics(Result& r, const PC::RadiusEstimationResult& analysis)
        {
            r.MeanRadius = analysis.AverageRadius;
            r.MinRadius = analysis.MinRadius;
            r.MaxRadius = analysis.MaxRadius;
            r.Centroid = analysis.Statistics.Centroid;
            r.AverageSpacing = analysis.Statistics.AverageSpacing;
            r.MinSpacing = analysis.Statistics.MinSpacing;
            r.MaxSpacing = analysis.Statistics.MaxSpacing;
            r.BoundingBoxDiagonal = analysis.Statistics.BoundingBoxDiagonal;
        }

        void Fold(Result& r, const R::ScalarAnalysisState& s)
        {
            r.Message = s.Message;
            r.SlotCount = s.SlotCount;
            r.LiveCount = s.LiveCount;
            if (!s.Output.Name.empty()) r.Radii = s.Output; // its resolved domain
            r.ActualBackend = s.GpuQueryBatches ? "vulkan_lbvh" : "";
            r.GpuInputUploadBytes = s.GpuInputUploadBytes;
            r.GpuInputCacheHits = s.GpuInputCacheHits;
            r.CpuStageReadbackBytes = s.CpuStageReadbackBytes;
            r.GpuQueryBatches = s.GpuQueryBatches;
            r.IndexReused = s.IndexReused;
            r.MeanRadius = float(s.Statistics.Mean);
            r.MinRadius = float(s.Statistics.Minimum);
            r.MaxRadius = float(s.Statistics.Maximum);
            r.AverageSpacing = float(s.Statistics.AverageSpacing);
            r.MinSpacing = float(s.Statistics.MinSpacing);
            r.MaxSpacing = float(s.Statistics.MaxSpacing);
            r.BoundingBoxDiagonal = float(s.Statistics.Diagonal);
            r.Centroid = {float(s.Statistics.CentroidX), float(s.Statistics.CentroidY), float(s.Statistics.CentroidZ)};
        }

        // A CPU run's state; the worker touches it only inside Work, the main thread everywhere else.
        struct Work
        {
            Config Cfg{};
            R::PointCloudEditingService* Service{nullptr};
            std::shared_ptr<R::PointEdit> Edit{};
            std::vector<glm::vec3> Points{};
            std::vector<float> Radii{};
            Result Current{};
            bool Failed{false};
            std::shared_ptr<Run> Shared{};
        };

        // Worker: the neighbourhoods (the octree's own, or k+1 rows of the cached LBVH, self included, as the
        // editor's LBVH runs query them) and the radii.
        void Compute(Work& w)
        {
            const auto started = std::chrono::steady_clock::now();
            const PC::RadiusEstimationParams params{.KNeighbors = w.Cfg.KNeighbors, .ScaleFactor = w.Cfg.ScaleFactor};
            std::optional<PC::RadiusEstimationResult> analysis;
            if (w.Cfg.Backend == SpacingBackend::CpuOctree)
                analysis = PC::EstimateRadii(w.Points, params);
            else
            {
                const auto* index = w.Service->CpuIndex(*w.Edit);
                const auto width = std::uint32_t(
                    std::min<std::size_t>(w.Points.size(), std::max<std::size_t>(w.Cfg.KNeighbors, 1) + 1));
                std::vector<std::uint32_t> rows;
                rows.reserve(w.Points.size() * width);
                for (const auto& point : w.Points)
                {
                    if (index == nullptr) break;
                    const auto row = index->KNearest(point, width, Geometry::PointLBVH::InvalidIndex, true);
                    if (row.size() != width)
                    {
                        w.Failed = true;
                        w.Current.Message = "Incomplete CPU kNN neighborhood.";
                        return;
                    }
                    for (const auto& neighbor : row) rows.push_back(neighbor.Index);
                }
                if (index == nullptr)
                {
                    w.Failed = true;
                    w.Current.Message = "The point spacing CPU index is unavailable.";
                    return;
                }
                analysis = PC::EstimateRadiiFromNeighbors(w.Points, rows, params);
            }
            if (!analysis || analysis->Radii.size() != w.Points.size())
            {
                w.Failed = true;
                w.Current.Message = "Point spacing failed: invalid neighborhoods or unrepresentable float distances/radii.";
                return;
            }
            FoldStatistics(w.Current, *analysis);
            w.Radii = std::move(analysis->Radii);
            w.Current.CpuComputeMilliseconds =
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
                r.Message = "The editor session of the point spacing run ended; nothing was written.";
            }
            else if (!applied || w.Failed || !w.Service->Current(*w.Edit))
            {
                // The job gate's check before publication, as the removed engine operation's: a cancel, or an input or
                // the output changed while the run went.
                r.Status = w.Failed ? "failed" : !w.Service->Current(*w.Edit) ? "stale" : "cancelled";
                if (!w.Failed) r.Message = "Radii estimation was cancelled or its source became stale; nothing was applied.";
            }
            else
            {
                const std::vector<std::vector<float>> columns{std::move(w.Radii)};
                const auto status = w.Service->PublishScalars(*w.Edit, columns, Request(w.Cfg).History);
                switch (status)
                {
                case R::PointPublishStatus::Applied:
                    r.Status = "applied";
                    r.WrittenCount = r.LiveCount;
                    r.Message = "Radii computed using " + r.ActualBackend +
                        " neighborhoods and CPU spacing/radius evaluation.";
                    break;
                case R::PointPublishStatus::Stale:
                    r.Status = "stale";
                    r.Message = "Radii input or output changed before publication.";
                    break;
                case R::PointPublishStatus::Unrepresentable:
                    r.Status = "invalid";
                    r.Message = "Output values are not exactly representable in the selected scalar storage.";
                    break;
                case R::PointPublishStatus::Rejected:
                    r.Status = "failed";
                    r.Message = "Radii publication rejected by history checks.";
                    break;
                }
            }
            if (run.Handle.IsValid()) R::ReportPluginJobOutcome(run.Handle, R::PluginJobDomain::Cpu, r.Message);
            g_Runs.Finish(run, r);
            // The job's callables keep `w` until the host reaps them: release the copies and the index lease now.
            w.Edit.reset();
            w.Points = std::vector<glm::vec3>{};
            w.Radii = std::vector<float>{};
            return r.Status == "applied";
        }

        // A CPU run: the capture (with the method's own checks and texts after the service's), one plugin job.
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
            auto w = std::make_shared<Work>(Work{.Cfg = config, .Service = &service, .Shared = run});
            // The index after the method's own checks, so their texts come first (as the readiness answers).
            w->Edit = service.Capture({.StableEntityId = config.StableEntityId,
                                       .Positions = config.Positions,
                                       .ScalarOutputs = {config.Radii},
                                       .MinimumLiveCount = 0u,
                                       .Noun = "Radii"},
                                      diagnostic);
            if (w->Edit == nullptr) return fail("invalid", diagnostic);
            auto view = service.View(*w->Edit);
            if (view.LiveCount < 2u) return fail("invalid", "Point spacing requires at least two live samples.");
            if (config.Backend == SpacingBackend::CpuLBVH)
            {
                if (!view.ValidLbvh || view.LiveCount > (1u << 24))
                    return fail("invalid",
                                "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.");
                if (!service.AcquireIndex(*w->Edit, diagnostic)) return fail("invalid", diagnostic);
                view = service.View(*w->Edit);
            }
            w->Points.assign(view.Points.begin(), view.Points.end());
            w->Current = run->Pending;
            w->Current.ActualBackend = ToString(config.Backend);
            w->Current.Radii = view.ScalarOutputs.front();
            w->Current.SlotCount = view.SlotCount;
            w->Current.LiveCount = view.LiveCount;
            w->Current.IndexReused = view.IndexReused;
            run->Pending = w->Current;
            std::string refusal;
            if (!R::SubmitPluginRunStage(*run, {.Label = "Radii estimation",
                                                .Output = R::PluginJobOutput{.StableEntityId = config.StableEntityId,
                                                                             .Property = w->Current.Radii},
                                                .Domain = R::PluginJobDomain::Cpu,
                                                .Work = [w](const std::function<bool()>&) { Compute(*w); },
                                                .Completion = [w](const bool applied) { return Complete(*w, applied); }},
                                         refusal))
                fail("busy", refusal.empty() ? "The point spacing run was not queued." : refusal);
        }
    }

    const char* ToString(const SpacingBackend backend) noexcept
    {
        // The tokens are string literals, so their views end in a NUL.
        return std::size_t(backend) < std::size(kBackendTokens) ? kBackendTokens[std::size_t(backend)].data() : "invalid";
    }

    std::string Preview(const Config& config)
    {
        if (auto invalid = Invalid(config); !invalid.empty()) return invalid;
        auto* service = R::PluginService<R::PointCloudEditingService>();
        if (service == nullptr) return "The point-cloud editing service is unavailable.";
        return service->ScalarAnalysisReadiness(Request(config), BackendOf(config.Backend));
    }

    std::shared_ptr<Run> Start(const Config& config, const bool autoAccept)
    {
        auto run = std::make_shared<Run>();
        run->Pending = {.Status = "pending", .Message = "Radii estimation queued.", .RequestedBackend = config.Backend,
                        .Radii = config.Radii};
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
        if (config.Backend != SpacingBackend::VulkanLBVH)
        {
            StartCpu(run, config, *service);
            return g_Runs.Keep(run, run->Final && run->Final->Status == "busy");
        }
        R::ScalarAnalysisRefusal refusal;
        run->Analysis = service->StartScalarAnalysis(Request(config), autoAccept, refusal);
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
