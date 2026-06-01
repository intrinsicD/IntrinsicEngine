module;

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.PrimitiveSelectionRefinement;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.SelectionSystem;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    using ECS::Components::GeometrySources::ConstSourceView;
    using ECS::Components::GeometrySources::Domain;
    using Extrinsic::Graphics::SelectionPrimitiveDomain;
    namespace pn = ECS::Components::GeometrySources::PropertyNames;

    namespace
    {
        // ---- small geometry helpers (local-space) ---------------------------

        [[nodiscard]] float SquaredDistance(const glm::vec3& a, const glm::vec3& b) noexcept
        {
            const glm::vec3 d = a - b;
            return glm::dot(d, d);
        }

        [[nodiscard]] float SquaredDistancePointSegment(const glm::vec3& p,
                                                        const glm::vec3& a,
                                                        const glm::vec3& b) noexcept
        {
            const glm::vec3 ab = b - a;
            const float denom = glm::dot(ab, ab);
            float t = 0.0f;
            if (denom > 0.0f)
            {
                t = glm::clamp(glm::dot(p - a, ab) / denom, 0.0f, 1.0f);
            }
            const glm::vec3 proj = a + t * ab;
            return SquaredDistance(p, proj);
        }

        [[nodiscard]] glm::vec3 ToWorld(const glm::mat4& localToWorld, const glm::vec3& local) noexcept
        {
            return glm::vec3(localToWorld * glm::vec4(local, 1.0f));
        }

        // ---- result construction --------------------------------------------

        [[nodiscard]] PrimitiveSelectionResult MakeBase(const ConstSourceView& view,
                                                        const PrimitiveRefineRequest& request) noexcept
        {
            PrimitiveSelectionResult result{};
            result.EntityId = request.EntityId;
            result.StableId = request.StableId;
            result.Domain = view.ActiveDomain;
            return result;
        }

        [[nodiscard]] PrimitiveSelectionResult Reject(const ConstSourceView& view,
                                                      const PrimitiveRefineRequest& request,
                                                      PrimitiveRefineStatus status) noexcept
        {
            PrimitiveSelectionResult result = MakeBase(view, request);
            result.Status = status;
            return result;
        }

        // Stamp the local/world hit either from the request's local anchor (when
        // present) or from the resolved primitive's representative local position.
        void StampHit(PrimitiveSelectionResult& result,
                      const PrimitiveRefineRequest& request,
                      bool hasRepresentative,
                      const glm::vec3& representativeLocal) noexcept
        {
            glm::vec3 local{0.0f};
            bool hasPos = false;
            if (request.HasLocalHit)
            {
                local = request.LocalHit;
                hasPos = true;
            }
            else if (hasRepresentative)
            {
                local = representativeLocal;
                hasPos = true;
            }

            result.HasHitPosition = hasPos;
            if (hasPos)
            {
                result.LocalHit = local;
                result.WorldHit = ToWorld(request.LocalToWorld, local);
            }
        }

        // ---- property accessors ---------------------------------------------

        // Returns nullptr when the source/property is absent or wrong-typed.
        template <typename T>
        [[nodiscard]] const std::vector<T>* TryVector(const Geometry::PropertySet& set,
                                                      std::string_view name)
        {
            const auto prop = set.Get<T>(name);
            if (!prop)
            {
                return nullptr;
            }
            return &prop.Vector();
        }

        // ---- mesh face ring traversal ---------------------------------------

        // Collects the face's boundary vertices (the `to_vertex` of each halfedge
        // around the face) in loop order. Returns false on malformed topology.
        [[nodiscard]] bool CollectFaceRing(const std::vector<std::uint32_t>& faceHalfedge,
                                           const std::vector<std::uint32_t>& halfedgeNext,
                                           const std::vector<std::uint32_t>& halfedgeToVertex,
                                           std::uint32_t faceIndex,
                                           std::vector<std::uint32_t>& outRing)
        {
            outRing.clear();
            const std::uint32_t halfedgeCount = static_cast<std::uint32_t>(halfedgeToVertex.size());
            if (faceIndex >= faceHalfedge.size())
            {
                return false;
            }
            const std::uint32_t start = faceHalfedge[faceIndex];
            if (start >= halfedgeCount)
            {
                return false;
            }

            std::uint32_t h = start;
            for (std::uint32_t guard = 0; guard <= halfedgeCount; ++guard)
            {
                if (h >= halfedgeCount)
                {
                    return false;
                }
                outRing.push_back(halfedgeToVertex[h]);
                h = halfedgeNext[h];
                if (h == start)
                {
                    return !outRing.empty();
                }
            }
            // Loop never closed within the halfedge budget -> malformed topology.
            return false;
        }

        // Resolve an unordered vertex pair to an edge row index by scanning the
        // edge endpoints. Returns kInvalidPrimitiveIndex when no edge matches.
        [[nodiscard]] std::uint32_t ResolveEdgeIndex(const std::vector<std::uint32_t>& edgeV0,
                                                     const std::vector<std::uint32_t>& edgeV1,
                                                     std::uint32_t a,
                                                     std::uint32_t b)
        {
            const std::uint32_t lo = a < b ? a : b;
            const std::uint32_t hi = a < b ? b : a;
            for (std::size_t e = 0; e < edgeV0.size(); ++e)
            {
                const std::uint32_t e0 = edgeV0[e];
                const std::uint32_t e1 = edgeV1[e];
                const std::uint32_t elo = e0 < e1 ? e0 : e1;
                const std::uint32_t ehi = e0 < e1 ? e1 : e0;
                if (elo == lo && ehi == hi)
                {
                    return static_cast<std::uint32_t>(e);
                }
            }
            return kInvalidPrimitiveIndex;
        }

        // ---- per-domain refinement ------------------------------------------

        [[nodiscard]] PrimitiveSelectionResult RefineMesh(const ConstSourceView& view,
                                                          const PrimitiveRefineRequest& request)
        {
            if (view.VertexSource == nullptr)
            {
                return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
            }
            const auto* positions = TryVector<glm::vec3>(view.VertexSource->Properties, pn::kPosition);
            if (positions == nullptr)
            {
                return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
            }
            const std::uint32_t vertexCount = static_cast<std::uint32_t>(positions->size());

            const SelectionPrimitiveDomain hintDomain = request.Hint.Domain();
            const std::uint32_t payload = request.Hint.Payload();

            switch (hintDomain)
            {
                case SelectionPrimitiveDomain::Entity:
                {
                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Entity;
                    StampHit(result, request, false, glm::vec3{0.0f});
                    return result;
                }
                case SelectionPrimitiveDomain::Point:
                {
                    // A mesh vertex picked through the RUNTIME-088 vertex point view.
                    if (payload >= vertexCount)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }
                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Vertex;
                    result.VertexId = payload;
                    StampHit(result, request, true, (*positions)[payload]);
                    return result;
                }
                case SelectionPrimitiveDomain::Edge:
                {
                    if (view.EdgeSource == nullptr)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
                    }
                    const auto* edgeV0 = TryVector<std::uint32_t>(view.EdgeSource->Properties, pn::kEdgeV0);
                    const auto* edgeV1 = TryVector<std::uint32_t>(view.EdgeSource->Properties, pn::kEdgeV1);
                    if (edgeV0 == nullptr || edgeV1 == nullptr || edgeV0->size() != edgeV1->size())
                    {
                        return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
                    }
                    if (payload >= edgeV0->size())
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }
                    const std::uint32_t a = (*edgeV0)[payload];
                    const std::uint32_t b = (*edgeV1)[payload];
                    if (a >= vertexCount || b >= vertexCount)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }

                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Edge;
                    result.EdgeId = payload;

                    const glm::vec3 pa = (*positions)[a];
                    const glm::vec3 pb = (*positions)[b];
                    glm::vec3 representative = 0.5f * (pa + pb);
                    if (request.HasLocalHit)
                    {
                        // Nearest endpoint of the hinted edge.
                        result.VertexId =
                            SquaredDistance(request.LocalHit, pa) <= SquaredDistance(request.LocalHit, pb)
                                ? a
                                : b;
                    }
                    StampHit(result, request, true, representative);
                    return result;
                }
                case SelectionPrimitiveDomain::Face:
                {
                    if (view.FaceSource == nullptr || view.HalfedgeSource == nullptr)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
                    }
                    const auto* faceHalfedge =
                        TryVector<std::uint32_t>(view.FaceSource->Properties, pn::kFaceHalfedge);
                    const auto* halfedgeNext =
                        TryVector<std::uint32_t>(view.HalfedgeSource->Properties, pn::kHalfedgeNext);
                    const auto* halfedgeToVertex =
                        TryVector<std::uint32_t>(view.HalfedgeSource->Properties, pn::kHalfedgeToVertex);
                    if (faceHalfedge == nullptr || halfedgeNext == nullptr || halfedgeToVertex == nullptr ||
                        halfedgeNext->size() != halfedgeToVertex->size())
                    {
                        return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
                    }
                    if (payload >= faceHalfedge->size())
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }

                    std::vector<std::uint32_t> ring;
                    if (!CollectFaceRing(*faceHalfedge, *halfedgeNext, *halfedgeToVertex, payload, ring))
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }
                    for (const std::uint32_t v : ring)
                    {
                        if (v >= vertexCount)
                        {
                            return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                        }
                    }

                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Face;
                    result.FaceId = payload;

                    // Face centroid is the representative position when no anchor.
                    glm::vec3 centroid{0.0f};
                    for (const std::uint32_t v : ring)
                    {
                        centroid += (*positions)[v];
                    }
                    centroid /= static_cast<float>(ring.size());

                    if (request.HasLocalHit)
                    {
                        // Nearest vertex on the face (tie-break: smallest index).
                        std::uint32_t bestVertex = ring.front();
                        float bestVertexSq = SquaredDistance(request.LocalHit, (*positions)[bestVertex]);
                        for (const std::uint32_t v : ring)
                        {
                            const float sq = SquaredDistance(request.LocalHit, (*positions)[v]);
                            if (sq < bestVertexSq || (sq == bestVertexSq && v < bestVertex))
                            {
                                bestVertexSq = sq;
                                bestVertex = v;
                            }
                        }
                        result.VertexId = bestVertex;

                        // Nearest boundary edge on the face (tie-break: smallest
                        // unordered endpoint pair), then resolve to an edge row.
                        const std::size_t ringSize = ring.size();
                        std::uint32_t bestA = ring[0];
                        std::uint32_t bestB = ring[ringSize > 1 ? 1 : 0];
                        float bestEdgeSq = SquaredDistancePointSegment(
                            request.LocalHit, (*positions)[bestA], (*positions)[bestB]);
                        for (std::size_t i = 0; i < ringSize; ++i)
                        {
                            const std::uint32_t va = ring[i];
                            const std::uint32_t vb = ring[(i + 1) % ringSize];
                            const float sq = SquaredDistancePointSegment(
                                request.LocalHit, (*positions)[va], (*positions)[vb]);
                            const std::uint32_t loNew = va < vb ? va : vb;
                            const std::uint32_t loBest = bestA < bestB ? bestA : bestB;
                            if (sq < bestEdgeSq || (sq == bestEdgeSq && loNew < loBest))
                            {
                                bestEdgeSq = sq;
                                bestA = va;
                                bestB = vb;
                            }
                        }
                        // The Edges PropertySet is required for the mesh domain;
                        // resolve the boundary pair to an edge index when present.
                        if (view.EdgeSource != nullptr)
                        {
                            const auto* edgeV0 =
                                TryVector<std::uint32_t>(view.EdgeSource->Properties, pn::kEdgeV0);
                            const auto* edgeV1 =
                                TryVector<std::uint32_t>(view.EdgeSource->Properties, pn::kEdgeV1);
                            if (edgeV0 != nullptr && edgeV1 != nullptr && edgeV0->size() == edgeV1->size())
                            {
                                result.EdgeId = ResolveEdgeIndex(*edgeV0, *edgeV1, bestA, bestB);
                            }
                        }
                    }

                    StampHit(result, request, true, centroid);
                    return result;
                }
                default:
                    return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
            }
        }

        [[nodiscard]] PrimitiveSelectionResult RefineGraph(const ConstSourceView& view,
                                                           const PrimitiveRefineRequest& request)
        {
            if (view.NodeSource == nullptr)
            {
                return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
            }
            const auto* positions = TryVector<glm::vec3>(view.NodeSource->Properties, pn::kPosition);
            if (positions == nullptr)
            {
                return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
            }
            const std::uint32_t nodeCount = static_cast<std::uint32_t>(positions->size());

            const SelectionPrimitiveDomain hintDomain = request.Hint.Domain();
            const std::uint32_t payload = request.Hint.Payload();

            switch (hintDomain)
            {
                case SelectionPrimitiveDomain::Entity:
                {
                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Entity;
                    StampHit(result, request, false, glm::vec3{0.0f});
                    return result;
                }
                case SelectionPrimitiveDomain::Point:
                {
                    // A graph node picked through the node point lane.
                    if (payload >= nodeCount)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }
                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Vertex; // graph node -> Vertex kind.
                    result.VertexId = payload;
                    StampHit(result, request, true, (*positions)[payload]);
                    return result;
                }
                case SelectionPrimitiveDomain::Edge:
                {
                    if (view.EdgeSource == nullptr)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
                    }
                    const auto* edgeV0 = TryVector<std::uint32_t>(view.EdgeSource->Properties, pn::kEdgeV0);
                    const auto* edgeV1 = TryVector<std::uint32_t>(view.EdgeSource->Properties, pn::kEdgeV1);
                    if (edgeV0 == nullptr || edgeV1 == nullptr || edgeV0->size() != edgeV1->size())
                    {
                        return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
                    }
                    if (payload >= edgeV0->size())
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }
                    const std::uint32_t a = (*edgeV0)[payload];
                    const std::uint32_t b = (*edgeV1)[payload];
                    if (a >= nodeCount || b >= nodeCount)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }

                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Edge;
                    result.EdgeId = payload;

                    const glm::vec3 pa = (*positions)[a];
                    const glm::vec3 pb = (*positions)[b];
                    if (request.HasLocalHit)
                    {
                        result.VertexId =
                            SquaredDistance(request.LocalHit, pa) <= SquaredDistance(request.LocalHit, pb)
                                ? a
                                : b;
                    }
                    StampHit(result, request, true, 0.5f * (pa + pb));
                    return result;
                }
                default:
                    return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
            }
        }

        [[nodiscard]] PrimitiveSelectionResult RefinePointCloud(const ConstSourceView& view,
                                                                const PrimitiveRefineRequest& request)
        {
            if (view.VertexSource == nullptr)
            {
                return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
            }
            const auto* positions = TryVector<glm::vec3>(view.VertexSource->Properties, pn::kPosition);
            if (positions == nullptr)
            {
                return Reject(view, request, PrimitiveRefineStatus::MissingGeometrySource);
            }
            const std::uint32_t pointCount = static_cast<std::uint32_t>(positions->size());

            const SelectionPrimitiveDomain hintDomain = request.Hint.Domain();
            const std::uint32_t payload = request.Hint.Payload();

            switch (hintDomain)
            {
                case SelectionPrimitiveDomain::Entity:
                {
                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Entity;
                    StampHit(result, request, false, glm::vec3{0.0f});
                    return result;
                }
                case SelectionPrimitiveDomain::Point:
                {
                    if (payload >= pointCount)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }
                    PrimitiveSelectionResult result = MakeBase(view, request);
                    result.Status = PrimitiveRefineStatus::Success;
                    result.Kind = RefinedPrimitiveKind::Point;
                    result.PointId = payload;
                    StampHit(result, request, true, (*positions)[payload]);
                    return result;
                }
                default:
                    return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
            }
        }
    } // namespace

    const char* DebugNameForPrimitiveRefineStatus(PrimitiveRefineStatus status) noexcept
    {
        switch (status)
        {
            case PrimitiveRefineStatus::Success:                 return "Primitive.Success";
            case PrimitiveRefineStatus::CpuFallbackResolved:     return "Primitive.CpuFallbackResolved";
            case PrimitiveRefineStatus::UnsupportedDomain:       return "Primitive.UnsupportedDomain";
            case PrimitiveRefineStatus::StaleEntity:             return "Primitive.StaleEntity";
            case PrimitiveRefineStatus::MissingGeometrySource:   return "Primitive.MissingGeometrySource";
            case PrimitiveRefineStatus::InvalidPrimitivePayload: return "Primitive.InvalidPrimitivePayload";
            case PrimitiveRefineStatus::CpuFallbackMiss:         return "Primitive.CpuFallbackMiss";
        }
        return "Primitive.Unknown";
    }

    PrimitiveSelectionResult RefinePrimitiveSelection(const ConstSourceView& view,
                                                      const PrimitiveRefineRequest& request)
    {
        // Stale entities are rejected by the single runtime authority before any
        // geometry is touched, so a recycled/destroyed slot never mis-resolves.
        if (!request.EntityIsLive)
        {
            return Reject(view, request, PrimitiveRefineStatus::StaleEntity);
        }

        switch (view.ActiveDomain)
        {
            case Domain::Mesh:       return RefineMesh(view, request);
            case Domain::Graph:      return RefineGraph(view, request);
            case Domain::PointCloud: return RefinePointCloud(view, request);
            case Domain::None:
            case Domain::Unknown:
            default:
                return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
        }
    }
}
