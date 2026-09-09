#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <random>
#include <vector>
import Geometry.PointLBVH;
import Geometry.PointCloud.Normals;
namespace LB = Geometry::PointLBVH;
TEST(PointLBVH, EmptySingletonCoincidentAndInvalidInputs)
{
    LB::Index index;
    ASSERT_TRUE(index.Build({}));
    EXPECT_EQ(index.Nearest({}).Index, LB::InvalidIndex);
    std::vector<glm::vec3> p(513, glm::vec3(2, 3, 4));
    ASSERT_TRUE(index.Build(p));
    EXPECT_EQ(index.Nodes().size(), 1025);
    EXPECT_EQ(index.Nearest({2, 3, 4}).Index, 0);
    auto r = index.Radius({2, 3, 4}, 0, 3);
    EXPECT_EQ(r.TotalCount, 513);
    EXPECT_TRUE(r.Overflowed());
    ASSERT_EQ(r.Neighbors.size(), 3);
    EXPECT_EQ(r.Neighbors[2].Index, 2);
    EXPECT_EQ(index.Radius({2, 3, 4}, 0, 0).TotalCount, 513);
    ASSERT_TRUE(index.Build(index.Points().subspan(1, 3)));
    EXPECT_EQ(index.Points().size(), 3);
    EXPECT_EQ(index.Nearest({2, 3, 4}).Index, 0);
    p.resize(1);
    ASSERT_TRUE(index.Build(p));
    EXPECT_EQ(index.Nodes().size(), 1);
    EXPECT_FLOAT_EQ(index.Nearest({2, 3, 5}).SquaredDistance, 1);
    p[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(index.Build(p));
    EXPECT_TRUE(index.Nodes().empty());
}
TEST(PointLBVH, TreeQueriesMatchIndependentExhaustiveOracle)
{
    std::mt19937 rng(917);
    std::uniform_real_distribution<float> dist(-5, 5);
    std::vector<glm::vec3> points;
    for (int i = 0; i < 1025; ++i)
        points.push_back({dist(rng), dist(rng), dist(rng)});
    points[9] = points[2];
    LB::Index index;
    ASSERT_TRUE(index.Build(points));
    for (int i = 0; i < 300; ++i)
    {
        glm::vec3 q{dist(rng), dist(rng), dist(rng)};
        const auto expected = LB::NearestReference(points, q), actual = index.Nearest(q);
        EXPECT_EQ(actual.Index, expected.Index);
        EXPECT_FLOAT_EQ(actual.SquaredDistance, expected.SquaredDistance);
        const auto er = LB::RadiusReference(points, q, 2.0f, 7), ar = index.Radius(q, 2.0f, 7);
        ASSERT_EQ(ar.Neighbors.size(), er.Neighbors.size());
        EXPECT_EQ(ar.TotalCount, er.TotalCount);
        for (std::size_t j = 0; j < er.Neighbors.size(); ++j)
            EXPECT_EQ(ar.Neighbors[j].Index, er.Neighbors[j].Index);
    }
}

TEST(PointLBVH, KNearestExcludesIdentityAndKeepsCoincidentPeers)
{
    const std::vector<glm::vec3> points{{0,0,0},{0,0,0},{1,0,0},{-1,0,0},{0,2,0}};
    LB::Index index;
    ASSERT_TRUE(index.Build(points));
    const auto actual = index.KNearest({}, 99, 0);
    ASSERT_EQ(actual.size(), 4);
    for (std::uint32_t i=0;i<4;++i) EXPECT_EQ(actual[i].Index, i+1);
    EXPECT_EQ(index.Nearest({},0).Index,1);
    EXPECT_EQ(index.Radius({},0,0,0).TotalCount,1);
    EXPECT_TRUE(index.KNearest({},0).empty());
    ASSERT_TRUE(index.Build(std::span(points).first(1)));
    EXPECT_TRUE(index.KNearest({},5,0).empty());
    EXPECT_EQ(index.Nearest({},0).Index,LB::InvalidIndex);
    EXPECT_EQ(index.Radius({},1,5,0).TotalCount,0);
    EXPECT_TRUE(index.KNearest({std::numeric_limits<float>::quiet_NaN(),0,0},5).empty());
}
TEST(PointLBVH, KNearestTreeMatchesExhaustiveSortedOracle)
{
    std::mt19937 random(381);
    std::uniform_real_distribution<float> value(-5,5);
    std::vector<glm::vec3> points;
    for (int i=0;i<513;++i) points.push_back({value(random),value(random),value(random)});
    std::fill_n(points.begin(),70,glm::vec3(0));
    LB::Index index;
    ASSERT_TRUE(index.Build(points));
    for (std::uint32_t i=0;i<80;++i)
        for (auto k : {0u,1u,2u,16u,64u,700u})
        {
            auto q = i<70 ? points[i] : glm::vec3(value(random),value(random),value(random));
            auto expected=LB::KNearestReference(points,q,k,i);
            auto actual=index.KNearest(q,k,i);
            ASSERT_EQ(actual.size(),expected.size());
            for (std::size_t j=0;j<actual.size();++j)
            {
                EXPECT_EQ(actual[j].Index,expected[j].Index);
                EXPECT_FLOAT_EQ(actual[j].SquaredDistance,expected[j].SquaredDistance);
            }
            EXPECT_EQ(index.Nearest(q,i).Index,LB::NearestReference(points,q,i).Index);
            auto radius=index.Radius(q,3,7,i), oracle=LB::RadiusReference(points,q,3,7,i);
            EXPECT_EQ(radius.TotalCount,oracle.TotalCount);
            ASSERT_EQ(radius.Neighbors.size(),oracle.Neighbors.size());
            for (std::size_t j=0;j<radius.Neighbors.size();++j)
                EXPECT_EQ(radius.Neighbors[j].Index,oracle.Neighbors[j].Index);
        }
}

TEST(PointLBVH, SuppliedNormalIndexPreservesReferenceNeighborhoodPolicy)
{
    namespace N = Geometry::PointCloud::Normals;
    std::vector<glm::vec3> points;
    for (int y=0;y<9;++y) for (int x=0;x<9;++x)
        points.push_back({float(x),float(y),.01f*x*x+.02f*y*y});
    for (int i=0;i<20;++i) points.push_back(points.front());
    LB::Index index;
    ASSERT_TRUE(index.Build(points));
    for (bool radius : {false,true})
    {
        N::Params params{.KNeighbors=15,.UseRadiusSearch=radius,.Radius=2.5f};
        const auto reference=N::Estimate(points,params), actual=N::Estimate(points,index,params);
        ASSERT_TRUE(reference); ASSERT_TRUE(actual);
        EXPECT_EQ(actual->Backend,N::NeighborhoodBackend::SuppliedPointLBVH);
        EXPECT_EQ(actual->Diagnostics.ValidNormalPointCount,reference->Diagnostics.ValidNormalPointCount);
        EXPECT_EQ(actual->Diagnostics.FallbackPointCount,reference->Diagnostics.FallbackPointCount);
        ASSERT_EQ(actual->Normals.size(),reference->Normals.size());
        for(std::size_t i=0;i<points.size();++i)
            for(int axis=0;axis<3;++axis) EXPECT_NEAR(actual->Normals[i][axis],reference->Normals[i][axis],1e-5);
    }
    points[0].x+=1;
    EXPECT_FALSE(N::Estimate(points,index));
    points.pop_back();
    EXPECT_FALSE(N::Estimate(points,index));
}
