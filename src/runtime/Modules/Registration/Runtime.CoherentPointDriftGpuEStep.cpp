module;
#include <chrono>
#include <condition_variable>
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
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.SpatialIndexCache;

namespace Extrinsic::Runtime
{
    namespace EStep = Geometry::CoherentPointDrift::EStep;

    namespace
    {
        // Immutable once queued; the recording closure keeps it alive.
        struct Request
        {
            std::shared_ptr<const std::vector<float>> Target{};
            std::uint64_t TargetGeneration{0u};
            std::vector<float> Source{};
            std::vector<std::uint32_t> SkipRows{};
            double Sigma2{1.0}, LogOutlier{0.0};
        };

        enum class Slot : std::uint8_t { Idle, Queued, InFlight, Done, Failed };
    }

    struct CoherentPointDriftGpuEStep::Impl
    {
        SpatialIndexCache& Cache;
        RHI::IDevice& Device;
        std::chrono::milliseconds Timeout;
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> Workspace{}; // main thread

        // Worker-side fp32 copy of the fixed target.
        std::shared_ptr<const std::vector<float>> Target{};
        std::uint64_t TargetGeneration{0u};

        mutable std::mutex Mutex{};
        std::condition_variable Changed{};
        Slot State{Slot::Idle};
        std::shared_ptr<const Request> Pending{};
        std::shared_ptr<SpatialGpuResult> Gpu{};
        std::vector<std::byte> Data{};
        bool IsClosed{false}, IsWorkerActive{false};
        std::string LastDiagnostic{};
        CoherentPointDriftGpuEStepStats Counters{};

        Impl(SpatialIndexCache& cache, RHI::IDevice& device, std::chrono::milliseconds timeout)
            : Cache(cache), Device(device), Timeout(timeout) {}
    };

    CoherentPointDriftGpuEStep::CoherentPointDriftGpuEStep(SpatialIndexCache& cache, RHI::IDevice& device,
                                                           std::chrono::milliseconds timeout)
        : m_Impl(std::make_unique<Impl>(cache, device, timeout)) {}

    CoherentPointDriftGpuEStep::~CoherentPointDriftGpuEStep() = default;

    bool CoherentPointDriftGpuEStep::Evaluate(const EStep::ExternalRequest& request)
    {
        auto& s = *m_Impl;
        const std::size_t n = request.Target.Size(), m = request.Moved.Size();
        if (n == 0u || m == 0u || request.LogWeights.size() != m || request.LogDenominator.size() != n ||
            request.Pt1.size() != n || request.P1.size() != m || request.PXx.size() != m || request.PXy.size() != m ||
            request.PXz.size() != m)
            return false;
        {
            std::scoped_lock lock{s.Mutex};
            if (s.IsClosed) return false;
        }
        // Normalized coordinates convert to float-float pairs here, off the main thread; the
        // target once per run.
        const auto split = [](float* to, const double x, const double y, const double z, const double w) {
            const float hx = float(x), hy = float(y), hz = float(z);
            to[0] = hx; to[1] = hy; to[2] = hz; to[3] = float(w);
            to[4] = float(x - double(hx)); to[5] = float(y - double(hy)); to[6] = float(z - double(hz)); to[7] = 0.0f;
        };
        if (!s.Target || s.TargetGeneration != request.TargetGeneration)
        {
            auto target = std::make_shared<std::vector<float>>(8u * n);
            for (std::size_t j = 0; j < n; ++j)
                split(target->data() + 8u * j, request.Target.X[j], request.Target.Y[j], request.Target.Z[j], 0.0);
            s.Target = std::move(target);
            s.TargetGeneration = request.TargetGeneration;
        }
        auto pending = std::make_shared<Request>();
        pending->Target = s.Target;
        pending->TargetGeneration = s.TargetGeneration;
        pending->Sigma2 = request.Sigma2;
        pending->LogOutlier = request.LogOutlier;
        pending->SkipRows.assign(request.SkipRows.begin(), request.SkipRows.end());
        pending->Source.resize(8u * m);
        for (std::size_t i = 0; i < m; ++i)
            split(pending->Source.data() + 8u * i, request.Moved.X[i], request.Moved.Y[i], request.Moved.Z[i],
                  request.LogWeights[i]);

        const auto start = std::chrono::steady_clock::now();
        std::vector<std::byte> data;
        {
            std::unique_lock lock{s.Mutex};
            if (s.IsClosed || s.State != Slot::Idle) return false;
            s.Pending = std::move(pending);
            s.State = Slot::Queued;
            ++s.Counters.Requests;
            const bool ended = s.Changed.wait_for(lock, s.Timeout, [&] {
                return s.State == Slot::Done || s.State == Slot::Failed || s.IsClosed;
            });
            s.Counters.WaitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (!ended)
            {
                // The device may still finish; nothing is queued after this.
                s.IsClosed = true;
                s.LastDiagnostic = "No Vulkan E-step result arrived in time; the run continues on the CPU.";
            }
            const bool done = s.State == Slot::Done;
            if (done) data = std::move(s.Data);
            if (s.State != Slot::InFlight) s.State = Slot::Idle; // a timed-out computation keeps its slot
            s.Pending.reset();
            if (!done) return false;
        }
        if (data.size() != Graphics::CoherentPointDriftEStepWorkspace::ReadbackBytes(n, m)) return false;
        const auto copy = [&](std::span<double> to, std::size_t offset) {
            std::memcpy(to.data(), data.data() + offset * sizeof(double), to.size_bytes());
        };
        copy(request.LogDenominator, 0u);
        copy(request.Pt1, n);
        copy(request.P1, 2u * n);
        copy(request.PXx, 2u * n + m);
        copy(request.PXy, 2u * n + 2u * m);
        copy(request.PXz, 2u * n + 3u * m);
        return true;
    }

    void CoherentPointDriftGpuEStep::Pump()
    {
        auto& s = *m_Impl;
        std::scoped_lock lock{s.Mutex};
        if (s.State == Slot::Queued)
        {
            if (s.IsClosed)
            {
                s.State = Slot::Failed;
                s.Changed.notify_all();
                return;
            }
            if (!s.Workspace) s.Workspace = std::make_shared<Graphics::CoherentPointDriftEStepWorkspace>(s.Device);
            const auto request = s.Pending;
            const std::size_t n = request->Target->size() / 8u, m = request->Source.size() / 8u;
            s.Gpu = s.Cache.QueueGpuCompute(Graphics::CoherentPointDriftEStepWorkspace::ReadbackBytes(n, m),
                [workspace = s.Workspace, request](RHI::ICommandContext& commands, const SpatialGpuIndexView&) {
                    return workspace->Record(commands, {.Target = *request->Target,
                        .TargetGeneration = request->TargetGeneration, .Source = request->Source,
                        .SkipRows = request->SkipRows,
                        .Sigma2 = request->Sigma2, .LogOutlier = request->LogOutlier});
                });
            s.State = Slot::InFlight;
        }
        if (s.State == Slot::InFlight && s.Gpu)
        {
            if (s.Gpu->State == SpatialQueryState::Ready)
            {
                s.Data = std::move(s.Gpu->Data);
                s.Gpu.reset();
                ++s.Counters.Completed;
                // After a timeout nobody waits: drop the result.
                s.State = s.Pending ? Slot::Done : Slot::Idle;
                s.Changed.notify_all();
            }
            else if (s.Gpu->State == SpatialQueryState::Failed)
            {
                s.LastDiagnostic = s.Gpu->Diagnostic.empty()
                    ? std::string("The Vulkan E-step was refused or failed; the run continues on the CPU.")
                    : s.Gpu->Diagnostic;
                s.Gpu.reset();
                ++s.Counters.Failed;
                s.IsClosed = true;
                s.State = s.Pending ? Slot::Failed : Slot::Idle;
                s.Changed.notify_all();
            }
        }
    }

    void CoherentPointDriftGpuEStep::ReleaseDeviceResources()
    {
        std::scoped_lock lock{m_Impl->Mutex};
        m_Impl->Workspace.reset(); // an in-flight computation keeps its own reference
    }

    void CoherentPointDriftGpuEStep::Close(std::string diagnostic)
    {
        auto& s = *m_Impl;
        std::scoped_lock lock{s.Mutex};
        s.IsClosed = true;
        if (!diagnostic.empty()) s.LastDiagnostic = std::move(diagnostic);
        s.Changed.notify_all();
    }

    bool CoherentPointDriftGpuEStep::Closed() const
    {
        std::scoped_lock lock{m_Impl->Mutex};
        return m_Impl->IsClosed;
    }

    void CoherentPointDriftGpuEStep::SetWorkerActive(const bool active)
    {
        std::scoped_lock lock{m_Impl->Mutex};
        m_Impl->IsWorkerActive = active;
    }

    bool CoherentPointDriftGpuEStep::WorkerActive() const
    {
        std::scoped_lock lock{m_Impl->Mutex};
        return m_Impl->IsWorkerActive;
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
