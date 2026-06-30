module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

#include <Eigen/Dense>

module Geometry.PointCloud.Features;

import Geometry.PointCloud;
import Geometry.KDTree;
import Geometry.PCA;
import Geometry.Properties;

namespace Geometry::PointCloud::Features
{
    namespace
    {
        constexpr std::uint32_t kFpfhBins = 11;
        constexpr std::uint32_t kFpfhDimension = 3 * kFpfhBins; // 33

        [[nodiscard]] bool IsFiniteVec(glm::vec3 v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // Deterministic 64-bit SplitMix-style step; no std::random.
        [[nodiscard]] std::uint64_t NextRandom(std::uint64_t& state) noexcept
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        // Live (non-deleted, finite) mask over the cloud's raw position slots.
        // Deleted-but-not-collected points must never seed descriptors, become
        // neighbors, or be emitted, matching LivePoints()/p:deleted semantics.
        [[nodiscard]] std::vector<std::uint8_t> BuildLiveMask(const Cloud& cloud)
        {
            const std::span<const glm::vec3> positions = cloud.Positions();
            std::vector<std::uint8_t> live(positions.size(), 0u);
            for (std::size_t i = 0; i < positions.size(); ++i)
            {
                const VertexHandle v{static_cast<PropertyIndex>(i)};
                if (!cloud.IsDeleted(v) && IsFiniteVec(positions[i]))
                {
                    live[i] = 1u;
                }
            }
            return live;
        }

        // Neighbors within radius (excluding self and deleted slots), sorted
        // ascending by index and optionally capped. Deterministic regardless of
        // KDTree traversal order.
        void GatherRadiusNeighbors(
            const KDTree& tree,
            std::uint32_t self,
            glm::vec3 query,
            float radius,
            std::uint32_t maxNeighbors,
            const std::vector<std::uint8_t>& live,
            std::vector<KDTree::ElementIndex>& scratch,
            std::vector<std::uint32_t>& out)
        {
            out.clear();
            scratch.clear();
            const auto result = tree.QueryRadius(query, radius, scratch);
            if (!result)
            {
                return;
            }
            for (const KDTree::ElementIndex idx : scratch)
            {
                if (idx != self && idx < live.size() && live[idx] != 0u)
                {
                    out.push_back(static_cast<std::uint32_t>(idx));
                }
            }
            std::sort(out.begin(), out.end());
            if (maxNeighbors > 0 && out.size() > maxNeighbors)
            {
                out.resize(maxNeighbors);
            }
        }

        // Simplified Point Feature Histogram of point p against its neighbors,
        // packed as three concatenated kFpfhBins-bin sub-histograms (alpha, phi,
        // theta of the Darboux frame), each normalized to sum 100.
        [[nodiscard]] std::array<float, kFpfhDimension> ComputeSpfh(
            glm::vec3 p,
            glm::vec3 np,
            std::span<const glm::vec3> positions,
            std::span<const glm::vec3> normals,
            const std::vector<std::uint32_t>& neighbors)
        {
            std::array<float, kFpfhDimension> hist{};
            const glm::dvec3 u = glm::normalize(glm::dvec3(np));
            std::uint32_t counted = 0;
            for (const std::uint32_t j : neighbors)
            {
                const glm::dvec3 d = glm::dvec3(positions[j]) - glm::dvec3(p);
                const double dist = glm::length(d);
                if (dist < 1e-9)
                {
                    continue;
                }
                const glm::dvec3 dir = d / dist;
                const glm::dvec3 nq = glm::normalize(glm::dvec3(normals[j]));
                glm::dvec3 v = glm::cross(dir, u);
                const double vlen = glm::length(v);
                if (vlen < 1e-9)
                {
                    continue; // normal parallel to the connecting line
                }
                v /= vlen;
                const glm::dvec3 w = glm::cross(u, v);

                const double alpha = std::clamp(glm::dot(v, nq), -1.0, 1.0);
                const double phi = std::clamp(glm::dot(u, dir), -1.0, 1.0);
                const double theta = std::atan2(glm::dot(w, nq), glm::dot(u, nq));

                const auto bin = [&](double value, double lo, double hi) -> std::uint32_t {
                    const double t = (value - lo) / (hi - lo);
                    const auto idx = static_cast<std::int32_t>(std::floor(t * kFpfhBins));
                    return static_cast<std::uint32_t>(
                        std::clamp(idx, 0, static_cast<std::int32_t>(kFpfhBins) - 1));
                };

                hist[bin(alpha, -1.0, 1.0)] += 1.0f;
                hist[kFpfhBins + bin(phi, -1.0, 1.0)] += 1.0f;
                hist[2 * kFpfhBins + bin(theta, -std::numbers::pi, std::numbers::pi)] += 1.0f;
                ++counted;
            }

            if (counted > 0)
            {
                for (std::uint32_t block = 0; block < 3; ++block)
                {
                    double sum = 0.0;
                    for (std::uint32_t b = 0; b < kFpfhBins; ++b)
                    {
                        sum += hist[block * kFpfhBins + b];
                    }
                    if (sum > 0.0)
                    {
                        const double scale = 100.0 / sum;
                        for (std::uint32_t b = 0; b < kFpfhBins; ++b)
                        {
                            hist[block * kFpfhBins + b] =
                                static_cast<float>(hist[block * kFpfhBins + b] * scale);
                        }
                    }
                }
            }
            return hist;
        }

        // Closed-form rigid alignment (Kabsch/Umeyama, no scale) of paired point
        // sets. Returns nullopt for fewer than three pairs or degenerate spans.
        [[nodiscard]] std::optional<glm::dmat4> KabschRigid(
            std::span<const glm::dvec3> src,
            std::span<const glm::dvec3> dst)
        {
            const std::size_t n = src.size();
            if (n < 3 || dst.size() != n)
            {
                return std::nullopt;
            }
            Eigen::Vector3d cs = Eigen::Vector3d::Zero();
            Eigen::Vector3d ct = Eigen::Vector3d::Zero();
            for (std::size_t i = 0; i < n; ++i)
            {
                cs += Eigen::Vector3d(src[i].x, src[i].y, src[i].z);
                ct += Eigen::Vector3d(dst[i].x, dst[i].y, dst[i].z);
            }
            cs /= static_cast<double>(n);
            ct /= static_cast<double>(n);

            Eigen::Matrix3d h = Eigen::Matrix3d::Zero();
            for (std::size_t i = 0; i < n; ++i)
            {
                const Eigen::Vector3d ps = Eigen::Vector3d(src[i].x, src[i].y, src[i].z) - cs;
                const Eigen::Vector3d pt = Eigen::Vector3d(dst[i].x, dst[i].y, dst[i].z) - ct;
                h += ps * pt.transpose();
            }

            Eigen::JacobiSVD<Eigen::Matrix3d> svd(h, Eigen::ComputeFullU | Eigen::ComputeFullV);
            Eigen::Matrix3d u = svd.matrixU();
            Eigen::Matrix3d vmat = svd.matrixV();
            Eigen::Matrix3d r = vmat * u.transpose();
            if (r.determinant() < 0.0)
            {
                vmat.col(2) *= -1.0;
                r = vmat * u.transpose();
            }
            const Eigen::Vector3d t = ct - r * cs;

            glm::dmat4 out(1.0);
            for (int col = 0; col < 3; ++col)
            {
                for (int row = 0; row < 3; ++row)
                {
                    out[col][row] = r(row, col); // glm is column-major
                }
            }
            out[3][0] = t.x();
            out[3][1] = t.y();
            out[3][2] = t.z();
            return out;
        }

        [[nodiscard]] glm::dvec3 ApplyTransform(const glm::dmat4& m, glm::vec3 p) noexcept
        {
            const glm::dvec4 r = m * glm::dvec4(glm::dvec3(p), 1.0);
            return glm::dvec3(r);
        }
    } // namespace

    std::optional<float> EstimateSpacing(const Cloud& cloud)
    {
        const std::span<const glm::vec3> positions = cloud.Positions();
        if (positions.size() < 2)
        {
            return std::nullopt;
        }
        const std::vector<std::uint8_t> live = BuildLiveMask(cloud);
        KDTree tree;
        if (!tree.BuildFromPoints(positions))
        {
            return std::nullopt;
        }
        std::vector<KDTree::ElementIndex> knn;
        double sum = 0.0;
        std::size_t counted = 0;
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            if (live[i] == 0u)
            {
                continue;
            }
            knn.clear();
            // Query a few neighbors so the nearest live (non-deleted) one is
            // reachable even when deleted slots remain in the index.
            const auto result = tree.QueryKNN(positions[i], 8, knn);
            if (!result)
            {
                continue;
            }
            for (const KDTree::ElementIndex idx : knn)
            {
                if (idx != i && idx < live.size() && live[idx] != 0u)
                {
                    sum += glm::distance(positions[i], positions[idx]);
                    ++counted;
                    break;
                }
            }
        }
        if (counted == 0)
        {
            return std::nullopt;
        }
        return static_cast<float>(sum / static_cast<double>(counted));
    }

    std::optional<KeypointSet> DetectKeypoints(const Cloud& cloud, const KeypointParams& params)
    {
        const std::span<const glm::vec3> positions = cloud.Positions();
        if (positions.size() < params.MinNeighbors + 1u)
        {
            return std::nullopt;
        }
        const std::optional<float> spacing = EstimateSpacing(cloud);
        if (!spacing || *spacing <= 0.0f)
        {
            return std::nullopt;
        }
        const float salientRadius = params.SalientRadius > 0.0f ? params.SalientRadius : 6.0f * *spacing;
        const float nonMaxRadius = params.NonMaxRadius > 0.0f ? params.NonMaxRadius : 4.0f * *spacing;

        const std::vector<std::uint8_t> live = BuildLiveMask(cloud);
        KDTree tree;
        if (!tree.BuildFromPoints(positions))
        {
            return std::nullopt;
        }

        // Saliency (smallest eigenvalue) for every candidate that passes the
        // ISS eigenvalue-ratio gates; NaN marks a rejected point.
        std::vector<double> saliency(positions.size(), std::numeric_limits<double>::quiet_NaN());
        std::vector<KDTree::ElementIndex> radiusScratch;
        std::vector<std::uint32_t> neighbors;
        std::vector<glm::vec3> neighborhood;
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            if (live[i] == 0u)
            {
                continue;
            }
            GatherRadiusNeighbors(tree, static_cast<std::uint32_t>(i), positions[i],
                                  salientRadius, 0, live, radiusScratch, neighbors);
            if (neighbors.size() < params.MinNeighbors)
            {
                continue;
            }
            neighborhood.clear();
            neighborhood.push_back(positions[i]);
            for (const std::uint32_t j : neighbors)
            {
                neighborhood.push_back(positions[j]);
            }
            const Geometry::PCAResult pca = Geometry::ToPCA(neighborhood);
            if (!pca.Valid)
            {
                continue;
            }
            std::array<double, 3> ev{pca.Eigenvalues.x, pca.Eigenvalues.y, pca.Eigenvalues.z};
            std::sort(ev.begin(), ev.end(), std::greater<double>());
            const double l1 = ev[0];
            const double l2 = ev[1];
            const double l3 = ev[2];
            if (l1 <= 0.0 || l2 <= 0.0)
            {
                continue;
            }
            if ((l2 / l1) <= params.Gamma21 && (l3 / l2) <= params.Gamma32)
            {
                saliency[i] = l3;
            }
        }

        // Non-maximum suppression: keep a candidate only if no neighbor within
        // nonMaxRadius has strictly greater saliency (ties resolved by index).
        KeypointSet keypoints;
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            if (std::isnan(saliency[i]))
            {
                continue;
            }
            GatherRadiusNeighbors(tree, static_cast<std::uint32_t>(i), positions[i],
                                  nonMaxRadius, 0, live, radiusScratch, neighbors);
            bool isMax = true;
            for (const std::uint32_t j : neighbors)
            {
                if (std::isnan(saliency[j]))
                {
                    continue;
                }
                if (saliency[j] > saliency[i] ||
                    (saliency[j] == saliency[i] && j < i))
                {
                    isMax = false;
                    break;
                }
            }
            if (isMax)
            {
                keypoints.Indices.push_back(static_cast<std::uint32_t>(i));
                keypoints.Saliency.push_back(saliency[i]);
            }
        }
        return keypoints;
    }

    std::optional<DescriptorSet> ComputeDescriptors(
        const Cloud& cloud,
        std::span<const std::uint32_t> indices,
        const DescriptorParams& params)
    {
        if (params.Kind != DescriptorKind::FPFH)
        {
            return std::nullopt;
        }
        if (!cloud.HasNormals())
        {
            return std::nullopt; // precondition: generate normals first
        }
        const std::span<const glm::vec3> positions = cloud.Positions();
        const std::span<const glm::vec3> normals = cloud.Normals();
        if (positions.empty() || normals.size() != positions.size())
        {
            return std::nullopt;
        }
        const std::optional<float> spacing = EstimateSpacing(cloud);
        if (!spacing || *spacing <= 0.0f)
        {
            return std::nullopt;
        }
        const float featureRadius = params.FeatureRadius > 0.0f ? params.FeatureRadius : 5.0f * *spacing;

        const std::vector<std::uint8_t> live = BuildLiveMask(cloud);
        KDTree tree;
        if (!tree.BuildFromPoints(positions))
        {
            return std::nullopt;
        }

        // Stage 1: SPFH for every live point (needed by neighbors of query
        // points). Deleted slots keep a zero histogram and are never neighbors.
        std::vector<std::array<float, kFpfhDimension>> spfh(positions.size());
        std::vector<std::vector<std::uint32_t>> pointNeighbors(positions.size());
        std::vector<KDTree::ElementIndex> radiusScratch;
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            if (live[i] == 0u)
            {
                continue;
            }
            GatherRadiusNeighbors(tree, static_cast<std::uint32_t>(i), positions[i],
                                  featureRadius, params.MaxNeighbors, live, radiusScratch,
                                  pointNeighbors[i]);
            spfh[i] = ComputeSpfh(positions[i], normals[i], positions, normals, pointNeighbors[i]);
        }

        // Stage 2: FPFH at the requested indices (empty => all live points).
        std::vector<std::uint32_t> queryIndices;
        if (indices.empty())
        {
            for (std::size_t i = 0; i < positions.size(); ++i)
            {
                if (live[i] != 0u)
                {
                    queryIndices.push_back(static_cast<std::uint32_t>(i));
                }
            }
        }
        else
        {
            queryIndices.assign(indices.begin(), indices.end());
        }

        DescriptorSet out;
        out.Kind = DescriptorKind::FPFH;
        out.Dimension = kFpfhDimension;
        out.Count = static_cast<std::uint32_t>(queryIndices.size());
        out.Data.resize(queryIndices.size() * kFpfhDimension, 0.0f);
        out.SourceIndices = queryIndices;

        for (std::size_t row = 0; row < queryIndices.size(); ++row)
        {
            const std::uint32_t i = queryIndices[row];
            if (i >= positions.size() || live[i] == 0u)
            {
                return std::nullopt; // invalid or deleted query index
            }
            std::array<double, kFpfhDimension> fpfh{};
            for (std::uint32_t b = 0; b < kFpfhDimension; ++b)
            {
                fpfh[b] = spfh[i][b];
            }
            const std::vector<std::uint32_t>& nb = pointNeighbors[i];
            if (!nb.empty())
            {
                double weightSum = 0.0;
                for (const std::uint32_t j : nb)
                {
                    const double dist = glm::distance(positions[i], positions[j]);
                    if (dist < 1e-9)
                    {
                        continue;
                    }
                    const double w = 1.0 / dist;
                    weightSum += w;
                    for (std::uint32_t b = 0; b < kFpfhDimension; ++b)
                    {
                        fpfh[b] += w * spfh[j][b];
                    }
                }
                if (weightSum > 0.0)
                {
                    // SPFH(i) already counted once; average the weighted neighbor
                    // contribution as in Rusu et al. (1/k sum).
                    const double inv = 1.0 / static_cast<double>(nb.size());
                    for (std::uint32_t b = 0; b < kFpfhDimension; ++b)
                    {
                        fpfh[b] = spfh[i][b] + inv * (fpfh[b] - spfh[i][b]);
                    }
                }
            }

            // Renormalize each sub-histogram to sum 100 for scale invariance.
            for (std::uint32_t block = 0; block < 3; ++block)
            {
                double sum = 0.0;
                for (std::uint32_t b = 0; b < kFpfhBins; ++b)
                {
                    sum += fpfh[block * kFpfhBins + b];
                }
                const std::size_t base = row * kFpfhDimension + block * kFpfhBins;
                if (sum > 0.0)
                {
                    const double scale = 100.0 / sum;
                    for (std::uint32_t b = 0; b < kFpfhBins; ++b)
                    {
                        out.Data[base + b] = static_cast<float>(fpfh[block * kFpfhBins + b] * scale);
                    }
                }
            }
        }
        return out;
    }

    std::optional<CorrespondenceSet> MatchDescriptors(
        const DescriptorSet& source,
        const DescriptorSet& target,
        const CorrespondenceParams& params)
    {
        if (source.Dimension == 0 || source.Dimension != target.Dimension)
        {
            return std::nullopt;
        }
        if (source.Count == 0 || target.Count == 0)
        {
            return CorrespondenceSet{};
        }
        const std::uint32_t dim = source.Dimension;

        const auto distanceSquared = [dim](std::span<const float> a, std::span<const float> b) -> double {
            double acc = 0.0;
            for (std::uint32_t d = 0; d < dim; ++d)
            {
                const double diff = static_cast<double>(a[d]) - static_cast<double>(b[d]);
                acc += diff * diff;
            }
            return acc;
        };

        // best[i] = (targetRow, dist^2) for each source row, deterministic ties.
        const auto bestMatch = [&](const DescriptorSet& from, std::uint32_t fromRow,
                                   const DescriptorSet& to,
                                   double& outBest, double& outSecond) -> std::uint32_t {
            outBest = std::numeric_limits<double>::infinity();
            outSecond = std::numeric_limits<double>::infinity();
            std::uint32_t bestRow = 0;
            const std::span<const float> q = from.Row(fromRow);
            for (std::uint32_t r = 0; r < to.Count; ++r)
            {
                const double d2 = distanceSquared(q, to.Row(r));
                if (d2 < outBest)
                {
                    outSecond = outBest;
                    outBest = d2;
                    bestRow = r;
                }
                else if (d2 < outSecond)
                {
                    outSecond = d2;
                }
            }
            return bestRow;
        };

        CorrespondenceSet result;
        for (std::uint32_t s = 0; s < source.Count; ++s)
        {
            double best = 0.0;
            double second = 0.0;
            const std::uint32_t t = bestMatch(source, s, target, best, second);

            if (params.MaxRatio > 0.0f)
            {
                // With only one target row, or two exact-duplicate nearest
                // descriptors (second == 0), there is no valid best/second-best
                // comparison. The match is ambiguous, so reject it rather than
                // letting sparse or duplicate descriptors seed registration.
                if (!std::isfinite(second) || second <= 0.0)
                {
                    continue;
                }
                const double ratio = std::sqrt(best / second);
                if (ratio >= static_cast<double>(params.MaxRatio))
                {
                    continue;
                }
            }

            if (params.MutualBest)
            {
                double rb = 0.0;
                double rs = 0.0;
                const std::uint32_t backRow = bestMatch(target, t, source, rb, rs);
                if (backRow != s)
                {
                    continue;
                }
            }

            Correspondence c;
            c.SourceRow = s;
            c.TargetRow = t;
            c.Distance = static_cast<float>(std::sqrt(best));
            result.Pairs.push_back(c);
        }
        return result;
    }

    CoarseAlignmentResult EstimateCoarseAlignment(
        std::span<const glm::vec3> sourcePoints,
        std::span<const glm::vec3> targetPoints,
        const CorrespondenceSet& correspondences,
        const CoarseAlignmentParams& params)
    {
        CoarseAlignmentResult result;
        const std::size_t m = correspondences.Pairs.size();
        const std::uint32_t sampleSize = std::max<std::uint32_t>(params.SampleSize, 3u);
        if (m < sampleSize || m < 3)
        {
            result.Status = CoarseAlignmentStatus::InsufficientCorrespondences;
            return result;
        }

        // Resolve correspondence positions; bail on any out-of-range row.
        std::vector<glm::dvec3> srcPts(m);
        std::vector<glm::dvec3> dstPts(m);
        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        for (std::size_t i = 0; i < m; ++i)
        {
            const Correspondence& c = correspondences.Pairs[i];
            if (c.SourceRow >= sourcePoints.size() || c.TargetRow >= targetPoints.size())
            {
                result.Status = CoarseAlignmentStatus::DegenerateInput;
                return result;
            }
            const glm::vec3 s = sourcePoints[c.SourceRow];
            const glm::vec3 t = targetPoints[c.TargetRow];
            if (!IsFiniteVec(s) || !IsFiniteVec(t))
            {
                result.Status = CoarseAlignmentStatus::DegenerateInput;
                return result;
            }
            srcPts[i] = glm::dvec3(s);
            dstPts[i] = glm::dvec3(t);
            lo = glm::min(lo, s);
            hi = glm::max(hi, s);
        }

        float threshold = params.InlierThreshold;
        if (threshold <= 0.0f)
        {
            threshold = 0.05f * glm::length(hi - lo);
        }
        if (!(threshold > 0.0f))
        {
            result.Status = CoarseAlignmentStatus::DegenerateInput;
            return result;
        }
        const double threshold2 = static_cast<double>(threshold) * threshold;

        std::uint64_t rng = params.Seed;
        std::uint32_t bestInliers = 0;
        double bestRmse = std::numeric_limits<double>::infinity();
        glm::dmat4 bestTransform(1.0);

        std::vector<std::size_t> sample(sampleSize);
        std::vector<glm::dvec3> ss(sampleSize);
        std::vector<glm::dvec3> tt(sampleSize);
        for (std::uint32_t iter = 0; iter < params.MaxIterations; ++iter)
        {
            ++result.IterationsUsed;

            // Pick `sampleSize` distinct correspondences deterministically.
            for (std::uint32_t k = 0; k < sampleSize; ++k)
            {
                sample[k] = NextRandom(rng) % m;
            }
            bool distinct = true;
            for (std::uint32_t a = 0; a < sampleSize && distinct; ++a)
            {
                for (std::uint32_t b = a + 1; b < sampleSize; ++b)
                {
                    if (sample[a] == sample[b])
                    {
                        distinct = false;
                        break;
                    }
                }
            }
            if (!distinct)
            {
                continue;
            }

            // Geometric consistency: all pairwise source/target edge-length ratios.
            bool consistent = true;
            for (std::uint32_t a = 0; a < sampleSize && consistent; ++a)
            {
                for (std::uint32_t b = a + 1; b < sampleSize; ++b)
                {
                    const double sl = glm::length(srcPts[sample[a]] - srcPts[sample[b]]);
                    const double tl = glm::length(dstPts[sample[a]] - dstPts[sample[b]]);
                    if (sl < 1e-9 || tl < 1e-9)
                    {
                        consistent = false;
                        break;
                    }
                    const double ratio = std::min(sl, tl) / std::max(sl, tl);
                    if (ratio < params.EdgeLengthSimilarity)
                    {
                        consistent = false;
                        break;
                    }
                }
            }
            if (!consistent)
            {
                continue;
            }

            for (std::uint32_t k = 0; k < sampleSize; ++k)
            {
                ss[k] = srcPts[sample[k]];
                tt[k] = dstPts[sample[k]];
            }
            const std::optional<glm::dmat4> candidate = KabschRigid(ss, tt);
            if (!candidate)
            {
                continue;
            }

            std::uint32_t inliers = 0;
            double sse = 0.0;
            for (std::size_t i = 0; i < m; ++i)
            {
                const glm::dvec3 mapped = ApplyTransform(*candidate, glm::vec3(srcPts[i]));
                const glm::dvec3 diff = mapped - dstPts[i];
                const double d2 = glm::dot(diff, diff);
                if (d2 <= threshold2)
                {
                    ++inliers;
                    sse += d2;
                }
            }
            const double rmse = inliers > 0 ? std::sqrt(sse / static_cast<double>(inliers)) : std::numeric_limits<double>::infinity();
            if (inliers > bestInliers || (inliers == bestInliers && rmse < bestRmse))
            {
                bestInliers = inliers;
                bestRmse = rmse;
                bestTransform = *candidate;
            }
        }

        if (bestInliers < 3)
        {
            result.Status = CoarseAlignmentStatus::NoConsensus;
            return result;
        }

        // Refine on all inliers of the best model.
        std::vector<glm::dvec3> inlierSrc;
        std::vector<glm::dvec3> inlierDst;
        for (std::size_t i = 0; i < m; ++i)
        {
            const glm::dvec3 mapped = ApplyTransform(bestTransform, glm::vec3(srcPts[i]));
            const glm::dvec3 diff = mapped - dstPts[i];
            if (glm::dot(diff, diff) <= threshold2)
            {
                inlierSrc.push_back(srcPts[i]);
                inlierDst.push_back(dstPts[i]);
            }
        }
        if (const std::optional<glm::dmat4> refined = KabschRigid(inlierSrc, inlierDst))
        {
            bestTransform = *refined;
        }

        double sse = 0.0;
        std::uint32_t inliers = 0;
        for (std::size_t i = 0; i < m; ++i)
        {
            const glm::dvec3 mapped = ApplyTransform(bestTransform, glm::vec3(srcPts[i]));
            const glm::dvec3 diff = mapped - dstPts[i];
            const double d2 = glm::dot(diff, diff);
            if (d2 <= threshold2)
            {
                ++inliers;
                sse += d2;
            }
        }

        result.Transform = bestTransform;
        result.InlierCount = inliers;
        result.InlierRmse = inliers > 0 ? std::sqrt(sse / static_cast<double>(inliers)) : 0.0;
        result.Status = CoarseAlignmentStatus::Success;
        return result;
    }
} // namespace Geometry::PointCloud::Features
