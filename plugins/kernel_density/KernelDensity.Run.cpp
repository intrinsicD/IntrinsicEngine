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
#include <string_view>
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

        // A CPU run's state; the worker touches it (Current, through Compute) only inside the step, the main thread
        // everywhere else.
        struct Work
        {
            Config Cfg{};
            R::PointCloudEditingService* Service{nullptr};
            std::shared_ptr<R::PointEdit> Edit{};
            std::vector<glm::vec3> Points{};
            Result Initial{}, Current{}; // Initial: as captured (a restart's step starts from it)
            std::shared_ptr<Run> Shared{};
        };

        // Worker, the one step: the neighbourhoods (the octree's own, or max(k,2)+1 rows of the cached LBVH, self
        // included, as the editor's LBVH runs query them) and the densities into `densities`; false with the reason
        // in Current.
        bool Compute(Work& w, std::vector<float>& densities)
        {
            const auto started = std::chrono::steady_clock::now();
            w.Current = w.Initial;
            const auto fail = [&](const char* why)
            {
                w.Current.Message = why;
                return false;
            };
            const PC::KDEParams params{.KNeighbors = w.Cfg.KNeighbors, .Bandwidth = w.Cfg.Bandwidth};
            std::optional<PC::KDEResult> analysis;
            if (w.Cfg.Backend == DensityBackend::CpuOctree)
                analysis = PC::EstimateKernelDensity(w.Points, params);
            else
            {
                const auto* index = w.Service->CpuIndex(*w.Edit);
                if (index == nullptr)
                    return fail("The kernel density CPU index is unavailable.");
                const auto width = std::uint32_t(
                    std::min<std::size_t>(w.Points.size(), std::max<std::size_t>(w.Cfg.KNeighbors, 2) + 1));
                std::vector<std::uint32_t> rows;
                rows.reserve(w.Points.size() * width);
                for (const auto& point : w.Points)
                {
                    const auto row = index->KNearest(point, width, Geometry::PointLBVH::InvalidIndex, true);
                    if (row.size() != width)
                        return fail("Incomplete CPU kNN neighborhood.");
                    for (const auto& neighbor : row) rows.push_back(neighbor.Index);
                }
                analysis = PC::EstimateKernelDensityFromNeighbors(w.Points, rows, params);
            }
            if (!analysis || analysis->Densities.size() != w.Points.size())
                return fail(
                    "Kernel density failed: invalid neighborhoods or unrepresentable float kernel values/bandwidth.");
            FoldStatistics(w.Current, *analysis);
            densities = std::move(analysis->Densities);
            w.Current.CpuComputeMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            return true;
        }

        // Main thread, the run's one settled end (ScalarCpuRun::Finish; the host frees `w` when the run retires).
        void Finish(Work& w, const std::string_view status, std::string message, const bool ownResult)
        {
            auto r = ownResult ? w.Current : w.Shared->Pending;
            if (status == "applied")
            {
                r.WrittenCount = r.LiveCount;
                message = "Density computed using " + r.ActualBackend +
                    " neighborhoods and CPU bandwidth/Gaussian evaluation.";
            }
            R::FinishScalarCpuPluginRun(g_Runs, *w.Shared, std::move(r), status, std::move(message));
        }

        // A CPU run: the capture (with the method's own checks and texts after the service's), then the host's
        // one-shot step run.
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
            auto& r = w->Initial;
            r = run->Pending;
            r.ActualBackend = ToString(config.Backend);
            r.Density = view.ScalarOutputs.front();
            r.SlotCount = view.SlotCount;
            r.LiveCount = view.LiveCount;
            r.IndexReused = view.IndexReused;
            w->Current = r;
            run->Pending = r;
            std::string refusal;
            const auto holder = std::make_shared<R::PluginJobHandle>();
            const auto id = service.StartScalarCpuRun(
                {.Edit = w->Edit,
                    .Session = run->Session,
                    .Output = {.StableEntityId = config.StableEntityId, .Property = r.Density},
                    .Label = "Density estimation",
                    .History = Request(config).History,
                    .Noun = "Density",
                    .Method = "kernel density",
                    .Execution = config.Execution,
                    .Compute =
                        [w](std::vector<float>& densities)
                    {
                        return Compute(*w, densities);
                    },
                    .Finish =
                        [w](const std::string_view status, std::string message, const bool ownResult)
                    {
                        Finish(*w, status, std::move(message), ownResult);
                    }},
                R::PluginStepRunJobs(holder),
                refusal);
            if (id == 0u)
                return fail("busy", refusal.empty() ? "The kernel density run was not queued." : refusal);
            run->Handle = *holder;
            run->Pending.StepRun = id;
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

    std::shared_ptr<Run> Start(const Config& config)
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
        run->Analysis = service->StartScalarAnalysis(Request(config), config.Execution, refusal);
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
