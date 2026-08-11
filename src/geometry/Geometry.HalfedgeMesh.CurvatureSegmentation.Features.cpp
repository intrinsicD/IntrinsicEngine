module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.HalfedgeMesh.CurvatureSegmentation.Features;

import Geometry.Curvature;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Features;
import Geometry.Properties;

namespace Geometry::CurvatureSegmentation
{
    namespace SharedFeatures = Geometry::HalfedgeMesh::Features;

    namespace
    {
        using ProfileClock = std::chrono::steady_clock;
        constexpr double kResponseScale = 0.10;
        constexpr double kLowThreshold = 0.35;
        constexpr double kHighThreshold = 0.65;
        constexpr double kMaximumTurnCosine = 0.5;
        constexpr double kJunctionContinuationCosine = 0.9659258262890683;

        struct FeatureFaceSample
        {
            FaceHandle Face{};
            glm::dvec3 Centroid{0.0};
            glm::dvec3 UnitNormal{0.0};
            glm::dvec2 Curvature{0.0};
            double Area{0.0};
        };

        struct FeatureDualTransition
        {
            std::uint32_t FaceA{0u};
            std::uint32_t FaceB{0u};
            EdgeHandle Edge{};
            double StepLength{0.0};
        };

        struct BoundedSearchWorkspace
        {
            std::vector<double> Distance{};
            std::vector<std::uint32_t> GenerationByFace{};
            std::vector<std::uint32_t> SettledFaces{};
            std::uint32_t Generation{0u};
        };

        [[nodiscard]] double ElapsedMilliseconds(
            const ProfileClock::time_point start) noexcept
        {
            return std::chrono::duration<double, std::milli>(
                       ProfileClock::now() - start)
                .count();
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::dvec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsFinite(const glm::dvec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsValidParams(
            const FeatureEvidenceParams &params) noexcept
        {
            return std::isfinite(params.HardDihedralThresholdDegrees) &&
                   params.HardDihedralThresholdDegrees >= 0.0 &&
                   params.HardDihedralThresholdDegrees <= 180.0 &&
                   std::isfinite(params.BaseRadiusRatio) &&
                   params.BaseRadiusRatio > 0.0 &&
                   params.BaseRadiusRatio <= 0.5;
        }

        void SetMeshCounts(FeatureEvidenceDiagnostics &diagnostics,
                           const HalfedgeMesh::Mesh &mesh) noexcept
        {
            diagnostics.VertexSlotCount = mesh.VerticesSize();
            diagnostics.LiveVertexCount = mesh.VertexCount();
            diagnostics.FaceSlotCount = mesh.FacesSize();
            diagnostics.LiveFaceCount = mesh.FaceCount();
            diagnostics.EdgeSlotCount = mesh.EdgesSize();
            diagnostics.LiveEdgeCount = mesh.EdgeCount();
        }

        [[nodiscard]] bool IsLiveFace(const HalfedgeMesh::Mesh &mesh,
                                      const FaceHandle face) noexcept
        {
            return face.IsValid() && mesh.IsValid(face) &&
                   !mesh.IsDeleted(face);
        }

        [[nodiscard]] FeatureEvidenceStatus BuildFeatureFaceSamples(
            const HalfedgeMesh::Mesh &mesh,
            const std::span<const double> maxPrincipal,
            const std::span<const double> minPrincipal,
            std::vector<FeatureFaceSample> &samples,
            std::vector<std::uint32_t> &faceSlotToSample,
            std::vector<glm::dvec3> &faceNormals, double &boundingBoxDiagonal)
        {
            if (maxPrincipal.size() != mesh.VerticesSize() ||
                minPrincipal.size() != mesh.VerticesSize())
            {
                return FeatureEvidenceStatus::CurvatureCountMismatch;
            }

            const double infinity = std::numeric_limits<double>::infinity();
            glm::dvec3 lower{infinity};
            glm::dvec3 upper{-infinity};
            for (const VertexHandle vertex : mesh.LiveVertices())
            {
                const glm::vec3 storedPosition = mesh.Position(vertex);
                if (!IsFinite(storedPosition))
                    return FeatureEvidenceStatus::NonFinitePosition;
                const glm::dvec3 position{storedPosition};
                lower = glm::min(lower, position);
                upper = glm::max(upper, position);

                const double k1 = maxPrincipal[vertex.Index];
                const double k2 = minPrincipal[vertex.Index];
                if (!std::isfinite(k1) || !std::isfinite(k2))
                    return FeatureEvidenceStatus::NonFiniteCurvature;
                if (k1 < k2)
                    return FeatureEvidenceStatus::InvalidCurvatureOrder;
            }

            boundingBoxDiagonal = glm::length(upper - lower);
            if (!std::isfinite(boundingBoxDiagonal) ||
                !(boundingBoxDiagonal > 0.0))
            {
                return FeatureEvidenceStatus::DegenerateFace;
            }

            faceSlotToSample.assign(mesh.FacesSize(), kInvalidFeatureIndex);
            faceNormals.assign(mesh.FacesSize(), glm::dvec3{0.0});
            samples.clear();
            samples.reserve(mesh.FaceCount());
            for (const FaceHandle face : mesh.LiveFaces())
            {
                if (mesh.Valence(face) != 3u)
                    return FeatureEvidenceStatus::NonTriangleFace;

                std::array<glm::dvec3, 3u> positions{};
                glm::dvec2 curvatureSum{0.0};
                std::size_t vertexCount = 0u;
                for (const VertexHandle vertex : mesh.VerticesAroundFace(face))
                {
                    if (vertexCount >= positions.size() || !vertex.IsValid() ||
                        !mesh.IsValid(vertex) || mesh.IsDeleted(vertex))
                    {
                        return FeatureEvidenceStatus::InvalidTopology;
                    }
                    positions[vertexCount] = glm::dvec3(mesh.Position(vertex));
                    curvatureSum += glm::dvec2{maxPrincipal[vertex.Index],
                                               minPrincipal[vertex.Index]};
                    ++vertexCount;
                }
                if (vertexCount != positions.size())
                    return FeatureEvidenceStatus::NonTriangleFace;

                const glm::dvec3 areaNormal =
                    glm::cross(positions[1u] - positions[0u],
                               positions[2u] - positions[0u]);
                const double doubleArea = glm::length(areaNormal);
                if (!std::isfinite(doubleArea) || !(doubleArea > 0.0))
                    return FeatureEvidenceStatus::DegenerateFace;

                const glm::dvec3 normal = areaNormal / doubleArea;
                const glm::dvec2 curvature = curvatureSum / 3.0;
                if (!IsFinite(normal) || !IsFinite(curvature))
                    return FeatureEvidenceStatus::NonFiniteCurvature;

                const std::uint32_t sample =
                    static_cast<std::uint32_t>(samples.size());
                faceSlotToSample[face.Index] = sample;
                faceNormals[face.Index] = normal;
                samples.push_back(FeatureFaceSample{
                    .Face = face,
                    .Centroid =
                        (positions[0u] + positions[1u] + positions[2u]) / 3.0,
                    .UnitNormal = normal,
                    .Curvature = curvature,
                    .Area = 0.5 * doubleArea,
                });
            }

            for (const EdgeHandle edge : mesh.LiveEdges())
            {
                const HalfedgeHandle h0 = mesh.Halfedge(edge, 0u);
                const HalfedgeHandle h1 = mesh.Halfedge(edge, 1u);
                if (!h0.IsValid() || !h1.IsValid() || !mesh.IsValid(h0) ||
                    !mesh.IsValid(h1) || mesh.Edge(h0) != edge ||
                    mesh.Edge(h1) != edge)
                {
                    return FeatureEvidenceStatus::InvalidTopology;
                }

                const VertexHandle from = mesh.FromVertex(h0);
                const VertexHandle to = mesh.ToVertex(h0);
                if (!from.IsValid() || !to.IsValid() || !mesh.IsValid(from) ||
                    !mesh.IsValid(to) || mesh.IsDeleted(from) ||
                    mesh.IsDeleted(to) || from == to)
                {
                    return FeatureEvidenceStatus::InvalidTopology;
                }

                const FaceHandle face0 = mesh.Face(h0);
                const FaceHandle face1 = mesh.Face(h1);
                const bool live0 = IsLiveFace(mesh, face0);
                const bool live1 = IsLiveFace(mesh, face1);
                const bool cleanBoundary =
                    (live0 && !face1.IsValid()) || (live1 && !face0.IsValid());
                if (!(live0 && live1) && !cleanBoundary)
                    return FeatureEvidenceStatus::InvalidTopology;
                if (live0 &&
                    faceSlotToSample[face0.Index] == kInvalidFeatureIndex)
                    return FeatureEvidenceStatus::InvalidTopology;
                if (live1 &&
                    faceSlotToSample[face1.Index] == kInvalidFeatureIndex)
                    return FeatureEvidenceStatus::InvalidTopology;
            }

            for (const VertexHandle vertex : mesh.LiveVertices())
            {
                if (mesh.IsIsolated(vertex) || !mesh.IsManifold(vertex))
                    return FeatureEvidenceStatus::InvalidTopology;
            }
            return FeatureEvidenceStatus::Success;
        }

        [[nodiscard]] glm::dvec3 EdgeMidpoint(const HalfedgeMesh::Mesh &mesh,
                                              const EdgeHandle edge)
        {
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            return 0.5 * (glm::dvec3(mesh.Position(mesh.FromVertex(halfedge))) +
                          glm::dvec3(mesh.Position(mesh.ToVertex(halfedge))));
        }

        [[nodiscard]] glm::dvec3 EdgeUnitTangent(const HalfedgeMesh::Mesh &mesh,
                                                 const EdgeHandle edge)
        {
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            const glm::dvec3 delta =
                glm::dvec3(mesh.Position(mesh.ToVertex(halfedge))) -
                glm::dvec3(mesh.Position(mesh.FromVertex(halfedge)));
            return delta / glm::length(delta);
        }

        void BoundedDistances(
            const std::uint32_t source, const double radius,
            const std::uint32_t blockedTransition,
            const std::vector<FeatureDualTransition> &transitions,
            const std::vector<std::vector<std::uint32_t>> &incident,
            BoundedSearchWorkspace &workspace,
            FeatureEvidenceDiagnostics &diagnostics)
        {
            using QueueEntry = std::pair<double, std::uint32_t>;
            if (workspace.Distance.size() != incident.size())
            {
                workspace.Distance.assign(incident.size(), 0.0);
                workspace.GenerationByFace.assign(incident.size(), 0u);
                workspace.Generation = 0u;
            }
            if (workspace.Generation ==
                std::numeric_limits<std::uint32_t>::max())
            {
                std::fill(workspace.GenerationByFace.begin(),
                          workspace.GenerationByFace.end(), 0u);
                workspace.Generation = 1u;
            }
            else
            {
                ++workspace.Generation;
            }
            workspace.SettledFaces.clear();

            std::priority_queue<QueueEntry, std::vector<QueueEntry>,
                                std::greater<QueueEntry>>
                queue;
            workspace.Distance[source] = 0.0;
            workspace.GenerationByFace[source] = workspace.Generation;
            queue.emplace(0.0, source);

            while (!queue.empty())
            {
                const auto [currentDistance, face] = queue.top();
                queue.pop();
                if (workspace.GenerationByFace[face] != workspace.Generation ||
                    currentDistance != workspace.Distance[face] ||
                    !(currentDistance < radius))
                {
                    continue;
                }
                workspace.SettledFaces.push_back(face);
                for (const std::uint32_t transitionIndex : incident[face])
                {
                    if (transitionIndex == blockedTransition)
                        continue;
                    const FeatureDualTransition &transition =
                        transitions[transitionIndex];
                    const std::uint32_t neighbor = transition.FaceA == face
                                                       ? transition.FaceB
                                                       : transition.FaceA;
                    const double candidate =
                        currentDistance + transition.StepLength;
                    if (!(candidate < radius) ||
                        (workspace.GenerationByFace[neighbor] ==
                             workspace.Generation &&
                         !(candidate < workspace.Distance[neighbor])))
                    {
                        continue;
                    }
                    workspace.Distance[neighbor] = candidate;
                    workspace.GenerationByFace[neighbor] = workspace.Generation;
                    queue.emplace(candidate, neighbor);
                }
            }

            ++diagnostics.BoundedSearchCount;
            diagnostics.SettledFaceVisitCount += workspace.SettledFaces.size();
            diagnostics.MaximumNeighborhoodFaceCount =
                std::max(diagnostics.MaximumNeighborhoodFaceCount,
                         workspace.SettledFaces.size());
        }

        [[nodiscard]] double CompactWeight(const double normalizedDistance)
        {
            if (!(normalizedDistance >= 0.0) || !(normalizedDistance < 1.0))
            {
                return 0.0;
            }
            const double complement =
                1.0 - normalizedDistance * normalizedDistance;
            return complement * complement;
        }

        [[nodiscard]] double BoundedResponse(const double value)
        {
            return 1.0 - std::exp(-std::max(value, 0.0) / kResponseScale);
        }

        [[nodiscard]] double SurfaceTypeTransitionResponse(
            const glm::dvec2 meanA, const glm::dvec2 meanB)
        {
            const double lengthA = glm::length(meanA);
            const double lengthB = glm::length(meanB);
            const glm::dvec2 directionA =
                lengthA > 0.0 ? meanA / lengthA : glm::dvec2{0.0};
            const glm::dvec2 directionB =
                lengthB > 0.0 ? meanB / lengthB : glm::dvec2{0.0};
            return std::clamp(glm::length(directionA - directionB), 0.0, 1.0);
        }

        [[nodiscard]] double MedianOfThree(std::array<double, 3u> values)
        {
            std::sort(values.begin(), values.end());
            return values[1u];
        }

        [[nodiscard]] FeatureVertexIncidence IncidenceForDegree(
            const std::size_t degree) noexcept
        {
            if (degree == 0u)
                return FeatureVertexIncidence::None;
            if (degree == 1u)
                return FeatureVertexIncidence::Endpoint;
            if (degree == 2u)
                return FeatureVertexIncidence::Crease;
            return FeatureVertexIncidence::Junction;
        }

        void IncrementEndpointDegree(const HalfedgeMesh::Mesh &mesh,
                                     const EdgeHandle edge,
                                     std::vector<std::size_t> &degree)
        {
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            const VertexHandle from = mesh.FromVertex(halfedge);
            const VertexHandle to = mesh.ToVertex(halfedge);
            ++degree[from.Index];
            if (to != from)
                ++degree[to.Index];
        }
    } // namespace

    const char *ToString(const FeatureEvidenceStatus status) noexcept
    {
        switch (status)
        {
        case FeatureEvidenceStatus::Success:
            return "success";
        case FeatureEvidenceStatus::EmptyMesh:
            return "empty_mesh";
        case FeatureEvidenceStatus::UnsupportedSubmeshView:
            return "unsupported_submesh_view";
        case FeatureEvidenceStatus::InvalidParameters:
            return "invalid_parameters";
        case FeatureEvidenceStatus::CurvatureCountMismatch:
            return "curvature_count_mismatch";
        case FeatureEvidenceStatus::NonTriangleFace:
            return "non_triangle_face";
        case FeatureEvidenceStatus::NonFinitePosition:
            return "non_finite_position";
        case FeatureEvidenceStatus::DegenerateFace:
            return "degenerate_face";
        case FeatureEvidenceStatus::NonFiniteCurvature:
            return "non_finite_curvature";
        case FeatureEvidenceStatus::InvalidCurvatureOrder:
            return "invalid_curvature_order";
        case FeatureEvidenceStatus::InvalidTopology:
            return "invalid_topology";
        case FeatureEvidenceStatus::HardFeatureClassificationFailed:
            return "hard_feature_classification_failed";
        case FeatureEvidenceStatus::NonFiniteResponse:
            return "non_finite_response";
        case FeatureEvidenceStatus::CurvatureEstimationFailed:
            return "curvature_estimation_failed";
        }
        return "unknown";
    }

    const char *ToString(const SoftFeatureSignal signal) noexcept
    {
        switch (signal)
        {
        case SoftFeatureSignal::None:
            return "none";
        case SoftFeatureSignal::Transition:
            return "transition";
        case SoftFeatureSignal::Ridge:
            return "ridge";
        case SoftFeatureSignal::Valley:
            return "valley";
        }
        return "unknown";
    }

    const char *ToString(const FeatureEdgeDecision decision) noexcept
    {
        switch (decision)
        {
        case FeatureEdgeDecision::NotCandidate:
            return "not_candidate";
        case FeatureEdgeDecision::SourceBoundary:
            return "source_boundary";
        case FeatureEdgeDecision::HardFeature:
            return "hard_feature";
        case FeatureEdgeDecision::BelowLowThreshold:
            return "below_low_threshold";
        case FeatureEdgeDecision::NonMaximumSuppressed:
            return "non_maximum_suppressed";
        case FeatureEdgeDecision::WeakDisconnected:
            return "weak_disconnected";
        case FeatureEdgeDecision::ShortFragmentRejected:
            return "short_fragment_rejected";
        case FeatureEdgeDecision::RetainedStrong:
            return "retained_strong";
        case FeatureEdgeDecision::RetainedWeak:
            return "retained_weak";
        }
        return "unknown";
    }

    FeatureEvidenceResult DetectFeatureEvidence(
        const HalfedgeMesh::Mesh &mesh,
        const std::span<const double> maxPrincipal,
        const std::span<const double> minPrincipal,
        const FeatureEvidenceParams &params)
    {
        const ProfileClock::time_point totalStart = ProfileClock::now();
        FeatureEvidenceResult result{};
        FeatureEvidenceDiagnostics &diagnostics = result.Diagnostics;
        SetMeshCounts(diagnostics, mesh);
        const auto finish =
            [&](const FeatureEvidenceStatus status) -> FeatureEvidenceResult
        {
            diagnostics.Status = status;
            diagnostics.Timings.TotalMilliseconds =
                ElapsedMilliseconds(totalStart);
            return std::move(result);
        };

        if (mesh.IsEmpty() || mesh.FaceCount() == 0u)
            return finish(FeatureEvidenceStatus::EmptyMesh);
        if (mesh.IsSubmeshView())
            return finish(FeatureEvidenceStatus::UnsupportedSubmeshView);
        if (!IsValidParams(params))
            return finish(FeatureEvidenceStatus::InvalidParameters);

        const ProfileClock::time_point validationStart = ProfileClock::now();
        std::vector<FeatureFaceSample> samples;
        std::vector<std::uint32_t> faceSlotToSample;
        std::vector<glm::dvec3> faceNormals;
        double boundingBoxDiagonal = 0.0;
        const FeatureEvidenceStatus sampleStatus = BuildFeatureFaceSamples(
            mesh, maxPrincipal, minPrincipal, samples, faceSlotToSample,
            faceNormals, boundingBoxDiagonal);
        diagnostics.Timings.ValidationAndFaceSamplingMilliseconds =
            ElapsedMilliseconds(validationStart);
        if (sampleStatus != FeatureEvidenceStatus::Success)
            return finish(sampleStatus);

        diagnostics.BoundingBoxDiagonal = boundingBoxDiagonal;
        result.ScaleRadiiWorld = {
            0.5 * params.BaseRadiusRatio * boundingBoxDiagonal,
            params.BaseRadiusRatio * boundingBoxDiagonal,
            2.0 * params.BaseRadiusRatio * boundingBoxDiagonal,
        };

        const ProfileClock::time_point hardStart = ProfileClock::now();
        SharedFeatures::Params hardParams{};
        hardParams.BoundaryIsFeature = params.BoundaryIsHardFeature;
        hardParams.DihedralThresholdDegrees =
            params.HardDihedralThresholdDegrees;
        SharedFeatures::Classification hard =
            SharedFeatures::Classify(mesh, faceNormals, hardParams);
        diagnostics.Timings.HardFeatureClassificationMilliseconds =
            ElapsedMilliseconds(hardStart);
        if (hard.Status != SharedFeatures::ClassificationStatus::Success)
            return finish(
                FeatureEvidenceStatus::HardFeatureClassificationFailed);
        if (hard.InvalidNormalFeatureEdgeCount != 0u ||
            hard.InvalidTopologyFeatureEdgeCount != 0u)
        {
            return finish(FeatureEvidenceStatus::InvalidTopology);
        }
        result.HardEdgeMask = std::move(hard.EdgeFeatureMask);
        diagnostics.HardFeatureEdgeCount = hard.FeatureEdgeCount;

        const std::size_t edgeSlotCount = mesh.EdgesSize();
        const std::size_t vertexSlotCount = mesh.VerticesSize();
        const std::size_t responseCount =
            edgeSlotCount * kFeatureEvidenceScaleCount;
        result.SoftEdgeConfidence.assign(edgeSlotCount, 0.0);
        result.TransitionResponse.assign(responseCount, 0.0);
        result.RidgeResponse.assign(responseCount, 0.0);
        result.ValleyResponse.assign(responseCount, 0.0);
        result.CombinedResponse.assign(responseCount, 0.0);
        result.EdgeRawConfidence.assign(edgeSlotCount, 0.0);
        result.EdgePersistentScaleMask.assign(edgeSlotCount, 0u);
        result.EdgeNonMaximumSurvivor.assign(edgeSlotCount, 0u);
        result.EdgeDominantSignal.assign(edgeSlotCount,
                                         SoftFeatureSignal::None);
        result.EdgeDecision.assign(edgeSlotCount,
                                   FeatureEdgeDecision::NotCandidate);
        result.HysteresisPredecessor.assign(edgeSlotCount,
                                            kInvalidFeatureIndex);
        result.VertexFeatureDegree.assign(vertexSlotCount, 0u);
        result.VertexFeatureIncidence.assign(vertexSlotCount,
                                             FeatureVertexIncidence::None);

        const ProfileClock::time_point responseStart = ProfileClock::now();
        std::vector<FeatureDualTransition> transitions;
        transitions.reserve(mesh.EdgeCount());
        std::vector<std::vector<std::uint32_t>> incident(samples.size());
        std::vector<std::uint32_t> edgeToTransition(edgeSlotCount,
                                                    kInvalidFeatureIndex);
        std::vector<glm::dvec3> edgeMidpoint(edgeSlotCount, glm::dvec3{0.0});
        std::vector<glm::dvec3> edgeTangent(edgeSlotCount, glm::dvec3{0.0});
        std::vector<double> edgeLength(edgeSlotCount, 0.0);
        std::vector<std::uint32_t> candidates;
        candidates.reserve(mesh.EdgeCount());

        for (const EdgeHandle edge : mesh.LiveEdges())
        {
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            const glm::dvec3 midpoint = EdgeMidpoint(mesh, edge);
            const glm::dvec3 tangent = EdgeUnitTangent(mesh, edge);
            const double length = glm::length(
                glm::dvec3(mesh.Position(mesh.ToVertex(halfedge))) -
                glm::dvec3(mesh.Position(mesh.FromVertex(halfedge))));
            if (!IsFinite(midpoint) || !IsFinite(tangent) ||
                !std::isfinite(length) || !(length > 0.0))
            {
                diagnostics.Timings.MultiScaleResponseMilliseconds =
                    ElapsedMilliseconds(responseStart);
                return finish(FeatureEvidenceStatus::InvalidTopology);
            }
            edgeMidpoint[edge.Index] = midpoint;
            edgeTangent[edge.Index] = tangent;
            edgeLength[edge.Index] = length;

            const FaceHandle face0 = mesh.Face(halfedge);
            const FaceHandle face1 = mesh.Face(mesh.OppositeHalfedge(halfedge));
            if (!face0.IsValid() || !face1.IsValid())
            {
                ++diagnostics.SourceBoundaryEdgeCount;
                result.EdgeDecision[edge.Index] =
                    result.HardEdgeMask[edge.Index] != 0u
                        ? FeatureEdgeDecision::HardFeature
                        : FeatureEdgeDecision::SourceBoundary;
                continue;
            }
            if (result.HardEdgeMask[edge.Index] != 0u)
            {
                result.EdgeDecision[edge.Index] =
                    FeatureEdgeDecision::HardFeature;
                continue;
            }

            const std::uint32_t a = faceSlotToSample[face0.Index];
            const std::uint32_t b = faceSlotToSample[face1.Index];
            if (a == kInvalidFeatureIndex || b == kInvalidFeatureIndex ||
                a == b)
            {
                diagnostics.Timings.MultiScaleResponseMilliseconds =
                    ElapsedMilliseconds(responseStart);
                return finish(FeatureEvidenceStatus::InvalidTopology);
            }
            const double step = glm::length(samples[a].Centroid - midpoint) +
                                glm::length(samples[b].Centroid - midpoint);
            if (!std::isfinite(step) || !(step > 0.0))
            {
                diagnostics.Timings.MultiScaleResponseMilliseconds =
                    ElapsedMilliseconds(responseStart);
                return finish(FeatureEvidenceStatus::InvalidTopology);
            }
            const std::uint32_t transitionIndex =
                static_cast<std::uint32_t>(transitions.size());
            transitions.push_back(FeatureDualTransition{
                .FaceA = a,
                .FaceB = b,
                .Edge = edge,
                .StepLength = step,
            });
            incident[a].push_back(transitionIndex);
            incident[b].push_back(transitionIndex);
            edgeToTransition[edge.Index] = transitionIndex;
            candidates.push_back(edge.Index);
        }
        diagnostics.InteriorCandidateEdgeCount = candidates.size();

        BoundedSearchWorkspace searchA{};
        BoundedSearchWorkspace searchB{};
        std::vector<std::uint32_t> assignmentGenerationByFace(samples.size(),
                                                              0u);
        std::uint32_t assignmentGeneration = 0u;
        for (const std::uint32_t edgeIndex : candidates)
        {
            const std::uint32_t transitionIndex = edgeToTransition[edgeIndex];
            const FeatureDualTransition &transition =
                transitions[transitionIndex];
            std::array<double, kFeatureEvidenceScaleCount> combined{};
            for (std::size_t scale = 0u; scale < kFeatureEvidenceScaleCount;
                 ++scale)
            {
                const double radius = result.ScaleRadiiWorld[scale];
                BoundedDistances(transition.FaceA, radius, transitionIndex,
                                 transitions, incident, searchA, diagnostics);
                BoundedDistances(transition.FaceB, radius, transitionIndex,
                                 transitions, incident, searchB, diagnostics);

                glm::dvec2 sumA{0.0};
                glm::dvec2 sumB{0.0};
                double weightA = 0.0;
                double weightB = 0.0;
                const PropertyIndex sourceFaceA =
                    samples[transition.FaceA].Face.Index;
                const PropertyIndex sourceFaceB =
                    samples[transition.FaceB].Face.Index;
                if (assignmentGeneration ==
                    std::numeric_limits<std::uint32_t>::max())
                {
                    std::fill(assignmentGenerationByFace.begin(),
                              assignmentGenerationByFace.end(), 0u);
                    assignmentGeneration = 1u;
                }
                else
                {
                    ++assignmentGeneration;
                }
                const auto accumulateFace = [&](const std::uint32_t face)
                {
                    if (assignmentGenerationByFace[face] ==
                        assignmentGeneration)
                    {
                        return;
                    }
                    assignmentGenerationByFace[face] = assignmentGeneration;
                    const bool withinA =
                        searchA.GenerationByFace[face] == searchA.Generation;
                    const bool withinB =
                        searchB.GenerationByFace[face] == searchB.Generation;
                    if (!withinA && !withinB)
                        return;

                    bool assignA = withinA;
                    if (withinA && withinB)
                    {
                        if (searchB.Distance[face] < searchA.Distance[face])
                            assignA = false;
                        else if (searchA.Distance[face] ==
                                 searchB.Distance[face])
                        {
                            assignA = sourceFaceA < sourceFaceB;
                        }
                    }
                    const double distance = assignA ? searchA.Distance[face]
                                                    : searchB.Distance[face];
                    const double weight =
                        samples[face].Area * CompactWeight(distance / radius);
                    const glm::dvec2 descriptor =
                        radius * samples[face].Curvature;
                    if (assignA)
                    {
                        sumA += weight * descriptor;
                        weightA += weight;
                    }
                    else
                    {
                        sumB += weight * descriptor;
                        weightB += weight;
                    }
                };
                for (const std::uint32_t face : searchA.SettledFaces)
                    accumulateFace(face);
                for (const std::uint32_t face : searchB.SettledFaces)
                    accumulateFace(face);
                if (!(weightA > 0.0) || !(weightB > 0.0))
                {
                    diagnostics.Timings.MultiScaleResponseMilliseconds =
                        ElapsedMilliseconds(responseStart);
                    return finish(FeatureEvidenceStatus::NonFiniteResponse);
                }
                const glm::dvec2 meanA = sumA / weightA;
                const glm::dvec2 meanB = sumB / weightB;

                const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
                const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
                const VertexHandle from = mesh.FromVertex(halfedge);
                const VertexHandle to = mesh.ToVertex(halfedge);
                const glm::dvec2 edgeDescriptor =
                    0.5 * radius *
                    (glm::dvec2{maxPrincipal[from.Index],
                                minPrincipal[from.Index]} +
                     glm::dvec2{maxPrincipal[to.Index],
                                minPrincipal[to.Index]});

                const double transitionResponse =
                    SurfaceTypeTransitionResponse(meanA, meanB);
                const double ridgeResponse = BoundedResponse(std::min(
                    edgeDescriptor.x - meanA.x, edgeDescriptor.x - meanB.x));
                const double valleyResponse = BoundedResponse(std::min(
                    meanA.y - edgeDescriptor.y, meanB.y - edgeDescriptor.y));
                const double combinedResponse = std::max(
                    {transitionResponse, ridgeResponse, valleyResponse});
                if (!std::isfinite(transitionResponse) ||
                    !std::isfinite(ridgeResponse) ||
                    !std::isfinite(valleyResponse) ||
                    !std::isfinite(combinedResponse))
                {
                    diagnostics.Timings.MultiScaleResponseMilliseconds =
                        ElapsedMilliseconds(responseStart);
                    return finish(FeatureEvidenceStatus::NonFiniteResponse);
                }

                const std::size_t outputIndex =
                    edgeIndex * kFeatureEvidenceScaleCount + scale;
                result.TransitionResponse[outputIndex] = transitionResponse;
                result.RidgeResponse[outputIndex] = ridgeResponse;
                result.ValleyResponse[outputIndex] = valleyResponse;
                result.CombinedResponse[outputIndex] = combinedResponse;
                combined[scale] = combinedResponse;
                if (combinedResponse >= kLowThreshold)
                {
                    result.EdgePersistentScaleMask[edgeIndex] |=
                        static_cast<std::uint8_t>(1u << scale);
                }
            }

            const double rawConfidence = MedianOfThree(combined);
            result.EdgeRawConfidence[edgeIndex] = rawConfidence;
            if (rawConfidence > 0.0)
            {
                std::size_t medianScale = 0u;
                while (medianScale + 1u < kFeatureEvidenceScaleCount &&
                       combined[medianScale] != rawConfidence)
                {
                    ++medianScale;
                }
                const std::size_t responseIndex =
                    edgeIndex * kFeatureEvidenceScaleCount + medianScale;
                const double transitionResponse =
                    result.TransitionResponse[responseIndex];
                const double ridgeResponse =
                    result.RidgeResponse[responseIndex];
                const double valleyResponse =
                    result.ValleyResponse[responseIndex];
                if (transitionResponse >= ridgeResponse &&
                    transitionResponse >= valleyResponse)
                {
                    result.EdgeDominantSignal[edgeIndex] =
                        SoftFeatureSignal::Transition;
                }
                else if (ridgeResponse >= valleyResponse)
                {
                    result.EdgeDominantSignal[edgeIndex] =
                        SoftFeatureSignal::Ridge;
                }
                else
                {
                    result.EdgeDominantSignal[edgeIndex] =
                        SoftFeatureSignal::Valley;
                }
            }
        }
        diagnostics.Timings.MultiScaleResponseMilliseconds =
            ElapsedMilliseconds(responseStart);

        const ProfileClock::time_point nmsStart = ProfileClock::now();
        std::vector<std::vector<std::uint32_t>> candidateAtVertex(
            vertexSlotCount);
        for (const std::uint32_t edgeIndex : candidates)
        {
            const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            candidateAtVertex[mesh.FromVertex(halfedge).Index].push_back(
                edgeIndex);
            candidateAtVertex[mesh.ToVertex(halfedge).Index].push_back(
                edgeIndex);
        }
        std::vector<std::uint8_t> strongJunctionVertex(vertexSlotCount, 0u);
        std::vector<std::size_t> alignedContinuationCount(edgeSlotCount, 0u);
        for (const std::uint32_t edgeIndex : candidates)
        {
            const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            const std::array<VertexHandle, 2u> endpoints{
                mesh.FromVertex(halfedge), mesh.ToVertex(halfedge)};
            for (const VertexHandle endpoint : endpoints)
            {
                for (const std::uint32_t neighbor :
                     candidateAtVertex[endpoint.Index])
                {
                    if (neighbor == edgeIndex ||
                        result.EdgeRawConfidence[neighbor] < kLowThreshold)
                    {
                        continue;
                    }
                    if (std::abs(glm::dot(edgeTangent[edgeIndex],
                                          edgeTangent[neighbor])) >=
                        kJunctionContinuationCosine)
                    {
                        ++alignedContinuationCount[edgeIndex];
                    }
                }
            }
        }
        for (const VertexHandle vertex : mesh.LiveVertices())
        {
            std::vector<std::uint32_t> strongIncident;
            for (const std::uint32_t edgeIndex :
                 candidateAtVertex[vertex.Index])
            {
                if (result.EdgeRawConfidence[edgeIndex] >= kHighThreshold &&
                    result.EdgeDominantSignal[edgeIndex] ==
                        SoftFeatureSignal::Transition)
                {
                    strongIncident.push_back(edgeIndex);
                }
            }
            if (strongIncident.size() < 3u)
                continue;
            for (std::size_t a = 0u; a < strongIncident.size() &&
                                     strongJunctionVertex[vertex.Index] == 0u;
                 ++a)
            {
                for (std::size_t b = a + 1u; b < strongIncident.size(); ++b)
                {
                    const double alignment =
                        std::abs(glm::dot(edgeTangent[strongIncident[a]],
                                          edgeTangent[strongIncident[b]]));
                    if (alignment < kMaximumTurnCosine)
                    {
                        strongJunctionVertex[vertex.Index] = 1u;
                        break;
                    }
                }
            }
        }
        for (const std::uint32_t edgeIndex : candidates)
        {
            const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            const std::array<FaceHandle, 2u> incidentFaces{
                mesh.Face(halfedge),
                mesh.Face(mesh.OppositeHalfedge(halfedge)),
            };
            const VertexHandle from = mesh.FromVertex(halfedge);
            const VertexHandle to = mesh.ToVertex(halfedge);
            const auto hasStrongContinuationAwayFrom =
                [&](const VertexHandle junction, const VertexHandle away)
            {
                for (const std::uint32_t neighbor :
                     candidateAtVertex[away.Index])
                {
                    if (neighbor == edgeIndex ||
                        result.EdgeRawConfidence[neighbor] < kHighThreshold ||
                        result.EdgeDominantSignal[neighbor] !=
                            SoftFeatureSignal::Transition)
                    {
                        continue;
                    }
                    const EdgeHandle neighborEdge{
                        static_cast<PropertyIndex>(neighbor)};
                    const HalfedgeHandle neighborHalfedge =
                        mesh.Halfedge(neighborEdge, 0u);
                    const VertexHandle neighborFrom =
                        mesh.FromVertex(neighborHalfedge);
                    const VertexHandle neighborTo =
                        mesh.ToVertex(neighborHalfedge);
                    if (neighborFrom != away && neighborTo != away)
                        continue;
                    if (neighborFrom == junction || neighborTo == junction)
                        continue;
                    if (std::abs(glm::dot(edgeTangent[edgeIndex],
                                          edgeTangent[neighbor])) >=
                        kJunctionContinuationCosine)
                    {
                        return true;
                    }
                }
                return false;
            };
            const bool protectedStrongBranch =
                result.EdgeRawConfidence[edgeIndex] >= kHighThreshold &&
                ((strongJunctionVertex[from.Index] != 0u &&
                  hasStrongContinuationAwayFrom(from, to)) ||
                 (strongJunctionVertex[to.Index] != 0u &&
                  hasStrongContinuationAwayFrom(to, from)));
            bool survives = true;
            if (!protectedStrongBranch)
            {
                for (const FaceHandle face : incidentFaces)
                {
                    const std::uint32_t sample = faceSlotToSample[face.Index];
                    glm::dvec3 binormal = glm::cross(samples[sample].UnitNormal,
                                                     edgeTangent[edgeIndex]);
                    const double binormalLength = glm::length(binormal);
                    if (!std::isfinite(binormalLength) ||
                        !(binormalLength > 0.0))
                    {
                        return finish(FeatureEvidenceStatus::NonFiniteResponse);
                    }
                    binormal /= binormalLength;

                    std::uint32_t competitor = kInvalidFeatureIndex;
                    double largestProjection = -1.0;
                    for (const HalfedgeHandle faceHalfedge :
                         mesh.HalfedgesAroundFace(face))
                    {
                        const EdgeHandle other = mesh.Edge(faceHalfedge);
                        if (other == edge || edgeToTransition[other.Index] ==
                                                 kInvalidFeatureIndex)
                        {
                            continue;
                        }
                        const double projection = std::abs(glm::dot(
                            edgeMidpoint[other.Index] - edgeMidpoint[edgeIndex],
                            binormal));
                        if (projection > largestProjection ||
                            (projection == largestProjection &&
                             other.Index < competitor))
                        {
                            largestProjection = projection;
                            competitor = other.Index;
                        }
                    }
                    if (competitor == kInvalidFeatureIndex)
                        continue;
                    const double candidateConfidence =
                        result.EdgeRawConfidence[edgeIndex];
                    const double competitorConfidence =
                        result.EdgeRawConfidence[competitor];
                    if (competitorConfidence > candidateConfidence ||
                        (competitorConfidence == candidateConfidence &&
                         competitor < edgeIndex))
                    {
                        survives = false;
                        break;
                    }
                }
            }
            if (survives && !protectedStrongBranch)
            {
                const std::uint32_t transitionIndex =
                    edgeToTransition[edgeIndex];
                const FeatureDualTransition &transition =
                    transitions[transitionIndex];
                glm::dvec3 normal = samples[transition.FaceA].UnitNormal +
                                    samples[transition.FaceB].UnitNormal;
                const double normalLength = glm::length(normal);
                if (!(normalLength > 0.0) || !std::isfinite(normalLength))
                {
                    return finish(FeatureEvidenceStatus::NonFiniteResponse);
                }
                normal /= normalLength;
                glm::dvec3 binormal =
                    glm::cross(normal, edgeTangent[edgeIndex]);
                const double binormalLength = glm::length(binormal);
                if (!(binormalLength > 0.0) || !std::isfinite(binormalLength))
                {
                    return finish(FeatureEvidenceStatus::NonFiniteResponse);
                }
                binormal /= binormalLength;
                const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
                const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
                const std::array<VertexHandle, 2u> endpoints{
                    mesh.FromVertex(halfedge), mesh.ToVertex(halfedge)};
                for (const VertexHandle endpoint : endpoints)
                {
                    for (const std::uint32_t competitor :
                         candidateAtVertex[endpoint.Index])
                    {
                        if (competitor == edgeIndex)
                            continue;
                        const glm::dvec3 offset =
                            edgeMidpoint[competitor] - edgeMidpoint[edgeIndex];
                        const double perpendicular =
                            std::abs(glm::dot(offset, binormal));
                        const double longitudinal =
                            std::abs(glm::dot(offset, edgeTangent[edgeIndex]));
                        if (!(perpendicular > longitudinal))
                            continue;
                        const double candidateConfidence =
                            result.EdgeRawConfidence[edgeIndex];
                        const double competitorConfidence =
                            result.EdgeRawConfidence[competitor];
                        if (competitorConfidence > candidateConfidence ||
                            (competitorConfidence == candidateConfidence &&
                             competitor < edgeIndex))
                        {
                            survives = false;
                            break;
                        }
                    }
                    if (!survives)
                        break;
                }
            }
            if (survives)
            {
                result.EdgeNonMaximumSurvivor[edgeIndex] = 1u;
                ++diagnostics.NonMaximumSurvivorCount;
            }
            else
            {
                result.EdgeDecision[edgeIndex] =
                    FeatureEdgeDecision::NonMaximumSuppressed;
            }
        }

        // A one-triangle three-edge plateau is a mesh-cell artifact, not a
        // one-dimensional feature branch. Remove its weakest-supported edge;
        // continuation count resolves equal-confidence junction plateaus.
        for (const FaceHandle face : mesh.LiveFaces())
        {
            std::array<std::uint32_t, 3u> survivors{};
            std::size_t survivorCount = 0u;
            for (const HalfedgeHandle halfedge : mesh.HalfedgesAroundFace(face))
            {
                const EdgeHandle edge = mesh.Edge(halfedge);
                if (result.EdgeNonMaximumSurvivor[edge.Index] != 0u)
                    survivors[survivorCount++] = edge.Index;
            }
            if (survivorCount != survivors.size())
                continue;
            std::uint32_t loser = survivors[0u];
            for (std::size_t index = 1u; index < survivors.size(); ++index)
            {
                const std::uint32_t candidate = survivors[index];
                if (result.EdgeRawConfidence[candidate] <
                        result.EdgeRawConfidence[loser] ||
                    (result.EdgeRawConfidence[candidate] ==
                         result.EdgeRawConfidence[loser] &&
                     alignedContinuationCount[candidate] <
                         alignedContinuationCount[loser]) ||
                    (result.EdgeRawConfidence[candidate] ==
                         result.EdgeRawConfidence[loser] &&
                     alignedContinuationCount[candidate] ==
                         alignedContinuationCount[loser] &&
                     candidate > loser))
                {
                    loser = candidate;
                }
            }
            result.EdgeNonMaximumSurvivor[loser] = 0u;
            result.EdgeDecision[loser] =
                FeatureEdgeDecision::NonMaximumSuppressed;
            --diagnostics.NonMaximumSurvivorCount;
        }
        diagnostics.Timings.NonMaximumSuppressionMilliseconds =
            ElapsedMilliseconds(nmsStart);

        const ProfileClock::time_point hysteresisStart = ProfileClock::now();
        std::vector<std::vector<std::uint32_t>> eligibleAtVertex(
            vertexSlotCount);
        std::vector<std::uint8_t> retained(edgeSlotCount, 0u);
        std::queue<std::uint32_t> queue;
        for (const std::uint32_t edgeIndex : candidates)
        {
            if (result.EdgeNonMaximumSurvivor[edgeIndex] == 0u)
                continue;
            const double confidence = result.EdgeRawConfidence[edgeIndex];
            if (confidence < kLowThreshold)
            {
                result.EdgeDecision[edgeIndex] =
                    FeatureEdgeDecision::BelowLowThreshold;
                continue;
            }
            const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            eligibleAtVertex[mesh.FromVertex(halfedge).Index].push_back(
                edgeIndex);
            eligibleAtVertex[mesh.ToVertex(halfedge).Index].push_back(
                edgeIndex);
            if (confidence >= kHighThreshold)
            {
                retained[edgeIndex] = 1u;
                result.EdgeDecision[edgeIndex] =
                    FeatureEdgeDecision::RetainedStrong;
                queue.push(edgeIndex);
                ++diagnostics.StrongEdgeCount;
            }
        }

        while (!queue.empty())
        {
            const std::uint32_t edgeIndex = queue.front();
            queue.pop();
            const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            const std::array<VertexHandle, 2u> endpoints{
                mesh.FromVertex(halfedge), mesh.ToVertex(halfedge)};
            for (const VertexHandle endpoint : endpoints)
            {
                for (const std::uint32_t neighbor :
                     eligibleAtVertex[endpoint.Index])
                {
                    if (retained[neighbor] != 0u)
                        continue;
                    const double alignment = std::abs(glm::dot(
                        edgeTangent[edgeIndex], edgeTangent[neighbor]));
                    if (alignment < kMaximumTurnCosine)
                        continue;
                    retained[neighbor] = 1u;
                    result.HysteresisPredecessor[neighbor] = edgeIndex;
                    result.EdgeDecision[neighbor] =
                        FeatureEdgeDecision::RetainedWeak;
                    queue.push(neighbor);
                    ++diagnostics.RetainedWeakEdgeCount;
                }
            }
        }

        for (const std::uint32_t edgeIndex : candidates)
        {
            if (result.EdgeNonMaximumSurvivor[edgeIndex] == 0u ||
                result.EdgeRawConfidence[edgeIndex] < kLowThreshold ||
                retained[edgeIndex] != 0u)
            {
                continue;
            }
            result.EdgeDecision[edgeIndex] =
                FeatureEdgeDecision::WeakDisconnected;
            ++diagnostics.WeakDisconnectedEdgeCount;
        }

        std::vector<std::uint8_t> hardIncident(vertexSlotCount, 0u);
        std::vector<std::size_t> provisionalDegree(vertexSlotCount, 0u);
        for (const EdgeHandle edge : mesh.LiveEdges())
        {
            if (result.HardEdgeMask[edge.Index] == 0u)
                continue;
            IncrementEndpointDegree(mesh, edge, provisionalDegree);
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            hardIncident[mesh.FromVertex(halfedge).Index] = 1u;
            hardIncident[mesh.ToVertex(halfedge).Index] = 1u;
        }
        std::vector<std::vector<std::uint32_t>> retainedAtVertex(
            vertexSlotCount);
        for (const std::uint32_t edgeIndex : candidates)
        {
            if (retained[edgeIndex] == 0u)
                continue;
            const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
            const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
            const VertexHandle from = mesh.FromVertex(halfedge);
            const VertexHandle to = mesh.ToVertex(halfedge);
            retainedAtVertex[from.Index].push_back(edgeIndex);
            retainedAtVertex[to.Index].push_back(edgeIndex);
            ++provisionalDegree[from.Index];
            ++provisionalDegree[to.Index];
        }

        std::vector<std::uint8_t> visited(edgeSlotCount, 0u);
        for (const std::uint32_t seed : candidates)
        {
            if (retained[seed] == 0u || visited[seed] != 0u)
                continue;
            std::vector<std::uint32_t> component;
            std::vector<std::uint32_t> stack{seed};
            visited[seed] = 1u;
            double componentLength = 0.0;
            bool protectedFragment = false;
            while (!stack.empty())
            {
                const std::uint32_t edgeIndex = stack.back();
                stack.pop_back();
                component.push_back(edgeIndex);
                componentLength += edgeLength[edgeIndex];
                const EdgeHandle edge{static_cast<PropertyIndex>(edgeIndex)};
                const HalfedgeHandle halfedge = mesh.Halfedge(edge, 0u);
                const std::array<VertexHandle, 2u> endpoints{
                    mesh.FromVertex(halfedge), mesh.ToVertex(halfedge)};
                for (const VertexHandle endpoint : endpoints)
                {
                    protectedFragment = protectedFragment ||
                                        mesh.IsBoundary(endpoint) ||
                                        hardIncident[endpoint.Index] != 0u ||
                                        provisionalDegree[endpoint.Index] >= 3u;
                    for (const std::uint32_t neighbor :
                         retainedAtVertex[endpoint.Index])
                    {
                        if (visited[neighbor] == 0u)
                        {
                            visited[neighbor] = 1u;
                            stack.push_back(neighbor);
                        }
                    }
                }
            }
            if (componentLength < result.ScaleRadiiWorld[1u] &&
                !protectedFragment)
            {
                for (const std::uint32_t edgeIndex : component)
                {
                    retained[edgeIndex] = 0u;
                    result.EdgeDecision[edgeIndex] =
                        FeatureEdgeDecision::ShortFragmentRejected;
                    ++diagnostics.ShortFragmentRejectedEdgeCount;
                }
            }
        }

        for (const std::uint32_t edgeIndex : candidates)
        {
            if (retained[edgeIndex] == 0u)
                continue;
            result.SoftEdgeConfidence[edgeIndex] =
                result.EdgeRawConfidence[edgeIndex];
            ++diagnostics.RetainedSoftEdgeCount;
        }

        std::fill(result.VertexFeatureDegree.begin(),
                  result.VertexFeatureDegree.end(), 0u);
        for (const EdgeHandle edge : mesh.LiveEdges())
        {
            if (result.HardEdgeMask[edge.Index] == 0u &&
                result.SoftEdgeConfidence[edge.Index] == 0.0)
            {
                continue;
            }
            IncrementEndpointDegree(mesh, edge, result.VertexFeatureDegree);
        }
        for (const VertexHandle vertex : mesh.LiveVertices())
        {
            const FeatureVertexIncidence incidence =
                IncidenceForDegree(result.VertexFeatureDegree[vertex.Index]);
            result.VertexFeatureIncidence[vertex.Index] = incidence;
            if (incidence == FeatureVertexIncidence::Endpoint)
                ++diagnostics.EndpointVertexCount;
            else if (incidence == FeatureVertexIncidence::Crease)
                ++diagnostics.CreaseVertexCount;
            else if (incidence == FeatureVertexIncidence::Junction)
                ++diagnostics.JunctionVertexCount;
        }
        diagnostics.Timings.HysteresisAndFragmentFilteringMilliseconds =
            ElapsedMilliseconds(hysteresisStart);
        return finish(FeatureEvidenceStatus::Success);
    }

    FeatureEvidenceResult ComputeFeatureEvidence(
        HalfedgeMesh::Mesh &mesh, const FeatureEvidenceParams &params)
    {
        const ProfileClock::time_point totalStart = ProfileClock::now();
        if (mesh.IsEmpty() || mesh.FaceCount() == 0u)
            return DetectFeatureEvidence(mesh, {}, {}, params);

        const ProfileClock::time_point curvatureStart = ProfileClock::now();
        Curvature::CurvatureField curvature = Curvature::ComputeCurvature(mesh);
        const double curvatureMilliseconds =
            ElapsedMilliseconds(curvatureStart);
        if (!curvature.MaxPrincipalCurvatureProperty ||
            !curvature.MinPrincipalCurvatureProperty)
        {
            FeatureEvidenceResult result{};
            SetMeshCounts(result.Diagnostics, mesh);
            result.Diagnostics.Status =
                FeatureEvidenceStatus::CurvatureEstimationFailed;
            result.Diagnostics.Timings.CurvatureEstimationMilliseconds =
                curvatureMilliseconds;
            result.Diagnostics.Timings.TotalMilliseconds =
                ElapsedMilliseconds(totalStart);
            return result;
        }

        const std::vector<double> &maximum =
            curvature.MaxPrincipalCurvatureProperty.Vector();
        const std::vector<double> &minimum =
            curvature.MinPrincipalCurvatureProperty.Vector();
        FeatureEvidenceResult result = DetectFeatureEvidence(
            mesh, std::span<const double>{maximum.data(), maximum.size()},
            std::span<const double>{minimum.data(), minimum.size()}, params);
        result.Diagnostics.Timings.CurvatureEstimationMilliseconds =
            curvatureMilliseconds;
        result.Diagnostics.Timings.TotalMilliseconds =
            ElapsedMilliseconds(totalStart);
        return result;
    }
} // namespace Geometry::CurvatureSegmentation
