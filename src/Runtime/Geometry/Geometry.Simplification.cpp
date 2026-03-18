module;

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:Simplification.Impl;

import :Quadric;
import :Simplification;
import :Properties;
import :HalfedgeMesh;
import :MeshUtils;
import :Validation;

namespace Geometry::Simplification
{
    using Validation::IsFinite;

    namespace
    {
        // =====================================================================
        // Normal Cone — tracks accumulated normal deviation per face
        //
        // A bounding cone on the unit sphere that encloses a set of normals.
        // Represented as a center direction and a half-angle (in radians).
        // Used to reject edge collapses whose resulting surface normals would
        // deviate beyond a user-specified threshold (MaxNormalDeviationDegrees).
        //
        // Merge algorithm (spherical bounding cone union):
        //   Given two cones (C1, a1) and (C2, a2), the merged cone's half-angle
        //   covers the angular extent of both. When the center directions nearly
        //   coincide (dp ≈ 1), we simply take the wider angle. When they are
        //   nearly opposite (dp ≈ -1), the cone degenerates to a full sphere.
        //   Otherwise, compute the arc angle between centers, find the min/max
        //   extents on that arc, and re-center via spherical interpolation.
        // =====================================================================

        struct NormalCone
        {
            glm::dvec3 CenterNormal{0.0, 0.0, 1.0};
            double Angle{0.0};  // half-angle in radians

            NormalCone() = default;

            explicit NormalCone(glm::dvec3 const& normal, double angle = 0.0)
                : CenterNormal(normal), Angle(angle)
            {
            }

            NormalCone& Merge(glm::dvec3 const& n) { return Merge(NormalCone(n)); }

            NormalCone& Merge(NormalCone const& nc)
            {
                const double dp = glm::dot(CenterNormal, nc.CenterNormal);

                if (dp > 0.99999)
                {
                    // Nearly parallel — just widen to the larger cone.
                    Angle = std::max(Angle, nc.Angle);
                }
                else if (dp < -0.99999)
                {
                    // Opposite directions — cone covers the full sphere.
                    Angle = 2.0 * std::numbers::pi;
                }
                else
                {
                    // General case: compute bounding cone of two spherical caps.
                    // centerAngle = arc between the two cone center directions.
                    const double centerAngle = std::acos(std::clamp(dp, -1.0, 1.0));
                    // Extent of the combined region along the great arc:
                    const double minAngle = std::min(-Angle, centerAngle - nc.Angle);
                    const double maxAngle = std::max(Angle, centerAngle + nc.Angle);
                    Angle = 0.5 * (maxAngle - minAngle);

                    // Re-center via spherical SLERP to the midpoint of the extent.
                    const double axisAngle = 0.5 * (minAngle + maxAngle);
                    const double sinCA = std::sin(centerAngle);
                    if (std::abs(sinCA) > 1e-12)
                    {
                        CenterNormal = (CenterNormal * std::sin(centerAngle - axisAngle)
                                      + nc.CenterNormal * std::sin(axisAngle)) / sinCA;
                    }
                }

                return *this;
            }
        };

        // =====================================================================
        // Edge-based binary min-heap with version-based stale detection
        // =====================================================================

        struct CollapseCandidate
        {
            HalfedgeHandle Halfedge;
            EdgeHandle Edge;
            glm::vec3 Position{0.0f};
            double Cost{std::numeric_limits<double>::infinity()};
            std::size_t Version{0};

            [[nodiscard]] bool IsValid() const noexcept
            {
                return Halfedge.IsValid() && Edge.IsValid() && IsFinite(Cost)
                    && Cost < std::numeric_limits<double>::max();
            }
        };

        struct EdgeHeap
        {
            std::vector<CollapseCandidate> Entries;

            void Push(CollapseCandidate c)
            {
                Entries.push_back(c);
                SiftUp(Entries.size() - 1);
            }

            [[nodiscard]] bool Empty() const noexcept { return Entries.empty(); }

            CollapseCandidate Pop()
            {
                assert(!Entries.empty());
                CollapseCandidate top = Entries[0];
                Entries[0] = Entries.back();
                Entries.pop_back();
                if (!Entries.empty())
                {
                    SiftDown(0);
                }
                return top;
            }

        private:
            void SiftUp(std::size_t i)
            {
                while (i > 0)
                {
                    const std::size_t parent = (i - 1) / 2;
                    if (Entries[i].Cost < Entries[parent].Cost)
                    {
                        std::swap(Entries[i], Entries[parent]);
                        i = parent;
                    }
                    else
                    {
                        break;
                    }
                }
            }

            void SiftDown(std::size_t i)
            {
                const std::size_t n = Entries.size();
                while (true)
                {
                    const std::size_t left = 2 * i + 1;
                    const std::size_t right = 2 * i + 2;
                    std::size_t smallest = i;
                    if (left < n && Entries[left].Cost < Entries[smallest].Cost)
                    {
                        smallest = left;
                    }
                    if (right < n && Entries[right].Cost < Entries[smallest].Cost)
                    {
                        smallest = right;
                    }
                    if (smallest == i)
                    {
                        break;
                    }
                    std::swap(Entries[i], Entries[smallest]);
                    i = smallest;
                }
            }
        };

        // =====================================================================
        // Point-to-triangle distance (Voronoi region method)
        // =====================================================================

        [[nodiscard]] double PointTriangleDistance(
            glm::dvec3 const& p,
            glm::dvec3 const& a,
            glm::dvec3 const& b,
            glm::dvec3 const& c_pt) noexcept
        {
            const glm::dvec3 ab = b - a;
            const glm::dvec3 ac = c_pt - a;
            const glm::dvec3 ap = p - a;

            const double d1 = glm::dot(ab, ap);
            const double d2 = glm::dot(ac, ap);
            if (d1 <= 0.0 && d2 <= 0.0)
                return glm::length(p - a);

            const glm::dvec3 bp = p - b;
            const double d3 = glm::dot(ab, bp);
            const double d4 = glm::dot(ac, bp);
            if (d3 >= 0.0 && d4 <= d3)
                return glm::length(p - b);

            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
            {
                const double v = d1 / (d1 - d3);
                return glm::length(p - (a + v * ab));
            }

            const glm::dvec3 cp = p - c_pt;
            const double d5 = glm::dot(ab, cp);
            const double d6 = glm::dot(ac, cp);
            if (d6 >= 0.0 && d5 <= d6)
                return glm::length(p - c_pt);

            const double vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
            {
                const double w = d2 / (d2 - d6);
                return glm::length(p - (a + w * ac));
            }

            const double va = d3 * d6 - d5 * d4;
            if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
            {
                const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                return glm::length(p - (b + w * (c_pt - b)));
            }

            const double denom = 1.0 / (va + vb + vc);
            const double v = vb * denom;
            const double w = vc * denom;
            return glm::length(p - (a + v * ab + w * ac));
        }

        // =====================================================================
        // Face helpers
        // =====================================================================

        struct FaceTrianglePointsD
        {
            glm::dvec3 P0{0.0};
            glm::dvec3 P1{0.0};
            glm::dvec3 P2{0.0};
        };

        [[nodiscard]] FaceTrianglePointsD GetFaceTrianglePointsD(Halfedge::Mesh const& mesh, FaceHandle f) noexcept
        {
            MeshUtils::TriangleFaceView tri{};
            if (MeshUtils::TryGetTriangleFaceView(mesh, f, tri))
            {
                return {
                    glm::dvec3(tri.P0),
                    glm::dvec3(tri.P1),
                    glm::dvec3(tri.P2)};
            }

            // Preserve historical behavior for any non-triangular face that may
            // still reach simplification internals: use the leading 3-vertex
            // cycle rather than changing semantics to "skip face entirely".
            const HalfedgeHandle h0 = mesh.Halfedge(f);
            const HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            const HalfedgeHandle h2 = mesh.NextHalfedge(h1);
            return {
                glm::dvec3(mesh.Position(mesh.ToVertex(h0))),
                glm::dvec3(mesh.Position(mesh.ToVertex(h1))),
                glm::dvec3(mesh.Position(mesh.ToVertex(h2)))};
        }

        [[nodiscard]] glm::dvec3 ComputeFaceNormalD(Halfedge::Mesh const& mesh, FaceHandle f) noexcept
        {
            const FaceTrianglePointsD tri = GetFaceTrianglePointsD(mesh, f);
            glm::dvec3 n = glm::cross(tri.P1 - tri.P0, tri.P2 - tri.P0);
            const double len = glm::length(n);
            return len > 1e-12 ? n / len : glm::dvec3(0.0);
        }

        [[nodiscard]] double FacePointDistance(Halfedge::Mesh const& mesh, FaceHandle f, glm::vec3 const& p) noexcept
        {
            const FaceTrianglePointsD tri = GetFaceTrianglePointsD(mesh, f);
            return PointTriangleDistance(glm::dvec3(p), tri.P0, tri.P1, tri.P2);
        }

        [[nodiscard]] double TriangleAspectRatioMetric(glm::vec3 const& p0, glm::vec3 const& p1, glm::vec3 const& p2) noexcept
        {
            const glm::vec3 d0 = p0 - p1;
            const glm::vec3 d1 = p1 - p2;
            const glm::vec3 d2 = p2 - p0;
            const double l0 = static_cast<double>(glm::dot(d0, d0));
            const double l1 = static_cast<double>(glm::dot(d1, d1));
            const double l2 = static_cast<double>(glm::dot(d2, d2));
            const double maxLen2 = std::max(l0, std::max(l1, l2));
            const double doubleArea = static_cast<double>(glm::length(glm::cross(d0, d1)));
            if (!IsFinite(maxLen2) || !IsFinite(doubleArea) || doubleArea <= 1e-24)
            {
                return std::numeric_limits<double>::infinity();
            }
            return maxLen2 / doubleArea;
        }

        [[nodiscard]] std::size_t CountIncidentFaces(Halfedge::Mesh const& mesh, VertexHandle v) noexcept
        {
            if (!v.IsValid() || mesh.IsDeleted(v) || mesh.IsIsolated(v))
            {
                return 0;
            }

            std::size_t count = 0;
            for (const FaceHandle f : mesh.FacesAroundVertex(v))
            {
                (void)f;
                ++count;
            }
            return count;
        }

        template <typename Fn>
        void ForEachFace(Halfedge::Mesh const& mesh, VertexHandle v, Fn&& fn) noexcept;

        [[nodiscard]] bool UsesVertexResidence(QuadricResidence residence) noexcept
        {
            return residence != QuadricResidence::Faces;
        }

        [[nodiscard]] bool UsesFaceResidence(QuadricResidence residence) noexcept
        {
            return residence != QuadricResidence::Vertices;
        }

        [[nodiscard]] bool UsesFacePrimitive(QuadricType type) noexcept
        {
            return type != QuadricType::Point;
        }

        [[nodiscard]] glm::dmat3 ReadVertexCovariance(
            Property<glm::dmat3> const& property,
            VertexHandle v) noexcept
        {
            if (property && v.IsValid())
            {
                return property[v.Index];
            }
            return glm::dmat3(0.0);
        }

        [[nodiscard]] glm::dmat3 ReadFaceCovariance(
            Property<glm::dmat3> const& property,
            FaceHandle f) noexcept
        {
            if (property && f.IsValid())
            {
                return property[f.Index];
            }
            return glm::dmat3(0.0);
        }

        [[nodiscard]] Quadric ComputePointQuadric(Halfedge::Mesh const& mesh, VertexHandle v) noexcept
        {
            if (!v.IsValid() || mesh.IsDeleted(v) || mesh.IsIsolated(v))
            {
                return {};
            }
            return Quadric::PointQuadric(glm::dvec3(mesh.Position(v)));
        }

        [[nodiscard]] Quadric ComputeFaceQuadric(
            Halfedge::Mesh const& mesh,
            FaceHandle f,
            glm::dvec3 const& faceNormal,
            QuadricOptions const& options,
            std::vector<Quadric> const& vertexPointQuadrics,
            Property<glm::dmat3> const& vertexSigmaP,
            Property<glm::dmat3> const& faceSigmaP,
            Property<glm::dmat3> const& faceSigmaN) noexcept
        {
            if (!f.IsValid() || mesh.IsDeleted(f))
            {
                return {};
            }

            const HalfedgeHandle h0 = mesh.Halfedge(f);
            const HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            const HalfedgeHandle h2 = mesh.NextHalfedge(h1);
            const VertexHandle v0 = mesh.ToVertex(h0);
            const VertexHandle v1 = mesh.ToVertex(h1);
            const VertexHandle v2 = mesh.ToVertex(h2);

            if (options.Type == QuadricType::Point)
            {
                Quadric faceQ = vertexPointQuadrics[v0.Index] + vertexPointQuadrics[v1.Index] + vertexPointQuadrics[v2.Index];
                faceQ *= (1.0 / 3.0);
                return faceQ;
            }

            const glm::dvec3 p0(mesh.Position(v0));
            const glm::dvec3 p1(mesh.Position(v1));
            const glm::dvec3 p2(mesh.Position(v2));

            if (options.Type == QuadricType::Plane)
            {
                const glm::dvec3 center = (p0 + p1 + p2) / 3.0;
                switch (options.ProbabilisticMode)
                {
                case QuadricProbabilisticMode::Deterministic:
                    return Quadric::PlaneQuadric(glm::vec3(center), glm::vec3(faceNormal));
                case QuadricProbabilisticMode::Isotropic:
                    return Quadric::ProbabilisticPlaneQuadric(center, faceNormal, options.PositionStdDev, options.NormalStdDev);
                case QuadricProbabilisticMode::Covariance:
                    return Quadric::ProbabilisticPlaneQuadric(
                        center,
                        faceNormal,
                        ReadFaceCovariance(faceSigmaP, f),
                        ReadFaceCovariance(faceSigmaN, f));
                }
            }

            switch (options.ProbabilisticMode)
            {
            case QuadricProbabilisticMode::Deterministic:
                return Quadric::TriangleQuadric(glm::vec3(p0), glm::vec3(p1), glm::vec3(p2));
            case QuadricProbabilisticMode::Isotropic:
                return Quadric::ProbabilisticTriangleQuadric(p0, p1, p2, options.PositionStdDev);
            case QuadricProbabilisticMode::Covariance:
                return Quadric::ProbabilisticTriangleQuadric(
                    p0,
                    p1,
                    p2,
                    ReadVertexCovariance(vertexSigmaP, v0),
                    ReadVertexCovariance(vertexSigmaP, v1),
                    ReadVertexCovariance(vertexSigmaP, v2));
            }

            return {};
        }

        [[nodiscard]] Quadric ComputeVertexResidentQuadric(
            Halfedge::Mesh const& mesh,
            VertexHandle v,
            QuadricOptions const& options,
            std::vector<Quadric> const& faceQuadrics,
            std::vector<Quadric> const& vertexPointQuadrics) noexcept
        {
            if (!v.IsValid() || mesh.IsDeleted(v) || mesh.IsIsolated(v))
            {
                return {};
            }

            if (options.Type == QuadricType::Point)
            {
                return vertexPointQuadrics[v.Index];
            }

            Quadric q;
            double count = 0.0;
            ForEachFace(mesh, v, [&](FaceHandle f)
            {
                if (f.IsValid() && !mesh.IsDeleted(f))
                {
                    q += faceQuadrics[f.Index];
                    count += 1.0;
                }
            });

            if (options.AverageVertexQuadrics && count > 0.0)
            {
                q *= (1.0 / count);
            }
            return q;
        }

        void AppendUniqueFace(std::vector<FaceHandle>& faces, FaceHandle f)
        {
            if (!f.IsValid())
            {
                return;
            }

            for (FaceHandle existing : faces)
            {
                if (existing == f)
                {
                    return;
                }
            }
            faces.push_back(f);
        }

        [[nodiscard]] Quadric ComputeFaceResidentCollapseQuadric(
            Halfedge::Mesh const& mesh,
            HalfedgeHandle hCollapse,
            QuadricOptions const& options,
            std::vector<Quadric> const& faceQuadrics) noexcept
        {
            const VertexHandle vRemoved = mesh.FromVertex(hCollapse);
            const VertexHandle vSurvivor = mesh.ToVertex(hCollapse);
            const HalfedgeHandle hOpp = mesh.OppositeHalfedge(hCollapse);
            const FaceHandle removedLeft = mesh.IsBoundary(hCollapse) ? FaceHandle{} : mesh.Face(hCollapse);
            const FaceHandle removedRight = mesh.IsBoundary(hOpp) ? FaceHandle{} : mesh.Face(hOpp);

            std::vector<FaceHandle> contributingFaces;
            contributingFaces.reserve(16);
            ForEachFace(mesh, vRemoved, [&](FaceHandle f)
            {
                if (f != removedLeft && f != removedRight && !mesh.IsDeleted(f))
                {
                    AppendUniqueFace(contributingFaces, f);
                }
            });
            ForEachFace(mesh, vSurvivor, [&](FaceHandle f)
            {
                if (f != removedLeft && f != removedRight && !mesh.IsDeleted(f))
                {
                    AppendUniqueFace(contributingFaces, f);
                }
            });

            Quadric q;
            for (FaceHandle f : contributingFaces)
            {
                q += faceQuadrics[f.Index];
            }

            if (options.AverageFaceQuadrics && !contributingFaces.empty())
            {
                q *= (1.0 / static_cast<double>(contributingFaces.size()));
            }
            return q;
        }

        [[nodiscard]] Quadric ComputeCollapseQuadric(
            Halfedge::Mesh const& mesh,
            HalfedgeHandle hCollapse,
            QuadricOptions const& options,
            std::vector<Quadric> const& faceQuadrics,
            std::vector<Quadric> const& vertexPointQuadrics) noexcept
        {
            const VertexHandle vRemoved = mesh.FromVertex(hCollapse);
            const VertexHandle vSurvivor = mesh.ToVertex(hCollapse);

            Quadric q;
            if (UsesVertexResidence(options.Residence))
            {
                q += ComputeVertexResidentQuadric(mesh, vRemoved, options, faceQuadrics, vertexPointQuadrics);
                q += ComputeVertexResidentQuadric(mesh, vSurvivor, options, faceQuadrics, vertexPointQuadrics);
            }

            if (UsesFaceResidence(options.Residence))
            {
                q += ComputeFaceResidentCollapseQuadric(mesh, hCollapse, options, faceQuadrics);
            }

            return q;
        }

        [[nodiscard]] bool TryAppendUniquePosition(
            std::vector<glm::vec3>& positions,
            glm::vec3 const& candidate,
            float epsilon = 1e-6f) noexcept
        {
            if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y) || !std::isfinite(candidate.z))
            {
                return false;
            }

            const float epsilon2 = epsilon * epsilon;
            for (glm::vec3 const& existing : positions)
            {
                if (glm::dot(existing - candidate, existing - candidate) <= epsilon2)
                {
                    return false;
                }
            }

            positions.push_back(candidate);
            return true;
        }

        // =====================================================================
        // One-ring iteration helpers
        // =====================================================================

        template <typename Fn>
        void ForEachFace(Halfedge::Mesh const& mesh, VertexHandle v, Fn&& fn) noexcept
        {
            if (!v.IsValid() || mesh.IsDeleted(v) || mesh.IsIsolated(v))
            {
                return;
            }

            for (const FaceHandle f : mesh.FacesAroundVertex(v))
            {
                fn(f);
            }
        }

    } // anonymous namespace

    // =========================================================================
    // Main simplification entry point
    // =========================================================================

    std::optional<SimplificationResult> Simplify(
        Halfedge::Mesh& mesh,
        const SimplificationParams& params)
    {
        using namespace Geometry;

        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nE = mesh.EdgesSize();
        const std::size_t nF = mesh.FacesSize();
        if (mesh.FaceCount() < 4)
        {
            return std::nullopt;
        }

        const std::size_t targetFaces = params.TargetFaces > 0 ? params.TargetFaces : 1;
        const double normalDeviationRad = params.MaxNormalDeviationDegrees > 0.0
            ? params.MaxNormalDeviationDegrees / 180.0 * std::numbers::pi
            : 0.0;


        const Property<glm::dmat3> vertexSigmaP = params.Quadric.ProbabilisticMode == QuadricProbabilisticMode::Covariance
            && !params.Quadric.VertexPositionCovarianceProperty.empty()
            ? mesh.VertexProperties().Get<glm::dmat3>(params.Quadric.VertexPositionCovarianceProperty)
            : Property<glm::dmat3>{};
        const Property<glm::dmat3> faceSigmaP = params.Quadric.ProbabilisticMode == QuadricProbabilisticMode::Covariance
            && !params.Quadric.FacePositionCovarianceProperty.empty()
            ? mesh.FaceProperties().Get<glm::dmat3>(params.Quadric.FacePositionCovarianceProperty)
            : Property<glm::dmat3>{};
        const Property<glm::dmat3> faceSigmaN = params.Quadric.ProbabilisticMode == QuadricProbabilisticMode::Covariance
            && !params.Quadric.FaceNormalCovarianceProperty.empty()
            ? mesh.FaceProperties().Get<glm::dmat3>(params.Quadric.FaceNormalCovarianceProperty)
            : Property<glm::dmat3>{};

        // -----------------------------------------------------------------
        // Phase 1: Compute face normals and initialize configurable quadrics
        // -----------------------------------------------------------------

        std::vector<glm::dvec3> faceNormals(nF, glm::dvec3(0.0));
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            const FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh))
            {
                continue;
            }
            faceNormals[fi] = ComputeFaceNormalD(mesh, fh);
        }

        std::vector<Quadric> vertexPointQuadrics(nV);
        if (params.Quadric.Type == QuadricType::Point)
        {
            for (std::size_t vi = 0; vi < nV; ++vi)
            {
                const VertexHandle vh{static_cast<PropertyIndex>(vi)};
                vertexPointQuadrics[vi] = ComputePointQuadric(mesh, vh);
            }
        }

        const bool needFaceQuadrics = UsesFacePrimitive(params.Quadric.Type) || UsesFaceResidence(params.Quadric.Residence);
        std::vector<Quadric> faceQuadrics(nF);
        if (needFaceQuadrics)
        {
            for (std::size_t fi = 0; fi < nF; ++fi)
            {
                const FaceHandle fh{static_cast<PropertyIndex>(fi)};
                if (!mesh.IsDeleted(fh))
                {
                    faceQuadrics[fi] = ComputeFaceQuadric(
                        mesh,
                        fh,
                        faceNormals[fi],
                        params.Quadric,
                        vertexPointQuadrics,
                        vertexSigmaP,
                        faceSigmaP,
                        faceSigmaN);
                }
            }
        }

        // -----------------------------------------------------------------
        // Phase 2: Initialize normal cones (optional)
        // -----------------------------------------------------------------

        std::vector<NormalCone> normalCones;
        if (normalDeviationRad > 0.0)
        {
            normalCones.resize(nF);
            for (std::size_t fi = 0; fi < nF; ++fi)
            {
                const FaceHandle fh{static_cast<PropertyIndex>(fi)};
                if (!mesh.IsDeleted(fh))
                {
                    normalCones[fi] = NormalCone(faceNormals[fi]);
                }
            }
        }

        // -----------------------------------------------------------------
        // Phase 3: Initialize per-face point lists (optional, for Hausdorff)
        // -----------------------------------------------------------------

        using PointList = std::vector<glm::vec3>;
        std::vector<PointList> facePoints;
        if (params.HausdorffError > 0.0)
        {
            facePoints.resize(nF);
        }

        // -----------------------------------------------------------------
        // Phase 4: Collapse legality and cost
        // -----------------------------------------------------------------

        std::vector<std::size_t> edgeVersion(nE, 0);

        auto isCollapseLegal = [&](HalfedgeHandle hCollapse, glm::vec3 const& targetPosition) -> bool
        {
            if (!mesh.IsCollapseOk(hCollapse))
            {
                return false;
            }

            const HalfedgeHandle hOpp = mesh.OppositeHalfedge(hCollapse);
            const VertexHandle vRemoved = mesh.FromVertex(hCollapse);
            const VertexHandle vSurvivor = mesh.ToVertex(hCollapse);
            if (mesh.IsDeleted(vRemoved) || mesh.IsDeleted(vSurvivor)
                || mesh.IsIsolated(vRemoved) || mesh.IsIsolated(vSurvivor))
            {
                return false;
            }

            const FaceHandle removedLeft = mesh.IsBoundary(hCollapse) ? FaceHandle{} : mesh.Face(hCollapse);
            const FaceHandle removedRight = mesh.IsBoundary(hOpp) ? FaceHandle{} : mesh.Face(hOpp);
            const VertexHandle vl = removedLeft.IsValid() ? mesh.ToVertex(mesh.NextHalfedge(hCollapse)) : VertexHandle{};
            const VertexHandle vr = removedRight.IsValid() ? mesh.ToVertex(mesh.NextHalfedge(hOpp)) : VertexHandle{};

            // Preserve the open boundary as an immutable feature set: when enabled,
            // no collapse may consume or move a boundary vertex, even through an
            // interior edge adjacent to the boundary.
            if (params.PreserveBoundary
                && (mesh.IsBoundary(vRemoved) || mesh.IsBoundary(vSurvivor)))
            {
                return false;
            }

            // Boundary → interior check
            if (params.ForbidBoundaryInteriorCollapse
                && mesh.IsBoundary(vRemoved) && !mesh.IsBoundary(vSurvivor))
            {
                return false;
            }

            // Minimum incident faces on removed vertex
            if (params.MinRemovedVertexIncidentFaces >= 2
                && CountIncidentFaces(mesh, vRemoved) < params.MinRemovedVertexIncidentFaces)
            {
                return false;
            }

            // Max valence check
            if (params.MaxValence > 0)
            {
                const std::size_t valRemoved = mesh.Valence(vRemoved);
                const std::size_t valSurvivor = mesh.Valence(vSurvivor);
                std::size_t predicted = valRemoved + valSurvivor - 1u;
                if (removedLeft.IsValid())
                {
                    --predicted;
                }
                if (removedRight.IsValid())
                {
                    --predicted;
                }
                if (predicted > params.MaxValence && !(predicted < std::max(valRemoved, valSurvivor)))
                {
                    return false;
                }
            }

            const glm::vec3 p0 = mesh.Position(vRemoved);
            const glm::vec3 p1 = targetPosition;

            // Max edge length check
            if (params.MaxEdgeLength > 0.0)
            {
                for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(vRemoved))
                {
                    const VertexHandle neighbor = mesh.ToVertex(h);
                    if (neighbor != vSurvivor && neighbor != vl && neighbor != vr)
                    {
                        const double len = static_cast<double>(glm::distance(p1, mesh.Position(neighbor)));
                        if (!IsFinite(len) || len > params.MaxEdgeLength)
                        {
                            return false;
                        }
                    }
                }
            }

            // Normal-flip / normal-cone check
            if (normalDeviationRad <= 1e-12)
            {
                // Simple normal-flip check
                mesh.Position(vRemoved) = p1;
                bool flipped = false;
                ForEachFace(mesh, vRemoved, [&](FaceHandle f)
                {
                    if (flipped || f == removedLeft || f == removedRight)
                    {
                        return;
                    }
                    const glm::dvec3 n0 = faceNormals[f.Index];
                    const glm::dvec3 n1 = ComputeFaceNormalD(mesh, f);
                    if (glm::dot(n0, n1) < 0.0)
                    {
                        flipped = true;
                    }
                });
                mesh.Position(vRemoved) = p0;
                if (flipped)
                {
                    return false;
                }
            }
            else
            {
                // Normal cone check
                mesh.Position(vRemoved) = p1;

                FaceHandle fll;
                FaceHandle frr;
                if (vl.IsValid())
                {
                    const HalfedgeHandle hOpp2 = mesh.OppositeHalfedge(mesh.PrevHalfedge(hCollapse));
                    fll = mesh.Face(hOpp2);
                }
                if (vr.IsValid())
                {
                    const HalfedgeHandle hOpp2 = mesh.OppositeHalfedge(mesh.NextHalfedge(hOpp));
                    frr = mesh.Face(hOpp2);
                }

                bool coneExceeded = false;
                ForEachFace(mesh, vRemoved, [&](FaceHandle f)
                {
                    if (coneExceeded || f == removedLeft || f == removedRight)
                    {
                        return;
                    }

                    NormalCone nc = normalCones[f.Index];
                    nc.Merge(ComputeFaceNormalD(mesh, f));

                    if (f == fll && removedLeft.IsValid())
                    {
                        nc.Merge(normalCones[removedLeft.Index]);
                    }
                    if (f == frr && removedRight.IsValid())
                    {
                        nc.Merge(normalCones[removedRight.Index]);
                    }

                    if (nc.Angle > 0.5 * normalDeviationRad)
                    {
                        coneExceeded = true;
                    }
                });

                mesh.Position(vRemoved) = p0;
                if (coneExceeded)
                {
                    return false;
                }
            }

            // Aspect ratio check
            if (params.MaxAspectRatio > 0.0)
            {
                double ar0 = 0.0;
                double ar1 = 0.0;
                ForEachFace(mesh, vRemoved, [&](FaceHandle f)
                {
                    if (f == removedLeft || f == removedRight)
                    {
                        return;
                    }
                    MeshUtils::TriangleFaceView tri{};
                    if (!MeshUtils::TryGetTriangleFaceView(mesh, f, tri))
                    {
                        return;
                    }
                    ar0 = std::max(ar0, TriangleAspectRatioMetric(tri.P0, tri.P1, tri.P2));

                    mesh.Position(vRemoved) = p1;
                    MeshUtils::TriangleFaceView triNew{};
                    if (MeshUtils::TryGetTriangleFaceView(mesh, f, triNew))
                    {
                        ar1 = std::max(ar1, TriangleAspectRatioMetric(triNew.P0, triNew.P1, triNew.P2));
                    }
                    mesh.Position(vRemoved) = p0;
                });

                if (ar1 > params.MaxAspectRatio && ar1 > ar0)
                {
                    return false;
                }
            }

            // Hausdorff error check
            if (params.HausdorffError > 0.0)
            {
                PointList testPoints;
                ForEachFace(mesh, vRemoved, [&](FaceHandle f)
                {
                    const auto& pts = facePoints[f.Index];
                    testPoints.insert(testPoints.end(), pts.begin(), pts.end());
                });
                testPoints.push_back(p0);

                mesh.Position(vRemoved) = p1;
                bool hausdorffOk = true;
                for (const auto& pt : testPoints)
                {
                    bool pointOk = false;
                    ForEachFace(mesh, vRemoved, [&](FaceHandle f)
                    {
                        if (pointOk || f == removedLeft || f == removedRight)
                        {
                            return;
                        }
                        if (FacePointDistance(mesh, f, pt) < params.HausdorffError)
                        {
                            pointOk = true;
                        }
                    });
                    if (!pointOk)
                    {
                        hausdorffOk = false;
                        break;
                    }
                }
                mesh.Position(vRemoved) = p0;
                if (!hausdorffOk)
                {
                    return false;
                }
            }

            return true;
        };

        auto recomputeFaceData = [&](FaceHandle f)
        {
            if (!f.IsValid() || mesh.IsDeleted(f) || f.Index >= faceNormals.size())
            {
                return;
            }

            faceNormals[f.Index] = ComputeFaceNormalD(mesh, f);
            if (needFaceQuadrics && f.Index < faceQuadrics.size())
            {
                faceQuadrics[f.Index] = ComputeFaceQuadric(
                    mesh,
                    f,
                    faceNormals[f.Index],
                    params.Quadric,
                    vertexPointQuadrics,
                    vertexSigmaP,
                    faceSigmaP,
                    faceSigmaN);
            }
        };

        // Compute directed collapse: evaluate cost of collapsing FromVertex(h) into ToVertex(h)
        auto computeDirectedCollapse = [&](HalfedgeHandle hCollapse) -> CollapseCandidate
        {
            CollapseCandidate best;
            best.Halfedge = hCollapse;
            best.Edge = mesh.Edge(hCollapse);
            best.Version = edgeVersion[best.Edge.Index];
            best.Position = mesh.Position(mesh.ToVertex(hCollapse));

            if (!mesh.IsCollapseOk(hCollapse))
            {
                return best;
            }

            const VertexHandle vRemoved = mesh.FromVertex(hCollapse);
            const VertexHandle vSurvivor = mesh.ToVertex(hCollapse);

            const Quadric Q = ComputeCollapseQuadric(mesh, hCollapse, params.Quadric, faceQuadrics, vertexPointQuadrics);

            std::vector<glm::vec3> candidatePositions;
            candidatePositions.reserve(3);
            const glm::vec3 removedPos = mesh.Position(vRemoved);
            const glm::vec3 survivorPos = mesh.Position(vSurvivor);

            switch (params.Quadric.PlacementPolicy)
            {
            case CollapsePlacementPolicy::KeepSurvivor:
                static_cast<void>(TryAppendUniquePosition(candidatePositions, survivorPos));
                break;
            case CollapsePlacementPolicy::QuadricMinimizer:
                if (auto minimizer = Q.TryMinimizer())
                {
                    static_cast<void>(TryAppendUniquePosition(candidatePositions, *minimizer));
                }
                static_cast<void>(TryAppendUniquePosition(candidatePositions, survivorPos));
                break;
            case CollapsePlacementPolicy::BestOfEndpointsAndMinimizer:
                static_cast<void>(TryAppendUniquePosition(candidatePositions, survivorPos));
                static_cast<void>(TryAppendUniquePosition(candidatePositions, removedPos));
                if (auto minimizer = Q.TryMinimizer())
                {
                    static_cast<void>(TryAppendUniquePosition(candidatePositions, *minimizer));
                }
                break;
            }

            for (glm::vec3 const& candidatePosition : candidatePositions)
            {
                if (!isCollapseLegal(hCollapse, candidatePosition))
                {
                    continue;
                }

                const double cost = Q.Evaluate(candidatePosition);
                if (IsFinite(cost) && cost < best.Cost)
                {
                    best.Cost = std::max(0.0, cost);
                    best.Position = candidatePosition;
                }
            }

            return best;
        };

        auto computeCollapse = [&](EdgeHandle eh) -> CollapseCandidate
        {
            const CollapseCandidate c0 = computeDirectedCollapse(mesh.Halfedge(eh, 0));
            const CollapseCandidate c1 = computeDirectedCollapse(mesh.Halfedge(eh, 1));
            if (!c0.IsValid()) return c1;
            if (!c1.IsValid()) return c0;
            return c1.Cost < c0.Cost ? c1 : c0;
        };

        // -----------------------------------------------------------------
        // Phase 5: Build edge heap
        // -----------------------------------------------------------------

        EdgeHeap heap;
        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            const EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh))
            {
                continue;
            }
            if (params.PreserveBoundary && mesh.IsBoundary(eh))
            {
                continue;
            }
            CollapseCandidate candidate = computeCollapse(eh);
            if (candidate.IsValid())
            {
                heap.Push(candidate);
            }
        }

        // -----------------------------------------------------------------
        // Phase 6: Greedy collapse loop
        // -----------------------------------------------------------------

        SimplificationResult result;
        result.FinalFaceCount = mesh.FaceCount();

        while (!heap.Empty() && result.FinalFaceCount > targetFaces)
        {
            const CollapseCandidate top = heap.Pop();
            if (!top.IsValid() || mesh.IsDeleted(top.Edge))
            {
                continue;
            }
            if (top.Version != edgeVersion[top.Edge.Index])
            {
                continue;
            }
            if (top.Cost > params.MaxError)
            {
                break;
            }
            if (!mesh.IsCollapseOk(top.Halfedge))
            {
                continue;
            }
            if (!isCollapseLegal(top.Halfedge, top.Position))
            {
                continue;
            }

            const VertexHandle vRemoved = mesh.FromVertex(top.Halfedge);
            const VertexHandle vSurvivor = mesh.ToVertex(top.Halfedge);
            const HalfedgeHandle hOpp = mesh.OppositeHalfedge(top.Halfedge);
            const FaceHandle flOld = mesh.Face(top.Halfedge);
            const FaceHandle frOld = mesh.Face(hOpp);
            const VertexHandle vl = (flOld.IsValid() && !mesh.IsDeleted(flOld))
                ? mesh.ToVertex(mesh.NextHalfedge(top.Halfedge)) : VertexHandle{};
            const VertexHandle vr = (frOld.IsValid() && !mesh.IsDeleted(frOld))
                ? mesh.ToVertex(mesh.NextHalfedge(hOpp)) : VertexHandle{};

            // Save pre-collapse data for post-processing
            const glm::vec3 removedPos = mesh.Position(vRemoved);
            const Quadric mergedVertexPointQ = vertexPointQuadrics[vRemoved.Index] + vertexPointQuadrics[vSurvivor.Index];

            NormalCone flCone, frCone;
            if (normalDeviationRad > 0.0)
            {
                if (flOld.IsValid() && !mesh.IsDeleted(flOld))
                {
                    flCone = normalCones[flOld.Index];
                }
                if (frOld.IsValid() && !mesh.IsDeleted(frOld))
                {
                    frCone = normalCones[frOld.Index];
                }
            }

            PointList hausdorffPoints;
            if (params.HausdorffError > 0.0)
            {
                // Collect from v1's one-ring
                ForEachFace(mesh, vSurvivor, [&](FaceHandle f)
                {
                    auto& pts = facePoints[f.Index];
                    hausdorffPoints.insert(hausdorffPoints.end(), pts.begin(), pts.end());
                    pts.clear();
                });

                // Collect from removed triangles
                if (flOld.IsValid() && flOld.Index < facePoints.size())
                {
                    auto& pts = facePoints[flOld.Index];
                    hausdorffPoints.insert(hausdorffPoints.end(), pts.begin(), pts.end());
                    PointList().swap(pts);
                }
                if (frOld.IsValid() && frOld.Index < facePoints.size())
                {
                    auto& pts = facePoints[frOld.Index];
                    hausdorffPoints.insert(hausdorffPoints.end(), pts.begin(), pts.end());
                    PointList().swap(pts);
                }

                hausdorffPoints.push_back(removedPos);
            }

            // Perform collapse (vRemoved removed, vSurvivor stays at its position)
            auto surviving = mesh.Collapse(top.Halfedge, top.Position);
            if (!surviving)
            {
                continue;
            }

            ++result.CollapseCount;
            result.FinalFaceCount = mesh.FaceCount();
            result.MaxCollapseError = std::max(result.MaxCollapseError, top.Cost);

            if (params.Quadric.Type == QuadricType::Point)
            {
                vertexPointQuadrics[surviving->Index] = mergedVertexPointQ;
            }

            // Update face normals for the surviving one-ring
            ForEachFace(mesh, *surviving, [&](FaceHandle f)
            {
                recomputeFaceData(f);
            });

            // Update normal cones
            if (normalDeviationRad > 0.0)
            {
                ForEachFace(mesh, *surviving, [&](FaceHandle f)
                {
                    normalCones[f.Index].Merge(ComputeFaceNormalD(mesh, f));
                });

                if (vl.IsValid())
                {
                    auto hOpt = mesh.FindHalfedge(*surviving, vl);
                    if (hOpt)
                    {
                        FaceHandle f = mesh.Face(*hOpt);
                        if (f.IsValid() && !mesh.IsDeleted(f))
                        {
                            normalCones[f.Index].Merge(flCone);
                        }
                    }
                }
                if (vr.IsValid())
                {
                    auto hOpt = mesh.FindHalfedge(vr, *surviving);
                    if (hOpt)
                    {
                        FaceHandle f = mesh.Face(*hOpt);
                        if (f.IsValid() && !mesh.IsDeleted(f))
                        {
                            normalCones[f.Index].Merge(frCone);
                        }
                    }
                }
            }

            // Redistribute Hausdorff points
            if (params.HausdorffError > 0.0 && !hausdorffPoints.empty())
            {
                for (const auto& pt : hausdorffPoints)
                {
                    double minDist = std::numeric_limits<double>::max();
                    FaceHandle closestFace;

                    ForEachFace(mesh, *surviving, [&](FaceHandle f)
                    {
                        const double d = FacePointDistance(mesh, f, pt);
                        if (d < minDist)
                        {
                            minDist = d;
                            closestFace = f;
                        }
                    });

                    if (closestFace.IsValid() && closestFace.Index < facePoints.size())
                    {
                        facePoints[closestFace.Index].push_back(pt);
                    }
                }
            }

            // Update edge heap: recompute cost for all edges around surviving vertex
            // Skip if we've already reached the target — avoids expensive recomputation
            // on degenerate post-collapse topology that won't be used.
            if (result.FinalFaceCount > targetFaces && !mesh.IsIsolated(*surviving))
            {
                for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(*surviving))
                {
                    const EdgeHandle eAdj = mesh.Edge(h);
                    if (!mesh.IsDeleted(eAdj) && (!params.PreserveBoundary || !mesh.IsBoundary(eAdj)))
                    {
                        ++edgeVersion[eAdj.Index];
                        CollapseCandidate candidate = computeCollapse(eAdj);
                        if (candidate.IsValid())
                        {
                            heap.Push(candidate);
                        }
                    }
                }
            }
        }

        return result;
    }

} // namespace Geometry::Simplification
