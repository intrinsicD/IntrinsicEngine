// Test.PointCloudFeatures.cpp — GEOM-017 point-cloud descriptor / keypoint /
// correspondence / coarse-registration seams.
//
// Covers: descriptor record validation and deterministic match tie-breaks,
// the normals precondition, ISS keypoint determinism, FPFH rotation-invariant
// matching, RANSAC coarse alignment recovering a known rigid transform with and
// without outliers, degenerate neighborhoods, and that the existing ICP path
// (Geometry.Registration) remains reachable and is not replaced.

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Geometry.PointCloud;
import Geometry.PointCloud.Features;
import Geometry.Registration;

namespace
{
    namespace Feat = Geometry::PointCloud::Features;

    // Deterministic wavy-surface cloud with per-point analytic normals, so a
    // rigid image (positions + normals rotated) has identical FPFH descriptors.
    struct CloudData
    {
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals;
    };

    CloudData MakeWavyGrid(int n = 11, float step = 0.2f)
    {
        CloudData data;
        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j < n; ++j)
            {
                const float x = static_cast<float>(i) * step;
                const float y = static_cast<float>(j) * step;
                const float z = 0.3f * std::sin(1.3f * x) * std::cos(1.7f * y);
                const float fx = 0.3f * 1.3f * std::cos(1.3f * x) * std::cos(1.7f * y);
                const float fy = -0.3f * 1.7f * std::sin(1.3f * x) * std::sin(1.7f * y);
                data.Positions.emplace_back(x, y, z);
                data.Normals.push_back(glm::normalize(glm::vec3(-fx, -fy, 1.0f)));
            }
        }
        return data;
    }

    Geometry::PointCloud::Cloud MakeCloud(const CloudData& data)
    {
        Geometry::PointCloud::Cloud cloud;
        cloud.EnableNormals();
        cloud.Reserve(data.Positions.size());
        for (const glm::vec3 p : data.Positions)
        {
            cloud.AddPoint(p);
        }
        const std::span<glm::vec3> normals = cloud.Normals();
        for (std::size_t i = 0; i < data.Normals.size() && i < normals.size(); ++i)
        {
            normals[i] = data.Normals[i];
        }
        return cloud;
    }

    glm::mat3 RotationAboutAxis(glm::vec3 axis, float radians)
    {
        return glm::mat3(glm::rotate(glm::mat4(1.0f), radians, glm::normalize(axis)));
    }

    Feat::DescriptorSet MakeDescriptorSet(
        std::uint32_t dim, const std::vector<std::vector<float>>& rows)
    {
        Feat::DescriptorSet d;
        d.Dimension = dim;
        d.Count = static_cast<std::uint32_t>(rows.size());
        for (std::uint32_t i = 0; i < rows.size(); ++i)
        {
            d.Data.insert(d.Data.end(), rows[i].begin(), rows[i].end());
            d.SourceIndices.push_back(i);
        }
        return d;
    }
}

// --- Descriptor record validation and deterministic matching ---

TEST(PointCloudFeatures, RowAccessorIsBoundsChecked)
{
    const Feat::DescriptorSet d = MakeDescriptorSet(2, {{1.0f, 2.0f}, {3.0f, 4.0f}});
    EXPECT_EQ(d.Row(0).size(), 2u);
    EXPECT_EQ(d.Row(1)[1], 4.0f);
    EXPECT_TRUE(d.Row(2).empty()); // out of range
}

TEST(PointCloudFeatures, MatchRejectsMismatchedDimensions)
{
    const Feat::DescriptorSet a = MakeDescriptorSet(2, {{1.0f, 0.0f}});
    const Feat::DescriptorSet b = MakeDescriptorSet(3, {{1.0f, 0.0f, 0.0f}});
    EXPECT_FALSE(Feat::MatchDescriptors(a, b).has_value());
}

TEST(PointCloudFeatures, MatchTieBreakPrefersLowestTargetRow)
{
    const Feat::DescriptorSet source = MakeDescriptorSet(2, {{1.0f, 0.0f}});
    // Two identical target rows: the tie must resolve to the lower index.
    const Feat::DescriptorSet target = MakeDescriptorSet(2, {{1.0f, 0.0f}, {1.0f, 0.0f}});
    Feat::CorrespondenceParams params;
    params.MutualBest = false;
    const auto result = Feat::MatchDescriptors(source, target, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->Pairs.size(), 1u);
    EXPECT_EQ(result->Pairs[0].SourceRow, 0u);
    EXPECT_EQ(result->Pairs[0].TargetRow, 0u);
}

TEST(PointCloudFeatures, MatchRatioRejectsSingleTargetAmbiguity)
{
    const Feat::DescriptorSet source = MakeDescriptorSet(2, {{1.0f, 0.0f}});
    const Feat::DescriptorSet target = MakeDescriptorSet(2, {{0.0f, 1.0f}}); // only one row
    Feat::CorrespondenceParams params;
    params.MutualBest = false;
    params.MaxRatio = 0.8f; // no valid second-best -> reject
    const auto result = Feat::MatchDescriptors(source, target, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Pairs.empty());
}

TEST(PointCloudFeatures, MatchRatioRejectsDuplicateTargets)
{
    const Feat::DescriptorSet source = MakeDescriptorSet(2, {{1.0f, 0.0f}});
    // Two exact-duplicate nearest targets => best == second == 0.
    const Feat::DescriptorSet target = MakeDescriptorSet(2, {{1.0f, 0.0f}, {1.0f, 0.0f}});
    Feat::CorrespondenceParams params;
    params.MutualBest = false;
    params.MaxRatio = 0.8f;
    const auto result = Feat::MatchDescriptors(source, target, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Pairs.empty());
}

TEST(PointCloudFeatures, ComputeDescriptorsSkipsDeletedPoints)
{
    auto cloud = MakeCloud(MakeWavyGrid());
    const std::size_t before = cloud.VertexCount();
    cloud.DeletePoint(Geometry::VertexHandle{5}); // delete without garbage collection

    const auto desc = Feat::ComputeDescriptors(cloud, {});
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->Count, cloud.VertexCount());
    EXPECT_EQ(static_cast<std::size_t>(desc->Count), before - 1u);
    for (const std::uint32_t idx : desc->SourceIndices)
    {
        EXPECT_NE(idx, 5u);
    }
}

TEST(PointCloudFeatures, KeypointsSkipDeletedPoints)
{
    auto cloud = MakeCloud(MakeWavyGrid());
    cloud.DeletePoint(Geometry::VertexHandle{5});
    const auto kp = Feat::DetectKeypoints(cloud);
    ASSERT_TRUE(kp.has_value());
    for (const std::uint32_t idx : kp->Indices)
    {
        EXPECT_NE(idx, 5u);
    }
}

TEST(PointCloudFeatures, ComputeDescriptorsRequiresNormals)
{
    CloudData data = MakeWavyGrid(5);
    Geometry::PointCloud::Cloud cloud; // no normals enabled
    for (const glm::vec3 p : data.Positions)
    {
        cloud.AddPoint(p);
    }
    EXPECT_FALSE(Feat::ComputeDescriptors(cloud, {}).has_value());
}

// --- Keypoints ---

TEST(PointCloudFeatures, DetectKeypointsIsDeterministic)
{
    const CloudData data = MakeWavyGrid();
    const auto cloud = MakeCloud(data);

    const auto a = Feat::DetectKeypoints(cloud);
    const auto b = Feat::DetectKeypoints(cloud);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a->Indices, b->Indices);
    // Indices must be sorted ascending and unique.
    for (std::size_t i = 1; i < a->Indices.size(); ++i)
    {
        EXPECT_LT(a->Indices[i - 1], a->Indices[i]);
    }
}

TEST(PointCloudFeatures, DetectKeypointsRejectsTooFewPoints)
{
    CloudData data;
    data.Positions = {{0, 0, 0}, {1, 0, 0}};
    Geometry::PointCloud::Cloud cloud;
    for (const glm::vec3 p : data.Positions)
    {
        cloud.AddPoint(p);
    }
    EXPECT_FALSE(Feat::DetectKeypoints(cloud).has_value());
}

// --- FPFH rotation invariance ---

TEST(PointCloudFeatures, DescriptorMatchingIsRotationInvariant)
{
    const CloudData src = MakeWavyGrid();
    const glm::mat3 R = RotationAboutAxis({0.3f, 1.0f, 0.5f}, 0.6f);

    CloudData tgt;
    for (std::size_t i = 0; i < src.Positions.size(); ++i)
    {
        tgt.Positions.push_back(R * src.Positions[i]);
        tgt.Normals.push_back(glm::normalize(R * src.Normals[i]));
    }

    const auto srcCloud = MakeCloud(src);
    const auto tgtCloud = MakeCloud(tgt);

    const auto srcDesc = Feat::ComputeDescriptors(srcCloud, {});
    const auto tgtDesc = Feat::ComputeDescriptors(tgtCloud, {});
    ASSERT_TRUE(srcDesc.has_value());
    ASSERT_TRUE(tgtDesc.has_value());
    EXPECT_EQ(srcDesc->Dimension, 33u);

    Feat::CorrespondenceParams params;
    params.MutualBest = true;
    const auto matches = Feat::MatchDescriptors(*srcDesc, *tgtDesc, params);
    ASSERT_TRUE(matches.has_value());
    ASSERT_GT(matches->Pairs.size(), 0u);

    // FPFH is rotation-invariant, so descriptor i is identical in both clouds;
    // most mutual-best matches must be the i<->i pair with near-zero distance.
    std::size_t correct = 0;
    for (const Feat::Correspondence& c : matches->Pairs)
    {
        if (c.SourceRow == c.TargetRow)
        {
            ++correct;
        }
    }
    EXPECT_GE(correct, matches->Pairs.size() * 7 / 10);
}

// --- Coarse alignment ---

namespace
{
    Feat::CorrespondenceSet GroundTruthCorrespondences(std::size_t count)
    {
        Feat::CorrespondenceSet set;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            set.Pairs.push_back({i, i, 0.0f});
        }
        return set;
    }

    double TransformError(const glm::dmat4& m,
                          const std::vector<glm::vec3>& src,
                          const std::vector<glm::vec3>& dst)
    {
        double worst = 0.0;
        for (std::size_t i = 0; i < src.size(); ++i)
        {
            const glm::dvec4 mapped = m * glm::dvec4(glm::dvec3(src[i]), 1.0);
            worst = std::max(worst, glm::length(glm::dvec3(mapped) - glm::dvec3(dst[i])));
        }
        return worst;
    }
}

TEST(PointCloudFeatures, CoarseAlignmentRecoversKnownTransform)
{
    const CloudData src = MakeWavyGrid();
    const glm::mat3 R = RotationAboutAxis({0.2f, 0.4f, 1.0f}, 0.5f);
    const glm::vec3 t(1.5f, -0.75f, 2.0f);

    std::vector<glm::vec3> tgt;
    for (const glm::vec3 p : src.Positions)
    {
        tgt.push_back(R * p + t);
    }

    const auto corr = GroundTruthCorrespondences(src.Positions.size());
    const auto result = Feat::EstimateCoarseAlignment(src.Positions, tgt, corr);

    EXPECT_EQ(result.Status, Feat::CoarseAlignmentStatus::Success);
    EXPECT_GE(result.InlierCount, src.Positions.size() * 9 / 10);
    EXPECT_LT(TransformError(result.Transform, src.Positions, tgt), 1e-3);
}

TEST(PointCloudFeatures, CoarseAlignmentRejectsOutliers)
{
    const CloudData src = MakeWavyGrid();
    const glm::mat3 R = RotationAboutAxis({1.0f, 0.1f, 0.2f}, 0.4f);
    const glm::vec3 t(0.5f, 1.0f, -1.0f);

    std::vector<glm::vec3> tgt;
    for (const glm::vec3 p : src.Positions)
    {
        tgt.push_back(R * p + t);
    }

    // Start from ground-truth pairs, then corrupt every 7th pair to a wrong
    // target to inject gross outliers.
    Feat::CorrespondenceSet corr = GroundTruthCorrespondences(src.Positions.size());
    const std::uint32_t n = static_cast<std::uint32_t>(src.Positions.size());
    for (std::uint32_t i = 0; i < corr.Pairs.size(); i += 7)
    {
        corr.Pairs[i].TargetRow = (corr.Pairs[i].TargetRow + n / 2) % n;
    }

    const auto result = Feat::EstimateCoarseAlignment(src.Positions, tgt, corr);
    EXPECT_EQ(result.Status, Feat::CoarseAlignmentStatus::Success);
    EXPECT_LT(TransformError(result.Transform, src.Positions, tgt), 1e-2);
    // Roughly the 6/7 inlier correspondences should be recovered.
    EXPECT_GE(result.InlierCount, src.Positions.size() * 3 / 4);
}

TEST(PointCloudFeatures, CoarseAlignmentHonorsLargerSampleSize)
{
    const CloudData src = MakeWavyGrid();
    const glm::mat3 R = RotationAboutAxis({0.5f, 0.2f, 1.0f}, 0.45f);
    const glm::vec3 t(0.4f, -0.6f, 1.2f);
    std::vector<glm::vec3> tgt;
    for (const glm::vec3 p : src.Positions)
        tgt.push_back(R * p + t);

    Feat::CoarseAlignmentParams params;
    params.SampleSize = 5; // larger minimal sample must still fit and converge
    const auto corr = GroundTruthCorrespondences(src.Positions.size());
    const auto result = Feat::EstimateCoarseAlignment(src.Positions, tgt, corr, params);

    EXPECT_EQ(result.Status, Feat::CoarseAlignmentStatus::Success);
    EXPECT_GE(result.InlierCount, src.Positions.size() * 9 / 10);
    EXPECT_LT(TransformError(result.Transform, src.Positions, tgt), 1e-3);
}

TEST(PointCloudFeatures, CoarseAlignmentInsufficientCorrespondences)
{
    const std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}};
    const auto corr = GroundTruthCorrespondences(2);
    const auto result = Feat::EstimateCoarseAlignment(pts, pts, corr);
    EXPECT_EQ(result.Status, Feat::CoarseAlignmentStatus::InsufficientCorrespondences);
}

// --- Degenerate neighborhoods ---

TEST(PointCloudFeatures, HandlesCollinearAndDuplicatePoints)
{
    CloudData data;
    for (int i = 0; i < 8; ++i)
    {
        data.Positions.emplace_back(static_cast<float>(i), 0.0f, 0.0f); // collinear
        data.Normals.emplace_back(0.0f, 0.0f, 1.0f);
    }
    data.Positions.emplace_back(3.0f, 0.0f, 0.0f); // duplicate
    data.Normals.emplace_back(0.0f, 0.0f, 1.0f);

    const auto cloud = MakeCloud(data);
    // Must not crash; keypoint detection returns a (possibly empty) set rather
    // than failing on a rank-deficient neighborhood.
    const auto kp = Feat::DetectKeypoints(cloud);
    ASSERT_TRUE(kp.has_value());
    // Descriptors are still computable (normals present); rows are finite.
    const auto desc = Feat::ComputeDescriptors(cloud, {});
    ASSERT_TRUE(desc.has_value());
    for (const float v : desc->Data)
    {
        EXPECT_TRUE(std::isfinite(v));
    }
}

// --- ICP path remains reachable (not replaced by GEOM-017) ---

TEST(PointCloudFeatures, IcpRegistrationRemainsReachable)
{
    const CloudData src = MakeWavyGrid(6);
    std::vector<glm::vec3> tgt;
    const glm::vec3 t(0.05f, 0.0f, 0.0f);
    for (const glm::vec3 p : src.Positions)
    {
        tgt.push_back(p + t);
    }

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    const auto result = Geometry::Registration::AlignICP(src.Positions, tgt, {}, params);
    ASSERT_TRUE(result.has_value());
    // The recovered translation should roughly cancel the applied offset.
    EXPECT_LT(std::abs(result->Transform[3][0] + static_cast<double>(t.x)), 0.1);
}
