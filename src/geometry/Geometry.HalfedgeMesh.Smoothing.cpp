// Implements the two-stage face-normal bilateral mesh denoiser.
module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.Smoothing;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.DEC;
import Geometry.HalfedgeMesh.Utils;

namespace Geometry::Smoothing
{
    using MeshUtils::FaceArea;
    using MeshUtils::FaceAreaVector;
    using MeshUtils::FaceCentroid;

    // =========================================================================
    // Bilateral denoiser shared helpers (Stage 1 + Stage 2)
    // =========================================================================

    namespace
    {
        [[nodiscard]] bool IsFiniteVec(const glm::dvec3 v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] bool IsFiniteVec(const glm::vec3 v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] double ResolveEpsilon(const BilateralDenoiseParams& params) noexcept
        {
            // Non-finite or non-positive epsilon falls back to the default; the
            // non-finite case is separately reported as InvalidParams by
            // PrepareDenoise so the caller still fails closed.
            return params.DegenerateNormalLengthEpsilon > 0.0
                ? params.DegenerateNormalLengthEpsilon
                : BilateralDenoiseParams{}.DegenerateNormalLengthEpsilon;
        }

        // Per-face precomputed data used by both stages.
        struct FaceData
        {
            bool Usable{false};
            glm::dvec3 Centroid{0.0};
            glm::dvec3 Normal{0.0}; // unit area-weighted face normal
            double Area{0.0};
        };

        // Append the storage indices of the edge-adjacent, usable neighbour
        // faces of `f` (the "1-ring" across each of f's edges).
        void GatherFaceNeighbors(const HalfedgeMesh::Mesh& mesh,
                                 const FaceHandle f,
                                 const std::vector<FaceData>& faces,
                                 std::vector<std::size_t>& out)
        {
            out.clear();
            for (const HalfedgeHandle h : mesh.HalfedgesAroundFace(f))
            {
                const HalfedgeHandle o = mesh.OppositeHalfedge(h);
                if (!o.IsValid() || mesh.IsBoundary(o))
                {
                    continue;
                }
                const FaceHandle nf = mesh.Face(o);
                if (!nf.IsValid())
                {
                    continue;
                }
                const std::size_t idx = nf.Index;
                if (idx < faces.size() && faces[idx].Usable)
                {
                    out.push_back(idx);
                }
            }
        }

        // Validate params/topology/geometry and build per-face data. Returns the
        // fail-closed status; on any non-Success status the caller must leave the
        // mesh unmodified.
        [[nodiscard]] DenoiseStatus PrepareDenoise(const HalfedgeMesh::Mesh& mesh,
                                                   const BilateralDenoiseParams& params,
                                                   const double epsilon,
                                                   std::vector<FaceData>& faces,
                                                   BilateralDenoiseResult& result)
        {
            // 1. Invalid params: non-finite sigma or epsilon. (sigma <= 0 means
            //    auto-select, which is valid.)
            if (!std::isfinite(params.SigmaSpatial)
                || !std::isfinite(params.SigmaRange)
                || !std::isfinite(params.DegenerateNormalLengthEpsilon))
            {
                return DenoiseStatus::InvalidParams;
            }

            // 2. Empty mesh.
            if (mesh.IsEmpty())
            {
                return DenoiseStatus::EmptyMesh;
            }

            // 3. Non-finite vertex positions anywhere -> fail closed.
            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            {
                const VertexHandle v{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(v))
                {
                    continue;
                }
                if (!IsFiniteVec(mesh.Position(v)))
                {
                    return DenoiseStatus::NonFiniteInput;
                }
            }

            // 4. Non-manifold input. The halfedge representation rejects an edge
            //    with >2 incident faces at construction (AddFace fails closed),
            //    so the remaining detectable non-manifold case is a non-manifold
            //    vertex (a "bowtie" fan), which Mesh::IsManifold catches.
            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            {
                const VertexHandle v{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
                {
                    continue;
                }
                if (!mesh.IsManifold(v))
                {
                    return DenoiseStatus::NonManifoldInput;
                }
            }

            // 5. Per-face data; degenerate (zero-area) faces are counted and
            //    excluded from weighting.
            const std::size_t nF = mesh.FacesSize();
            faces.assign(nF, FaceData{});
            for (std::size_t i = 0; i < nF; ++i)
            {
                const FaceHandle f{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(f))
                {
                    ++result.SkippedDeletedFaceCount;
                    continue;
                }

                const glm::dvec3 areaVec = FaceAreaVector(mesh, f);
                const double areaVecLen = glm::length(areaVec);
                if (!IsFiniteVec(areaVec) || !std::isfinite(areaVecLen))
                {
                    ++result.NonFiniteFaceCount;
                    continue;
                }
                if (areaVecLen <= epsilon)
                {
                    ++result.DegenerateFaceCount;
                    continue;
                }

                FaceData& fd = faces[i];
                fd.Usable = true;
                fd.Normal = areaVec / areaVecLen;
                fd.Area = FaceArea(mesh, f);
                fd.Centroid = FaceCentroid(mesh, f);
                ++result.ProcessedFaceCount;
            }

            if (result.ProcessedFaceCount == 0)
            {
                return DenoiseStatus::DegenerateGeometry;
            }

            return DenoiseStatus::Success;
        }

        // Resolve auto (<=0) sigma values from 1-ring face-neighbour statistics.
        void ResolveSigmas(const HalfedgeMesh::Mesh& mesh,
                           const std::vector<FaceData>& faces,
                           const BilateralDenoiseParams& params,
                           double& sigmaSpatial,
                           double& sigmaRange)
        {
            sigmaSpatial = params.SigmaSpatial;
            sigmaRange = params.SigmaRange;
            if (sigmaSpatial > 0.0 && sigmaRange > 0.0)
            {
                return;
            }

            double distSum = 0.0;
            double normSum = 0.0;
            std::size_t pairCount = 0;
            std::vector<std::size_t> nbrs;
            for (std::size_t i = 0; i < faces.size(); ++i)
            {
                if (!faces[i].Usable)
                {
                    continue;
                }
                const FaceHandle f{static_cast<PropertyIndex>(i)};
                GatherFaceNeighbors(mesh, f, faces, nbrs);
                for (const std::size_t j : nbrs)
                {
                    distSum += glm::length(faces[i].Centroid - faces[j].Centroid);
                    normSum += glm::length(faces[i].Normal - faces[j].Normal);
                    ++pairCount;
                }
            }

            const double meanDist = pairCount ? distSum / static_cast<double>(pairCount) : 0.0;
            const double meanNorm = pairCount ? normSum / static_cast<double>(pairCount) : 0.0;

            // Floors keep the Gaussians well-defined on flat / isolated meshes.
            if (sigmaSpatial <= 0.0)
            {
                sigmaSpatial = meanDist > 1.0e-12 ? meanDist : 1.0;
            }
            if (sigmaRange <= 0.0)
            {
                sigmaRange = std::max(meanNorm, 1.0e-3);
            }
        }

        // Stage 1 core: iterate bilateral face-normal filtering into `filtered`
        // (double precision, indexed by face storage index; unusable faces hold
        // the zero vector). Double-buffered so each iteration reads only the
        // previous iteration's normals -> order-independent and deterministic.
        void FilterFaceNormalsCore(const HalfedgeMesh::Mesh& mesh,
                                   const std::vector<FaceData>& faces,
                                   const std::size_t iterations,
                                   const double sigmaSpatial,
                                   const double sigmaRange,
                                   const double epsilon,
                                   std::vector<glm::dvec3>& filtered)
        {
            const std::size_t nF = faces.size();
            filtered.assign(nF, glm::dvec3(0.0));
            for (std::size_t i = 0; i < nF; ++i)
            {
                if (faces[i].Usable)
                {
                    filtered[i] = faces[i].Normal;
                }
            }

            if (iterations == 0)
            {
                return;
            }

            const double invTwoSpatialSq = 1.0 / (2.0 * sigmaSpatial * sigmaSpatial);
            const double invTwoRangeSq = 1.0 / (2.0 * sigmaRange * sigmaRange);

            std::vector<glm::dvec3> next(nF, glm::dvec3(0.0));
            std::vector<std::size_t> nbrs;
            for (std::size_t iter = 0; iter < iterations; ++iter)
            {
                for (std::size_t i = 0; i < nF; ++i)
                {
                    if (!faces[i].Usable)
                    {
                        next[i] = filtered[i];
                        continue;
                    }

                    const FaceHandle f{static_cast<PropertyIndex>(i)};
                    // Include the face itself, weighted by its own area.
                    glm::dvec3 accum = faces[i].Area * filtered[i];
                    GatherFaceNeighbors(mesh, f, faces, nbrs);
                    for (const std::size_t j : nbrs)
                    {
                        const glm::dvec3 dc = faces[i].Centroid - faces[j].Centroid;
                        const double d2 = glm::dot(dc, dc);
                        const glm::dvec3 dn = filtered[i] - filtered[j];
                        const double nd2 = glm::dot(dn, dn);
                        const double w = std::exp(-d2 * invTwoSpatialSq)
                                       * std::exp(-nd2 * invTwoRangeSq)
                                       * faces[j].Area;
                        accum += w * filtered[j];
                    }

                    const double len = glm::length(accum);
                    next[i] = (std::isfinite(len) && len > epsilon)
                        ? accum / len
                        : filtered[i];
                }
                filtered.swap(next);
            }
        }
    } // namespace

    // =========================================================================
    // Bilateral denoiser — Stage 1 public entry point
    // =========================================================================

    std::string_view DebugName(const DenoiseStatus status) noexcept
    {
        switch (status)
        {
        case DenoiseStatus::Success:
            return "Success";
        case DenoiseStatus::EmptyMesh:
            return "EmptyMesh";
        case DenoiseStatus::NonManifoldInput:
            return "NonManifoldInput";
        case DenoiseStatus::DegenerateGeometry:
            return "DegenerateGeometry";
        case DenoiseStatus::NonFiniteInput:
            return "NonFiniteInput";
        case DenoiseStatus::InvalidParams:
            return "InvalidParams";
        }

        return "Unknown";
    }

    BilateralDenoiseResult FilterFaceNormals(const HalfedgeMesh::Mesh& mesh,
                                             const BilateralDenoiseParams& params,
                                             std::vector<glm::vec3>& filteredFaceNormals)
    {
        BilateralDenoiseResult result;
        result.VertexCount = mesh.VertexCount();
        filteredFaceNormals.assign(mesh.FacesSize(), glm::vec3(0.0f));

        const double epsilon = ResolveEpsilon(params);
        std::vector<FaceData> faces;
        result.Status = PrepareDenoise(mesh, params, epsilon, faces, result);
        if (result.Status != DenoiseStatus::Success)
        {
            return result;
        }

        double sigmaSpatial = 0.0;
        double sigmaRange = 0.0;
        ResolveSigmas(mesh, faces, params, sigmaSpatial, sigmaRange);
        result.SigmaSpatialUsed = sigmaSpatial;
        result.SigmaRangeUsed = sigmaRange;

        std::vector<glm::dvec3> filtered;
        FilterFaceNormalsCore(mesh, faces, params.NormalIterations,
                              sigmaSpatial, sigmaRange, epsilon, filtered);
        result.NormalIterationsPerformed = params.NormalIterations;

        for (std::size_t i = 0; i < faces.size(); ++i)
        {
            if (faces[i].Usable)
            {
                filteredFaceNormals[i] = glm::vec3(filtered[i]);
            }
        }
        return result;
    }

    // =========================================================================
    // Bilateral denoiser — Stage 2 vertex update + orchestrator
    // =========================================================================

    namespace
    {
        // Stage 2 (Sun et al. 2007 / Ohtake normal-projection update). The
        // filtered face normals are held fixed; only the incident-face centroids
        // are recomputed each iteration as positions move. Jacobi-style: all new
        // positions are computed against the previous iteration's state, then
        // written back, so the update is order-independent and deterministic.
        void UpdateVertexPositions(HalfedgeMesh::Mesh& mesh,
                                   const std::vector<FaceData>& faces,
                                   const std::vector<glm::dvec3>& filteredNormals,
                                   const std::size_t iterations,
                                   const bool preserveBoundary)
        {
            const std::size_t nV = mesh.VerticesSize();
            std::vector<glm::dvec3> newPos(nV, glm::dvec3(0.0));

            for (std::size_t iter = 0; iter < iterations; ++iter)
            {
                for (std::size_t i = 0; i < nV; ++i)
                {
                    const VertexHandle v{static_cast<PropertyIndex>(i)};
                    const glm::dvec3 xi = glm::dvec3(mesh.Position(v));
                    newPos[i] = xi;

                    if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
                    {
                        continue;
                    }
                    if (preserveBoundary && mesh.IsBoundary(v))
                    {
                        continue;
                    }

                    glm::dvec3 delta(0.0);
                    std::size_t count = 0;
                    for (const FaceHandle f : mesh.FacesAroundVertex(v))
                    {
                        if (!f.IsValid())
                        {
                            continue;
                        }
                        const std::size_t fi = f.Index;
                        if (fi >= faces.size() || !faces[fi].Usable)
                        {
                            continue;
                        }
                        const glm::dvec3 nf = filteredNormals[fi];
                        const glm::dvec3 cf = FaceCentroid(mesh, f); // current positions
                        delta += nf * glm::dot(nf, cf - xi);
                        ++count;
                    }

                    if (count == 0)
                    {
                        continue;
                    }

                    const glm::dvec3 step = delta / static_cast<double>(count);
                    // Fail closed: never write a non-finite position. The step
                    // can be finite in double precision yet push the coordinate
                    // past the float storage range, so validate the candidate
                    // *after* narrowing to glm::vec3; on overflow the vertex is
                    // left at its (finite) current position.
                    const glm::dvec3 candidate = xi + step;
                    if (IsFiniteVec(candidate) && IsFiniteVec(glm::vec3(candidate)))
                    {
                        newPos[i] = candidate;
                    }
                }

                for (std::size_t i = 0; i < nV; ++i)
                {
                    const VertexHandle v{static_cast<PropertyIndex>(i)};
                    if (mesh.IsDeleted(v))
                    {
                        continue;
                    }
                    mesh.Position(v) = glm::vec3(newPos[i]);
                }
            }
        }
    } // namespace

    BilateralDenoiseResult DenoiseBilateral(HalfedgeMesh::Mesh& mesh,
                                            const BilateralDenoiseParams& params)
    {
        BilateralDenoiseResult result;
        result.VertexCount = mesh.VertexCount();

        const double epsilon = ResolveEpsilon(params);
        std::vector<FaceData> faces;
        result.Status = PrepareDenoise(mesh, params, epsilon, faces, result);
        if (result.Status != DenoiseStatus::Success)
        {
            return result; // mesh left unmodified
        }

        double sigmaSpatial = 0.0;
        double sigmaRange = 0.0;
        ResolveSigmas(mesh, faces, params, sigmaSpatial, sigmaRange);
        result.SigmaSpatialUsed = sigmaSpatial;
        result.SigmaRangeUsed = sigmaRange;

        // Stage 1 — filter the face-normal field.
        std::vector<glm::dvec3> filtered;
        FilterFaceNormalsCore(mesh, faces, params.NormalIterations,
                              sigmaSpatial, sigmaRange, epsilon, filtered);
        result.NormalIterationsPerformed = params.NormalIterations;

        // Capture originals for move accounting.
        const std::size_t nV = mesh.VerticesSize();
        std::vector<glm::vec3> original(nV);
        for (std::size_t i = 0; i < nV; ++i)
        {
            original[i] = mesh.Position(VertexHandle{static_cast<PropertyIndex>(i)});
        }

        // Stage 2 — move vertices to agree with the filtered normals.
        UpdateVertexPositions(mesh, faces, filtered, params.VertexIterations,
                              params.PreserveBoundary);
        result.VertexIterationsPerformed = params.VertexIterations;

        // Diagnostics: pinned boundary vertices and vertices actually displaced.
        for (std::size_t i = 0; i < nV; ++i)
        {
            const VertexHandle v{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(v))
            {
                continue;
            }
            if (params.PreserveBoundary && mesh.IsBoundary(v))
            {
                ++result.PinnedBoundaryVertexCount;
            }
            const glm::vec3 p = mesh.Position(v);
            if (p.x != original[i].x || p.y != original[i].y || p.z != original[i].z)
            {
                ++result.MovedVertexCount;
            }
        }

        return result;
    }

} // namespace Geometry::Smoothing
