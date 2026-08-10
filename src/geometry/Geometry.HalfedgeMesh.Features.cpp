module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <span>
#include <string>

#include <glm/glm.hpp>

module Geometry.HalfedgeMesh.Features;

namespace Geometry::HalfedgeMesh::Features
{
    namespace
    {
        struct EdgeEvaluation
        {
            bool IsFeature{false};
            bool IsBoundaryFeature{false};
            bool HasInvalidNormal{false};
            bool HasInvalidTopology{false};
        };

        [[nodiscard]] bool IsValidParams(const Params& params) noexcept
        {
            return std::isfinite(params.DihedralThresholdDegrees)
                && params.DihedralThresholdDegrees >= 0.0
                && params.DihedralThresholdDegrees <= 180.0;
        }

        [[nodiscard]] bool TryNormalize(
            const glm::dvec3 value,
            glm::dvec3& normalized) noexcept
        {
            if (!std::isfinite(value.x)
                || !std::isfinite(value.y)
                || !std::isfinite(value.z))
            {
                return false;
            }

            const double scale = std::max({
                std::abs(value.x),
                std::abs(value.y),
                std::abs(value.z)});
            if (!std::isfinite(scale) || scale <= 0.0)
            {
                return false;
            }

            // Spell out component division: some vector libraries implement
            // v / s as v * (1 / s), whose reciprocal overflows for subnormal s.
            const glm::dvec3 scaled{
                value.x / scale,
                value.y / scale,
                value.z / scale};
            const double scaledLength = std::sqrt(glm::dot(scaled, scaled));
            if (!std::isfinite(scaledLength) || scaledLength <= 0.0)
            {
                return false;
            }

            normalized = scaled / scaledLength;
            return std::isfinite(normalized.x)
                && std::isfinite(normalized.y)
                && std::isfinite(normalized.z);
        }

        [[nodiscard]] bool IsLiveFace(const Mesh& mesh, const FaceHandle face) noexcept
        {
            return face.IsValid() && mesh.IsValid(face) && !mesh.IsDeleted(face);
        }

        [[nodiscard]] EdgeEvaluation EvaluateEdge(
            const Mesh& mesh,
            const EdgeHandle edge,
            const std::span<const glm::dvec3> faceNormals,
            const Params& params,
            const double angleThresholdRadians) noexcept
        {
            EdgeEvaluation evaluation{};
            if (!edge.IsValid() || !mesh.IsValid(edge) || mesh.IsDeleted(edge))
            {
                evaluation.IsFeature = true;
                evaluation.HasInvalidTopology = true;
                return evaluation;
            }

            const HalfedgeHandle h0 = mesh.Halfedge(edge, 0u);
            const HalfedgeHandle h1 = mesh.Halfedge(edge, 1u);
            if (!h0.IsValid() || !h1.IsValid()
                || !mesh.IsValid(h0) || !mesh.IsValid(h1))
            {
                evaluation.IsFeature = true;
                evaluation.HasInvalidTopology = true;
                return evaluation;
            }

            const FaceHandle f0 = mesh.Face(h0);
            const FaceHandle f1 = mesh.Face(h1);
            const bool f0Live = IsLiveFace(mesh, f0);
            const bool f1Live = IsLiveFace(mesh, f1);

            // Exactly one live face paired with the invalid-face sentinel is a
            // regular open boundary. Deleted/out-of-range faces are malformed
            // adjacency and remain fail-closed regardless of boundary policy.
            const bool cleanBoundary =
                (f0Live && !f1.IsValid()) || (f1Live && !f0.IsValid());
            if (cleanBoundary)
            {
                const FaceHandle boundaryFace = f0Live ? f0 : f1;
                if (boundaryFace.Index >= faceNormals.size())
                {
                    evaluation.IsFeature = true;
                    evaluation.HasInvalidTopology = true;
                    return evaluation;
                }

                glm::dvec3 boundaryNormal{0.0};
                if (!TryNormalize(faceNormals[boundaryFace.Index], boundaryNormal))
                {
                    evaluation.IsFeature = true;
                    evaluation.HasInvalidNormal = true;
                    return evaluation;
                }

                evaluation.IsFeature = params.BoundaryIsFeature;
                evaluation.IsBoundaryFeature = evaluation.IsFeature;
                return evaluation;
            }

            if (!f0Live || !f1Live)
            {
                evaluation.IsFeature = true;
                evaluation.HasInvalidTopology = true;
                return evaluation;
            }

            if (f0.Index >= faceNormals.size() || f1.Index >= faceNormals.size())
            {
                evaluation.IsFeature = true;
                evaluation.HasInvalidTopology = true;
                return evaluation;
            }

            glm::dvec3 n0{0.0};
            glm::dvec3 n1{0.0};
            if (!TryNormalize(faceNormals[f0.Index], n0)
                || !TryNormalize(faceNormals[f1.Index], n1))
            {
                evaluation.IsFeature = true;
                evaluation.HasInvalidNormal = true;
                return evaluation;
            }

            const double dot = std::clamp(glm::dot(n0, n1), -1.0, 1.0);
            evaluation.IsFeature = std::acos(dot) > angleThresholdRadians;
            return evaluation;
        }

        [[nodiscard]] VertexIncidenceCategory CategoryForCount(
            const std::size_t count) noexcept
        {
            if (count == 0u)
            {
                return VertexIncidenceCategory::None;
            }
            if (count == 1u)
            {
                return VertexIncidenceCategory::Endpoint;
            }
            if (count == 2u)
            {
                return VertexIncidenceCategory::Crease;
            }
            return VertexIncidenceCategory::Junction;
        }

        void IncrementIncidentCount(
            const Mesh& mesh,
            const VertexHandle vertex,
            std::vector<std::size_t>& counts) noexcept
        {
            if (vertex.IsValid()
                && mesh.IsValid(vertex)
                && !mesh.IsDeleted(vertex)
                && vertex.Index < counts.size())
            {
                ++counts[vertex.Index];
            }
        }
    } // namespace

    Classification Classify(
        const Mesh& mesh,
        const std::span<const glm::dvec3> faceNormals,
        const Params& params)
    {
        Classification result{};
        if (mesh.IsSubmeshView())
        {
            result.Status = ClassificationStatus::UnsupportedSubmeshView;
            return result;
        }
        if (!IsValidParams(params))
        {
            result.Status = ClassificationStatus::InvalidDihedralThreshold;
            return result;
        }
        if (faceNormals.size() != mesh.FacesSize())
        {
            result.Status = ClassificationStatus::FaceNormalSlotCountMismatch;
            return result;
        }

        result.EdgeFeatureMask.assign(mesh.EdgesSize(), 0u);
        result.IncidentFeatureEdgeCount.assign(mesh.VerticesSize(), 0u);
        result.VertexIncidence.assign(
            mesh.VerticesSize(), VertexIncidenceCategory::None);

        const double angleThresholdRadians =
            params.DihedralThresholdDegrees
            * std::numbers::pi / 180.0;

        for (std::size_t edgeIndex = 0u; edgeIndex < mesh.EdgesSize(); ++edgeIndex)
        {
            const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
            if (mesh.IsDeleted(edge))
            {
                ++result.DeletedEdgeSlotCount;
                continue;
            }

            const EdgeEvaluation evaluation = EvaluateEdge(
                mesh, edge, faceNormals, params, angleThresholdRadians);
            if (!evaluation.IsFeature)
            {
                continue;
            }

            result.EdgeFeatureMask[edgeIndex] = 1u;
            ++result.FeatureEdgeCount;
            result.BoundaryFeatureEdgeCount += evaluation.IsBoundaryFeature ? 1u : 0u;
            result.InvalidNormalFeatureEdgeCount += evaluation.HasInvalidNormal ? 1u : 0u;
            result.InvalidTopologyFeatureEdgeCount += evaluation.HasInvalidTopology ? 1u : 0u;

            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            if (!halfedge.IsValid() || !mesh.IsValid(halfedge))
            {
                continue;
            }
            const VertexHandle from = mesh.FromVertex(halfedge);
            const VertexHandle to = mesh.ToVertex(halfedge);
            IncrementIncidentCount(mesh, from, result.IncidentFeatureEdgeCount);
            if (to != from)
            {
                IncrementIncidentCount(mesh, to, result.IncidentFeatureEdgeCount);
            }
        }

        for (std::size_t vertexIndex = 0u;
             vertexIndex < result.IncidentFeatureEdgeCount.size();
             ++vertexIndex)
        {
            result.VertexIncidence[vertexIndex] =
                CategoryForCount(result.IncidentFeatureEdgeCount[vertexIndex]);
        }

        return result;
    }

    bool IsFeatureEdgeFailClosed(
        const Mesh& mesh,
        const EdgeHandle edge,
        const std::span<const glm::dvec3> faceNormals,
        const Params& params) noexcept
    {
        if (mesh.IsSubmeshView()
            || !IsValidParams(params)
            || faceNormals.size() != mesh.FacesSize()
            || !edge.IsValid()
            || !mesh.IsValid(edge)
            || mesh.IsDeleted(edge))
        {
            return true;
        }

        const double angleThresholdRadians =
            params.DihedralThresholdDegrees
            * std::numbers::pi / 180.0;
        return EvaluateEdge(
            mesh, edge, faceNormals, params, angleThresholdRadians).IsFeature;
    }

    std::optional<EdgeProperty<bool>> MaterializeEdgeProperty(
        Mesh& mesh,
        const Classification& classification,
        const std::string_view propertyName)
    {
        if (mesh.IsSubmeshView()
            || classification.Status != ClassificationStatus::Success
            || propertyName.empty()
            || classification.EdgeFeatureMask.size() != mesh.EdgesSize()
            || classification.IncidentFeatureEdgeCount.size() != mesh.VerticesSize()
            || classification.VertexIncidence.size() != mesh.VerticesSize())
        {
            return std::nullopt;
        }

        std::size_t featureCount = 0u;
        for (const std::uint8_t value : classification.EdgeFeatureMask)
        {
            if (value > 1u)
            {
                return std::nullopt;
            }
            featureCount += value;
        }
        if (featureCount != classification.FeatureEdgeCount)
        {
            return std::nullopt;
        }

        EdgeProperty<bool> property(
            mesh.EdgeProperties().GetOrAdd<bool>(std::string(propertyName), false));
        if (!property.IsValid() || property.Size() != mesh.EdgesSize())
        {
            return std::nullopt;
        }

        for (std::size_t edgeIndex = 0u; edgeIndex < mesh.EdgesSize(); ++edgeIndex)
        {
            property[EdgeHandle{static_cast<PropertyIndex>(edgeIndex)}] =
                classification.EdgeFeatureMask[edgeIndex] != 0u;
        }
        return property;
    }
} // namespace Geometry::HalfedgeMesh::Features
