module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <glm/glm.hpp>

module Extrinsic.Runtime.PointSamplingGpu;

import Extrinsic.Graphics.FarthestPointSampling;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.SpatialIndexCache;

namespace Extrinsic::Runtime
{
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
        PS::Params Params{};
        std::size_t Count{0u};
        PS::Result Prefix{};
        bool Began{false};
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
        s.Began = s.Workspace && points.size() > 0u && s.Workspace->Begin(input);
    }

    PointSamplingGpuRun::~PointSamplingGpuRun() = default;

    std::shared_ptr<SpatialGpuResult> PointSamplingGpuRun::QueueNext(SpatialIndexCache& cache)
    {
        auto& s = *m_Impl;
        if (!s.Began || !s.Workspace) return nullptr;
        // Earlier chunks wait for completion without moving any intermediate values to the CPU.
        const std::size_t bytes = s.Workspace->NextChunkFinishes()
            ? Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(s.Count)) : 0u;
        return cache.QueueGpuCompute(bytes,
            [workspace = s.Workspace](RHI::ICommandContext& commands, const SpatialGpuIndexView&) {
                return workspace->RecordNext(commands);
            }, SpatialGpuLatency::Immediate);
    }

    bool PointSamplingGpuRun::Observe(const SpatialGpuResult& chunk)
    {
        auto& s = *m_Impl;
        if (!s.Workspace) return true;
        const std::size_t produced = s.Workspace->Produced();
        if (produced > s.Count) return true;
        // An intermediate chunk carries no samples; the order arrives with the last one.
        if (chunk.Data.size() != Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(s.Count)))
            return !chunk.Data.empty() || s.Workspace->Finished();
        // Entries already published never change; only the new tail is appended.
        std::vector<double> clearance(produced);
        std::vector<std::uint32_t> order(produced);
        std::memcpy(clearance.data(), chunk.Data.data(), produced * sizeof(double));
        std::memcpy(order.data(), chunk.Data.data() + s.Count * sizeof(double), produced * sizeof(std::uint32_t));
        s.Prefix.Order.insert(s.Prefix.Order.end(), order.begin() + std::ptrdiff_t(s.Prefix.Order.size()), order.end());
        s.Prefix.Clearance.insert(s.Prefix.Clearance.end(), clearance.begin() + std::ptrdiff_t(s.Prefix.Clearance.size()),
                                  clearance.end());
        s.Prefix.DistancePairs = std::uint64_t(s.X.size()) * (produced > 0u ? produced - 1u : 0u);
        const bool finished = s.Workspace->Finished();
        if (finished) s.Workspace.reset(); // Observe is called only after GPU completion.
        return finished;
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
