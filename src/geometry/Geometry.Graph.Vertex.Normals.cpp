module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

module Geometry.Graph.Vertex.Normals;

import Geometry.Graph;
import Geometry.PCA;
import Geometry.Properties;

namespace Geometry::Graph::VertexNormals
{
    namespace
    {
        constexpr glm::vec3 kDefaultFallbackNormal{0.0f, 0.0f, 1.0f};

        struct EdgeEndpoints
        {
            VertexHandle A{};
            VertexHandle B{};
        };

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        void NormalizeFallback(const Params& params,
                               Diagnostics& diagnostics,
                               glm::vec3& out) noexcept
        {
            if (IsFinite(params.FallbackNormal))
            {
                const double lenSq = static_cast<double>(glm::dot(params.FallbackNormal, params.FallbackNormal));
                const double epsilon = params.DegenerateNormalLengthEpsilon > 0.0
                    ? params.DegenerateNormalLengthEpsilon
                    : Params{}.DegenerateNormalLengthEpsilon;
                if (std::isfinite(lenSq) && lenSq > epsilon * epsilon)
                {
                    out = params.FallbackNormal * static_cast<float>(1.0 / std::sqrt(lenSq));
                    return;
                }
            }

            out = kDefaultFallbackNormal;
            diagnostics.FallbackNormalWasRepaired = true;
        }

        [[nodiscard]] bool PropertyMatchesSize(const ConstProperty<glm::vec3>& property,
                                               const std::size_t size) noexcept
        {
            return property.IsValid() && property.Vector().size() == size;
        }

        [[nodiscard]] bool PropertyMatchesSize(const ConstProperty<bool>& property,
                                               const std::size_t size) noexcept
        {
            return !property.IsValid() || property.Vector().size() == size;
        }

        [[nodiscard]] bool ReadEdgeEndpoints(const ConstProperty<HalfedgeConnectivity>& halfedgeConnectivity,
                                             const std::size_t edgeSlotCount,
                                             const std::size_t vertexSlotCount,
                                             const std::size_t edgeIndex,
                                             EdgeEndpoints& endpoints) noexcept
        {
            const std::size_t h0Index = edgeIndex * 2u;
            const std::size_t h1Index = h0Index + 1u;
            if (!halfedgeConnectivity.IsValid()
                || h1Index >= halfedgeConnectivity.Vector().size()
                || edgeIndex >= edgeSlotCount)
            {
                return false;
            }

            const auto h0 = HalfedgeHandle{static_cast<PropertyIndex>(h0Index)};
            const auto h1 = HalfedgeHandle{static_cast<PropertyIndex>(h1Index)};
            endpoints.A = halfedgeConnectivity[h1.Index].Vertex;
            endpoints.B = halfedgeConnectivity[h0.Index].Vertex;
            return endpoints.A.IsValid()
                && endpoints.B.IsValid()
                && endpoints.A.Index < vertexSlotCount
                && endpoints.B.Index < vertexSlotCount
                && endpoints.A != endpoints.B;
        }

        void BuildAdjacency(const ConstProperty<HalfedgeConnectivity>& halfedgeConnectivity,
                            const ConstProperty<bool>& edgeDeleted,
                            const Params& params,
                            PropertySetResult& result,
                            std::vector<std::vector<std::size_t>>& adjacency)
        {
            adjacency.assign(result.Diagnostics.VertexSlotCount, {});

            for (std::size_t edgeIndex = 0; edgeIndex < result.Diagnostics.EdgeSlotCount; ++edgeIndex)
            {
                const auto edge = EdgeHandle{static_cast<PropertyIndex>(edgeIndex)};
                if (params.SkipDeleted && edgeDeleted.IsValid() && edgeDeleted[edge.Index])
                {
                    ++result.Diagnostics.SkippedDeletedEdgeCount;
                    continue;
                }

                EdgeEndpoints endpoints{};
                if (!ReadEdgeEndpoints(halfedgeConnectivity,
                                       result.Diagnostics.EdgeSlotCount,
                                       result.Diagnostics.VertexSlotCount,
                                       edgeIndex,
                                       endpoints))
                {
                    ++result.Diagnostics.InvalidEdgeCount;
                    continue;
                }

                adjacency[endpoints.A.Index].push_back(endpoints.B.Index);
                adjacency[endpoints.B.Index].push_back(endpoints.A.Index);
            }

            for (auto& neighbors : adjacency)
            {
                std::sort(neighbors.begin(), neighbors.end());
                neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
            }
        }

        [[nodiscard]] bool IsDeletedVertex(const ConstProperty<bool>& vertexDeleted,
                                           const VertexHandle vertex,
                                           const Params& params) noexcept
        {
            return params.SkipDeleted && vertexDeleted.IsValid() && vertexDeleted[vertex.Index];
        }

        [[nodiscard]] bool IsCollinearNeighborhood(const PCAResult& pca,
                                                   const Params& params) noexcept
        {
            const float largest = std::max(pca.Eigenvalues.x, 0.0f);
            if (largest <= static_cast<float>(params.DegenerateNormalLengthEpsilon))
            {
                return true;
            }
            return pca.Eigenvalues.y <= largest * static_cast<float>(params.CollinearEigenvalueRatioEpsilon);
        }

        [[nodiscard]] bool NormalizeNormal(glm::vec3& normal,
                                           const Params& params) noexcept
        {
            const double lenSq = static_cast<double>(glm::dot(normal, normal));
            const double epsilon = params.DegenerateNormalLengthEpsilon > 0.0
                ? params.DegenerateNormalLengthEpsilon
                : Params{}.DegenerateNormalLengthEpsilon;
            if (!std::isfinite(lenSq) || lenSq <= epsilon * epsilon)
            {
                return false;
            }
            normal *= static_cast<float>(1.0 / std::sqrt(lenSq));
            return IsFinite(normal);
        }
    } // namespace

    std::string_view DebugName(const RecomputeStatus status) noexcept
    {
        switch (status)
        {
        case RecomputeStatus::Success:
            return "Success";
        case RecomputeStatus::EmptyGraph:
            return "EmptyGraph";
        case RecomputeStatus::InvalidPositionProperty:
            return "InvalidPositionProperty";
        case RecomputeStatus::InvalidTopologyProperty:
            return "InvalidTopologyProperty";
        case RecomputeStatus::InvalidOutputProperty:
            return "InvalidOutputProperty";
        case RecomputeStatus::PropertyTypeConflict:
            return "PropertyTypeConflict";
        case RecomputeStatus::CountMismatch:
            return "CountMismatch";
        }

        return "Unknown";
    }

    PropertySetResult Recompute(Vertices& vertices,
                                const ConstProperty<glm::vec3> positions,
                                const ConstProperty<HalfedgeConnectivity> halfedgeConnectivity,
                                const std::size_t edgeSlotCount,
                                const Params& params,
                                const ConstProperty<bool> vertexDeleted,
                                const ConstProperty<bool> edgeDeleted)
    {
        PropertySetResult result{};
        result.Diagnostics.VertexSlotCount = vertices.Size();
        result.Diagnostics.EdgeSlotCount = edgeSlotCount;

        if (params.OutputProperty.empty())
        {
            result.Status = RecomputeStatus::InvalidOutputProperty;
            return result;
        }

        glm::vec3 fallbackNormal{0.0f};
        NormalizeFallback(params, result.Diagnostics, fallbackNormal);

        auto normals = vertices.GetOrAdd<glm::vec3>(std::string(params.OutputProperty), fallbackNormal);
        if (!normals.IsValid())
        {
            result.Status = RecomputeStatus::PropertyTypeConflict;
            return result;
        }
        result.Normals = normals;

        if (vertices.Size() == 0u)
        {
            result.Status = RecomputeStatus::EmptyGraph;
            return result;
        }

        if (!PropertyMatchesSize(positions, vertices.Size()))
        {
            result.Status = positions.IsValid()
                ? RecomputeStatus::CountMismatch
                : RecomputeStatus::InvalidPositionProperty;
            return result;
        }

        if (!halfedgeConnectivity.IsValid()
            || halfedgeConnectivity.Vector().size() < edgeSlotCount * 2u)
        {
            result.Status = RecomputeStatus::InvalidTopologyProperty;
            return result;
        }

        if (!PropertyMatchesSize(vertexDeleted, vertices.Size())
            || !PropertyMatchesSize(edgeDeleted, edgeSlotCount))
        {
            result.Status = RecomputeStatus::CountMismatch;
            return result;
        }

        std::vector<std::vector<std::size_t>> adjacency;
        BuildAdjacency(halfedgeConnectivity, edgeDeleted, params, result, adjacency);

        std::vector<glm::vec3> samples;
        for (std::size_t vertexIndex = 0; vertexIndex < vertices.Size(); ++vertexIndex)
        {
            const auto vertex = VertexHandle{static_cast<PropertyIndex>(vertexIndex)};
            normals[vertexIndex] = fallbackNormal;

            if (IsDeletedVertex(vertexDeleted, vertex, params))
            {
                ++result.Diagnostics.SkippedDeletedVertexCount;
                continue;
            }

            const glm::vec3 center = positions[vertexIndex];
            if (!IsFinite(center))
            {
                ++result.Diagnostics.NonFinitePositionCount;
                ++result.Diagnostics.FallbackVertexCount;
                ++result.Diagnostics.WrittenCount;
                continue;
            }

            const auto& neighbors = adjacency[vertexIndex];
            if (neighbors.empty())
            {
                ++result.Diagnostics.IsolatedVertexCount;
                ++result.Diagnostics.FallbackVertexCount;
                ++result.Diagnostics.WrittenCount;
                continue;
            }
            if (neighbors.size() == 1u)
            {
                ++result.Diagnostics.DegreeOneVertexCount;
                ++result.Diagnostics.FallbackVertexCount;
                ++result.Diagnostics.WrittenCount;
                continue;
            }

            samples.clear();
            samples.push_back(center);
            for (const std::size_t neighborIndex : neighbors)
            {
                const auto neighbor = VertexHandle{static_cast<PropertyIndex>(neighborIndex)};
                if (IsDeletedVertex(vertexDeleted, neighbor, params))
                {
                    continue;
                }

                const glm::vec3 neighborPosition = positions[neighborIndex];
                if (!IsFinite(neighborPosition))
                {
                    ++result.Diagnostics.NonFinitePositionCount;
                    continue;
                }

                const glm::vec3 delta = neighborPosition - center;
                const double distSq = static_cast<double>(glm::dot(delta, delta));
                const double epsilon = params.DegenerateNormalLengthEpsilon > 0.0
                    ? params.DegenerateNormalLengthEpsilon
                    : Params{}.DegenerateNormalLengthEpsilon;
                if (!std::isfinite(distSq) || distSq <= epsilon * epsilon)
                {
                    ++result.Diagnostics.DuplicatePositionCount;
                }
                samples.push_back(neighborPosition);
            }

            if (samples.size() < 3u)
            {
                ++result.Diagnostics.CollinearNeighborhoodCount;
                ++result.Diagnostics.FallbackVertexCount;
                ++result.Diagnostics.WrittenCount;
                continue;
            }

            const PCAResult pca = ToPCA(samples);
            if (!pca.Valid || IsCollinearNeighborhood(pca, params))
            {
                ++result.Diagnostics.CollinearNeighborhoodCount;
                ++result.Diagnostics.FallbackVertexCount;
                ++result.Diagnostics.WrittenCount;
                continue;
            }

            glm::vec3 normal = pca.Eigenvectors[2];
            if (!NormalizeNormal(normal, params))
            {
                ++result.Diagnostics.CollinearNeighborhoodCount;
                ++result.Diagnostics.FallbackVertexCount;
                ++result.Diagnostics.WrittenCount;
                continue;
            }

            if (params.OrientTowardFallback && glm::dot(normal, fallbackNormal) < 0.0f)
            {
                normal = -normal;
            }

            normals[vertexIndex] = normal;
            ++result.Diagnostics.ValidNormalVertexCount;
            ++result.Diagnostics.WrittenCount;
        }

        return result;
    }

    Result Recompute(Graph& graph, const Params& params)
    {
        Result result{};
        const auto positions = graph.VertexProperties().Get<glm::vec3>(params.PositionProperty);
        const auto halfedgeConnectivity =
            graph.HalfedgeProperties().Get<HalfedgeConnectivity>("h:connectivity");
        const auto vertexDeleted = graph.VertexProperties().Get<bool>("v:deleted");
        const auto edgeDeleted = graph.EdgeProperties().Get<bool>("e:deleted");

        const PropertySetResult raw = Recompute(graph.VertexProperties(),
                                                ConstPropertySet(graph.VertexProperties()).Get<glm::vec3>(params.PositionProperty),
                                                ConstPropertySet(graph.HalfedgeProperties()).Get<HalfedgeConnectivity>("h:connectivity"),
                                                graph.EdgesSize(),
                                                params,
                                                ConstPropertySet(graph.VertexProperties()).Get<bool>("v:deleted"),
                                                ConstPropertySet(graph.EdgeProperties()).Get<bool>("e:deleted"));

        static_cast<void>(positions);
        static_cast<void>(halfedgeConnectivity);
        static_cast<void>(vertexDeleted);
        static_cast<void>(edgeDeleted);

        result.Status = raw.Status;
        result.Diagnostics = raw.Diagnostics;
        if (raw.Normals.IsValid())
        {
            result.Normals = VertexProperty<glm::vec3>(raw.Normals);
        }
        return result;
    }
} // namespace Geometry::Graph::VertexNormals
