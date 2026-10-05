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

import Geometry.Validation;
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
    void PrepareBoundVertexChannels(
        const Geometry::PropertySet& properties,
        const GeometryElementDomain domain,
        const VertexChannelBindingSet* channelBindings,
        const std::size_t vertexCount,
        VertexChannelStreams& channels)
    {
        if (channelBindings != nullptr && IsVertexChannelBindingEnabled(channelBindings->Normal))
        {
            const std::optional<AttributeSourceType> sourceType =
                channelBindings->Normal.Property.Domain == domain
                    ? ToAttributeSourceType(
                          channelBindings->Normal.Property.ValueKind)
                    : std::nullopt;
            std::vector<glm::vec3> normals(vertexCount);
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
                    properties,
                    normalBinding,
                    static_cast<std::uint32_t>(vertexCount),
                    normals);
            if (normalResult.Ok())
            {
                SetChannelVec3(
                    channels,
                    VertexChannel::Normal,
                    std::span<const glm::vec3>{normals.data(), normals.size()});
            }
        }
    }

    namespace
    {
        using Geometry::Validation::IsFinite;

        constexpr const char* kGraphDebugName = "Runtime.Graph";

        [[nodiscard]] GraphPlanBuildResult Failure(
            GraphPackStatus status,
            GraphPackBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return GraphPlanBuildResult{status, std::nullopt};
        }

    }


    void GraphPackBuffer::Clear() noexcept
    {
        VertexBytes.clear();
        Channels = {};
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
        const std::span<const glm::vec3> positions = ResolveDisplayedPositions(
            view.VertexSource->Properties, GeometryElementDomain::GraphNode, channelBindings).Values;
        if (positions.empty() && !view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kPosition))
        {
            return Failure(GraphPackStatus::MissingNodes, outBuffer);
        }
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
        PrepareBoundVertexChannels(
            view.VertexSource->Properties, GeometryElementDomain::GraphNode,
            channelBindings, nodeCount, outBuffer.Channels);

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
