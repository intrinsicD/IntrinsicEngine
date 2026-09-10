#include <algorithm>
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
        const auto width = std::min(points.size()-1, k)+1;
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
TEST(BilateralPointFilter, SimultaneousUpdateAndFixedNormals)
{
    const std::vector<glm::vec3> points{{0,0,0},{0,0,1}}, normals(2,glm::vec3(0,0,1));
    auto once=PC::BilateralFilter(points,normals,{.KNeighbors=1,.SpatialSigma=1});
    ASSERT_TRUE(once);EXPECT_EQ(once->Positions[0],points[1]);EXPECT_EQ(once->Positions[1],points[0]);
    EXPECT_FLOAT_EQ(once->Diagnostics.AverageDisplacement,1);
    auto twice=PC::BilateralFilter(points,normals,{.KNeighbors=1,.SpatialSigma=1,.Iterations=2});
    ASSERT_TRUE(twice);EXPECT_EQ(twice->Positions,points);
    auto zero=PC::BilateralFilter(points,normals,{.Iterations=0});ASSERT_TRUE(zero);EXPECT_EQ(zero->Positions,points);
    EXPECT_EQ(zero->Diagnostics.PointsFiltered,0);
}
TEST(BilateralPointFilter, IndependentNeighborhoodsAcrossMovingPassesAndTies)
{
    const std::vector<glm::vec3> original{{0,0,0},{0,0,0},{1,0,.1f},{2,0,-.2f},{0,1,.3f},{1,1,-.1f},{2,1,.2f}};
    const std::vector<glm::vec3> normals(original.size(),glm::vec3(0,0,1));
    for(auto k:{std::size_t{0},std::size_t{1},std::size_t{3},std::numeric_limits<std::size_t>::max()})
    {
        PC::BilateralFilterParams p{.KNeighbors=k,.SpatialSigma=2,.Iterations=3};
        auto reference=PC::BilateralFilter(original,normals,p);ASSERT_TRUE(reference);
        auto moving=original;p.Iterations=1;
        for(int i=0;i<3;++i){auto step=PC::BilateralFilterStepFromNeighbors(moving,normals,Rows(moving,k),p);ASSERT_TRUE(step);moving=std::move(step->Positions);}
        EXPECT_EQ(reference->Positions,moving);
    }
}
TEST(BilateralPointFilter, PlaneCoincidentAndDegenerateNormalsStayFinite)
{
    std::vector<glm::vec3> points{{0,0,0},{1,0,0},{0,1,0}},normals(3,glm::vec3(0,0,1));
    auto plane=PC::BilateralFilter(points,normals);ASSERT_TRUE(plane);EXPECT_EQ(plane->Positions,points);
    normals[1]={};auto degenerate=PC::BilateralFilter(points,normals);ASSERT_TRUE(degenerate);
    EXPECT_EQ(degenerate->Diagnostics.DegenerateNormals,1);EXPECT_EQ(degenerate->Positions[1],points[1]);
    points.assign(3,glm::vec3(0));auto duplicates=PC::BilateralFilter(points,normals);ASSERT_TRUE(duplicates);
    EXPECT_EQ(duplicates->Positions,points);EXPECT_FLOAT_EQ(duplicates->SpatialSigmaUsed,.01f);
}
TEST(BilateralPointFilter, InvalidRowsAndParametersNeverPartiallyPublish)
{
    std::vector<glm::vec3> points{{0,0,0},{0,0,1},{1,0,1}},normals(3,glm::vec3(0,0,1));
    PC::BilateralFilterParams p{.KNeighbors=2,.SpatialSigma=1};
    for(auto kind:{0,1,2,3}){auto rows=Rows(points,2);if(kind==0)rows.pop_back();if(kind==1)rows[0]=99;if(kind==2)rows[1]=rows[0];if(kind==3)std::swap(rows[0],rows[1]);EXPECT_FALSE(PC::BilateralFilterStepFromNeighbors(points,normals,rows,p));}
    p.SpatialSigma=0;EXPECT_FALSE(PC::BilateralFilterStepFromNeighbors(points,normals,Rows(points,2),p));
    p.SpatialSigma=std::numeric_limits<float>::quiet_NaN();EXPECT_FALSE(PC::BilateralFilter(points,normals,p));
    PC::Cloud cloud;for(auto point:points)(void)cloud.AddPoint(point);cloud.EnableNormals();
    std::ranges::copy(normals,cloud.Normals().begin());cloud.Normals()[2].x=std::numeric_limits<float>::infinity();
    EXPECT_FALSE(PC::BilateralFilter(cloud));EXPECT_TRUE(std::ranges::equal(cloud.Positions(),points));
    normals.pop_back();EXPECT_FALSE(PC::BilateralFilter(points,normals));
}
