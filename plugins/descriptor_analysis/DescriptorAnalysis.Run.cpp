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

namespace Intrinsic::Plugins::DescriptorAnalysis
{
    namespace
    {
        namespace Features = Geometry::PointCloud::Features;

        // One run's state; a worker touches it only inside Work, the main thread everywhere else.
        struct Work
        {
            Config Cfg{};
            R::PointCloudEditingService* Service{nullptr};
            std::shared_ptr<R::PointEdit> Edit{};
            std::vector<glm::vec3> Points{}, Normals{};
            R::PointNeighbourRows Rows{};
            std::vector<std::vector<float>> Columns{}; // one per bin, one value per live point
            Result Current{};
            bool Failed{false};
            bool Stopped{false}; // the inputs changed under the gated stage: the chain ends, as stale
            std::shared_ptr<Run> Shared{};
        };

        Features::DescriptorParams Parameters(const Config& c)
        {
            return {.FeatureRadius = c.FeatureRadius, .MaxNeighbors = c.MaxNeighbors};
        }

        double MillisecondsSince(const std::chrono::steady_clock::time_point started)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }

        // The one terminal outcome of a run: the window and a waiting MCP call read it, the Jobs window too.
        void Finish(Work& w, std::string status, std::string message)
        {
            if (w.Shared->Final) return;
            w.Current.Status = std::move(status);
            if (!message.empty()) w.Current.Message = std::move(message);
            if (w.Edit != nullptr && w.Cfg.Backend == DescriptorBackend::VulkanLBVH)
            {
                const auto stats = w.Service->GpuRowStats(*w.Edit);
                w.Current.GpuQueryBatches = stats.QueryBatches;
                w.Current.GpuNeighborhoodMilliseconds = stats.Milliseconds;
            }
            w.Shared->Final = w.Current;
            if (w.Shared->Handle.IsValid())
                R::ReportPluginJobOutcome(w.Shared->Handle,
                                          w.Cfg.Backend == DescriptorBackend::VulkanLBVH ? R::PluginJobDomain::Gpu
                                                                                         : R::PluginJobDomain::Cpu,
                                          w.Current.Message);
            // The jobs' callables keep `w` until the host reaps them: release what a finished run no longer needs.
            // Move-assigned empties free the storage (`= {}` would keep the capacity).
            w.Edit.reset();
            w.Points = std::vector<glm::vec3>{};
            w.Normals = std::vector<glm::vec3>{};
            w.Rows = R::PointNeighbourRows{};
            w.Columns = std::vector<std::vector<float>>{};
        }

        bool Fail(Work& w, std::string why)
        {
            w.Failed = true;
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
            return true;
        }

        // Worker: the histograms. The k-d tree finds its own neighbourhoods; the LBVH backends use `Rows` (the
        // GPU's, or rows from the cached CPU index made here).
        bool Compute(Work& w)
        {
            w.Current.ActualBackend = ToString(w.Cfg.Backend);
            if (w.Cfg.Backend == DescriptorBackend::CpuLBVH && !Prepare(w)) return false; // Prepare times itself
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

        // Main thread: the last stage's completion.
        bool Publish(Work& w)
        {
            const auto status = w.Service->PublishScalars(*w.Edit, w.Columns, "Compute FPFH descriptors");
            switch (status)
            {
            case R::PointPublishStatus::Applied:
                w.Current.WrittenCount = w.Points.size();
                Finish(w, "applied", "FPFH histogram columns computed with " + w.Current.ActualBackend +
                                         " neighborhoods; scale, SPFH and FPFH run on CPU.");
                return true;
            case R::PointPublishStatus::Stale:
                Finish(w, "stale", "Descriptor input or output changed before publication.");
                return false;
            case R::PointPublishStatus::Unrepresentable:
                Finish(w, "invalid", "Descriptor output cannot be represented exactly in the selected scalar storage.");
                return false;
            case R::PointPublishStatus::Rejected:
                break;
            }
            Finish(w, "failed", "Descriptor publication rejected by history checks.");
            return false;
        }

        // A stage's completion when it did not apply: cancelled, or its inputs changed.
        bool Abandoned(Work& w)
        {
            if (w.Shared->Final) return false; // already ended; its state is released
            if (w.Failed) Finish(w, "failed", {});
            else if (w.Stopped || !w.Service->Current(*w.Edit))
                Finish(w, "stale", "Descriptor input changed or the job was cancelled.");
            else Finish(w, "cancelled", "The descriptor run was cancelled.");
            return false;
        }

        // Every stage joins the run of the first; a refused submission ends the run (no earlier stage is still
        // running: each is submitted from the previous one's completion).
        bool Submit(const std::shared_ptr<Work>& w, R::PluginJob job)
        {
            job.Output = R::PluginJobOutput{.StableEntityId = w->Cfg.StableEntityId,
                                            .Property = w->Service->View(*w->Edit).ScalarOutputs.front()};
            const bool first = !w->Shared->Handle.IsValid();
            if (!first) job.Run = w->Shared->Handle;
            const std::string label = job.Label;
            std::string refusal;
            const auto handle = R::SubmitPluginJob({}, std::move(job), &refusal);
            if (!handle.IsValid())
            {
                Finish(*w, "failed", refusal.empty() ? "The descriptor stage '" + label + "' was not queued." : refusal);
                return false;
            }
            if (first) w->Shared->Handle = handle;
            return true;
        }

        // The CPU backends' one stage, and Vulkan's last: the histograms (over the GPU's rows for Vulkan).
        bool SubmitHistograms(const std::shared_ptr<Work>& w)
        {
            return Submit(w, {.Label = "FPFH histograms",
                              .Domain = R::PluginJobDomain::Cpu,
                              .Work = [w](const std::function<bool()>&) { (void)Compute(*w); },
                              .Completion = [w](const bool applied)
                              {
                                  if (!applied || w->Failed) return Abandoned(*w);
                                  return Publish(*w);
                              }});
        }

        // Vulkan, second stage: the radius rows, paged on the main thread behind the gate.
        bool SubmitSupport(const std::shared_ptr<Work>& w)
        {
            return Submit(w, {.Label = "Descriptor radius support (Vulkan)",
                              .Domain = R::PluginJobDomain::Gpu,
                              .Work = [](const std::function<bool()>&) {},
                              .IsReadyToApply = [w]
                              {
                                  if (w->Failed || w->Stopped || w->Shared->Final) return true;
                                  std::string diagnostic;
                                  const auto state = w->Service->AdvanceGpuRadiusRows(
                                      *w->Edit, w->Current.FeatureRadius, w->Cfg.GpuQueryBatchSize,
                                      w->Cfg.GpuRadiusCapacity, w->Cfg.MaxNeighbors, w->Rows, diagnostic);
                                  w->Current.MaximumNeighbors = w->Rows.MaximumNeighbors;
                                  if (w->Service->GpuRowStats(*w->Edit).QueryBatches > 0)
                                      w->Current.ActualBackend = "vulkan_lbvh";
                                  if (state == R::PointRowsState::Failed)
                                  {
                                      if (!w->Service->Current(*w->Edit)) w->Stopped = true;
                                      else (void)Fail(*w, diagnostic);
                                  }
                                  return state != R::PointRowsState::Pending;
                              },
                              .Completion = [w](const bool applied)
                              {
                                  if (!applied || w->Failed || w->Stopped || w->Shared->Final) return Abandoned(*w);
                                  return SubmitHistograms(w);
                              }});
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
            if (service == nullptr)
            {
                diagnostic = "The point-cloud editing service is unavailable.";
                return {};
            }
            const bool indexed = config.Backend != DescriptorBackend::CpuKDTree;
            auto w = std::make_shared<Work>(Work{.Cfg = config, .Service = service});
            w->Edit = service->Capture({.StableEntityId = config.StableEntityId,
                                        .Positions = config.Positions,
                                        .Normals = config.Normals,
                                        .ScalarOutputs = {config.Outputs.begin(), config.Outputs.end()},
                                        .MinimumLiveCount = 2u,
                                        .CopyValues = execute,
                                        .AcquireIndex = execute && indexed},
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
                diagnostic = "Vulkan descriptors require framed GPU queries/jobs and at most 2^20 samples.";
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

    std::shared_ptr<Run> Start(const Config& config)
    {
        auto run = std::make_shared<Run>();
        run->Pending = {.Status = "pending", .Message = "Descriptor analysis queued.", .RequestedBackend = config.Backend,
                        .Outputs = config.Outputs};
        std::string diagnostic;
        const auto w = Capture(config, true, diagnostic);
        if (w == nullptr)
        {
            run->Final = run->Pending;
            run->Final->Status = "invalid";
            run->Final->Message = diagnostic;
            return run;
        }
        w->Shared = run;
        run->Pending.SlotCount = w->Current.SlotCount;
        run->Pending.LiveCount = w->Current.LiveCount;
        run->Pending.Outputs = w->Current.Outputs;
        if (config.Backend != DescriptorBackend::VulkanLBVH)
        {
            (void)SubmitHistograms(w);
            return run;
        }
        (void)Submit(w, {.Label = "Descriptor scale",
                         .Domain = R::PluginJobDomain::Cpu,
                         .Work = [w](const std::function<bool()>&) { (void)Prepare(*w); },
                         .Completion = [w](const bool applied)
                         {
                             if (!applied || w->Failed) return Abandoned(*w);
                             return SubmitSupport(w);
                         }});
        return run;
    }

    bool Cancel(const Run& run) { return run.Handle.IsValid() && R::CancelPluginJob(run.Handle); }
}
