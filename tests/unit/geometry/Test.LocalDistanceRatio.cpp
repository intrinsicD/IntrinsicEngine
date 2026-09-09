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
    std::vector<std::uint32_t> Rows(const std::vector<glm::vec3>& points, std::size_t k)
    {
        std::vector<std::uint32_t> rows, ids(points.size());
        const auto width = std::min(points.size()-1, std::max(k,std::size_t{2}))+1;
        for (auto point : points)
        {
            std::iota(ids.begin(),ids.end(),0u);
            std::ranges::sort(ids,[&](auto a,auto b) {
                auto x=points[a]-point,y=points[b]-point;
                const auto da=glm::dot(x,x),db=glm::dot(y,y);
                return da==db ? a<b : da<db;
            });
            rows.insert(rows.end(),ids.begin(),ids.begin()+width);
        }
        return rows;
    }
}
TEST(LocalDistanceRatio, AnalyticRatiosAndStrictThreshold)
{
    const std::vector<glm::vec3> points{{0,0,0},{2,0,0},{5,0,0}};
    const auto result=PC::EstimateOutlierProbability(points,{.KNeighbors=2,.ScoreThreshold=1});
    ASSERT_TRUE(result); ASSERT_EQ(result->Scores.size(),3);
    EXPECT_FLOAT_EQ(result->Scores[0],14.f/13);
    EXPECT_FLOAT_EQ(result->Scores[1],2.f/3);
    EXPECT_FLOAT_EQ(result->Scores[2],4.f/3);
    EXPECT_EQ(result->OutlierCount,2);
    const auto boundary=PC::EstimateOutlierProbability(points,{.KNeighbors=2,.ScoreThreshold=4.f/3});
    ASSERT_TRUE(boundary);EXPECT_EQ(boundary->OutlierCount,0);
    EXPECT_FLOAT_EQ(result->MeanScore,(14.f/13+2.f/3+4.f/3)/3);
}
TEST(LocalDistanceRatio, SuppliedRowsPreserveCandidateTiesAndExtremeK)
{
    std::vector<glm::vec3> points{{0,0,0},{0,0,0},{0,0,0},{0,0,0},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    for(auto k:{std::size_t{0},std::size_t{1},std::size_t{2},std::size_t{5},std::numeric_limits<std::size_t>::max()})
    {
        const PC::OutlierEstimationParams params{.KNeighbors=k};
        auto reference=PC::EstimateOutlierProbability(points,params);
        auto supplied=PC::EstimateOutlierProbabilityFromNeighbors(points,Rows(points,k),params);
        ASSERT_TRUE(reference);ASSERT_TRUE(supplied);EXPECT_EQ(reference->Scores,supplied->Scores);
        EXPECT_EQ(reference->OutlierCount,supplied->OutlierCount);
        PC::Cloud cloud;for(auto point:points)(void)cloud.AddPoint(point);
        auto wrapped=PC::EstimateOutlierProbability(cloud,params);ASSERT_TRUE(wrapped);
        EXPECT_EQ(wrapped->Scores,reference->Scores);
        EXPECT_EQ(cloud.GetVertexProperty<float>("p:outlier_score").Vector(),reference->Scores);
    }
    points.assign(8,glm::vec3(0));auto zero=PC::EstimateOutlierProbability(points);ASSERT_TRUE(zero);
    EXPECT_EQ(zero->Scores,std::vector<float>(8,0));
}
TEST(LocalDistanceRatio, MalformedAndNonfiniteInputsRejectWithoutPublication)
{
    std::vector<glm::vec3> points{{0,0,0},{1,0,0},{2,0,0}};
    for(auto kind:{0,1,2,3})
    {
        auto rows=Rows(points,2);
        if(kind==0)rows.pop_back();if(kind==1)rows[0]=99;
        if(kind==2)rows[1]=rows[0];if(kind==3)std::swap(rows[0],rows[1]);
        EXPECT_FALSE(PC::EstimateOutlierProbabilityFromNeighbors(points,rows,{.KNeighbors=2}));
    }
    EXPECT_FALSE(PC::EstimateOutlierProbability(points,{.ScoreThreshold=-1}));
    EXPECT_FALSE(PC::EstimateOutlierProbability(points,{.ScoreThreshold=std::numeric_limits<float>::quiet_NaN()}));
    points[0].x=1e30f;EXPECT_FALSE(PC::EstimateOutlierProbability(points));
    PC::Cloud cloud;for(auto point:points)(void)cloud.AddPoint(point);
    auto property=cloud.GetOrAddVertexProperty<float>("p:outlier_score",17);
    EXPECT_FALSE(PC::EstimateOutlierProbability(cloud));EXPECT_EQ(property.Vector(),std::vector<float>(3,17));
    points[0].x=std::numeric_limits<float>::quiet_NaN();EXPECT_FALSE(PC::EstimateOutlierProbability(points));
    points.resize(1);EXPECT_FALSE(PC::EstimateOutlierProbability(points));
}
