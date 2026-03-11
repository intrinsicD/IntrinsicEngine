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

import :Simplification;
import :Properties;
import :HalfedgeMesh;
import :MeshUtils;

namespace Geometry::Simplification
{
    namespace
    {
        [[nodiscard]] bool IsFinite(glm::vec3 v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] bool IsFinite(double v) noexcept
        {
            return std::isfinite(v);
        }

        [[nodiscard]] glm::dmat3 SelfOuterProduct(glm::dvec3 const& v) noexcept
        {
            glm::dmat3 m(0.0);
            m[0][0] = v.x * v.x;
            m[1][1] = v.y * v.y;
            m[2][2] = v.z * v.z;
            m[1][0] = m[0][1] = v.x * v.y;
            m[2][0] = m[0][2] = v.x * v.z;
            m[2][1] = m[1][2] = v.y * v.z;
            return m;
        }

        [[nodiscard]] glm::dmat3 CrossProductSquaredTranspose(glm::dvec3 const& v) noexcept
        {
            const double a = v.x;
            const double b = v.y;
            const double c = v.z;
            const double a2 = a * a;
            const double b2 = b * b;
            const double c2 = c * c;

            glm::dmat3 m(0.0);
            m[0][0] = b2 + c2;
            m[1][1] = a2 + c2;
            m[2][2] = a2 + b2;
            m[1][0] = m[0][1] = -a * b;
            m[2][0] = m[0][2] = -a * c;
            m[2][1] = m[1][2] = -b * c;
            return m;
        }

        struct Quadric
        {
            // x^T A x - 2 b^T x + c
            double A00{0.0};
            double A01{0.0};
            double A02{0.0};
            double A11{0.0};
            double A12{0.0};
            double A22{0.0};
            double b0{0.0};
            double b1{0.0};
            double b2{0.0};
            double c{0.0};

            [[nodiscard]] static Quadric FromCoefficients(glm::dmat3 const& A, glm::dvec3 const& b, double c) noexcept
            {
                Quadric q;
                q.A00 = A[0][0];
                q.A01 = A[0][1];
                q.A02 = A[0][2];
                q.A11 = A[1][1];
                q.A12 = A[1][2];
                q.A22 = A[2][2];
                q.b0 = b.x;
                q.b1 = b.y;
                q.b2 = b.z;
                q.c = c;
                return q;
            }

            [[nodiscard]] static Quadric PlaneQuadric(glm::dvec3 const& point, glm::dvec3 const& normal) noexcept
            {
                const double d = glm::dot(point, normal);
                return FromCoefficients(SelfOuterProduct(normal), normal * d, d * d);
            }

            [[nodiscard]] static Quadric TriangleQuadric(glm::dvec3 const& p, glm::dvec3 const& q, glm::dvec3 const& r) noexcept
            {
                const glm::dvec3 pxq = glm::cross(p, q);
                const glm::dvec3 qxr = glm::cross(q, r);
                const glm::dvec3 rxp = glm::cross(r, p);
                const glm::dvec3 crossSum = pxq + qxr + rxp;
                const double detPqr = glm::dot(pxq, r);
                return FromCoefficients(SelfOuterProduct(crossSum), crossSum * detPqr, detPqr * detPqr);
            }

            [[nodiscard]] static Quadric ProbabilisticTriangleQuadric(
                glm::dvec3 const& p,
                glm::dvec3 const& q,
                glm::dvec3 const& r,
                double positionStdDev) noexcept
            {
                const double sigma = positionStdDev * positionStdDev;
                const glm::dvec3 pxq = glm::cross(p, q);
                const glm::dvec3 qxr = glm::cross(q, r);
                const glm::dvec3 rxp = glm::cross(r, p);
                const double detPqr = glm::dot(pxq, r);
                const glm::dvec3 crossSum = pxq + qxr + rxp;
                const glm::dvec3 pmq = p - q;
                const glm::dvec3 qmr = q - r;
                const glm::dvec3 rmp = r - p;

                glm::dmat3 A = SelfOuterProduct(crossSum)
                             + (CrossProductSquaredTranspose(pmq)
                              +  CrossProductSquaredTranspose(qmr)
                              +  CrossProductSquaredTranspose(rmp)) * sigma;

                const double sigma2 = sigma * sigma;
                const double sigma2x6 = 6.0 * sigma2;
                const double sigma2x2 = 2.0 * sigma2;
                A[0][0] += sigma2x6;
                A[1][1] += sigma2x6;
                A[2][2] += sigma2x6;

                glm::dvec3 b = crossSum * detPqr;
                b -= (glm::cross(pmq, pxq) + glm::cross(qmr, qxr) + glm::cross(rmp, rxp)) * sigma;
                b += (p + q + r) * sigma2x2;

                double c = detPqr * detPqr;
                c += sigma * (glm::dot(pxq, pxq) + glm::dot(qxr, qxr) + glm::dot(rxp, rxp));
                c += sigma2x2 * (glm::dot(p, p) + glm::dot(q, q) + glm::dot(r, r));
                c += 6.0 * sigma2 * sigma;

                return FromCoefficients(A, b, c);
            }

            Quadric& operator+=(Quadric const& rhs) noexcept
            {
                A00 += rhs.A00; A01 += rhs.A01; A02 += rhs.A02;
                A11 += rhs.A11; A12 += rhs.A12; A22 += rhs.A22;
                b0 += rhs.b0; b1 += rhs.b1; b2 += rhs.b2;
                c += rhs.c;
                return *this;
            }

            [[nodiscard]] Quadric operator+(Quadric const& rhs) const noexcept
            {
                Quadric q = *this;
                q += rhs;
                return q;
            }

            Quadric& operator*=(double s) noexcept
            {
                A00 *= s; A01 *= s; A02 *= s;
                A11 *= s; A12 *= s; A22 *= s;
                b0 *= s; b1 *= s; b2 *= s;
                c *= s;
                return *this;
            }

            [[nodiscard]] double Evaluate(glm::dvec3 const& p) const noexcept
            {
                const glm::dvec3 Ap{
                    A00 * p.x + A01 * p.y + A02 * p.z,
                    A01 * p.x + A11 * p.y + A12 * p.z,
                    A02 * p.x + A12 * p.y + A22 * p.z
                };
                return glm::dot(p, Ap) - 2.0 * (p.x * b0 + p.y * b1 + p.z * b2) + c;
            }

            [[nodiscard]] double Evaluate(glm::vec3 const& p) const noexcept
            {
                return Evaluate(glm::dvec3(static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z)));
            }

            [[nodiscard]] std::optional<glm::vec3> TryMinimizer() const noexcept
            {
                const double det = A00 * (A11 * A22 - A12 * A12)
                                 - A01 * (A01 * A22 - A12 * A02)
                                 + A02 * (A01 * A12 - A11 * A02);

                if (!IsFinite(det) || std::abs(det) < 1e-18)
                {
                    return std::nullopt;
                }

                const double invDet = 1.0 / det;
                const double x = invDet * (
                    b0 * (A11 * A22 - A12 * A12) +
                    b1 * (A02 * A12 - A01 * A22) +
                    b2 * (A01 * A12 - A02 * A11));
                const double y = invDet * (
                    b0 * (A12 * A02 - A01 * A22) +
                    b1 * (A00 * A22 - A02 * A02) +
                    b2 * (A02 * A01 - A00 * A12));
                const double z = invDet * (
                    b0 * (A01 * A12 - A11 * A02) +
                    b1 * (A01 * A02 - A00 * A12) +
                    b2 * (A00 * A11 - A01 * A01));

                glm::vec3 p{
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z)
                };
                if (!IsFinite(p))
                {
                    return std::nullopt;
                }
                return p;
            }
        };

        [[nodiscard]] double ComputeMeanEdgeLength(Halfedge::Mesh const& mesh) noexcept
        {
            double total = 0.0;
            std::size_t count = 0;
            for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
            {
                const EdgeHandle e{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(e))
                {
                    continue;
                }
                const auto h = mesh.Halfedge(e, 0);
                const auto len = static_cast<double>(glm::distance(mesh.Position(mesh.FromVertex(h)), mesh.Position(mesh.ToVertex(h))));
                if (!IsFinite(len) || len <= 0.0)
                {
                    continue;
                }
                total += len;
                ++count;
            }
            return count > 0 ? total / static_cast<double>(count) : 1.0;
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
            HalfedgeHandle h = mesh.Halfedge(v);
            const HalfedgeHandle start = h;
            const std::size_t maxIter = mesh.HalfedgesSize();
            std::size_t iter = 0;
            do
            {
                const FaceHandle f = mesh.Face(h);
                if (f.IsValid() && !mesh.IsDeleted(f))
                {
                    ++count;
                }
                h = mesh.CWRotatedHalfedge(h);
                if (++iter > maxIter)
                {
                    return 0;
                }
            } while (h != start);
            return count;
        }

        struct CollapseCandidate
        {
            Geometry::HalfedgeHandle Halfedge;
            Geometry::EdgeHandle Edge;
            double Cost{std::numeric_limits<double>::infinity()};
            glm::vec3 OptimalPos{0.0f};
            std::size_t Version{0};

            [[nodiscard]] bool IsValid() const noexcept
            {
                return Halfedge.IsValid() && Edge.IsValid() && IsFinite(Cost) && Cost < std::numeric_limits<double>::max();
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
    }

    std::optional<SimplificationResult> Simplify(
        Halfedge::Mesh& mesh,
        const SimplificationParams& params)
    {
        using namespace Geometry;

        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nE = mesh.EdgesSize();
        if (mesh.FaceCount() < 4)
        {
            return std::nullopt;
        }

        const std::size_t targetFaces = params.TargetFaces > 0 ? params.TargetFaces : 1;
        const double meanEdgeLength = ComputeMeanEdgeLength(mesh);
        const double probabilisticStdDev = std::max(0.0, params.ProbabilisticPositionStdDevFactor) * meanEdgeLength;
        const double areaNormalEps2 = std::max(1e-24, meanEdgeLength * meanEdgeLength * meanEdgeLength * meanEdgeLength * 1e-12);
        const double maxNormalDeviationCos = params.MaxNormalDeviationDegrees > 0.0
            ? std::cos(std::numbers::pi_v<double> * params.MaxNormalDeviationDegrees / 180.0)
            : -1.0;

        auto isCollapseLegal = [&](HalfedgeHandle hCollapse, glm::vec3 const& newPos) -> bool
        {
            if (!mesh.IsCollapseOk(hCollapse) || !IsFinite(newPos))
            {
                return false;
            }

            const HalfedgeHandle hOpp = mesh.OppositeHalfedge(hCollapse);
            const VertexHandle vRemoved = mesh.FromVertex(hCollapse);
            const VertexHandle vSurvivor = mesh.ToVertex(hCollapse);
            if (mesh.IsDeleted(vRemoved) || mesh.IsDeleted(vSurvivor) || mesh.IsIsolated(vRemoved) || mesh.IsIsolated(vSurvivor))
            {
                return false;
            }

            const FaceHandle removedLeft = mesh.IsBoundary(hCollapse) ? FaceHandle{} : mesh.Face(hCollapse);
            const FaceHandle removedRight = mesh.IsBoundary(hOpp) ? FaceHandle{} : mesh.Face(hOpp);
            const VertexHandle vl = removedLeft.IsValid() ? mesh.ToVertex(mesh.NextHalfedge(hCollapse)) : VertexHandle{};
            const VertexHandle vr = removedRight.IsValid() ? mesh.ToVertex(mesh.NextHalfedge(hOpp)) : VertexHandle{};

            if (params.ForbidBoundaryInteriorCollapse && mesh.IsBoundary(vRemoved) && !mesh.IsBoundary(vSurvivor))
            {
                return false;
            }

            if (params.MinRemovedVertexIncidentFaces >= 2
                && CountIncidentFaces(mesh, vRemoved) < params.MinRemovedVertexIncidentFaces)
            {
                return false;
            }

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

            if (params.MaxEdgeLength > 0.0)
            {
                HalfedgeHandle h = mesh.Halfedge(vRemoved);
                const HalfedgeHandle start = h;
                const std::size_t maxIter = mesh.HalfedgesSize();
                std::size_t iter = 0;
                do
                {
                    const VertexHandle neighbor = mesh.ToVertex(h);
                    if (neighbor != vSurvivor && neighbor != vl && neighbor != vr)
                    {
                        const double len = static_cast<double>(glm::distance(newPos, mesh.Position(neighbor)));
                        if (!IsFinite(len) || len > params.MaxEdgeLength)
                        {
                            return false;
                        }
                    }
                    h = mesh.CWRotatedHalfedge(h);
                    if (++iter > maxIter)
                    {
                        return false;
                    }
                } while (h != start);
            }

            std::vector<uint8_t> visited(mesh.FacesSize(), 0u);
            auto checkRing = [&](VertexHandle v) -> bool
            {
                if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
                {
                    return true;
                }

                HalfedgeHandle h = mesh.Halfedge(v);
                const HalfedgeHandle start = h;
                const std::size_t maxIter = mesh.HalfedgesSize();
                std::size_t iter = 0;
                do
                {
                    const FaceHandle f = mesh.Face(h);
                    if (f.IsValid() && f != removedLeft && f != removedRight && !visited[f.Index])
                    {
                        visited[f.Index] = 1u;

                        MeshUtils::TriangleFaceView tri{};
                        if (!MeshUtils::TryGetTriangleFaceView(mesh, f, tri))
                        {
                            return false;
                        }

                        std::array<VertexHandle, 3> handles{tri.V0, tri.V1, tri.V2};
                        std::array<glm::vec3, 3> pts{tri.P0, tri.P1, tri.P2};
                        int replaced = 0;
                        for (int k = 0; k < 3; ++k)
                        {
                            if (handles[k] == vRemoved || handles[k] == vSurvivor)
                            {
                                pts[k] = newPos;
                                ++replaced;
                            }
                        }

                        if (replaced == 0)
                        {
                            h = mesh.CWRotatedHalfedge(h);
                            if (++iter > maxIter) return false;
                            continue;
                        }
                        if (replaced > 1)
                        {
                            return false;
                        }

                        const glm::vec3 oldNormal = glm::cross(tri.P1 - tri.P0, tri.P2 - tri.P0);
                        const glm::vec3 newNormal = glm::cross(pts[1] - pts[0], pts[2] - pts[0]);
                        const double oldLen2 = static_cast<double>(glm::dot(oldNormal, oldNormal));
                        const double newLen2 = static_cast<double>(glm::dot(newNormal, newNormal));
                        if (!IsFinite(newLen2) || newLen2 < areaNormalEps2)
                        {
                            return false;
                        }

                        if (oldLen2 >= areaNormalEps2)
                        {
                            const double orient = static_cast<double>(glm::dot(oldNormal, newNormal));
                            if (!IsFinite(orient) || orient <= 0.0)
                            {
                                return false;
                            }

                            if (params.MaxNormalDeviationDegrees > 0.0)
                            {
                                const double denom = std::sqrt(oldLen2 * newLen2);
                                const double cosAngle = orient / denom;
                                if (!IsFinite(cosAngle) || std::clamp(cosAngle, -1.0, 1.0) < maxNormalDeviationCos)
                                {
                                    return false;
                                }
                            }
                        }

                        if (params.MaxAspectRatio > 0.0)
                        {
                            const double before = TriangleAspectRatioMetric(tri.P0, tri.P1, tri.P2);
                            const double after = TriangleAspectRatioMetric(pts[0], pts[1], pts[2]);
                            if (!IsFinite(after) || (after > params.MaxAspectRatio && after > before))
                            {
                                return false;
                            }
                        }
                    }

                    h = mesh.CWRotatedHalfedge(h);
                    if (++iter > maxIter) return false;
                } while (h != start);

                return true;
            };

            return checkRing(vRemoved) && checkRing(vSurvivor);
        };

        std::vector<Quadric> vertexQuadrics(nV);

        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            const FaceHandle fh{static_cast<PropertyIndex>(fi)};
            MeshUtils::TriangleFaceView tri{};
            if (!MeshUtils::TryGetTriangleFaceView(mesh, fh, tri))
            {
                continue;
            }

            const glm::vec3 areaNormal = glm::cross(tri.P1 - tri.P0, tri.P2 - tri.P0);
            if (!IsFinite(areaNormal) || glm::dot(areaNormal, areaNormal) < 1e-20f)
            {
                continue;
            }

            const glm::dvec3 p{static_cast<double>(tri.P0.x), static_cast<double>(tri.P0.y), static_cast<double>(tri.P0.z)};
            const glm::dvec3 q{static_cast<double>(tri.P1.x), static_cast<double>(tri.P1.y), static_cast<double>(tri.P1.z)};
            const glm::dvec3 r{static_cast<double>(tri.P2.x), static_cast<double>(tri.P2.y), static_cast<double>(tri.P2.z)};

            const Quadric Kf = params.UseProbabilisticQuadrics
                ? Quadric::ProbabilisticTriangleQuadric(p, q, r, probabilisticStdDev)
                : Quadric::TriangleQuadric(p, q, r);

            vertexQuadrics[tri.V0.Index] += Kf;
            vertexQuadrics[tri.V1.Index] += Kf;
            vertexQuadrics[tri.V2.Index] += Kf;
        }

        if (!params.PreserveBoundary)
        {
            for (std::size_t ei = 0; ei < nE; ++ei)
            {
                const EdgeHandle eh{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(eh) || !mesh.IsBoundary(eh))
                {
                    continue;
                }

                const HalfedgeHandle h0 = mesh.Halfedge(eh, 0);
                const HalfedgeHandle h1 = mesh.Halfedge(eh, 1);
                const HalfedgeHandle hBnd = mesh.IsBoundary(h0) ? h0 : h1;
                const HalfedgeHandle hInt = mesh.OppositeHalfedge(hBnd);
                const VertexHandle vi = mesh.FromVertex(hInt);
                const VertexHandle vj = mesh.ToVertex(hInt);
                const FaceHandle f = mesh.Face(hInt);
                if (!f.IsValid())
                {
                    continue;
                }

                MeshUtils::TriangleFaceView tri{};
                if (!MeshUtils::TryGetTriangleFaceView(mesh, f, tri))
                {
                    continue;
                }

                glm::vec3 faceNormal = glm::cross(tri.P1 - tri.P0, tri.P2 - tri.P0);
                const float faceNormalLen = glm::length(faceNormal);
                const glm::vec3 edgeVec = mesh.Position(vj) - mesh.Position(vi);
                const float edgeLen = glm::length(edgeVec);
                if (faceNormalLen < 1e-10f || edgeLen < 1e-10f)
                {
                    continue;
                }

                faceNormal /= faceNormalLen;
                const glm::vec3 bndNormalF = glm::cross(edgeVec / edgeLen, faceNormal);
                const float bndNormalLen = glm::length(bndNormalF);
                if (bndNormalLen < 1e-10f)
                {
                    continue;
                }

                const glm::dvec3 bndNormal = glm::dvec3(bndNormalF / bndNormalLen);
                const glm::dvec3 pi{
                    static_cast<double>(mesh.Position(vi).x),
                    static_cast<double>(mesh.Position(vi).y),
                    static_cast<double>(mesh.Position(vi).z)
                };

                auto Kb = Quadric::PlaneQuadric(pi, bndNormal);
                Kb *= params.BoundaryWeight;
                vertexQuadrics[vi.Index] += Kb;
                vertexQuadrics[vj.Index] += Kb;
            }
        }

        std::vector<std::size_t> edgeVersion(nE, 0);

        auto computeDirectedCollapse = [&](HalfedgeHandle hCollapse) -> CollapseCandidate
        {
            CollapseCandidate best;
            best.Halfedge = hCollapse;
            best.Edge = mesh.Edge(hCollapse);
            best.Version = edgeVersion[best.Edge.Index];

            if (!mesh.IsCollapseOk(hCollapse))
            {
                return best;
            }

            const VertexHandle vRemoved = mesh.FromVertex(hCollapse);
            const VertexHandle vSurvivor = mesh.ToVertex(hCollapse);
            const Quadric Q = vertexQuadrics[vRemoved.Index] + vertexQuadrics[vSurvivor.Index];
            const glm::vec3 pRemoved = mesh.Position(vRemoved);
            const glm::vec3 pSurvivor = mesh.Position(vSurvivor);
            const glm::vec3 pm = (pRemoved + pSurvivor) * 0.5f;

            auto consider = [&](glm::vec3 const& candidate)
            {
                if (!IsFinite(candidate) || !isCollapseLegal(hCollapse, candidate))
                {
                    return;
                }
                const double c = Q.Evaluate(candidate);
                if (IsFinite(c) && c < best.Cost)
                {
                    best.Cost = std::max(0.0, c);
                    best.OptimalPos = candidate;
                }
            };

            if (auto opt = Q.TryMinimizer())
            {
                consider(*opt);
            }
            consider(pSurvivor);
            consider(pRemoved);
            consider(pm);

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
            if (!isCollapseLegal(top.Halfedge, top.OptimalPos))
            {
                continue;
            }

            const VertexHandle vRemoved = mesh.FromVertex(top.Halfedge);
            const VertexHandle vSurvivor = mesh.ToVertex(top.Halfedge);
            const Quadric merged = vertexQuadrics[vRemoved.Index] + vertexQuadrics[vSurvivor.Index];

            auto surviving = mesh.Collapse(top.Halfedge, top.OptimalPos);
            if (!surviving)
            {
                continue;
            }

            vertexQuadrics[surviving->Index] = merged;
            ++result.CollapseCount;
            result.FinalFaceCount = mesh.FaceCount();
            result.MaxCollapseError = std::max(result.MaxCollapseError, top.Cost);

            if (!mesh.IsIsolated(*surviving))
            {
                const HalfedgeHandle hStart = mesh.Halfedge(*surviving);
                HalfedgeHandle h = hStart;
                const std::size_t maxRingIter = mesh.HalfedgesSize();
                std::size_t ringIter = 0;
                do
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
                    h = mesh.CWRotatedHalfedge(h);
                    if (++ringIter > maxRingIter)
                    {
                        break;
                    }
                } while (h != hStart);
            }
        }

        return result;
    }

} // namespace Geometry::Simplification
