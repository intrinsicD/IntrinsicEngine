#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Geometry.PointCloud;
import Geometry.PointCloud.Utils;
namespace PC = Geometry::PointCloud;
namespace
{
    std::vector<std::uint32_t> Candidates(const std::vector<glm::vec3>& p, std::size_t k)
    {
        std::vector<std::uint32_t> result, ids(p.size());
        for (const auto point : p)
        {
            std::iota(ids.begin(), ids.end(), 0u);
            std::ranges::sort(ids, [&](auto a, auto b) {
                auto x=point-p[a], y=point-p[b];
                auto da=glm::dot(x,x), db=glm::dot(y,y);
                return da==db ? a<b : da<db;
            });
            const auto width=std::min(p.size()-1,std::max(k,std::size_t{1}))+1;
            result.insert(result.end(),ids.begin(),ids.begin()+width);
        }
        return result;
    }
}
TEST(PointSpacing, AnalyticRadiiAndNearestSpacingRemainDistinct)
{
    const std::vector<glm::vec3> p{{0,0,0},{2,0,0},{5,0,0}};
    const auto r=PC::EstimateRadii(p,{.KNeighbors=2,.ScaleFactor=2});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->Radii,(std::vector<float>{7,5,8}));
    EXPECT_FLOAT_EQ(r->AverageRadius,20.f/3);
    EXPECT_FLOAT_EQ(r->MinRadius,5); EXPECT_FLOAT_EQ(r->MaxRadius,8);
    EXPECT_FLOAT_EQ(r->Statistics.AverageSpacing,7.f/3);
    EXPECT_FLOAT_EQ(r->Statistics.MinSpacing,2); EXPECT_FLOAT_EQ(r->Statistics.MaxSpacing,3);
    EXPECT_FLOAT_EQ(r->Statistics.Centroid.x,7.f/3);
    EXPECT_FLOAT_EQ(r->Statistics.BoundingBoxDiagonal,5);
    const auto stats=PC::ComputeStatistics(p);
    ASSERT_TRUE(stats); EXPECT_EQ(stats->AverageSpacing,r->Statistics.AverageSpacing);
}
TEST(PointSpacing, SuppliedCandidatesMatchOctreeAndCloudForTiesScalesAndExtremeK)
{
    const std::vector<glm::vec3> p{{0,0,0},{0,0,0},{0,0,0},{0,0,0},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    for (auto k : {std::size_t{0},std::size_t{1},std::size_t{5},std::numeric_limits<std::size_t>::max()})
        for (float scale : {0.f,1.f,2.f})
        {
            PC::RadiusEstimationParams params{.KNeighbors=k,.ScaleFactor=scale};
            const auto reference=PC::EstimateRadii(p,params);
            const auto supplied=PC::EstimateRadiiFromNeighbors(p,Candidates(p,k),params);
            ASSERT_TRUE(reference); ASSERT_TRUE(supplied);
            EXPECT_EQ(reference->Radii,supplied->Radii);
            EXPECT_EQ(reference->Statistics.AverageSpacing,supplied->Statistics.AverageSpacing);
            PC::Cloud cloud; for(auto point:p)(void)cloud.AddPoint(point);
            const auto wrapper=PC::EstimateRadii(cloud,params);
            ASSERT_TRUE(wrapper); EXPECT_EQ(wrapper->Radii,reference->Radii);
        }
}
TEST(PointSpacing, StatisticsPreserveDeterministicStrideSampling)
{
    const std::vector<glm::vec3> p{{0,0,0},{1,0,0},{4,0,0},{9,0,0},{16,0,0}};
    for (auto sampleCount : {0u,1u,2u,3u,99u})
    {
        PC::StatisticsParams params{.SpacingSampleCount=sampleCount};
        auto all=Candidates(p,1); std::vector<std::uint32_t> sampled;
        const auto count=sampleCount ? std::min<std::size_t>(sampleCount,p.size()) : p.size();
        for(std::size_t i=0;i<count;++i)
        {auto start=all.begin()+2*i*(p.size()/count); sampled.insert(sampled.end(),start,start+2);}
        auto reference=PC::ComputeStatistics(p,params), supplied=PC::ComputeStatisticsFromNeighbors(p,sampled,params);
        ASSERT_TRUE(reference);ASSERT_TRUE(supplied);EXPECT_EQ(reference->AverageSpacing,supplied->AverageSpacing);
        if(sampleCount==2)EXPECT_FLOAT_EQ(reference->AverageSpacing,2); // rows 0 and 2: 1 and 3.
    }
    const std::vector<glm::vec3> one{{3,4,5}};
    ASSERT_TRUE(PC::ComputeStatisticsFromNeighbors(one,{}));
    EXPECT_FLOAT_EQ(PC::ComputeStatistics(one)->AverageSpacing,0);
    EXPECT_FALSE(PC::EstimateRadii(one));
}
TEST(PointSpacing, MalformedAndUnrepresentableInputsFail)
{
    std::vector<glm::vec3> p{{0,0,0},{1,0,0},{2,0,0}};
    auto ids=Candidates(p,2);ids[1]=ids[0];
    EXPECT_FALSE(PC::EstimateRadiiFromNeighbors(p,ids,{.KNeighbors=2}));
    ids=Candidates(p,2);ids[0]=99;EXPECT_FALSE(PC::EstimateRadiiFromNeighbors(p,ids,{.KNeighbors=2}));
    ids=Candidates(p,2);std::swap(ids[0],ids[1]);EXPECT_FALSE(PC::EstimateRadiiFromNeighbors(p,ids,{.KNeighbors=2}));
    ids.pop_back();EXPECT_FALSE(PC::EstimateRadiiFromNeighbors(p,ids,{.KNeighbors=2}));
    EXPECT_FALSE(PC::EstimateRadii(p,{.ScaleFactor=-1}));
    EXPECT_FALSE(PC::EstimateRadii(p,{.ScaleFactor=std::numeric_limits<float>::max()}));
    p[0].x=std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(PC::EstimateRadii(p));EXPECT_FALSE(PC::ComputeStatistics(p));
    p[0].x=1e30f;EXPECT_FALSE(PC::EstimateRadii(p));
}
