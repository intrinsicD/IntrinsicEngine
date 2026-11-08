module;
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

module Extrinsic.Runtime.PointSamplingGpu;

import Extrinsic.Graphics.FarthestPointSampling;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Private.WorkspaceLease;

#include "Services/Runtime.WorkerGpuPages.hpp"

namespace Extrinsic::Runtime
{
    template std::shared_ptr<Graphics::FarthestPointSamplingWorkspace>
    SpatialIndexCache::LeaseGpuWorkspace<Graphics::FarthestPointSamplingWorkspace>();

    namespace PS = Geometry::PointSampling;

    std::string PointSamplingGpuUnsupportedReason(const PS::Params& params, const std::size_t points,
                                                  const std::size_t count, const RHI::IDevice* device)
    {
        if (params.Method != PS::Method::FarthestPoint)
            return "The " + std::string(PS::ToString(params.Method)) +
                   " method has no Vulkan kernel yet; it ran on the CPU.";
        if (device == nullptr || !device->IsOperational()) return "No operational Vulkan device; sampling ran on the CPU.";
        if (!device->SupportsShaderFloat64()) return "The Vulkan device lacks shader float64; sampling ran on the CPU.";
        if (points == 0u || points > Graphics::FarthestPointSamplingWorkspace::MaxPoints || count == 0u ||
            params.FirstIndex >= points || (!params.Weights.empty() && params.Weights.size() != points))
            return "The request is outside the Vulkan sampler's limits; sampling ran on the CPU.";
        // The CPU reference rejects these too; refuse before spending device frames.
        for (const double w : params.Weights)
            if (!(w > 0.0) || !std::isfinite(w)) return "Importance weights must be finite and positive.";
        return {};
    }

    struct PointSamplingGpuRun::Impl
    {
        std::shared_ptr<Graphics::FarthestPointSamplingWorkspace> Workspace;
        std::vector<double> X{}, Y{}, Z{}, Weights{};
        // The device input, its live-row map owned here (Restart begins the workspace again).
        Graphics::FarthestPointGpuInput Input{};
        std::vector<std::uint32_t> Rows{};
        PS::Params Params{};
        std::size_t Count{0u};
        PS::Result Prefix{};
        bool Began{false};
        // RUNTIME-355 F3: a PrepareWorker run's page gate (written once on the main thread before any worker uses it,
        // then only sealed), its device and the residency whose input slots Release stamps.
        std::unique_ptr<WorkerGpuPages> Pages{};
        RHI::IDevice* Device{};
        Graphics::GpuPropertyResidency* Residency{};
    };

    PointSamplingGpuRun::PointSamplingGpuRun(SpatialIndexCache& cache, const Graphics::FarthestPointGpuInput& input,
                                             const std::span<const glm::vec3> points, const PS::Params& params)
        : m_Impl(std::make_unique<Impl>())
    {
        auto& s = *m_Impl;
        s.Workspace = cache.LeaseGpuWorkspace<Graphics::FarthestPointSamplingWorkspace>();
        // The CPU reference converts the same floats to doubles, so both see identical inputs.
        for (const auto& p : points)
        {
            s.X.push_back(p.x);
            s.Y.push_back(p.y);
            s.Z.push_back(p.z);
        }
        s.Weights.assign(params.Weights.begin(), params.Weights.end());
        s.Params = params;
        s.Params.Weights = s.Weights;
        s.Count = input.Count;
        s.Rows.assign(input.Rows.begin(), input.Rows.end());
        s.Input = input;
        s.Input.Rows = s.Rows;
        s.Began = s.Workspace && points.size() > 0u && s.Workspace->Begin(s.Input);
    }

    PointSamplingGpuRun::~PointSamplingGpuRun() = default;

    std::shared_ptr<SpatialGpuResult> PointSamplingGpuRun::QueueNext(
        SpatialIndexCache& cache, const std::uint32_t maxSamples, const bool readback)
    {
        auto& s = *m_Impl;
        if (!s.Began || !s.Workspace) return nullptr;
        // Earlier chunks wait for completion without moving any intermediate values to the CPU.
        const std::size_t bytes = readback || s.Workspace->NextChunkFinishes(maxSamples)
            ? Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(s.Count))
            : 0u;
        return cache.QueueGpuCompute(bytes,
            [workspace = s.Workspace, maxSamples](RHI::ICommandContext& commands, const SpatialGpuIndexView&)
            {
                return workspace->RecordNext(commands, maxSamples);
            });
    }

    std::uint32_t PointSamplingGpuRun::NextChunkSamples(const std::uint32_t maxSamples) const noexcept
    {
        const auto& s = *m_Impl;
        return s.Began && s.Workspace ? s.Workspace->NextChunkSamples(maxSamples) : 0u;
    }

    bool PointSamplingGpuRun::Restart(SpatialIndexCache& cache)
    {
        auto& s = *m_Impl;
        if (!s.Workspace)
            s.Workspace = cache.LeaseGpuWorkspace<Graphics::FarthestPointSamplingWorkspace>();
        s.Prefix = {};
        s.Began = s.Workspace && !s.X.empty() && s.Workspace->Begin(s.Input);
        return s.Began;
    }

    bool PointSamplingGpuRun::Prepare()
    {
        auto& s = *m_Impl;
        return s.Began && s.Workspace && s.Workspace->Prepare();
    }

    bool PointSamplingGpuRun::PrepareWorker(RHI::IDevice& device, Graphics::GpuPropertyResidency& residency)
    {
        auto& s = *m_Impl;
        if (s.Pages || !Prepare())
            return false;
        s.Device = &device;
        s.Residency = &residency;
        s.Pages = std::make_unique<WorkerGpuPages>(device); // its first page waits for the issued input uploads
        return true;
    }

    bool PointSamplingGpuRun::OrderOnWorker(std::string& diagnostic)
    {
        auto& s = *m_Impl;
        const auto cpu = [&diagnostic](const std::string_view why)
        {
            diagnostic = std::string(why) + "; sampling ran on the CPU.";
            return false;
        };
        if (!s.Pages || !s.Workspace || !s.Began)
            return cpu("GPU sampling submission refused");
        auto& workspace = *s.Workspace; // the run's own reference: only Release drops it, after this returned
        std::vector<std::byte> bytes;
        while (!workspace.Finished())
        {
            if (workspace.NextChunkSamples() == 0u)
                return cpu("GPU sampling submission refused");
            // Earlier chunks need completion only; the one adding the last sample carries the order.
            bytes.assign(workspace.NextChunkFinishes()
                    ? Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(s.Count))
                    : 0u,
                std::byte{0});
            switch (s.Pages->Run(
                [&workspace](RHI::ICommandContext& commands)
                {
                    return workspace.RecordNext(commands);
                },
                bytes))
            {
            case WorkerGpuPageStatus::Complete: break;
            case WorkerGpuPageStatus::Aborted:  diagnostic = "GPU sampling was cancelled."; return false;
            case WorkerGpuPageStatus::Refused:  return cpu("GPU sampling submission refused");
            case WorkerGpuPageStatus::Failed:
            case WorkerGpuPageStatus::TimedOut: return cpu("GPU sampling did not complete");
            }
            (void)Observe(std::span<const std::byte>(bytes));
        }
        if (s.Prefix.Order.size() != s.Count)
            return cpu("The Vulkan sampler returned an incomplete order");
        return VerifyPrefix(diagnostic);
    }

    void PointSamplingGpuRun::Abort() noexcept
    {
        if (m_Impl->Pages)
            m_Impl->Pages->Abort();
    }

    std::shared_ptr<Graphics::FarthestPointSamplingWorkspace> PointSamplingGpuRun::Workspace() const noexcept
    {
        return m_Impl->Workspace;
    }

    void PointSamplingGpuRun::Release()
    {
        auto& s = *m_Impl;
        if (s.Pages && !s.Pages->Sealed())
        {
            // Nothing is submitted any more, so the compute fence covers every page, a given-up one included: the
            // resident input slots it read are reused or freed only once it completed.
            s.Pages->Seal();
            const auto frame = s.Device->GetGlobalFrameNumber();
            s.Residency->BeginSubmission();
            for (const auto& view : {s.Input.Positions, s.Input.Weights})
                if (view.Valid())
                    s.Residency->NoteUse(view.Buffer, frame);
            s.Residency->EndSubmission(s.Device->GetTransferQueue().ComputeFence());
        }
        s.Workspace.reset();
        s.Began = false;
    }

    bool PointSamplingGpuRun::Observe(const SpatialGpuResult& chunk)
    {
        return Observe(std::span<const std::byte>(chunk.Data));
    }

    bool PointSamplingGpuRun::Observe(const std::span<const std::byte> chunk)
    {
        auto& s = *m_Impl;
        if (!s.Workspace) return true;
        const std::size_t produced = s.Workspace->Produced();
        if (produced > s.Count) return true;
        // An intermediate chunk carries no samples; the order arrives with the last one.
        if (chunk.size() != Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(s.Count)))
            return !chunk.empty() || s.Workspace->Finished();
        // Entries already published never change; only the new tail is appended.
        std::vector<double> clearance(produced);
        std::vector<std::uint32_t> order(produced);
        std::memcpy(clearance.data(), chunk.data(), produced * sizeof(double));
        std::memcpy(order.data(), chunk.data() + s.Count * sizeof(double), produced * sizeof(std::uint32_t));
        s.Prefix.Order.insert(s.Prefix.Order.end(), order.begin() + std::ptrdiff_t(s.Prefix.Order.size()), order.end());
        s.Prefix.Clearance.insert(s.Prefix.Clearance.end(), clearance.begin() + std::ptrdiff_t(s.Prefix.Clearance.size()),
                                  clearance.end());
        s.Prefix.DistancePairs = std::uint64_t(s.X.size()) * (produced > 0u ? produced - 1u : 0u);
        // The workspace stays until Release (or this run's end), so a step run keeps it through Accept.
        return s.Workspace->Finished();
    }

    const PS::Result& PointSamplingGpuRun::Current() const noexcept { return m_Impl->Prefix; }

    bool PointSamplingGpuRun::VerifyPrefix(std::string& diagnostic) const
    {
        const auto& s = *m_Impl;
        const std::size_t checked = std::min({s.Prefix.Order.size(), kPointSamplingVerifiedPrefix,
                                              std::max<std::size_t>(1u, kPointSamplingVerifiedPairs / s.X.size())});
        const PS::Result reference =
            PS::FarthestPointBruteForce({s.X, s.Y, s.Z}, s.Params.FirstIndex, s.Weights, checked);
        for (std::size_t k = 0; k < checked; ++k)
        {
            if (k >= reference.Order.size() || reference.Order[k] != s.Prefix.Order[k] ||
                reference.Clearance[k] != s.Prefix.Clearance[k])
            {
                diagnostic = "The Vulkan order differs from the CPU reference at sample " + std::to_string(k) +
                             "; the CPU result was used.";
                return false;
            }
        }
        return checked > 0u;
    }
}
