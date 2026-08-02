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
        constexpr const char* kGraphDebugName = "Runtime.Graph";

        [[nodiscard]] GraphPlanBuildResult Failure(
            GraphPackStatus status,
            GraphPackBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return GraphPlanBuildResult{status, std::nullopt};
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }
    }

    const char* DebugNameForGraphPackStatus(GraphPackStatus status) noexcept
    {
        switch (status)
        {
            case GraphPackStatus::Success:             return "Graph.Success";
            case GraphPackStatus::WrongDomain:         return "Graph.WrongDomain";
            case GraphPackStatus::NoRenderLane:        return "Graph.NoRenderLane";
            case GraphPackStatus::MissingNodes:        return "Graph.MissingNodes";
            case GraphPackStatus::EmptyGraph:          return "Graph.EmptyGraph";
            case GraphPackStatus::MissingEdgeTopology: return "Graph.MissingEdgeTopology";
            case GraphPackStatus::InvalidEdge:         return "Graph.InvalidEdge";
            case GraphPackStatus::NonFinitePosition:   return "Graph.NonFinitePosition";
        }
        return "Graph.Unknown";
    }

    void GraphPackBuffer::Clear() noexcept
    {
        VertexBytes.clear();
        Channels = {};
        PackedColors.clear();
        LineIndices.clear();
    }

    GraphPlanBuildResult BuildGraphGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const bool wantLines,
        const bool wantPoints,
        const GeometryPlanBuildRequest& request,
        GraphPackBuffer& outBuffer)
    {
        return BuildGraphGeometryPlan(
            view, wantLines, wantPoints, nullptr, request, outBuffer);
    }

    GraphPlanBuildResult BuildGraphGeometryPlan(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const bool wantLines,
        const bool wantPoints,
        const VertexChannelBindingSet* channelBindings,
        const GeometryPlanBuildRequest& request,
        GraphPackBuffer& outBuffer)
    {
        outBuffer.Clear();

        using namespace ECS::Components::GeometrySources;

        const SourceAvailability availability = BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != Domain::Graph)
        {
            return Failure(GraphPackStatus::WrongDomain, outBuffer);
        }
        if (!wantLines && !wantPoints)
        {
            return Failure(GraphPackStatus::NoRenderLane, outBuffer);
        }

        if (view.VertexSource == nullptr)
        {
            return Failure(GraphPackStatus::MissingNodes, outBuffer);
        }
        const auto posProp = view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kPosition);
        if (!posProp)
        {
            return Failure(GraphPackStatus::MissingNodes, outBuffer);
        }
        const auto& positions = posProp.Vector();
        const std::size_t nodeCount = positions.size();
        if (nodeCount == 0)
        {
            return Failure(GraphPackStatus::EmptyGraph, outBuffer);
        }

        // Line lane: validate edge endpoints index into the node rows. A graph
        // with an empty `Edges` PropertySet is valid (isolated nodes) and
        // yields no line indices; the line lane is still meaningful for a
        // points+lines entity whose lines are currently empty.
        if (wantLines)
        {
            if (view.EdgeSource == nullptr)
            {
                return Failure(GraphPackStatus::MissingEdgeTopology, outBuffer);
            }
            const auto v0Prop = view.EdgeSource->Properties.Get<std::uint32_t>(PropertyNames::kEdgeV0);
            const auto v1Prop = view.EdgeSource->Properties.Get<std::uint32_t>(PropertyNames::kEdgeV1);
            if (!v0Prop || !v1Prop)
            {
                return Failure(GraphPackStatus::MissingEdgeTopology, outBuffer);
            }
            const auto& v0 = v0Prop.Vector();
            const auto& v1 = v1Prop.Vector();
            if (v0.size() != v1.size())
            {
                return Failure(GraphPackStatus::MissingEdgeTopology, outBuffer);
            }

            const auto nodeCountU32 = static_cast<std::uint32_t>(nodeCount);
            outBuffer.LineIndices.reserve(v0.size() * 2u);
            for (std::size_t e = 0; e < v0.size(); ++e)
            {
                if (v0[e] >= nodeCountU32 || v1[e] >= nodeCountU32)
                {
                    return Failure(GraphPackStatus::InvalidEdge, outBuffer);
                }
                outBuffer.LineIndices.push_back(v0[e]);
                outBuffer.LineIndices.push_back(v1[e]);
            }
        }

        outBuffer.VertexBytes.resize(sizeof(GraphVertex) * nodeCount);
        auto* vData = reinterpret_cast<GraphVertex*>(outBuffer.VertexBytes.data());
        const auto nodeCountU32 = static_cast<std::uint32_t>(nodeCount);

        constexpr float kInf = std::numeric_limits<float>::infinity();
        glm::vec3 minP{+kInf, +kInf, +kInf};
        glm::vec3 maxP{-kInf, -kInf, -kInf};

        for (std::size_t i = 0; i < nodeCount; ++i)
        {
            const glm::vec3 p = positions[i];
            if (!IsFinite(p))
            {
                return Failure(GraphPackStatus::NonFinitePosition, outBuffer);
            }
            vData[i] = GraphVertex{p.x, p.y, p.z, 0.0f, 0.0f};
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }

        std::vector<glm::vec2> texcoords(nodeCount, glm::vec2{0.0f, 0.0f});
        outBuffer.Channels.SetVertexCount(nodeCountU32);
        SetChannelVec3(
            outBuffer.Channels,
            VertexChannel::Position,
            std::span<const glm::vec3>{positions.data(), positions.size()});
        SetChannelVec2(
            outBuffer.Channels,
            VertexChannel::Texcoord,
            std::span<const glm::vec2>{texcoords.data(), texcoords.size()});
        if (channelBindings != nullptr && IsVertexChannelBindingEnabled(channelBindings->Normal))
        {
            const std::optional<AttributeSourceType> sourceType =
                channelBindings->Normal.Property.Domain ==
                        GeometryElementDomain::GraphNode
                    ? ToAttributeSourceType(
                          channelBindings->Normal.Property.ValueKind)
                    : std::nullopt;
            std::vector<glm::vec3> normals(nodeCount);
            const VertexAttributeBinding normalBinding{
                .Channel = VertexChannel::Normal,
                .SourceType = sourceType.value_or(AttributeSourceType::Vec3),
                .SourceProperty = sourceType == AttributeSourceType::Vec3
                    ? std::string_view{channelBindings->Normal.Property.Name}
                    : std::string_view{},
                .AllowFallback = false,
                .Normalize = true,
                .Fallback = glm::vec4{0.0f, 0.0f, 1.0f, 0.0f},
            };
            const AttributeBindResult normalResult =
                ResolveVec3Channel(
                    view.VertexSource->Properties,
                    normalBinding,
                    nodeCountU32,
                    normals);
            if (normalResult.Ok())
            {
                SetChannelVec3(
                    outBuffer.Channels,
                    VertexChannel::Normal,
                    std::span<const glm::vec3>{normals.data(), normals.size()});
            }
        }
        if (channelBindings != nullptr && IsVertexChannelBindingEnabled(channelBindings->Color))
        {
            const std::optional<AttributeSourceType> sourceType =
                channelBindings->Color.Property.Domain ==
                        GeometryElementDomain::GraphNode
                    ? ToAttributeSourceType(
                          channelBindings->Color.Property.ValueKind)
                    : std::nullopt;
            if (sourceType == AttributeSourceType::Vec3 ||
                sourceType == AttributeSourceType::Vec4)
            {
                outBuffer.PackedColors.resize(nodeCount);
                const VertexAttributeBinding colorBinding{
                    .Channel = VertexChannel::Color,
                    .SourceType = *sourceType,
                    .SourceProperty = std::string_view{
                        channelBindings->Color.Property.Name},
                    .AllowFallback = false,
                    .Normalize = false,
                    .Fallback = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                };
                const AttributeBindResult colorResult =
                    ResolveColorChannelPackedUnorm8(
                        view.VertexSource->Properties,
                        colorBinding,
                        nodeCountU32,
                        outBuffer.PackedColors);
                if (colorResult.Ok())
                {
                    SetChannelPackedUnorm8(
                        outBuffer.Channels,
                        VertexChannel::Color,
                        std::span<const std::uint32_t>{outBuffer.PackedColors});
                }
                else
                {
                    outBuffer.PackedColors.clear();
                }
            }
        }

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
        desc.LineIndices = wantLines
            ? std::span<const std::uint32_t>{outBuffer.LineIndices}
            : std::span<const std::uint32_t>{};
        desc.VertexCount = nodeCountU32;

        const glm::vec3 center = 0.5f * (minP + maxP);
        const float radius = 0.5f * glm::length(maxP - minP);
        desc.LocalBounds.LocalSphere = glm::vec4{center, radius};
        desc.DebugName = kGraphDebugName;

        return GraphPlanBuildResult{
            GraphPackStatus::Success,
            Graphics::MakeGeometryUploadPlan(
                request.Key,
                request.Generation,
                desc,
                request.UpdateClass,
                request.UpdateChannels),
        };
    }
}
