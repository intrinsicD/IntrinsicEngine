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

module Intrinsic.Plugin.BilateralFilter;

import Geometry.PointCloud.Utils;
import Geometry.PointLBVH;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PointCloudEditing;

namespace Intrinsic::Plugins::BilateralFilter
{
    namespace
    {
        namespace PC = Geometry::PointCloud;

        std::size_t Width(const std::size_t count, const std::size_t k) { return std::min(count - 1, k) + 1; }

        // One run's state; the worker touches it only inside Work, the main thread everywhere else.
        struct Work
        {
            Config Cfg{};
            R::PointCloudEditingService* Service{nullptr};
            std::shared_ptr<R::PointEdit> Edit{};
            std::vector<glm::vec3> Points{}, Normals{};
            std::vector<std::uint32_t> Rows{};
            PC::BilateralFilterParams Params{};
            Result Current{};
            bool Failed{false};
            bool Stopped{false}; // the inputs changed under a gated stage: the chain ends, as stale
            std::shared_ptr<Run> Shared{};
        };

        // The one terminal outcome of a run: the window and a waiting MCP call read it, the Jobs window too.
        void Finish(Work& w, std::string status, std::string message)
        {
            if (w.Shared->Final) return;
            w.Current.Status = std::move(status);
            if (!message.empty()) w.Current.Message = std::move(message);
            if (w.Edit != nullptr)
            {
                const auto stats = w.Service->GpuRowStats(*w.Edit);
                w.Current.GpuQueryBatches = stats.QueryBatches;
                w.Current.WorkspaceBuilds += stats.WorkspaceBuilds;
                w.Current.GpuNeighborhoodMilliseconds = stats.Milliseconds;
            }
            w.Shared->Final = w.Current;
            if (w.Shared->Handle.IsValid())
                R::ReportPluginJobOutcome(w.Shared->Handle,
                                          w.Cfg.Backend == FilterBackend::VulkanLBVH && w.Cfg.Iterations > 0
                                              ? R::PluginJobDomain::Gpu
                                              : R::PluginJobDomain::Cpu,
                                          w.Current.Message);
            // The jobs' callables keep `w` until the host reaps them: release what a finished run no longer needs
            // (copies, rows, the index lease and a private GPU workspace).
            // Move-assigned empties free the storage (`= {}` would keep the capacity).
            w.Edit.reset();
            w.Points = std::vector<glm::vec3>{};
            w.Normals = std::vector<glm::vec3>{};
            w.Rows = std::vector<std::uint32_t>{};
        }

        // Worker: the automatic bandwidth.
        bool Prepare(Work& w)
        {
            w.Params = {.KNeighbors = w.Cfg.KNeighbors, .SpatialSigma = w.Cfg.SpatialSigma,
                        .NormalSigma = w.Cfg.NormalSigma, .Iterations = 1};
            w.Current.ActualBackend = ToString(w.Cfg.Backend);
            if (w.Cfg.Iterations && w.Params.SpatialSigma <= 0)
            {
                const auto stats = PC::ComputeStatistics(w.Points, {.SpacingSampleCount = std::min(w.Points.size(), std::size_t{500})});
                if (!stats)
                {
                    w.Failed = true;
                    w.Current.Message = "Automatic bilateral bandwidth failed.";
                    return false;
                }
                w.Params.SpatialSigma = stats->AverageSpacing > 0 ? 2.f * stats->AverageSpacing : .01f;
            }
            w.Current.SpatialSigmaUsed = w.Params.SpatialSigma;
            return true;
        }

        // Worker: one pass. The octree builds its own neighbourhoods; the LBVH backends use `Rows` (the GPU's, or
        // the cached CPU index on the first pass and a rebuilt one on later passes).
        bool Step(Work& w)
        {
            const auto started = std::chrono::steady_clock::now();
            auto fail = [&](std::string why)
            {
                w.Failed = true;
                w.Current.Message = std::move(why);
                return false;
            };
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
                        if (!rebuilt.Build(w.Points)) return fail("Moving points exceed LBVH limits.");
                        index = &rebuilt;
                        ++w.Current.WorkspaceBuilds;
                    }
                    if (index == nullptr) return fail("Bilateral CPU index is unavailable.");
                    const auto width = Width(w.Points.size(), w.Cfg.KNeighbors);
                    w.Rows.clear();
                    w.Rows.reserve(w.Points.size() * width);
                    for (const auto& point : w.Points)
                    {
                        const auto row = index->KNearest(point, std::uint32_t(width));
                        if (row.size() != width) return fail("Incomplete CPU bilateral neighborhood.");
                        for (const auto& neighbor : row) w.Rows.push_back(neighbor.Index);
                    }
                }
                filtered = PC::BilateralFilterStepFromNeighbors(w.Points, w.Normals, w.Rows, w.Params);
            }
            if (!filtered)
                return fail("Bilateral filtering failed: invalid neighborhoods or unrepresentable float updates.");
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

        // Main thread: the last stage's completion.
        bool Publish(Work& w)
        {
            const auto status = w.Service->Publish(*w.Edit, w.Points, "Bilateral point filter");
            if (status == R::PointPublishStatus::Applied)
            {
                w.Current.WrittenCount = w.Points.size();
                Finish(w, "applied",
                       "Bilateral point filtering completed using " + w.Current.ActualBackend +
                           " neighborhoods and CPU updates with fixed normals.");
                return true;
            }
            Finish(w, status == R::PointPublishStatus::Stale ? "stale" : "failed",
                   status == R::PointPublishStatus::Stale ? "Bilateral input or output changed before publication."
                                                          : "Bilateral publication rejected by history checks.");
            return false;
        }

        // A stage's completion when it did not apply: cancelled, or its inputs changed.
        bool Abandoned(Work& w)
        {
            if (w.Shared->Final) return false; // already ended; its state is released
            if (w.Failed) Finish(w, "failed", {});
            else if (!w.Service->Current(*w.Edit)) Finish(w, "stale", "Bilateral inputs changed or the job was cancelled.");
            else Finish(w, "cancelled", "The bilateral run was cancelled.");
            return false;
        }

        bool Submit(const std::shared_ptr<Work>& w, R::PluginJob job)
        {
            job.Output = R::PluginJobOutput{.StableEntityId = w->Cfg.StableEntityId,
                                            .Property = w->Service->View(*w->Edit).Output};
            const bool first = !w->Shared->Handle.IsValid();
            if (!first) job.Run = w->Shared->Handle;
            std::string label = job.Label;
            std::string refusal;
            const auto handle = R::SubmitPluginJob({}, std::move(job), &refusal);
            if (!handle.IsValid())
            {
                Finish(*w, "failed", refusal.empty() ? "The bilateral stage '" + label + "' was not queued." : refusal);
                return false;
            }
            if (first) w->Shared->Handle = handle;
            return true;
        }

        bool SubmitNeighborhoods(const std::shared_ptr<Work>& w);

        // Vulkan: one pass's position update, after its neighbourhood rows.
        bool SubmitUpdate(const std::shared_ptr<Work>& w)
        {
            return Submit(w, {.Label = "Bilateral position update",
                              .Domain = R::PluginJobDomain::Cpu,
                              .Work = [w](const std::function<bool()>&) { (void)Step(*w); },
                              .Completion = [w](const bool applied)
                              {
                                  if (!applied || w->Failed) return Abandoned(*w);
                                  if (w->Current.CompletedIterations < w->Cfg.Iterations)
                                  {
                                      w->Service->RestartGpuKnnRows(*w->Edit);
                                      w->Rows.clear();
                                      return SubmitNeighborhoods(w);
                                  }
                                  return Publish(*w);
                              }});
        }

        // Vulkan: one pass's neighbourhood rows, paged on the main thread behind the gate.
        bool SubmitNeighborhoods(const std::shared_ptr<Work>& w)
        {
            return Submit(w, {.Label = "Bilateral neighborhoods (Vulkan)",
                              .Domain = R::PluginJobDomain::Gpu,
                              .Work = [](const std::function<bool()>&) {},
                              .IsReadyToApply = [w]
                              {
                                  if (w->Failed || w->Stopped || w->Shared->Final) return true;
                                  std::string diagnostic;
                                  const auto state = w->Service->AdvanceGpuKnnRows(
                                      *w->Edit, w->Points, std::uint32_t(Width(w->Points.size(), w->Cfg.KNeighbors)),
                                      w->Cfg.GpuQueryBatchSize, w->Rows, diagnostic);
                                  if (state == R::PointRowsState::Failed)
                                  {
                                      if (!w->Service->Current(*w->Edit)) w->Stopped = true;
                                      else
                                      {
                                          w->Failed = true;
                                          w->Current.Message = diagnostic;
                                      }
                                  }
                                  return state != R::PointRowsState::Pending;
                              },
                              .Completion = [w](const bool applied)
                              {
                                  if (!applied || w->Failed || w->Stopped || w->Shared->Final) return Abandoned(*w);
                                  return SubmitUpdate(w);
                              }});
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
            if (service == nullptr)
            {
                diagnostic = "The point-cloud editing service is unavailable.";
                return {};
            }
            const bool indexed = config.Backend != FilterBackend::CpuOctree && config.Iterations > 0;
            auto w = std::make_shared<Work>(Work{.Cfg = config, .Service = service});
            w->Edit = service->Capture({.StableEntityId = config.StableEntityId,
                                        .Positions = config.Positions,
                                        .Normals = config.Normals,
                                        .Output = config.Output,
                                        .MinimumLiveCount = 2u,
                                        .CopyValues = execute,
                                        .AcquireIndex = execute && indexed},
                                       diagnostic);
            if (w->Edit == nullptr) return {};
            const auto view = service->View(*w->Edit);
            if (config.Backend != FilterBackend::CpuOctree && (!view.ValidLbvh || view.LiveCount > (1u << 24)))
            {
                diagnostic = "LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.";
                return {};
            }
            if (config.Backend == FilterBackend::VulkanLBVH)
            {
                if (!service->GpuQueriesAvailable())
                {
                    diagnostic = "Vulkan bilateral filter neighborhoods require the framed spatial cache and job service.";
                    return {};
                }
                if (view.LiveCount > (1u << 20) || config.KNeighbors > 63)
                {
                    diagnostic = "Vulkan bilateral filter queries support at most 2^20 live samples and k<=63 (64 "
                                 "candidates including self).";
                    return {};
                }
            }
            w->Points.assign(view.Points.begin(), view.Points.end());
            w->Normals.assign(view.PointNormals.begin(), view.PointNormals.end());
            w->Current = {.RequestedBackend = config.Backend, .Output = view.Output, .SlotCount = view.SlotCount,
                          .LiveCount = view.LiveCount, .IndexReused = view.IndexReused};
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

    std::shared_ptr<Run> Start(const Config& config)
    {
        auto run = std::make_shared<Run>();
        run->Pending = {.Status = "pending", .Message = "Bilateral filtering queued.", .RequestedBackend = config.Backend,
                        .Output = config.Output};
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
        if (config.Backend != FilterBackend::VulkanLBVH || config.Iterations == 0)
        {
            (void)Submit(w, {.Label = "Bilateral point filter",
                             .Domain = R::PluginJobDomain::Cpu,
                             .Work = [w](const std::function<bool()>& cancelled)
                             {
                                 if (!Prepare(*w)) return;
                                 for (std::uint32_t i = 0; i < w->Cfg.Iterations && !w->Failed && !cancelled(); ++i)
                                     (void)Step(*w);
                             },
                             .Completion = [w](const bool applied)
                             {
                                 if (!applied || w->Failed) return Abandoned(*w);
                                 return Publish(*w);
                             }});
            return run;
        }
        (void)Submit(w, {.Label = "Bilateral bandwidth",
                         .Domain = R::PluginJobDomain::Cpu,
                         .Work = [w](const std::function<bool()>&) { (void)Prepare(*w); },
                         .Completion = [w](const bool applied)
                         {
                             if (!applied || w->Failed) return Abandoned(*w);
                             return SubmitNeighborhoods(w);
                         }});
        return run;
    }

    bool Cancel(const Run& run) { return run.Handle.IsValid() && R::CancelPluginJob(run.Handle); }
}
