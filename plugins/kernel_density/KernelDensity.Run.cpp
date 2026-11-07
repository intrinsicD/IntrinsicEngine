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

module Intrinsic.Plugin.KernelDensity;

import Geometry.PointCloud.Utils;
import Geometry.PointLBVH;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PointCloudEditing;

namespace Intrinsic::Plugins::KernelDensity
{
    namespace
    {
        namespace PC = Geometry::PointCloud;

        // The plugin's runs; freed by Shutdown, which discards the GPU previews still waiting.
        R::PluginRunBook<Run> g_Runs{};

        R::ScalarAnalysisBackend BackendOf(const DensityBackend backend)
        {
            switch (backend)
            {
            case DensityBackend::CpuOctree: return R::ScalarAnalysisBackend::CpuReference;
            case DensityBackend::CpuLBVH: return R::ScalarAnalysisBackend::CpuIndex;
            case DensityBackend::VulkanLBVH: break;
            }
            return R::ScalarAnalysisBackend::Gpu;
        }

        // The method as the operation runtime named it (texts, the GPU kernel's method 0).
        R::ScalarAnalysisRequest Request(const Config& c)
        {
            return {.StableEntityId = c.StableEntityId,
                    .Positions = c.Positions,
                    .Output = c.Density,
                    .Params = {.Method = 0, .K = c.KNeighbors, .Bandwidth = c.Bandwidth},
                    .Noun = "Density",
                    .Method = "Kernel density",
                    .History = "Estimate kernel density"};
        }

        std::string Invalid(const Config& c)
        {
            const auto validation =
                R::ValidateDerivedSection<kSection>(R::SerializeDerivedSection<kSection>(c), {}, kSectionName);
            if (validation.Usable()) return {};
            return validation.Diagnostics.empty() ? "Invalid kernel density settings." : validation.Diagnostics.front().Message;
        }

        void FoldStatistics(Result& r, const PC::KDEResult& analysis)
        {
            r.UsedBandwidth = analysis.UsedBandwidth;
            r.MeanDensity = analysis.MeanDensity;
            r.MinDensity = analysis.MinDensity;
            r.MaxDensity = analysis.MaxDensity;
        }

        void Fold(Result& r, const R::ScalarAnalysisState& s)
        {
            r.Message = s.Message;
            r.SlotCount = s.SlotCount;
            r.LiveCount = s.LiveCount;
            if (!s.Output.Name.empty()) r.Density = s.Output; // its resolved domain
            r.ActualBackend = s.GpuQueryBatches ? "vulkan_lbvh" : "";
            r.GpuInputUploadBytes = s.GpuInputUploadBytes;
            r.GpuInputCacheHits = s.GpuInputCacheHits;
            r.CpuStageReadbackBytes = s.CpuStageReadbackBytes;
            r.GpuQueryBatches = s.GpuQueryBatches;
            r.IndexReused = s.IndexReused;
            r.UsedBandwidth = float(s.Statistics.Bandwidth);
            r.MeanDensity = float(s.Statistics.Mean);
            r.MinDensity = float(s.Statistics.Minimum);
            r.MaxDensity = float(s.Statistics.Maximum);
        }

        // A CPU run's state; the worker touches it only inside Work, the main thread everywhere else.
        struct Work
        {
            Config Cfg{};
            R::PointCloudEditingService* Service{nullptr};
            std::shared_ptr<R::PointEdit> Edit{};
            std::vector<glm::vec3> Points{};
            std::vector<float> Densities{};
            Result Current{};
            bool Failed{false};
            std::shared_ptr<Run> Shared{};
        };

        // Worker: the neighbourhoods (the octree's own, or max(k,2)+1 rows of the cached LBVH, self included, as the
        // editor's LBVH runs query them) and the densities.
        void Compute(Work& w)
        {
            const auto started = std::chrono::steady_clock::now();
            const PC::KDEParams params{.KNeighbors = w.Cfg.KNeighbors, .Bandwidth = w.Cfg.Bandwidth};
            std::optional<PC::KDEResult> analysis;
            if (w.Cfg.Backend == DensityBackend::CpuOctree)
                analysis = PC::EstimateKernelDensity(w.Points, params);
            else
            {
                const auto* index = w.Service->CpuIndex(*w.Edit);
                const auto width = std::uint32_t(
                    std::min<std::size_t>(w.Points.size(), std::max<std::size_t>(w.Cfg.KNeighbors, 2) + 1));
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
                    w.Current.Message = "The kernel density CPU index is unavailable.";
                    return;
                }
                analysis = PC::EstimateKernelDensityFromNeighbors(w.Points, rows, params);
            }
            if (!analysis || analysis->Densities.size() != w.Points.size())
            {
                w.Failed = true;
                w.Current.Message =
                    "Kernel density failed: invalid neighborhoods or unrepresentable float kernel values/bandwidth.";
                return;
            }
            FoldStatistics(w.Current, *analysis);
            w.Densities = std::move(analysis->Densities);
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
                r.Message = "The editor session of the kernel density run ended; nothing was written.";
            }
            else if (!applied || w.Failed || !w.Service->Current(*w.Edit))
            {
                // The job gate's check before publication, as the removed engine operation's: a cancel, or an input or
                // the output changed while the run went.
                r.Status = w.Failed ? "failed" : !w.Service->Current(*w.Edit) ? "stale" : "cancelled";
                if (!w.Failed) r.Message = "Density estimation was cancelled or its source became stale; nothing was applied.";
            }
            else
            {
                const std::vector<std::vector<float>> columns{std::move(w.Densities)};
                const auto status = w.Service->PublishScalars(*w.Edit, columns, Request(w.Cfg).History);
                switch (status)
                {
                case R::PointPublishStatus::Applied:
                    r.Status = "applied";
                    r.WrittenCount = r.LiveCount;
                    r.Message = "Density computed using " + r.ActualBackend +
                        " neighborhoods and CPU bandwidth/Gaussian evaluation.";
                    break;
                case R::PointPublishStatus::Stale:
                    r.Status = "stale";
                    r.Message = "Density input or output changed before publication.";
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
            w.Densities = std::vector<float>{};
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
                                       .ScalarOutputs = {config.Density},
                                       .MinimumLiveCount = 0u,
                                       .Noun = "Density"},
                                      diagnostic);
            if (w->Edit == nullptr) return fail("invalid", diagnostic);
            auto view = service.View(*w->Edit);
            if (view.LiveCount < 2u) return fail("invalid", "Kernel density requires at least two live samples.");
            if (config.Backend == DensityBackend::CpuLBVH)
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
            w->Current.Density = view.ScalarOutputs.front();
            w->Current.SlotCount = view.SlotCount;
            w->Current.LiveCount = view.LiveCount;
            w->Current.IndexReused = view.IndexReused;
            run->Pending = w->Current;
            std::string refusal;
            if (!R::SubmitPluginRunStage(*run, {.Label = "Density estimation",
                                                .Output = R::PluginJobOutput{.StableEntityId = config.StableEntityId,
                                                                             .Property = w->Current.Density},
                                                .Domain = R::PluginJobDomain::Cpu,
                                                .Work = [w](const std::function<bool()>&) { Compute(*w); },
                                                .Completion = [w](const bool applied) { return Complete(*w, applied); }},
                                         refusal))
                fail("busy", refusal.empty() ? "The kernel density run was not queued." : refusal);
        }
    }

    const char* ToString(const DensityBackend backend) noexcept
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
        run->Pending = {.Status = "pending", .Message = "Density estimation queued.", .RequestedBackend = config.Backend,
                        .Density = config.Density};
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
        if (config.Backend != DensityBackend::VulkanLBVH)
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
