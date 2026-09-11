module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.GeometryPlanBuilders;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.GeometryResidency;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.VertexChannelStreams;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr const char* kCloudDebugName = "Runtime.PointCloud";

        [[nodiscard]] PointCloudPlanBuildResult Failure(
            PointCloudPackStatus status,
            PointCloudPackBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return PointCloudPlanBuildResult{status, std::nullopt};
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
        Channels = {};
        PackedColors.clear();
    }

    PointCloudPlanBuildResult BuildPointCloudGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const GeometryPlanBuildRequest& request,
        PointCloudPackBuffer& outBuffer)
    {
        return BuildPointCloudGeometryPlan(
            view, nullptr, request, outBuffer);
    }

    PointCloudPlanBuildResult BuildPointCloudGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        PointCloudPackBuffer& outBuffer)
    {
        outBuffer.Clear();

        using namespace ECS::Components::GeometrySources;

        const SourceAvailability availability = BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != Domain::PointCloud)
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

        const auto pointCountU32 = static_cast<std::uint32_t>(pointCount);
        std::vector<glm::vec2> texcoords(pointCount, glm::vec2{0.0f, 0.0f});
        outBuffer.Channels.SetVertexCount(pointCountU32);
        SetChannelVec3(
            outBuffer.Channels,
            VertexChannel::Position,
            std::span<const glm::vec3>{positions.data(), positions.size()});
        SetChannelVec2(
            outBuffer.Channels,
            VertexChannel::Texcoord,
            std::span<const glm::vec2>{texcoords.data(), texcoords.size()});
        PrepareBoundVertexChannels(
            view.VertexSource->Properties, GeometryElementDomain::PointCloudPoint,
            channelBindings, pointCount, outBuffer.Channels, outBuffer.PackedColors);

        const auto channelBytes = [&outBuffer](const VertexChannel channel) -> std::span<const std::byte> {
            const VertexChannelStreams::Stream* stream = outBuffer.Channels.Find(channel);
            return stream != nullptr ? std::span<const std::byte>{stream->Bytes}
                                     : std::span<const std::byte>{};
        };

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.PositionBytes = channelBytes(VertexChannel::Position);
        desc.TexcoordBytes = channelBytes(VertexChannel::Texcoord);
        desc.NormalBytes = channelBytes(VertexChannel::Normal);
        desc.PackedVertexColors = std::span<const std::uint32_t>{outBuffer.PackedColors};
        desc.SurfaceIndices = {};
        desc.LineIndices = {};
        desc.VertexCount = pointCountU32;

        const glm::vec3 center = 0.5f * (minP + maxP);
        const float radius = 0.5f * glm::length(maxP - minP);
        desc.LocalBounds.LocalSphere = glm::vec4{center, radius};
        desc.DebugName = kCloudDebugName;

        return PointCloudPlanBuildResult{
            PointCloudPackStatus::Success,
            Graphics::MakeGeometryUploadPlan(
                request.Key,
                request.Generation,
                desc,
                request.UpdateClass,
                request.UpdateChannels),
        };
    }
}
