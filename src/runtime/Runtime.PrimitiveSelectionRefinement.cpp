module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.PrimitiveSelectionRefinement;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.StableEntityLookup;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    using ECS::Components::GeometrySources::ConstSourceView;
    using ECS::Components::GeometrySources::Domain;
    using ECS::Components::GeometrySources::BuildSourceAvailability;
    using ECS::Components::GeometrySources::SourceAvailability;
    using ECS::Components::GeometrySources::SourceCapability;
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

        // Below this squared direction length the pick ray is treated as
        // degenerate and the fallback fails closed (`CpuFallbackMiss`) rather
        // than dividing by ~zero.
        inline constexpr float kRayDirEpsilonSq = 1.0e-12f;

        // Nearest point index whose perpendicular distance to the pick ray (a
        // half-line clamped at the origin, t >= 0) is minimal and within
        // `radius`. Returns kInvalidPrimitiveIndex when nothing qualifies or the
        // ray is degenerate. Tie-break: smallest index (strict `<` keeps the
        // first-seen winner).
        [[nodiscard]] std::uint32_t NearestPointAlongRay(const std::vector<glm::vec3>& positions,
                                                         const glm::vec3& origin,
                                                         const glm::vec3& direction,
                                                         float radius) noexcept
        {
            const float dirLenSq = glm::dot(direction, direction);
            if (dirLenSq <= kRayDirEpsilonSq)
            {
                return kInvalidPrimitiveIndex;
            }
            const glm::vec3 dir = direction * glm::inversesqrt(dirLenSq);
            const float radiusSq = radius > 0.0f ? radius * radius : 0.0f;

            std::uint32_t best = kInvalidPrimitiveIndex;
            float bestSq = 0.0f;
            for (std::size_t i = 0; i < positions.size(); ++i)
            {
                const glm::vec3 p = positions[i];
                const float t = glm::max(glm::dot(p - origin, dir), 0.0f);
                const glm::vec3 closest = origin + t * dir;
                const float sq = SquaredDistance(p, closest);
                if (sq <= radiusSq && (best == kInvalidPrimitiveIndex || sq < bestSq))
                {
                    bestSq = sq;
                    best = static_cast<std::uint32_t>(i);
                }
            }
            return best;
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

        // CPU ray fallback for a missing hint: resolve the nearest mesh vertex /
        // graph node / point-cloud point along the request's local pick ray. On a
        // qualifying hit reports `CpuFallbackResolved` with the resolved index in
        // the field matching `kind` and the primitive's local position transformed
        // to world; otherwise reports the deterministic `CpuFallbackMiss`.
        [[nodiscard]] PrimitiveSelectionResult RefineByRayFallback(const ConstSourceView& view,
                                                                   const PrimitiveRefineRequest& request,
                                                                   const std::vector<glm::vec3>& positions,
                                                                   RefinedPrimitiveKind kind) noexcept
        {
            const std::uint32_t idx = NearestPointAlongRay(
                positions, request.RayOrigin, request.RayDirection, request.FallbackRadius);
            if (idx == kInvalidPrimitiveIndex)
            {
                return Reject(view, request, PrimitiveRefineStatus::CpuFallbackMiss);
            }

            PrimitiveSelectionResult result = MakeBase(view, request);
            result.Status = PrimitiveRefineStatus::CpuFallbackResolved;
            result.Kind = kind;
            if (kind == RefinedPrimitiveKind::Point)
            {
                result.PointId = idx;
            }
            else
            {
                // Mesh vertex / graph node.
                result.VertexId = idx;
            }
            result.HasHitPosition = true;
            result.LocalHit = positions[idx];
            result.WorldHit = ToWorld(request.LocalToWorld, positions[idx]);
            return result;
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
                    // A `Face` selection-id payload is the GPU per-draw triangle
                    // index (`gl_PrimitiveID`), not a face row: `PackMesh`
                    // fan-triangulates each n-gon ring to (n - 2) triangles, so
                    // we must invert that emission order to recover the face.
                    // `BuildSurfaceTriangleFaceMap` shares `PackMesh`'s walk, so
                    // the table matches the surface draw exactly.
                    std::vector<std::uint32_t> triangleToFace;
                    if (BuildSurfaceTriangleFaceMap(view, triangleToFace) != MeshPackStatus::Success)
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }
                    if (payload >= triangleToFace.size())
                    {
                        return Reject(view, request, PrimitiveRefineStatus::InvalidPrimitivePayload);
                    }
                    const std::uint32_t faceRow = triangleToFace[payload];

                    std::vector<std::uint32_t> ring;
                    if (!CollectFaceRing(*faceHalfedge, *halfedgeNext, *halfedgeToVertex, faceRow, ring))
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
                    result.FaceId = faceRow;

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
                case SelectionPrimitiveDomain::None:
                {
                    // No usable hint: resolve the nearest mesh vertex along the
                    // local pick ray when one is supplied, else fail closed.
                    if (request.HasPickRay)
                    {
                        return RefineByRayFallback(view, request, *positions, RefinedPrimitiveKind::Vertex);
                    }
                    return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
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
                case SelectionPrimitiveDomain::None:
                {
                    // No usable hint: resolve the nearest graph node along the
                    // local pick ray (reported as `Vertex` kind) when one is
                    // supplied, else fail closed.
                    if (request.HasPickRay)
                    {
                        return RefineByRayFallback(view, request, *positions, RefinedPrimitiveKind::Vertex);
                    }
                    return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
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
                case SelectionPrimitiveDomain::None:
                {
                    // No usable hint: resolve the nearest cloud point along the
                    // local pick ray when one is supplied, else fail closed.
                    if (request.HasPickRay)
                    {
                        return RefineByRayFallback(view, request, *positions, RefinedPrimitiveKind::Point);
                    }
                    return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
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

        const SourceAvailability availability = BuildSourceAvailability(view);
        switch (availability.ProvenanceDomain)
        {
            case Domain::Mesh:
                if (availability.Has(SourceCapability::VertexPoints) &&
                    availability.Has(SourceCapability::Halfedges) &&
                    availability.Has(SourceCapability::Faces))
                {
                    return RefineMesh(view, request);
                }
                return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
            case Domain::Graph:
                if (availability.Has(SourceCapability::NodePoints) &&
                    availability.Has(SourceCapability::Edges))
                {
                    return RefineGraph(view, request);
                }
                return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
            case Domain::PointCloud:
                if (availability.Has(SourceCapability::VertexPoints))
                    return RefinePointCloud(view, request);
                return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
            case Domain::None:
            case Domain::Unknown:
            default:
                return Reject(view, request, PrimitiveRefineStatus::UnsupportedDomain);
        }
    }

    bool IsOrthographicProjection(const glm::mat4& projection) noexcept
    {
        // GLM perspective projections write -1 into the w-row coefficient of
        // view-space z (column-major: projection[2][3]); orthographic
        // projections leave it 0 and keep w constant. The Vulkan Y flip only
        // negates [1][1], so it does not affect this signal.
        return std::abs(projection[2][3]) <= 1.0e-6f;
    }

    std::optional<glm::vec3> UnprojectPickDepth(const glm::mat4& inverseViewProjection,
                                                const std::uint32_t pixelX,
                                                const std::uint32_t pixelY,
                                                const std::uint32_t viewportWidth,
                                                const std::uint32_t viewportHeight,
                                                const float depth) noexcept
    {
        if (viewportWidth == 0u || viewportHeight == 0u ||
            pixelX >= viewportWidth || pixelY >= viewportHeight ||
            !std::isfinite(depth))
        {
            return std::nullopt;
        }
        // Pixel-center -> NDC mapping mirrors BuildCameraViewSnapshot's pick-ray
        // derivation (Graphics.CameraSnapshots.cpp); the depth-buffer sample is
        // the Vulkan [0..1] clip-space z and plugs in directly.
        const float ndcX =
            ((static_cast<float>(pixelX) + 0.5f) / static_cast<float>(viewportWidth)) * 2.f - 1.f;
        const float ndcY =
            1.f - ((static_cast<float>(pixelY) + 0.5f) / static_cast<float>(viewportHeight)) * 2.f;
        const glm::vec4 clip{ndcX, ndcY, depth, 1.0f};
        const glm::vec4 world = inverseViewProjection * clip;
        if (!std::isfinite(world.w) || std::abs(world.w) <= 0.000001f)
        {
            return std::nullopt;
        }
        const glm::vec3 position = glm::vec3{world} / world.w;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        {
            return std::nullopt;
        }
        return position;
    }

    std::optional<PrimitiveSelectionResult> RefinePickReadbackResult(
        ECS::Scene::Registry& scene,
        const Extrinsic::Graphics::PickReadbackResult& readback,
        const PickReadbackContext* context)
    {
        // A background (no-hit) readback resolves to no sub-primitive.
        if (!readback.Hit)
        {
            return std::nullopt;
        }

        PrimitiveRefineRequest request{};
        // The readback carries the render id (entt handle + 1, 0 = background;
        // see StableEntityLookup::ToRenderId); echo it as the result's
        // correlation key. The durable StableId is resolved by the separate
        // StableEntityLookup path and is left 0 here.
        request.EntityId = readback.StableEntityId;
        request.Hint     = readback.EncodedId;

        const entt::entity entity =
            StableEntityLookup::ToEntityHandle(readback.StableEntityId);

        // Recycling safety: a stale render id naming a destroyed/recycled slot has
        // a mismatched version, so the live registry rejects it and the bridge
        // reports a deterministic StaleEntity result (RefinePrimitiveSelection
        // fail-closes on `!EntityIsLive` before touching any geometry) rather than
        // refining whatever entity now occupies the slot.
        if (!scene.Raw().valid(entity))
        {
            request.EntityIsLive = false;
            return RefinePrimitiveSelection(ConstSourceView{}, request);
        }

        request.EntityIsLive = true;
        if (const auto* world =
                scene.Raw().try_get<ECS::Components::Transform::WorldMatrix>(entity))
        {
            request.LocalToWorld = world->Matrix;
        }

        // BUG-026 — reconstruct the cursor from the depth readback and feed it
        // (plus the pick ray) into the refinement request in entity-local
        // space. Depth 1.0 is the clear value (no geometry); an ID hit with
        // clear depth is contradictory, so the anchor is simply skipped and
        // refinement falls back to hint-representative positions.
        const glm::mat4 worldToLocal = glm::inverse(request.LocalToWorld);
        std::optional<glm::vec3> worldCursor{};
        float cursorDepth = 1.0f;
        if (context != nullptr && readback.HasDepth && readback.Depth < 1.0f)
        {
            worldCursor = UnprojectPickDepth(context->InverseViewProjection,
                                             readback.PixelX,
                                             readback.PixelY,
                                             context->ViewportWidth,
                                             context->ViewportHeight,
                                             readback.Depth);
            if (worldCursor.has_value())
            {
                cursorDepth = readback.Depth;
                request.HasLocalHit = true;
                request.LocalHit = glm::vec3{worldToLocal * glm::vec4{*worldCursor, 1.0f}};
            }
        }
        if (context != nullptr && context->HasWorldRay)
        {
            // Entity-local pick ray for the missing-hint CPU fallback. The
            // fallback radius converts the pixel radius to world units, then
            // to local units via the largest inverse scale axis so
            // non-uniform scaling stays conservative. Perspective: one pixel
            // spans more world units the farther the hit, so scale by the
            // hit distance (the cursor when a depth anchor exists, else the
            // entity origin's distance). Orthographic: units-per-pixel is
            // depth-invariant, so the radius stays the constant pixel
            // footprint — multiplying by the hit distance would grow the
            // top-down pick radius with camera altitude (BUG-026 review
            // follow-up).
            float worldRadius =
                context->PickRadiusPixels * context->WorldUnitsPerPixelAtUnitDepth;
            if (!context->OrthographicProjection)
            {
                const glm::vec3 radiusReference = worldCursor.has_value()
                    ? *worldCursor
                    : glm::vec3{request.LocalToWorld[3]};
                worldRadius *= glm::max(
                    glm::length(radiusReference - context->WorldRayOrigin), 0.0f);
            }
            worldRadius = glm::max(worldRadius, 1.0e-5f);
            const glm::mat3 invLinear{worldToLocal};
            const float maxInverseScale = glm::max(
                glm::length(invLinear[0]),
                glm::max(glm::length(invLinear[1]), glm::length(invLinear[2])));
            request.HasPickRay = true;
            request.RayOrigin =
                glm::vec3{worldToLocal * glm::vec4{context->WorldRayOrigin, 1.0f}};
            request.RayDirection = invLinear * context->WorldRayDirection;
            request.FallbackRadius = glm::max(worldRadius * maxInverseScale, 1.0e-6f);
        }

        const ConstSourceView view =
            ECS::Components::GeometrySources::BuildConstView(scene.Raw(), entity);
        PrimitiveSelectionResult result = RefinePrimitiveSelection(view, request);
        if (request.HasLocalHit && worldCursor.has_value())
        {
            result.CursorFromDepth = true;
            result.Depth = cursorDepth;
            result.LocalCursor = request.LocalHit;
            result.WorldCursor = *worldCursor;
        }
        return result;
    }

    std::optional<PrimitiveSelectionResult> RefinePickReadbackResult(
        ECS::Scene::Registry& scene,
        const Extrinsic::Graphics::PickReadbackResult& readback)
    {
        return RefinePickReadbackResult(scene, readback, nullptr);
    }
}
