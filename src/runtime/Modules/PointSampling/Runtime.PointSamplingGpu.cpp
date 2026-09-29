module;
#include <algorithm>
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
        return {};
    }

    struct PointSamplingGpuRun::Impl
    {
        std::shared_ptr<Graphics::FarthestPointSamplingWorkspace> Workspace;
        std::vector<glm::vec3> Points{};
        std::vector<double> X{}, Y{}, Z{}, Weights{};
        PS::Params Params{};
        std::size_t Count{0u};
        PS::Result Prefix{};
        bool Began{false};
    };

    PointSamplingGpuRun::PointSamplingGpuRun(RHI::IDevice& device, const std::span<const glm::vec3> points,
                                             const PS::Params& params, const std::size_t count)
        : m_Impl(std::make_unique<Impl>())
    {
        auto& s = *m_Impl;
        s.Workspace = std::make_shared<Graphics::FarthestPointSamplingWorkspace>(device);
        s.Points.assign(points.begin(), points.end());
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
        s.Count = std::min(count, points.size());
        s.Began = s.Workspace->Begin({.X = s.X, .Y = s.Y, .Z = s.Z, .Weights = s.Weights,
                                      .FirstIndex = params.FirstIndex, .Count = std::uint32_t(s.Count)});
    }

    PointSamplingGpuRun::~PointSamplingGpuRun() = default;

    std::shared_ptr<SpatialGpuResult> PointSamplingGpuRun::QueueNext(SpatialIndexCache& cache)
    {
        auto& s = *m_Impl;
        if (!s.Began) return nullptr;
        return cache.QueueGpuCompute(Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(s.Count)),
            [workspace = s.Workspace](RHI::ICommandContext& commands, const SpatialGpuIndexView&) {
                return workspace->RecordNext(commands);
            });
    }

    bool PointSamplingGpuRun::Observe(const SpatialGpuResult& chunk)
    {
        auto& s = *m_Impl;
        const std::size_t produced = s.Workspace->Produced();
        if (chunk.Data.size() != Graphics::FarthestPointSamplingWorkspace::ReadbackBytes(std::uint32_t(s.Count)) ||
            produced > s.Count)
            return true;
        // Entries already published never change; only the new tail is appended.
        std::vector<double> clearance(produced);
        std::vector<std::uint32_t> order(produced);
        std::memcpy(clearance.data(), chunk.Data.data(), produced * sizeof(double));
        std::memcpy(order.data(), chunk.Data.data() + s.Count * sizeof(double), produced * sizeof(std::uint32_t));
        s.Prefix.Order.insert(s.Prefix.Order.end(), order.begin() + std::ptrdiff_t(s.Prefix.Order.size()), order.end());
        s.Prefix.Clearance.insert(s.Prefix.Clearance.end(), clearance.begin() + std::ptrdiff_t(s.Prefix.Clearance.size()),
                                  clearance.end());
        s.Prefix.DistancePairs = std::uint64_t(s.X.size()) * (produced > 0u ? produced - 1u : 0u);
        return s.Workspace->Finished();
    }

    const PS::Result& PointSamplingGpuRun::Current() const noexcept { return m_Impl->Prefix; }

    bool PointSamplingGpuRun::VerifyPrefix(std::string& diagnostic) const
    {
        const auto& s = *m_Impl;
        const std::size_t checked = std::min(s.Prefix.Order.size(), kPointSamplingVerifiedPrefix);
        const PS::Result reference = PS::Order(std::span<const glm::vec3>(s.Points), s.Params, checked);
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
