module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:MeshQuality.Impl;

import :MeshQuality;
import :Properties;
import :HalfedgeMesh;

namespace Geometry::MeshQuality
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kRadToDeg = 180.0 / kPi;
        constexpr double kSqrt3 = 1.7320508075688772;

        // Compute angle at vertex B in triangle ABC using atan2 for robustness
        double TriangleAngleAt(glm::vec3 a, glm::vec3 b, glm::vec3 c)
        {
            glm::vec3 ba = a - b;
            glm::vec3 bc = c - b;
            double crossLen = static_cast<double>(glm::length(glm::cross(ba, bc)));
            double dotVal = static_cast<double>(glm::dot(ba, bc));
            return std::atan2(crossLen, dotVal);
        }

        double TriangleArea(glm::vec3 a, glm::vec3 b, glm::vec3 c)
        {
            return 0.5 * static_cast<double>(glm::length(glm::cross(b - a, c - a)));
        }
    }

    std::optional<QualityResult> ComputeQuality(
        const Halfedge::Mesh& mesh,
        const QualityParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        QualityResult result;
        result.VertexCount = mesh.VertexCount();
        result.EdgeCount = mesh.EdgeCount();
        result.FaceCount = mesh.FaceCount();
        result.EulerCharacteristic = static_cast<int>(result.VertexCount)
                                   - static_cast<int>(result.EdgeCount)
                                   + static_cast<int>(result.FaceCount);

        // ------------------------------------------------------------------
        // Closedness & boundary loops
        // ------------------------------------------------------------------
        {
            bool isClosed = true;
            std::vector<bool> visited(mesh.HalfedgesSize(), false);
            std::size_t boundaryLoops = 0;

            for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
            {
                EdgeHandle eh{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(eh)) continue;
                if (mesh.IsBoundary(eh))
                {
                    isClosed = false;
                    // Walk boundary loop from this edge if not already visited
                    HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
                    HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);
                    HalfedgeHandle bh = mesh.IsBoundary(h0) ? h0 : h1;

                    if (!visited[bh.Index])
                    {
                        ++boundaryLoops;
                        HalfedgeHandle cur = bh;
                        std::size_t safety = 0;
                        do
                        {
                            visited[cur.Index] = true;
                            cur = mesh.NextHalfedge(cur);
                            if (++safety > mesh.HalfedgesSize()) break;
                        } while (cur != bh);
                    }
                }
            }
            result.IsClosed = isClosed;
            result.BoundaryLoopCount = boundaryLoops;
        }

        // ------------------------------------------------------------------
        // Edge length statistics (Welford's online algorithm)
        // ------------------------------------------------------------------
        if (params.ComputeEdgeLengths)
        {
            double minLen = std::numeric_limits<double>::max();
            double maxLen = 0.0;
            double mean = 0.0;
            double m2 = 0.0;
            std::size_t count = 0;

            for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
            {
                EdgeHandle eh{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(eh)) continue;

                HalfedgeHandle h{static_cast<PropertyIndex>(2u * ei)};
                glm::vec3 a = mesh.Position(mesh.FromVertex(h));
                glm::vec3 b = mesh.Position(mesh.ToVertex(h));
                double len = static_cast<double>(glm::distance(a, b));

                ++count;
                double delta = len - mean;
                mean += delta / static_cast<double>(count);
                double delta2 = len - mean;
                m2 += delta * delta2;

                if (len < minLen) minLen = len;
                if (len > maxLen) maxLen = len;
            }

            result.MinEdgeLength = (count > 0) ? minLen : 0.0;
            result.MaxEdgeLength = maxLen;
            result.MeanEdgeLength = mean;
            result.StdDevEdgeLength = (count > 1) ? std::sqrt(m2 / static_cast<double>(count - 1)) : 0.0;
        }

        // ------------------------------------------------------------------
        // Valence statistics
        // ------------------------------------------------------------------
        if (params.ComputeValence)
        {
            std::size_t minVal = std::numeric_limits<std::size_t>::max();
            std::size_t maxVal = 0;
            std::size_t sumVal = 0;
            std::size_t vertCount = 0;

            for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
            {
                VertexHandle vh{static_cast<PropertyIndex>(vi)};
                if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

                std::size_t val = mesh.Valence(vh);
                if (val < minVal) minVal = val;
                if (val > maxVal) maxVal = val;
                sumVal += val;
                ++vertCount;
            }

            result.MinValence = (vertCount > 0) ? minVal : 0;
            result.MaxValence = maxVal;
            result.MeanValence = (vertCount > 0) ? static_cast<double>(sumVal) / static_cast<double>(vertCount) : 0.0;
        }

        // ------------------------------------------------------------------
        // Per-face metrics: angles, aspect ratios, areas, volume
        // ------------------------------------------------------------------
        {
            double minAngle = std::numeric_limits<double>::max();
            double maxAngle = 0.0;
            double sumAngle = 0.0;
            std::size_t angleCount = 0;
            std::size_t smallAngleCount = 0;
            std::size_t largeAngleCount = 0;

            double minAR = std::numeric_limits<double>::max();
            double maxAR = 0.0;
            double sumAR = 0.0;
            std::size_t arCount = 0;

            double minArea = std::numeric_limits<double>::max();
            double maxArea = 0.0;
            double totalArea = 0.0;
            std::size_t degenerateFaces = 0;

            double volumeSum = 0.0;

            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                FaceHandle fh{static_cast<PropertyIndex>(fi)};
                if (mesh.IsDeleted(fh)) continue;

                HalfedgeHandle h0 = mesh.Halfedge(fh);
                HalfedgeHandle h1 = mesh.NextHalfedge(h0);
                HalfedgeHandle h2 = mesh.NextHalfedge(h1);

                VertexHandle va = mesh.ToVertex(h0);
                VertexHandle vb = mesh.ToVertex(h1);
                VertexHandle vc = mesh.ToVertex(h2);

                glm::vec3 pa = mesh.Position(va);
                glm::vec3 pb = mesh.Position(vb);
                glm::vec3 pc = mesh.Position(vc);

                // Area
                double area = TriangleArea(pa, pb, pc);
                if (params.ComputeAreas)
                {
                    if (area < minArea) minArea = area;
                    if (area > maxArea) maxArea = area;
                    totalArea += area;
                    if (area < params.DegenerateAreaEpsilon) ++degenerateFaces;
                }

                // Volume contribution (divergence theorem)
                if (params.ComputeVolume)
                {
                    volumeSum += static_cast<double>(glm::dot(pa, glm::cross(pb, pc)));
                }

                // Angles
                if (params.ComputeAngles)
                {
                    double angles[3] = {
                        TriangleAngleAt(pb, pa, pc) * kRadToDeg,
                        TriangleAngleAt(pa, pb, pc) * kRadToDeg,
                        TriangleAngleAt(pa, pc, pb) * kRadToDeg
                    };

                    for (double ang : angles)
                    {
                        if (ang < minAngle) minAngle = ang;
                        if (ang > maxAngle) maxAngle = ang;
                        sumAngle += ang;
                        ++angleCount;

                        if (ang < params.SmallAngleThreshold) ++smallAngleCount;
                        if (ang > params.LargeAngleThreshold) ++largeAngleCount;
                    }
                }

                // Aspect ratio: longest_edge / (2*sqrt(3)*inradius)
                // inradius = area / semi-perimeter
                if (params.ComputeAspectRatios && area > params.DegenerateAreaEpsilon)
                {
                    double la = static_cast<double>(glm::distance(pb, pc));
                    double lb = static_cast<double>(glm::distance(pa, pc));
                    double lc = static_cast<double>(glm::distance(pa, pb));
                    double longest = std::max({la, lb, lc});
                    double s = (la + lb + lc) / 2.0;
                    double inradius = area / s;
                    double ar = longest / (2.0 * kSqrt3 * inradius);

                    if (ar < minAR) minAR = ar;
                    if (ar > maxAR) maxAR = ar;
                    sumAR += ar;
                    ++arCount;
                }
            }

            if (params.ComputeAngles)
            {
                result.MinAngle = (angleCount > 0) ? minAngle : 0.0;
                result.MaxAngle = maxAngle;
                result.MeanAngle = (angleCount > 0) ? sumAngle / static_cast<double>(angleCount) : 0.0;
                result.SmallAngleCount = smallAngleCount;
                result.LargeAngleCount = largeAngleCount;
            }

            if (params.ComputeAspectRatios)
            {
                result.MinAspectRatio = (arCount > 0) ? minAR : 0.0;
                result.MaxAspectRatio = maxAR;
                result.MeanAspectRatio = (arCount > 0) ? sumAR / static_cast<double>(arCount) : 0.0;
            }

            if (params.ComputeAreas)
            {
                result.MinFaceArea = (mesh.FaceCount() > 0) ? minArea : 0.0;
                result.MaxFaceArea = maxArea;
                result.TotalArea = totalArea;
                result.MeanFaceArea = (mesh.FaceCount() > 0) ? totalArea / static_cast<double>(mesh.FaceCount()) : 0.0;
                result.DegenerateFaceCount = degenerateFaces;
            }

            if (params.ComputeVolume)
            {
                result.Volume = volumeSum / 6.0;
            }
        }

        return result;
    }

} // namespace Geometry::MeshQuality
