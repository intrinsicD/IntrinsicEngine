module;
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
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.SpatialIndexCache;

namespace Extrinsic::Runtime
{
    namespace EStep = Geometry::CoherentPointDrift::EStep;

    namespace
    {
        // One iteration's device input, reused (capacity kept) by every iteration of the run. The
        // worker fills it only while the slot is Idle, so no computation can still read it; the
        // recording closure keeps it alive and reads it only before its result is Ready or Failed.
        struct Request
        {
            // Float-float target, converted once per target generation (every request for 0).
            std::shared_ptr<const std::vector<float>> Target{};
            std::uint64_t TargetGeneration{0u};
            std::vector<float> Source{};
            std::vector<std::uint32_t> SkipRows{};
            double Sigma2{1.0}, LogOutlier{0.0};
        };

        enum class Slot : std::uint8_t { Idle, Preparing, Queued, InFlight, Done };

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
        // Main thread: a SpatialIndexCache lease, held from a step's first request until its pump
        // ends; the pool then serves it (pipelines, grown buffers, uploaded target) to the next.
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> Workspace{};
        const std::shared_ptr<Request> Staging{std::make_shared<Request>()};

        mutable std::mutex Mutex{};
        Slot State{Slot::Idle};
        std::shared_ptr<Core::Tasks::CounterEvent> Pending{};
        std::chrono::steady_clock::time_point Started{};
        bool Settled{false};
        std::shared_ptr<SpatialGpuResult> Gpu{};
        std::vector<std::byte> Data{};
        bool IsClosed{false}, IsWorkerActive{false};
        std::string LastDiagnostic{};
        CoherentPointDriftGpuEStepStats Counters{};

        std::shared_ptr<Core::Tasks::CounterEvent> SettleLocked()
        {
            if (!Pending || Settled) return {};
            Settled = true;
            Counters.WaitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count();
            return Pending;
        }

        Impl(SpatialIndexCache& cache, std::chrono::milliseconds timeout) : Cache(cache), Timeout(timeout) {}
    };

    CoherentPointDriftGpuEStep::CoherentPointDriftGpuEStep(SpatialIndexCache& cache, std::chrono::milliseconds timeout)
        : m_Impl(std::make_unique<Impl>(cache, timeout)) {}

    CoherentPointDriftGpuEStep::~CoherentPointDriftGpuEStep() = default;

    std::shared_ptr<Core::Tasks::CounterEvent> CoherentPointDriftGpuEStep::Queue(const EStep::ExternalRequest& request)
    {
        auto& s = *m_Impl;
        const std::size_t n = request.Target.Size(), m = request.Moved.Size();
        if (!HasValidShape(request)) return {};
        {
            std::scoped_lock lock{s.Mutex};
            if (s.IsClosed || s.State != Slot::Idle) return {}; // the staging may still be read
            s.State = Slot::Preparing;
        }
        // Normalized coordinates convert to float-float pairs here, off the main thread; the
        // target once per run.
        const auto split = [](float* to, const double x, const double y, const double z, const double w) {
            const float hx = float(x), hy = float(y), hz = float(z);
            to[0] = hx; to[1] = hy; to[2] = hz; to[3] = float(w);
            to[4] = float(x - double(hx)); to[5] = float(y - double(hy)); to[6] = float(z - double(hz)); to[7] = 0.0f;
        };
        Request& staged = *s.Staging;
        if (!staged.Target || request.TargetGeneration == 0u || staged.TargetGeneration != request.TargetGeneration)
        {
            auto target = std::make_shared<std::vector<float>>(8u * n);
            for (std::size_t j = 0; j < n; ++j)
                split(target->data() + 8u * j, request.Target.X[j], request.Target.Y[j], request.Target.Z[j], 0.0);
            staged.Target = std::move(target);
            staged.TargetGeneration = request.TargetGeneration;
        }
        staged.Sigma2 = request.Sigma2;
        staged.LogOutlier = request.LogOutlier;
        staged.SkipRows.assign(request.SkipRows.begin(), request.SkipRows.end());
        staged.Source.resize(8u * m);
        for (std::size_t i = 0; i < m; ++i)
            split(staged.Source.data() + 8u * i, request.Moved.X[i], request.Moved.Y[i], request.Moved.Z[i],
                  request.LogWeights[i]);

        std::scoped_lock lock{s.Mutex};
        if (s.IsClosed)
        {
            s.State = Slot::Idle;
            return {};
        }
        s.Pending = std::make_shared<Core::Tasks::CounterEvent>(1u);
        s.Started = std::chrono::steady_clock::now();
        s.Settled = false;
        s.State = Slot::Queued;
        ++s.Counters.Requests;
        return s.Pending;
    }

    bool CoherentPointDriftGpuEStep::Complete(
        const std::shared_ptr<Core::Tasks::CounterEvent>& event,
        const EStep::ExternalRequest& request)
    {
        auto& s = *m_Impl;
        if (!HasValidShape(request)) return false;
        const std::size_t n = request.Target.Size(), m = request.Moved.Size();
        std::vector<std::byte> data;
        {
            std::scoped_lock lock{s.Mutex};
            if (!event || event != s.Pending || s.IsClosed || s.State != Slot::Done) return false;
            data = std::move(s.Data);
            s.Pending.reset();
            s.State = Slot::Idle;
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
        std::shared_ptr<Core::Tasks::CounterEvent> signal;
        {
            std::scoped_lock lock{s.Mutex};
            if (s.Pending && !s.Settled && std::chrono::steady_clock::now() - s.Started >= s.Timeout)
            {
                s.IsClosed = true;
                s.LastDiagnostic = "No Vulkan E-step result arrived in time; the run continues on the CPU.";
                signal = s.SettleLocked();
                s.Pending.reset();
                if (s.State != Slot::InFlight) s.State = Slot::Idle;
            }
            if (s.State == Slot::Queued)
            {
                if (!s.Workspace) s.Workspace = s.Cache.LeaseGpuWorkspace<Graphics::CoherentPointDriftEStepWorkspace>();
                if (!s.Workspace)
                {
                    s.LastDiagnostic = "No Vulkan E-step workspace (no device); the run continues on the CPU.";
                    ++s.Counters.Failed;
                    s.IsClosed = true;
                    s.State = Slot::Idle;
                    signal = s.SettleLocked();
                    s.Pending.reset();
                }
                else
                {
                    const auto request = s.Staging;
                    const std::size_t n = request->Target->size() / 8u, m = request->Source.size() / 8u;
                    s.Gpu = s.Cache.QueueGpuCompute(Graphics::CoherentPointDriftEStepWorkspace::ReadbackBytes(n, m),
                        [workspace = s.Workspace, request](RHI::ICommandContext& commands, const SpatialGpuIndexView&) {
                            return workspace->Record(commands, {.Target = *request->Target,
                                .TargetGeneration = request->TargetGeneration, .Source = request->Source,
                                .SkipRows = request->SkipRows,
                                .Sigma2 = request->Sigma2, .LogOutlier = request->LogOutlier});
                        }, SpatialGpuLatency::Immediate);
                    s.State = Slot::InFlight;
                }
            }
            if (s.State == Slot::InFlight && s.Gpu)
            {
                if (s.Gpu->State == SpatialQueryState::Ready)
                {
                    ++s.Counters.Completed;
                    s.State = s.Pending ? Slot::Done : Slot::Idle;
                    if (s.Pending)
                    {
                        s.Data = std::move(s.Gpu->Data);
                        signal = s.SettleLocked();
                    }
                    s.Gpu.reset();
                }
                else if (s.Gpu->State == SpatialQueryState::Failed)
                {
                    s.LastDiagnostic = s.Gpu->Diagnostic.empty()
                        ? std::string("The Vulkan E-step was refused or failed; the run continues on the CPU.")
                        : s.Gpu->Diagnostic;
                    s.Gpu.reset();
                    ++s.Counters.Failed;
                    s.IsClosed = true;
                    s.State = Slot::Idle;
                    if (auto completed = s.SettleLocked()) signal = std::move(completed);
                    s.Pending.reset();
                }
            }
        }
        if (signal) signal->Signal();
    }

    void CoherentPointDriftGpuEStep::ReleaseDeviceResources()
    {
        std::scoped_lock lock{m_Impl->Mutex};
        m_Impl->Workspace.reset(); // back to the pool; an in-flight computation keeps its own lease
    }

    void CoherentPointDriftGpuEStep::Close(std::string diagnostic)
    {
        auto& s = *m_Impl;
        std::shared_ptr<Core::Tasks::CounterEvent> signal;
        {
            std::scoped_lock lock{s.Mutex};
            s.IsClosed = true;
            if (!diagnostic.empty()) s.LastDiagnostic = std::move(diagnostic);
            signal = s.SettleLocked();
            s.Pending.reset();
            s.Data.clear();
            if (s.State == Slot::Queued || s.State == Slot::Done) s.State = Slot::Idle;
        }
        if (signal) signal->Signal();
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
