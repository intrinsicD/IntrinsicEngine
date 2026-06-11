module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.GraphGeometryPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr const char* kGraphDebugName = "Runtime.Graph";
        constexpr glm::vec2 kNoNormalUv{2.0f, 2.0f};

        [[nodiscard]] GraphPackResult Failure(GraphPackStatus status, GraphPackBuffer& outBuffer) noexcept
        {
            outBuffer.Clear();
            return GraphPackResult{status, std::nullopt};
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
        LineIndices.clear();
    }

    GraphPackResult PackGraph(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const bool wantLines,
        const bool wantPoints,
        GraphPackBuffer& outBuffer)
    {
        outBuffer.Clear();

        using namespace ECS::Components::GeometrySources;

        if (view.ActiveDomain != Domain::Graph)
        {
            return Failure(GraphPackStatus::WrongDomain, outBuffer);
        }
        if (!wantLines && !wantPoints)
        {
            return Failure(GraphPackStatus::NoRenderLane, outBuffer);
        }

        if (view.NodeSource == nullptr)
        {
            return Failure(GraphPackStatus::MissingNodes, outBuffer);
        }
        const auto posProp = view.NodeSource->Properties.Get<glm::vec3>(PropertyNames::kPosition);
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
            vData[i] = GraphVertex{p.x, p.y, p.z, kNoNormalUv.x, kNoNormalUv.y};
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }

        Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
        desc.SurfaceIndices = {};
        desc.LineIndices = wantLines
            ? std::span<const std::uint32_t>{outBuffer.LineIndices}
            : std::span<const std::uint32_t>{};
        desc.VertexCount = static_cast<std::uint32_t>(nodeCount);

        const glm::vec3 center = 0.5f * (minP + maxP);
        const float radius = 0.5f * glm::length(maxP - minP);
        desc.LocalBounds.LocalSphere = glm::vec4{center, radius};
        desc.DebugName = kGraphDebugName;

        return GraphPackResult{GraphPackStatus::Success, desc};
    }
}
