module;
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.CoherentPointDriftGpuEStep;

import Extrinsic.Graphics.CoherentPointDriftEStep;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Private.WorkspaceLease;

#include "Services/Runtime.WorkerGpuPages.hpp"

namespace Extrinsic::Runtime
{
    template std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace>
    SpatialIndexCache::LeaseGpuWorkspace<Graphics::CoherentPointDriftEStepWorkspace>();

    namespace EStep = Geometry::CoherentPointDrift::EStep;

    namespace
    {
        bool HasValidShape(const EStep::ExternalRequest& request)
        {
            const std::size_t n = request.Target.Size(), m = request.Moved.Size();
            return n != 0u && m != 0u && request.LogWeights.size() == m && request.LogDenominator.size() == n &&
                request.Pt1.size() == n && request.P1.size() == m && request.PXx.size() == m &&
                request.PXy.size() == m && request.PXz.size() == m;
        }
    }

    struct CoherentPointDriftGpuEStep::Impl
    {
        SpatialIndexCache& Cache;
        std::chrono::milliseconds Timeout;
        WorkerGpuPages Pages;
        // A SpatialIndexCache lease, held from the run's Init until the run is done (the terminal release, Close or
        // destruction): steps keep its prepared buffers and uploaded target.
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> Workspace{};
        std::size_t Targets{0u}, Sources{0u}; // the prepared shape

        // One iteration's device input, reused (capacity kept) by every iteration of the run; touched only by the
        // one request in flight (Evaluate is serial).
        std::shared_ptr<const std::vector<float>> Target{}; // float-float, converted once per target generation
        std::uint64_t TargetGeneration{0u};
        std::vector<float> Source{};
        std::vector<std::uint32_t> SkipRows{};
        std::vector<std::byte> Data{};

        mutable std::mutex Mutex{};
        bool IsClosed{false};
        std::string LastDiagnostic{};
        CoherentPointDriftGpuEStepStats Counters{};

        Impl(SpatialIndexCache& cache, RHI::IDevice& device, std::chrono::milliseconds timeout)
            : Cache(cache), Timeout(timeout), Pages(device, RHI::ComputeInputOrder::LaneOrderOnly)
        {
        }

        // Closes with `why` (kept when one was given before), returning the workspace reference for the caller to drop
        // outside the lock.
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> CloseLocked(std::string why)
        {
            IsClosed = true;
            Pages.Abort();
            if (!why.empty())
                LastDiagnostic = std::move(why);
            return std::move(Workspace);
        }
    };

    CoherentPointDriftGpuEStep::CoherentPointDriftGpuEStep(
        SpatialIndexCache& cache, RHI::IDevice& device, std::chrono::milliseconds timeout)
        : m_Impl(std::make_unique<Impl>(cache, device, timeout))
    {
    }

    CoherentPointDriftGpuEStep::~CoherentPointDriftGpuEStep()
    {
        if (m_Impl)
            m_Impl->Pages.Seal(); // nothing more is recorded with the workspace or the device
    }

    bool CoherentPointDriftGpuEStep::Prepare(const std::size_t targets, const std::size_t sources)
    {
        auto& s = *m_Impl;
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> dropped;
        std::scoped_lock lock{s.Mutex};
        if (s.IsClosed)
            return false;
        if (!s.Workspace)
            s.Workspace = s.Cache.LeaseGpuWorkspace<Graphics::CoherentPointDriftEStepWorkspace>();
        if (!s.Workspace)
        {
            ++s.Counters.Failed;
            dropped = s.CloseLocked("No Vulkan E-step workspace (no device); the run continues on the CPU.");
            return false;
        }
        if (!s.Workspace->Prepare(targets, sources))
        {
            ++s.Counters.Failed;
            dropped = s.CloseLocked("The Vulkan E-step workspace could not be prepared; the run continues on the CPU.");
            return false;
        }
        s.Targets = targets;
        s.Sources = sources;
        return true;
    }

    bool CoherentPointDriftGpuEStep::Evaluate(const EStep::ExternalRequest& request)
    {
        auto& s = *m_Impl;
        if (!HasValidShape(request))
            return false;
        const std::size_t n = request.Target.Size(), m = request.Moved.Size();
        const auto started = std::chrono::steady_clock::now();
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> workspace;
        {
            std::scoped_lock lock{s.Mutex};
            if (s.IsClosed || !s.Workspace)
                return false;
            ++s.Counters.Requests;
            if (n > s.Targets || m > s.Sources)
            {
                ++s.Counters.Failed;
                (void)s.CloseLocked(
                    "The Vulkan E-step request exceeds its prepared workspace; the run continues on the CPU.");
                return false;
            }
            workspace = s.Workspace; // the step's own reference: a Close meanwhile drops only the run's
        }
        // Normalized coordinates convert to float-float pairs here, off the main thread; the target once per run.
        const auto split = [](float* to, const double x, const double y, const double z, const double w) {
            const float hx = float(x), hy = float(y), hz = float(z);
            to[0] = hx; to[1] = hy; to[2] = hz; to[3] = float(w);
            to[4] = float(x - double(hx)); to[5] = float(y - double(hy)); to[6] = float(z - double(hz)); to[7] = 0.0f;
        };
        if (!s.Target || request.TargetGeneration == 0u || s.TargetGeneration != request.TargetGeneration)
        {
            auto target = std::make_shared<std::vector<float>>(8u * n);
            for (std::size_t j = 0; j < n; ++j)
                split(target->data() + 8u * j, request.Target.X[j], request.Target.Y[j], request.Target.Z[j], 0.0);
            s.Target = std::move(target);
            s.TargetGeneration = request.TargetGeneration;
        }
        s.SkipRows.assign(request.SkipRows.begin(), request.SkipRows.end());
        s.Source.resize(8u * m);
        for (std::size_t i = 0; i < m; ++i)
            split(s.Source.data() + 8u * i,
                request.Moved.X[i],
                request.Moved.Y[i],
                request.Moved.Z[i],
                request.LogWeights[i]);
        s.Data.resize(Graphics::CoherentPointDriftEStepWorkspace::ReadbackBytes(n, m));
        const auto status = s.Pages.Run(
            [&](RHI::ICommandContext& commands)
            {
                return workspace->Record(commands,
                    {.Target = *s.Target,
                        .TargetGeneration = s.TargetGeneration,
                        .Source = s.Source,
                        .SkipRows = s.SkipRows,
                        .Sigma2 = request.Sigma2,
                        .LogOutlier = request.LogOutlier});
            },
            s.Data,
            s.Timeout);
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> dropped;
        {
            std::scoped_lock lock{s.Mutex};
            s.Counters.WaitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            switch (status)
            {
            case WorkerGpuPageStatus::Complete: ++s.Counters.Completed; break;
            case WorkerGpuPageStatus::Aborted:  return false; // closed meanwhile (cancelled): no fallback reason
            case WorkerGpuPageStatus::TimedOut:
                dropped = s.CloseLocked("No Vulkan E-step result arrived in time; the run continues on the CPU.");
                return false;
            case WorkerGpuPageStatus::Refused:
            case WorkerGpuPageStatus::Failed:
                ++s.Counters.Failed;
                dropped = s.CloseLocked("The Vulkan E-step was refused or failed; the run continues on the CPU.");
                return false;
            }
        }
        const auto copy = [&](std::span<double> to, std::size_t offset)
        {
            std::memcpy(to.data(), s.Data.data() + offset * sizeof(double), to.size_bytes());
        };
        copy(request.LogDenominator, 0u);
        copy(request.Pt1, n);
        copy(request.P1, 2u * n);
        copy(request.PXx, 2u * n + m);
        copy(request.PXy, 2u * n + 2u * m);
        copy(request.PXz, 2u * n + 3u * m);
        return true;
    }

    void CoherentPointDriftGpuEStep::ReleaseDeviceResources()
    {
        m_Impl->Pages.Seal();
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> dropped;
        std::scoped_lock lock{m_Impl->Mutex};
        dropped = std::move(m_Impl->Workspace); // back to the pool once its compute fence retired
    }

    void CoherentPointDriftGpuEStep::Close(std::string diagnostic)
    {
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> dropped;
        std::scoped_lock lock{m_Impl->Mutex};
        dropped = m_Impl->CloseLocked(std::move(diagnostic));
    }

    bool CoherentPointDriftGpuEStep::Closed() const
    {
        std::scoped_lock lock{m_Impl->Mutex};
        return m_Impl->IsClosed;
    }

    std::string CoherentPointDriftGpuEStep::Diagnostic() const
    {
        std::scoped_lock lock{m_Impl->Mutex};
        return m_Impl->LastDiagnostic;
    }

    CoherentPointDriftGpuEStepStats CoherentPointDriftGpuEStep::Stats() const
    {
        std::scoped_lock lock{m_Impl->Mutex};
        return m_Impl->Counters;
    }
}
