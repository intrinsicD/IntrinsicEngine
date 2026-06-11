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
        constexpr glm::vec2 kNoNormalUv{2.0f, 2.0f};

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

        [[nodiscard]] float SignNotZero(const float value) noexcept
        {
            return value < 0.0f ? -1.0f : 1.0f;
        }

        [[nodiscard]] glm::vec2 EncodeOctNormal(glm::vec3 normal) noexcept
        {
            const float len = glm::length(normal);
            if (!std::isfinite(len) || len <= 1.0e-6f)
            {
                return kNoNormalUv;
            }
            normal /= len;

            const float denom =
                std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z);
            if (denom <= 1.0e-6f)
            {
                return kNoNormalUv;
            }

            glm::vec2 encoded{normal.x / denom, normal.y / denom};
            if (normal.z < 0.0f)
            {
                encoded = glm::vec2{
                    (1.0f - std::abs(encoded.y)) * SignNotZero(encoded.x),
                    (1.0f - std::abs(encoded.x)) * SignNotZero(encoded.y),
                };
            }
            return encoded;
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
        const auto normalProp =
            view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kNormal);
        const std::vector<glm::vec3>* normals = nullptr;
        if (normalProp && normalProp.Vector().size() == pointCount)
        {
            normals = &normalProp.Vector();
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
            const glm::vec2 normalUv =
                normals != nullptr ? EncodeOctNormal((*normals)[i]) : kNoNormalUv;
            vData[i] = PointCloudVertex{p.x, p.y, p.z, normalUv.x, normalUv.y};
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
