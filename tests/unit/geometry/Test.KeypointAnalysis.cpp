#include <algorithm>
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include <glm/glm.hpp>
import Geometry.PointCloud;
import Geometry.PointCloud.Features;
import Geometry.Properties;
import Geometry.SpatialQueries;
namespace
{
    namespace F=Geometry::PointCloud::Features;
    struct Rows
    {
        std::vector<std::uint32_t> Offsets{0},Indices{};
        operator Geometry::PointNeighborhoods() const {return {Offsets,Indices};}
    };
    Rows CompleteRows(std::span<const glm::vec3> p,float radius)
    {
        Rows rows;
        for(std::uint32_t i=0;i<p.size();++i)
        {
            for(std::uint32_t j=0;j<p.size();++j)
            {
                const auto d=p[j]-p[i];
                if(j!=i && glm::dot(d,d)<=radius*radius)rows.Indices.push_back(j);
            }
            rows.Offsets.push_back(std::uint32_t(rows.Indices.size()));
        }
        return rows;
    }
    const std::vector<glm::vec3> AxisSamples{{0,0,0},{3,0,0},{-3,0,0},{0,2,0},{0,-2,0},{0,0,1},{0,0,-1}};
}
TEST(KeypointAnalysis, AnalyticCovarianceAndLowestIdSuppression)
{
    F::KeypointParams p{.SalientRadius=10,.NonMaxRadius=10,.Gamma21=.9,.Gamma32=.9,.MinNeighbors=6};
    const auto result=F::AnalyzeKeypoints(AxisSamples,p);ASSERT_TRUE(result);
    EXPECT_EQ(result->Keypoints.Indices,(std::vector<std::uint32_t>{0}));
    for(std::size_t i=0;i<AxisSamples.size();++i)
    {EXPECT_NEAR(result->Saliency[i],2.f/7.f,1e-6);EXPECT_EQ(result->Mask[i],i==0?1u:0u);}
    auto flat=AxisSamples;flat[5]={1,0,0};flat[6]={-1,0,0};
    const auto plane=F::AnalyzeKeypoints(flat,p);ASSERT_TRUE(plane);
    EXPECT_EQ(plane->Keypoints.Indices,(std::vector<std::uint32_t>{0}));
    EXPECT_EQ(plane->Saliency[0],0);EXPECT_EQ(plane->Mask[0],1);
}
TEST(KeypointAnalysis, CompleteSuppliedSupportMatchesReferenceAtBothRadii)
{
    std::vector<glm::vec3> points;
    for(int i=0;i<9;++i)for(int j=0;j<9;++j)
        points.push_back({.2f*i,.2f*j,.3f*std::sin(.3f*i)*std::cos(.23f*j)});
    points.push_back(points[0]);
    for(float suppression:{.25f,.9f})
    {
        F::KeypointParams p{.SalientRadius=.7f,.NonMaxRadius=suppression};
        const auto scale=F::ResolveKeypointScale(points,p);ASSERT_TRUE(scale);
        const auto rows=CompleteRows(points,std::max(scale->SalientRadius,scale->NonMaxRadius));
        const auto reference=F::AnalyzeKeypoints(points,p);ASSERT_TRUE(reference);
        const auto indexed=F::AnalyzeKeypointsFromNeighbors(points,p,*scale,rows);ASSERT_TRUE(indexed);
        EXPECT_EQ(indexed->Keypoints.Indices,reference->Keypoints.Indices);
        EXPECT_EQ(indexed->Saliency,reference->Saliency);EXPECT_EQ(indexed->Mask,reference->Mask);
    }
    const auto automatic=F::AnalyzeKeypoints(points);ASSERT_TRUE(automatic);
    EXPECT_FLOAT_EQ(automatic->Scale.SalientRadius,6*automatic->Scale.MeanSpacing);
    EXPECT_FLOAT_EQ(automatic->Scale.NonMaxRadius,4*automatic->Scale.MeanSpacing);
}
TEST(KeypointAnalysis, RejectsMalformedSupportAndUnrepresentableInputs)
{
    F::KeypointParams p{.SalientRadius=10,.NonMaxRadius=10};
    const auto scale=F::ResolveKeypointScale(AxisSamples,p);ASSERT_TRUE(scale);
    const auto valid=CompleteRows(AxisSamples,10);
    auto rows=valid;rows.Offsets.pop_back();EXPECT_FALSE(F::AnalyzeKeypointsFromNeighbors(AxisSamples,p,*scale,rows));
    rows=valid;rows.Offsets[0]=1;EXPECT_FALSE(F::AnalyzeKeypointsFromNeighbors(AxisSamples,p,*scale,rows));
    rows=valid;rows.Offsets[1]=99;EXPECT_FALSE(F::AnalyzeKeypointsFromNeighbors(AxisSamples,p,*scale,rows));
    rows=valid;rows.Indices[0]=0;EXPECT_FALSE(F::AnalyzeKeypointsFromNeighbors(AxisSamples,p,*scale,rows));
    rows=valid;rows.Indices[1]=rows.Indices[0];EXPECT_FALSE(F::AnalyzeKeypointsFromNeighbors(AxisSamples,p,*scale,rows));
    rows=valid;rows.Indices[0]=99;EXPECT_FALSE(F::AnalyzeKeypointsFromNeighbors(AxisSamples,p,*scale,rows));
    auto moved=AxisSamples;moved[1].x=100;EXPECT_FALSE(F::AnalyzeKeypointsFromNeighbors(moved,p,*scale,valid));
    auto badScale=*scale;badScale.NonMaxRadius=9;EXPECT_FALSE(F::AnalyzeKeypointsFromNeighbors(AxisSamples,p,badScale,valid));
    auto invalid=AxisSamples;invalid[0].x=std::numeric_limits<float>::quiet_NaN();EXPECT_FALSE(F::AnalyzeKeypoints(invalid,p));
    auto bad=p;bad.MinNeighbors=std::numeric_limits<std::uint32_t>::max();EXPECT_FALSE(F::AnalyzeKeypoints(AxisSamples,bad));
    bad=p;bad.Gamma21=std::numeric_limits<double>::quiet_NaN();EXPECT_FALSE(F::AnalyzeKeypoints(AxisSamples,bad));
    bad=p;bad.SalientRadius=std::numeric_limits<float>::max();EXPECT_FALSE(F::AnalyzeKeypoints(AxisSamples,bad));
    EXPECT_FALSE(F::AnalyzeKeypoints(std::vector<glm::vec3>(7),p));
}
TEST(KeypointAnalysis, CloudSpacingUsesNearestLivePointsAndRemapsOriginalSlots)
{
    Geometry::PointCloud::Cloud sparse;
    sparse.AddPoint({0,0,0});sparse.AddPoint({10,0,0});
    for(int i=1;i<=9;++i){sparse.AddPoint({.01f*i,0,0});sparse.AddPoint({10-.01f*i,0,0});}
    for(std::uint32_t i=2;i<sparse.Positions().size();++i)sparse.DeletePoint(Geometry::VertexHandle{i});
    const auto spacing=F::EstimateSpacing(sparse);ASSERT_TRUE(spacing);EXPECT_FLOAT_EQ(*spacing,10);
    Geometry::PointCloud::Cloud cloud;cloud.AddPoint({100,100,100});
    for(auto p:AxisSamples)cloud.AddPoint(p);
    cloud.DeletePoint(Geometry::VertexHandle{0});
    const auto keypoints=F::DetectKeypoints(cloud,{.SalientRadius=10,.NonMaxRadius=10});ASSERT_TRUE(keypoints);
    EXPECT_EQ(keypoints->Indices,(std::vector<std::uint32_t>{1}));
}
