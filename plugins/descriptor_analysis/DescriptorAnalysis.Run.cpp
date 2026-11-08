module;

#include <algorithm>
#include <array>
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

module Intrinsic.Plugin.DescriptorAnalysis;

import Geometry.PointCloud.Features;
import Geometry.PointLBVH;
import Geometry.SpatialQueries;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.StepRuns;

namespace Intrinsic::Plugins::DescriptorAnalysis
{
    namespace
    {
        namespace Features = Geometry::PointCloud::Features;

        // One run's state. A worker touches the analysis state (rows, columns, Current) only inside the step (a Vulkan
        // step pages its radius rows there too, ARCH-023); Valid and Abort use only the capture, the service and Gpu's
        // abort (a step may run then); Publish, Restart and Ended run while no step does (Ended at the settled end).
        struct Work
        {
            Config Cfg{};
            R::PointCloudEditingService* Service{nullptr};
            R::StepRunsService* Runs{nullptr};
            std::shared_ptr<R::PointEdit> Edit{};
            std::vector<glm::vec3> Points{}, Normals{};
            R::PointNeighbourRows Rows{};
            std::vector<std::vector<float>> Columns{}; // one per bin, one value per live point
            bool Prepared{false}; // the scale is resolved (it stays for a restart)
            Result Current{};
            std::shared_ptr<Run> Shared{};
            std::uint64_t RunId{0u};
            std::shared_ptr<R::PluginJobHandle> Holder{std::make_shared<R::PluginJobHandle>()};
            std::shared_ptr<R::PointGpuRows> Gpu{}; // Vulkan: the worker radius rows prepared at the start
        };

        std::shared_ptr<Run>* g_LastRun{nullptr}; // freed by ShutdownRuns

        Features::DescriptorParams Parameters(const Config& c)
        {
            return {.FeatureRadius = c.FeatureRadius, .MaxNeighbors = c.MaxNeighbors};
        }

        double MillisecondsSince(const std::chrono::steady_clock::time_point started)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }

        // The one terminal outcome of a run (at Accept, or at the settled end): the window and a waiting MCP call read
        // it, the Jobs window too. With `ownResult` (a publication's answer, or the step failing itself) the run's
        // diagnostics; else (a discard, cancel or stale capture) the start's.
        void Finish(Work& w, std::string status, std::string message, const bool ownResult)
        {
            if (w.Shared->Final) return;
            Result r = ownResult ? w.Current : w.Shared->Pending;
            r.Status = std::move(status);
            if (!message.empty())
                r.Message = std::move(message);
            if (w.Cfg.Backend == DescriptorBackend::VulkanLBVH)
            {
                const auto stats = w.Gpu ? R::GpuRowStats(*w.Gpu) : R::PointGpuRowStats{};
                r.GpuQueryBatches = stats.QueryBatches;
                r.GpuNeighborhoodMilliseconds = stats.Milliseconds;
                r.GpuQueryUploadBytes = stats.QueryUploadBytes;
                r.GpuReadbackBytes = stats.ReadbackBytes;
            }
            r.StepRun = w.RunId;
            w.Shared->Final = std::move(r);
            if (w.Shared->Handle.IsValid())
                R::ReportPluginJobOutcome(w.Shared->Handle,
                    w.Cfg.Backend == DescriptorBackend::VulkanLBVH ? R::PluginJobDomain::Gpu : R::PluginJobDomain::Cpu,
                    w.Shared->Final->Message);
        }

        bool Fail(Work& w, std::string why)
        {
            w.Current.Message = std::move(why);
            return false;
        }

        // Worker: the descriptor scale (mean spacing, and the radius when automatic).
        bool Prepare(Work& w)
        {
            const auto started = std::chrono::steady_clock::now();
            const auto scale = Features::ResolveDescriptorScale(w.Points, Parameters(w.Cfg));
            if (!scale) return Fail(w, "Descriptor scale requires positive spacing and representable radius.");
            if (w.Cfg.Backend != DescriptorBackend::CpuKDTree &&
                scale->FeatureRadius > Geometry::PointLBVH::CoordinateLimit)
                return Fail(w, "Resolved descriptor radius exceeds the LBVH range.");
            w.Current.MeanSpacing = scale->MeanSpacing;
            w.Current.FeatureRadius = scale->FeatureRadius;
            w.Current.CpuComputeMilliseconds += MillisecondsSince(started);
            w.Prepared = true;
            return true;
        }

        // Worker: the histograms. The k-d tree finds its own neighbourhoods; the LBVH backends use `Rows`: the device's
        // (paged here), or rows from the cached CPU index made here. False: failed (the reason in Current), or a Vulkan
        // step the run's discard aborted.
        bool Compute(Work& w)
        {
            w.Current.ActualBackend = ToString(w.Cfg.Backend);
            if (w.Cfg.Backend != DescriptorBackend::CpuKDTree && !w.Prepared && !Prepare(w))
                return false;
            if (w.Cfg.Backend == DescriptorBackend::VulkanLBVH)
            {
                std::string diagnostic;
                const auto state = R::ComputeGpuRadiusRows(*w.Gpu, w.Current.FeatureRadius, w.Rows, diagnostic);
                w.Current.MaximumNeighbors = w.Rows.MaximumNeighbors;
                if (state == R::PointRowsState::Pending)
                    return false; // discarded: the run ended, nothing to report
                if (state == R::PointRowsState::Failed)
                    return Fail(w, diagnostic);
            }
            const auto started = std::chrono::steady_clock::now();
            std::optional<Features::DescriptorSet> analysis;
            if (w.Cfg.Backend == DescriptorBackend::CpuKDTree)
                analysis = Features::ComputeDescriptors(w.Points, w.Normals, {}, Parameters(w.Cfg));
            else
            {
                if (w.Cfg.Backend == DescriptorBackend::CpuLBVH)
                {
                    std::string diagnostic;
                    const bool complete = w.Service->CpuRadiusRows(*w.Edit, w.Current.FeatureRadius, w.Cfg.MaxNeighbors,
                                                                   w.Rows, diagnostic);
                    w.Current.MaximumNeighbors = w.Rows.MaximumNeighbors;
                    if (!complete) return Fail(w, diagnostic);
                }
                analysis = Features::ComputeDescriptorsFromNeighbors(
                    w.Points, w.Normals, {}, Parameters(w.Cfg),
                    {.MeanSpacing = w.Current.MeanSpacing, .FeatureRadius = w.Current.FeatureRadius},
                    Geometry::PointNeighborhoods{.Offsets = w.Rows.Offsets, .Indices = w.Rows.Indices});
            }
            if (!analysis) return Fail(w, "Descriptor analysis rejected invalid support or invalid normals.");
            w.Columns.assign(kBins, std::vector<float>(w.Points.size()));
            for (std::uint32_t i = 0; i < w.Points.size(); ++i)
            {
                const auto row = analysis->Row(i);
                for (std::size_t b = 0; b < kBins; ++b) w.Columns[b][i] = row[b];
            }
            w.Current.MeanSpacing = analysis->Scale.MeanSpacing;
            w.Current.FeatureRadius = analysis->Scale.FeatureRadius;
            w.Current.CpuComputeMilliseconds += MillisecondsSince(started);
            return true;
        }

        void FillMetric(const Work& w, R::StepRunMetric& metric)
        {
            metric.Values = {double(w.Current.MaximumNeighbors), w.Current.MeanSpacing, w.Current.FeatureRadius, 0.0};
        }

        // Main thread, the run's Accept: the 33 columns with one undo entry; false with the reason when refused.
        bool Publish(Work& w, std::string& diagnostic)
        {
            const auto status = w.Service->PublishScalars(*w.Edit, w.Columns, "Compute FPFH descriptors");
            switch (status)
            {
            case R::PointPublishStatus::Applied:
                w.Current.WrittenCount = w.Points.size();
                Finish(w,
                    "applied",
                    "FPFH histogram columns computed with " + w.Current.ActualBackend +
                        " neighborhoods; scale, SPFH and FPFH run on CPU.",
                    true);
                return true;
            case R::PointPublishStatus::Stale:
                diagnostic = "Descriptor input or output changed before publication.";
                Finish(w, "stale", diagnostic, true);
                return false;
            case R::PointPublishStatus::Unrepresentable:
                diagnostic = "Descriptor output cannot be represented exactly in the selected scalar storage.";
                Finish(w, "invalid", diagnostic, true);
                return false;
            case R::PointPublishStatus::Rejected:
                break;
            }
            diagnostic = "Descriptor publication rejected by history checks.";
            Finish(w, "failed", diagnostic, true);
            return false;
        }

        // Main thread, the run's settled end without a publication (the step returned, the holder released the
        // output): nothing is published.
        void Ended(Work& w, const R::StepRunObservation& run)
        {
            // Changed inputs take precedence even when cancellation reaches the holder first.
            const std::string status{!w.Service->Current(*w.Edit) ? "stale" : R::UncommittedStatus(run)};
            Finish(w,
                status,
                run.StepFailed ? w.Current.Message
                    : !run.Diagnostic.empty()
                    ? "The descriptor run ended (" + run.Diagnostic + "); nothing was written."
                    : std::string{},
                run.StepFailed);
        }

        // Capture and the backend's admission limits; nullptr with the reason.
        std::shared_ptr<Work> Capture(const Config& config, const bool execute, std::string& diagnostic)
        {
            const auto validation = R::ValidateDerivedSection<kSection>(R::SerializeDerivedSection<kSection>(config), {},
                                                                        kSectionName);
            if (!validation.Usable())
            {
                diagnostic = validation.Diagnostics.empty() ? "Invalid descriptor settings."
                                                            : validation.Diagnostics.front().Message;
                return {};
            }
            auto* service = R::PluginService<R::PointCloudEditingService>();
            auto* runs = R::PluginService<R::StepRunsService>();
            if (service == nullptr || runs == nullptr)
            {
                diagnostic = "The point-cloud editing or step-run service is unavailable.";
                return {};
            }
            const bool indexed = config.Backend != DescriptorBackend::CpuKDTree;
            auto w = std::make_shared<Work>();
            w->Cfg = config;
            w->Service = service;
            w->Runs = runs;
            w->Edit = service->Capture({.StableEntityId = config.StableEntityId,
                                           .Positions = config.Positions,
                                           .Normals = config.Normals,
                                           .ScalarOutputs = {config.Outputs.begin(), config.Outputs.end()},
                                           .MinimumLiveCount = 2u,
                                           .CopyValues = execute,
                                           .AcquireIndex = execute && indexed,
                                           .CpuIndex = config.Backend != DescriptorBackend::VulkanLBVH},
                diagnostic);
            if (w->Edit == nullptr) return {};
            const auto view = service->View(*w->Edit);
            if (view.HasZeroNormals)
            {
                diagnostic = "Live positions must be finite and normals finite and nonzero.";
                return {};
            }
            if (indexed && (config.FeatureRadius > Geometry::PointLBVH::CoordinateLimit || !view.ValidLbvh ||
                            view.LiveCount > (1u << 24)))
            {
                diagnostic = "LBVH needs the spatial cache, at most 2^24 samples and coordinates/radii within 1e18.";
                return {};
            }
            if (config.Backend == DescriptorBackend::VulkanLBVH &&
                (!service->GpuQueriesAvailable() || view.LiveCount > (1u << 20)))
            {
                diagnostic = "Vulkan descriptors require GPU queries/jobs and at most 2^20 samples.";
                return {};
            }
            w->Points.assign(view.Points.begin(), view.Points.end());
            w->Normals.assign(view.PointNormals.begin(), view.PointNormals.end());
            w->Current = {.RequestedBackend = config.Backend, .SlotCount = view.SlotCount, .LiveCount = view.LiveCount,
                          .IndexReused = view.IndexReused};
            std::copy(view.ScalarOutputs.begin(), view.ScalarOutputs.end(), w->Current.Outputs.begin());
            return w;
        }
    }

    const char* ToString(const DescriptorBackend backend) noexcept
    {
        // The tokens are string literals, so their views end in a NUL.
        return std::size_t(backend) < std::size(kBackendTokens) ? kBackendTokens[std::size_t(backend)].data()
                                                                 : "invalid";
    }

    std::array<R::GeometryPropertyRef, kBins> MakeOutputs(const R::GeometryElementDomain domain,
                                                          const std::string_view prefix)
    {
        constexpr std::array blocks{"alpha", "phi", "theta"};
        std::array<R::GeometryPropertyRef, kBins> outputs;
        for (std::size_t i = 0; i < kBins; ++i)
            outputs[i] = {domain, std::string(prefix) + "." + blocks[i / 11] + std::to_string(i % 11),
                          Geometry::PropertyValueKind::Float};
        return outputs;
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
            .Message = "Descriptor analysis is a one-step run.",
            .RequestedBackend = config.Backend,
            .Outputs = config.Outputs};
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
        run->Pending.Message = "Descriptor analysis is a one-step run.";

        if (config.Backend == DescriptorBackend::VulkanLBVH)
        {
            // The device query session, once (resident index, buffers, pipelines); the radius comes with the step.
            w->Gpu = w->Service->PrepareGpuRows(*w->Edit,
                {.BatchSize = config.GpuQueryBatchSize,
                    .Capacity = config.GpuRadiusCapacity,
                    .LowestIdLimit = config.MaxNeighbors},
                diagnostic);
            if (!w->Gpu)
                return refuse("failed", "Vulkan descriptor support could not be prepared: " + diagnostic);
        }
        // Every backend is the shared one-shot worker run; a Vulkan step pages its device radius rows there.
        R::OneShotWorkerCallbacks callbacks{.Compute =
                                                [w]
            {
                return Compute(*w);
            },
            .Metric =
                [w](R::StepRunMetric& metric)
            {
                FillMetric(*w, metric);
            },
            .Valid =
                [w]
            {
                return w->Service->Current(*w->Edit);
            },
            .Publish =
                [w](std::string& diagnostic)
            {
                return Publish(*w, diagnostic);
            },
            .Ended =
                [w](const R::StepRunObservation& run)
            {
                Ended(*w, run);
            },
            // run_control restart (no step runs then): the analysis again from the capture; the scale and the device
            // session stay.
            .Restart =
                [w]
            {
                w->Columns = std::vector<std::vector<float>>{};
                w->Rows = R::PointNeighbourRows{};
            },
            .Abort =
                [w]
            {
                if (w->Gpu)
                    R::AbortGpuRows(*w->Gpu); // a waiting page gives up; the step returns
            }};
        R::StepRunOptions options{.Label = "Descriptor analysis",
            .Owner = "descriptor_analysis",
            .Output = {.StableEntityId = config.StableEntityId, .Property = w->Current.Outputs.front()},
            .Execution = config.Execution,
            .MetricNames = {"maximum_neighbors", "mean_spacing", "feature_radius", ""}};
        std::string refusal;
        w->RunId = R::StartOneShotWorkerRun(
            *w->Runs, std::move(options), std::move(callbacks), R::PluginStepRunJobs(w->Holder), refusal);
        // Another run owns the output, or the host refused the job: a failed start, not invalid settings.
        if (w->RunId == 0u)
            return refuse("failed", refusal.empty() ? "The descriptor run was refused." : refusal);
        run->Handle = *w->Holder;
        run->Pending.StepRun = w->RunId;
        if (g_LastRun == nullptr)
            g_LastRun = new std::shared_ptr<Run>();
        *g_LastRun = run;
        return run;
    }
}
