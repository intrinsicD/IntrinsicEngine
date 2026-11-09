module;

#include <algorithm>
#include <atomic>
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

module Intrinsic.Plugin.BilateralFilter;

import Geometry.PointCloud.Utils;
import Geometry.PointLBVH;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.StepRuns;

namespace Intrinsic::Plugins::BilateralFilter
{
    namespace
    {
        namespace PC = Geometry::PointCloud;

        std::size_t Width(const std::size_t count, const std::size_t k) { return std::min(count - 1, k) + 1; }

        // One run's state. The worker touches the filter state (points, rows, params, Current) only inside a step (a
        // Vulkan step pages its kNN rows there too, ARCH-023); Valid, Show and Discard use only the capture, the
        // service and Gpu's abort (a step may run then); Commit, Preview and Restart run while no step does.
        struct Work
        {
            Config Cfg{};
            R::PointCloudEditingService* Service{nullptr};
            R::StepRunsService* Runs{nullptr};
            std::shared_ptr<R::PointEdit> Edit{};
            std::vector<glm::vec3> Source{}, Points{}, Normals{}; // Source: the captured positions (a restart's)
            std::vector<std::uint32_t> Rows{};
            PC::BilateralFilterParams Params{};
            bool Prepared{false}; // the bandwidth is resolved (the first step does it, on a worker)
            bool InPlace{false}; // output = positions: live preview can show the moving points
            Result Current{};
            std::atomic_bool Failed{false};
            std::shared_ptr<Run> Shared{};
            std::uint64_t RunId{0u};
            std::shared_ptr<R::PluginJobHandle> Holder{std::make_shared<R::PluginJobHandle>()};
            // Vulkan: the worker rows prepared at the start (the cached index first, then the moved points).
            std::shared_ptr<R::PointGpuRows> Gpu{};
        };

        std::shared_ptr<Run>* g_LastRun{nullptr}; // freed by Shutdown

        // The one terminal outcome of a run: the window and a waiting MCP call read it, the Jobs window too. Called
        // only while no step runs (Commit, or the settled end). With `settled` (Commit, or a failed step) the run's
        // diagnostics; else (a discard, cancel or stale end: nothing was written) the start's.
        void Finish(Work& w, std::string status, std::string message, const bool settled)
        {
            if (w.Shared->Final) return;
            Result r = settled ? w.Current : w.Shared->Pending;
            r.Status = std::move(status);
            if (!message.empty())
                r.Message = std::move(message);
            const auto stats = w.Gpu ? R::GpuRowStats(*w.Gpu) : R::PointGpuRowStats{};
            r.GpuQueryBatches = stats.QueryBatches;
            r.WorkspaceBuilds += stats.WorkspaceBuilds;
            r.GpuNeighborhoodMilliseconds = stats.Milliseconds;
            r.GpuQueryUploadBytes = stats.QueryUploadBytes;
            r.GpuWorkspaceUploadBytes = stats.WorkspaceUploadBytes;
            r.GpuReadbackBytes = stats.ReadbackBytes;
            r.StepRun = w.RunId;
            w.Shared->Final = std::move(r);
            if (w.Shared->Handle.IsValid())
                R::ReportPluginJobOutcome(w.Shared->Handle,
                    IsVulkan(w.Cfg.Backend) && w.Cfg.Execution.IterationCap > 0u ? R::PluginJobDomain::Gpu
                                                                                 : R::PluginJobDomain::Cpu,
                    w.Shared->Final->Message);
        }

        bool Fail(Work& w, std::string why)
        {
            w.Current.Message = std::move(why);
            w.Failed.store(true, std::memory_order_release);
            return false;
        }

        // Worker, once per run: the automatic bandwidth (of the captured positions, so a restart keeps it).
        bool Prepare(Work& w)
        {
            w.Params = {.KNeighbors = w.Cfg.KNeighbors, .SpatialSigma = w.Cfg.SpatialSigma,
                        .NormalSigma = w.Cfg.NormalSigma, .Iterations = 1};
            if (w.Params.SpatialSigma <= 0)
            {
                const auto stats = PC::ComputeStatistics(
                    w.Source, {.SpacingSampleCount = std::min(w.Source.size(), std::size_t{500})});
                if (!stats)
                    return Fail(w, "Automatic bilateral bandwidth failed.");
                w.Params.SpatialSigma = stats->AverageSpacing > 0 ? 2.f * stats->AverageSpacing : .01f;
            }
            w.Current.SpatialSigmaUsed = w.Params.SpatialSigma;
            w.Prepared = true;
            return true;
        }

        // Worker: one pass. The octree builds its own neighbourhoods; the LBVH backends use `Rows`: the device's (paged
        // here, the cached index on the first pass, then the moved points), or the cached CPU index on the first pass
        // and a rebuilt one on later passes. False: failed (Failed set), or a Vulkan pass the run's discard aborted.
        bool Step(Work& w)
        {
            if (!w.Prepared && !Prepare(w))
                return false;
            const auto started = std::chrono::steady_clock::now();
            std::optional<PC::BilateralFilterOutput> filtered;
            if (w.Cfg.Backend == FilterBackend::CpuOctree)
                filtered = PC::BilateralFilter(w.Points, w.Normals, w.Params);
            else
            {
                if (w.Cfg.Backend == FilterBackend::CpuLBVH)
                {
                    Geometry::PointLBVH::Index rebuilt;
                    const auto* index = w.Service->CpuIndex(*w.Edit);
                    if (w.Current.CompletedIterations > 0)
                    {
                        if (!rebuilt.Build(w.Points))
                            return Fail(w, "Moving points exceed LBVH limits.");
                        index = &rebuilt;
                        ++w.Current.WorkspaceBuilds;
                    }
                    if (index == nullptr)
                        return Fail(w, "Bilateral CPU index is unavailable.");
                    const auto width = Width(w.Points.size(), w.Cfg.KNeighbors);
                    w.Rows.clear();
                    w.Rows.reserve(w.Points.size() * width);
                    for (const auto& point : w.Points)
                    {
                        const auto row = index->KNearest(point, std::uint32_t(width));
                        if (row.size() != width)
                            return Fail(w, "Incomplete CPU bilateral neighborhood.");
                        for (const auto& neighbor : row) w.Rows.push_back(neighbor.Index);
                    }
                }
                else
                {
                    std::string diagnostic;
                    const auto state = R::ComputeGpuKnnRows(*w.Gpu, w.Points, w.Rows, diagnostic);
                    if (state == R::PointRowsState::Pending)
                        return false; // discarded: the run ended, nothing to report
                    if (state == R::PointRowsState::Failed)
                        return Fail(w, diagnostic);
                }
                filtered = PC::BilateralFilterStepFromNeighbors(w.Points, w.Normals, w.Rows, w.Params);
            }
            if (!filtered)
                return Fail(w, "Bilateral filtering failed: invalid neighborhoods or unrepresentable float updates.");
            w.Points = std::move(filtered->Positions);
            w.Current.PointsFiltered = filtered->Diagnostics.PointsFiltered;
            w.Current.DegenerateNormals = filtered->Diagnostics.DegenerateNormals;
            w.Current.AverageDisplacement = filtered->Diagnostics.AverageDisplacement;
            w.Current.MaxDisplacement = filtered->Diagnostics.MaxDisplacement;
            ++w.Current.CompletedIterations;
            w.Current.CpuComputeMilliseconds +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            return true;
        }

        void FillMetric(const Work& w, R::StepRunMetric& metric)
        {
            metric.Values = {w.Current.AverageDisplacement,
                w.Current.MaxDisplacement,
                double(w.Current.PointsFiltered),
                double(w.Current.DegenerateNormals)};
        }

        // Main thread: shows `points` as the positions' ring front (an in-place output only).
        R::StepRunShow ShowPoints(Work& w, const std::span<const glm::vec3> points)
        {
            std::string diagnostic;
            switch (w.Service->PreviewPositions(*w.Edit, points, diagnostic, w.Cfg.Execution.PreviewBuffers))
            {
            case R::PointPreviewStatus::Shown:       return R::StepRunShow::Shown;
            case R::PointPreviewStatus::Busy:        return R::StepRunShow::Later;
            case R::PointPreviewStatus::Unavailable: break; // no device, a non-finite value: nothing to show
            }
            return R::StepRunShow::Never;
        }

        // Main thread, the run's Accept.
        R::StepRunCommit Publish(Work& w, std::string& diagnostic)
        {
            const auto status = w.Service->Publish(*w.Edit, w.Points, "Bilateral point filter");
            if (status == R::PointPublishStatus::Applied)
            {
                w.Current.WrittenCount = w.Points.size();
                Finish(w,
                    "applied",
                    "Bilateral point filtering completed using " + w.Current.ActualBackend +
                        " neighborhoods and CPU updates with fixed normals.",
                    true);
                return R::StepRunCommit::Committed;
            }
            diagnostic = status == R::PointPublishStatus::Stale
                ? "Bilateral input or output changed before publication."
                : "Bilateral publication rejected by history checks.";
            Finish(w, status == R::PointPublishStatus::Stale ? "stale" : "failed", diagnostic, true);
            return R::StepRunCommit::Refused;
        }

        // Main thread, the settled end (StepRunCallbacks::Finished: the step returned and the holder released the
        // output, so a new run there is admitted from here on): why, from the run's final observation. After a
        // Commit its answer is already the result (Finish keeps the first).
        void Ended(Work& w, const R::StepRunObservation& run)
        {
            const bool failed = w.Failed.load(std::memory_order_acquire);
            Finish(w,
                std::string{R::UncommittedStatus(run)},
                failed                        ? w.Current.Message
                    : !run.Diagnostic.empty() ? "The bilateral run ended (" + run.Diagnostic + "); nothing was written."
                                              : std::string{},
                failed);
        }

        // Capture and the backend's admission limits; nullptr with the reason.
        std::shared_ptr<Work> Capture(const Config& config, const bool execute, std::string& diagnostic)
        {
            const auto validation = R::ValidateDerivedSection<kSection>(R::SerializeDerivedSection<kSection>(config), {},
                                                                        kSectionName);
            if (!validation.Usable())
            {
                diagnostic = validation.Diagnostics.empty() ? "Invalid bilateral settings." : validation.Diagnostics.front().Message;
                return {};
            }
            auto* service = R::PluginService<R::PointCloudEditingService>();
            auto* runs = R::PluginService<R::StepRunsService>();
            if (service == nullptr || runs == nullptr)
            {
                diagnostic = "The point-cloud editing or step-run service is unavailable.";
                return {};
            }
            const bool indexed = config.Backend != FilterBackend::CpuOctree;
            auto w = std::make_shared<Work>();
            w->Cfg = config;
            w->Service = service;
            w->Runs = runs;
            w->Edit = service->Capture({.StableEntityId = config.StableEntityId,
                                           .Positions = config.Positions,
                                           .Normals = config.Normals,
                                           .Output = config.Output,
                                           .MinimumLiveCount = 2u,
                                           .CopyValues = execute,
                                           .AcquireIndex = execute && indexed,
                                           .CpuIndex = !IsVulkan(config.Backend)}, // GPU rows build no CPU hierarchy
                diagnostic);
            if (w->Edit == nullptr) return {};
            const auto view = service->View(*w->Edit);
            if (indexed && (!view.ValidLbvh || view.LiveCount > (1u << 24)))
            {
                diagnostic = "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.";
                return {};
            }
            if (IsVulkan(config.Backend))
            {
                // The largest page against every live sample; device-independent, so checked first.
                if (config.Backend == FilterBackend::VulkanBruteForce && view.LiveCount <= (1u << 20))
                    if (const auto refusal = Extrinsic::Graphics::PointBruteForceRefusal(
                            std::uint32_t(std::min<std::size_t>(config.GpuQueryBatchSize, view.LiveCount)),
                            std::uint32_t(view.LiveCount),
                            std::uint32_t(Width(view.LiveCount, config.KNeighbors)));
                        !refusal.empty())
                    {
                        diagnostic =
                            std::string(refusal) + " Lower gpu_query_batch_size or k_neighbors, or use vulkan_lbvh.";
                        return {};
                    }
                if (!service->GpuQueriesAvailable())
                {
                    diagnostic = "Vulkan bilateral filter neighborhoods require the GPU spatial cache and job service.";
                    return {};
                }
                if (view.LiveCount > (1u << 20) || config.KNeighbors > 63)
                {
                    diagnostic = "Vulkan bilateral filter queries support at most 2^20 live samples and k<=63 (64 "
                                 "candidates including self).";
                    return {};
                }
            }
            w->Source.assign(view.Points.begin(), view.Points.end());
            w->Points = w->Source;
            w->Normals.assign(view.PointNormals.begin(), view.PointNormals.end());
            w->InPlace = view.Output.Domain == config.Positions.Domain && view.Output.Name == config.Positions.Name;
            w->Current = {.RequestedBackend = config.Backend,
                .ActualBackend = ToString(config.Backend),
                .Output = view.Output,
                .SlotCount = view.SlotCount,
                .LiveCount = view.LiveCount,
                .IndexReused = view.IndexReused};
            return w;
        }
    }

    const char* ToString(const FilterBackend backend) noexcept
    {
        // The tokens are string literals, so their views end in a NUL.
        return std::size_t(backend) < std::size(kBackendTokens) ? kBackendTokens[std::size_t(backend)].data() : "invalid";
    }

    std::string Preview(const Config& config)
    {
        std::string diagnostic;
        return Capture(config, false, diagnostic) ? std::string{} : diagnostic;
    }

    std::shared_ptr<Run> LastRun() noexcept
    {
        return g_LastRun != nullptr ? *g_LastRun : nullptr;
    }

    void ShutdownRuns() noexcept
    {
        delete g_LastRun;
        g_LastRun = nullptr;
    }

    std::shared_ptr<Run> Start(const Config& config)
    {
        auto run = std::make_shared<Run>();
        run->Pending = {.Status = "pending",
            .Message = "Bilateral filtering is a step run.",
            .RequestedBackend = config.Backend,
            .Output = config.Output};
        const auto refuse = [&](std::string status, std::string why)
        {
            run->Final = run->Pending;
            run->Final->Status = std::move(status);
            run->Final->Message = std::move(why);
            return run;
        };
        std::string diagnostic;
        const auto w = Capture(config, true, diagnostic);
        if (w == nullptr)
            return refuse("invalid", diagnostic);
        w->Shared = run;
        run->Pending = w->Current;
        run->Pending.Status = "pending";

        if (IsVulkan(config.Backend) && config.Execution.IterationCap > 0u)
        {
            // The device query sessions, once (resident index, buffers, pipelines, the moving target's storage).
            w->Gpu = w->Service->PrepareGpuRows(*w->Edit,
                {.Strategy = config.Backend == FilterBackend::VulkanBruteForce
                        ? Extrinsic::Graphics::PointQueryStrategy::BruteForce
                        : Extrinsic::Graphics::PointQueryStrategy::Lbvh,
                    .BatchSize = config.GpuQueryBatchSize,
                    .Width = std::uint32_t(Width(w->Points.size(), config.KNeighbors)),
                    .Moving = true},
                diagnostic);
            if (!w->Gpu)
                return refuse("failed", "Vulkan bilateral neighborhoods could not be prepared: " + diagnostic);
        }
        R::StepRunCallbacks callbacks{
            .Valid =
                [w]
            {
                return w->Service->Current(*w->Edit);
            },
            .Commit =
                [w](std::string& diagnostic)
            {
                return Publish(*w, diagnostic);
            },
            // At once, also while a step runs: only the main thread's (the result waits for Finished).
            .Discard =
                [w]
            {
                if (w->Gpu)
                    R::AbortGpuRows(*w->Gpu); // a waiting page gives up; the step returns
                w->Service->DiscardPreview(*w->Edit);
            },
            // run_control restart (no step runs then): the captured positions again, the bandwidth kept; the device
            // sessions stay (the next pass uploads the positions to the moving target).
            .Restart =
                [w](std::string&)
            {
                w->Points = w->Source;
                w->Rows.clear();
                w->Current.CompletedIterations = 0u;
                return true;
            },
            .Finished =
                [w](const R::StepRunObservation& run)
            {
                Ended(*w, run);
            },
        };
        // Every backend steps on a worker; a Vulkan step pages its device rows there, then updates on the CPU.
        callbacks.Step = [w](R::StepRunMetric& metric)
        {
            if (!Step(*w))
                return R::StepOutcome::Failed;
            FillMetric(*w, metric);
            return R::StepOutcome::Continue; // a fixed number of passes: the cap ends the run
        };
        if (w->InPlace)
        {
            callbacks.Preview = [w](R::StepPreview& preview)
            {
                auto& values = preview.Values;
                values.resize(w->Points.size() * 3u);
                for (std::size_t i = 0; i < w->Points.size(); ++i)
                    for (int c = 0; c < 3; ++c)
                        values[3 * i + std::size_t(c)] = w->Points[i][c];
                return true;
            };
            callbacks.Show = [w](const R::StepPreview& preview)
            {
                std::vector<glm::vec3> points(preview.Values.size() / 3u);
                for (std::size_t i = 0; i < points.size(); ++i)
                    points[i] = {preview.Values[3 * i], preview.Values[3 * i + 1], preview.Values[3 * i + 2]};
                return ShowPoints(*w, points);
            };
        }
        R::StepRunOptions options{.Label = "Bilateral point filter",
            .Owner = "bilateral_filter",
            .Output = {.StableEntityId = config.StableEntityId, .Property = w->Current.Output},
            .Execution = config.Execution,
            .DevicePreview = w->InPlace,
            .MetricNames = {"mean_displacement", "max_displacement", "points_filtered", "degenerate_normals"}};
        std::string refusal;
        w->RunId = w->Runs->Start(std::move(options), std::move(callbacks), R::PluginStepRunJobs(w->Holder), refusal);
        // Another run owns the output, or the host refused the job: a failed start, not invalid settings.
        if (w->RunId == 0u)
            return refuse("failed", refusal.empty() ? "The bilateral run was refused." : refusal);
        run->Handle = *w->Holder;
        run->Pending.StepRun = w->RunId;
        if (g_LastRun == nullptr)
            g_LastRun = new std::shared_ptr<Run>();
        *g_LastRun = run;
        return run;
    }
}
