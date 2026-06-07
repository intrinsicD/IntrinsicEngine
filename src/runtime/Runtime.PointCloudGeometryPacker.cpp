module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.PointCloudGeometryPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr const char* kCloudDebugName = "Runtime.PointCloud";

        [[nodiscard]] PointCloudPackResult Failure(PointCloudPackStatus status,
                                                   PointCloudPackBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return PointCloudPackResult{status, std::nullopt};
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }
    }

    const char* DebugNameForPointCloudPackStatus(PointCloudPackStatus status) noexcept
    {
        switch (status)
        {
            case PointCloudPackStatus::Success:           return "PointCloud.Success";
            case PointCloudPackStatus::WrongDomain:       return "PointCloud.WrongDomain";
            case PointCloudPackStatus::MissingPositions:  return "PointCloud.MissingPositions";
            case PointCloudPackStatus::EmptyCloud:        return "PointCloud.EmptyCloud";
            case PointCloudPackStatus::NonFinitePosition: return "PointCloud.NonFinitePosition";
        }
        return "PointCloud.Unknown";
    }

    void PointCloudPackBuffer::Clear() noexcept
    {
        VertexBytes.clear();
    }

    PointCloudPackResult PackCloud(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        PointCloudPackBuffer& outBuffer)
    {
        outBuffer.Clear();

        using namespace ECS::Components::GeometrySources;

        if (view.ActiveDomain != Domain::PointCloud)
        {
            return Failure(PointCloudPackStatus::WrongDomain, outBuffer);
        }

        if (view.VertexSource == nullptr)
        {
            return Failure(PointCloudPackStatus::MissingPositions, outBuffer);
        }
        const auto posProp = view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kPosition);
        if (!posProp)
        {
            return Failure(PointCloudPackStatus::MissingPositions, outBuffer);
        }
        const auto& positions = posProp.Vector();
        const std::size_t pointCount = positions.size();
        if (pointCount == 0)
        {
            return Failure(PointCloudPackStatus::EmptyCloud, outBuffer);
        }

        outBuffer.VertexBytes.resize(sizeof(PointCloudVertex) * pointCount);
        auto* vData = reinterpret_cast<PointCloudVertex*>(outBuffer.VertexBytes.data());

        constexpr float kInf = std::numeric_limits<float>::infinity();
        glm::vec3 minP{+kInf, +kInf, +kInf};
        glm::vec3 maxP{-kInf, -kInf, -kInf};

        for (std::size_t i = 0; i < pointCount; ++i)
        {
            const glm::vec3 p = positions[i];
            if (!IsFinite(p))
            {
                return Failure(PointCloudPackStatus::NonFinitePosition, outBuffer);
            }
            vData[i] = PointCloudVertex{p.x, p.y, p.z, 0.0f, 0.0f};
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.SurfaceIndices = {};
        desc.LineIndices = {};
        desc.VertexCount = static_cast<std::uint32_t>(pointCount);

        const glm::vec3 center = 0.5f * (minP + maxP);
        const float radius = 0.5f * glm::length(maxP - minP);
        desc.LocalBounds.LocalSphere = glm::vec4{center, radius};
        desc.DebugName = kCloudDebugName;

        return PointCloudPackResult{PointCloudPackStatus::Success, desc};
    }
}
