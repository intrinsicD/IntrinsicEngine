#include <algorithm>
#include <array>
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
    std::vector<std::uint32_t> Candidates(const std::vector<glm::vec3>& points, std::size_t k)
    {
        std::vector<std::uint32_t> result, ids(points.size());
        for (auto p : points)
        {
            std::iota(ids.begin(), ids.end(), 0u);
            std::ranges::sort(ids, [&](auto a, auto b) {
                const auto da=glm::dot(p-points[a],p-points[a]), db=glm::dot(p-points[b],p-points[b]);
                return da==db ? a<b : da<db;
            });
            result.insert(result.end(), ids.begin(), ids.begin()+std::min(points.size(), std::max(k,std::size_t{2})+1));
        }
        return result;
    }
}
TEST(KernelDensity, AnalyticGaussianAndSpacingBandwidth)
{
    const std::vector<glm::vec3> p{{0,0,0},{1,0,0},{2,0,0}};
    const auto fixed=PC::EstimateKernelDensity(p,{.KNeighbors=2,.Bandwidth=1});
    ASSERT_TRUE(fixed);
    const double norm=std::pow(2*std::acos(-1.),-1.5);
    EXPECT_NEAR(fixed->Densities[0],norm*(std::exp(-.5)+std::exp(-2.))/2,1e-8);
    EXPECT_NEAR(fixed->Densities[1],norm*std::exp(-.5),1e-8);
    const auto automatic=PC::EstimateKernelDensity(p);
    ASSERT_TRUE(automatic);
    EXPECT_NEAR(automatic->UsedBandwidth,1.06*std::pow(3.,-.2),1e-7);
}
TEST(KernelDensity, ExhaustiveCandidatesMatchOctreeAndCloudWithTiesAndCoincidentPeers)
{
    const std::vector<glm::vec3> p{{0,0,0},{0,0,0},{0,0,0},{0,0,0},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    for (auto k : {0u,2u,5u,99u}) for (auto h : {0.f,.3f,2.f})
    {
        const PC::KDEParams params{.KNeighbors=k,.Bandwidth=h};
        const auto reference=PC::EstimateKernelDensity(p,params);
        const auto supplied=PC::EstimateKernelDensityFromNeighbors(p,Candidates(p,k),params);
        ASSERT_TRUE(reference);ASSERT_TRUE(supplied);
        EXPECT_EQ(reference->Densities,supplied->Densities);
        PC::Cloud cloud;for(auto point:p)(void)cloud.AddPoint(point);
        const auto wrapper=PC::EstimateKernelDensity(cloud,params);
        ASSERT_TRUE(wrapper);EXPECT_EQ(wrapper->Densities,reference->Densities);
    }
}
TEST(KernelDensity, DegenerateAndMalformedInputsFailWithoutPublication)
{
    std::vector<glm::vec3> p(4,glm::vec3(0));
    const auto coincident=PC::EstimateKernelDensity(p);
    ASSERT_TRUE(coincident);EXPECT_FLOAT_EQ(coincident->UsedBandwidth,1e-8f);
    EXPECT_TRUE(std::isfinite(coincident->MeanDensity));
    auto candidates=Candidates(p,2);candidates[1]=candidates[0];
    EXPECT_FALSE(PC::EstimateKernelDensityFromNeighbors(p,candidates,{.KNeighbors=2}));
    candidates=Candidates(p,2);candidates[0]=99;
    EXPECT_FALSE(PC::EstimateKernelDensityFromNeighbors(p,candidates,{.KNeighbors=2}));
    candidates.pop_back();EXPECT_FALSE(PC::EstimateKernelDensityFromNeighbors(p,candidates,{.KNeighbors=2}));
    EXPECT_FALSE(PC::EstimateKernelDensity(p,{.Bandwidth=-1}));
    EXPECT_FALSE(PC::EstimateKernelDensity(p,{.Bandwidth=1e-30f}));
    EXPECT_FALSE(PC::EstimateKernelDensity(p,{.Bandwidth=std::numeric_limits<float>::infinity()}));
    p[0].x=std::numeric_limits<float>::quiet_NaN();EXPECT_FALSE(PC::EstimateKernelDensity(p));
}

TEST(KernelDensity, FloatSubnormalKernelIsNormalizedInDouble)
{
    const float x=1.34e-11f,h=1e-12f;
    const std::vector<glm::vec3> p{{0,0,0},{x,0,0}};
    const auto result=PC::EstimateKernelDensity(p,{.Bandwidth=h});
    ASSERT_TRUE(result);
    const double expected=std::exp(-0.5*(double(x)*x)/(double(h)*h)) /
        (std::pow(2*std::acos(-1.),1.5)*double(h)*h*h);
    EXPECT_GT(expected,1e-5);
    EXPECT_FLOAT_EQ(result->Densities[0],float(expected));
    EXPECT_EQ(result->Densities[0],result->Densities[1]);
    EXPECT_EQ(PC::EstimateKernelDensityFromNeighbors(p,Candidates(p,2),{.Bandwidth=h})->Densities,result->Densities);
}
TEST(KernelDensity, DoubleSubnormalTailsFlushToZero)
{
    const std::vector<glm::vec3> p{{0,0,0},{1,0,0},{40,0,0}};
    for(float bandwidth : {0.f, 1.f, .2f})
    {
        const PC::KDEParams params{.KNeighbors=2,.Bandwidth=bandwidth};
        const auto reference=PC::EstimateKernelDensity(p,params);
        const auto supplied=PC::EstimateKernelDensityFromNeighbors(p,Candidates(p,2),params);
        ASSERT_TRUE(reference);ASSERT_TRUE(supplied);
        EXPECT_EQ(reference->Densities,supplied->Densities);
        for(float density:reference->Densities)EXPECT_TRUE(std::isfinite(density));
        if(bandwidth>0){
            EXPECT_EQ(reference->Densities[2],0.f);
            const double h=bandwidth;
            const double expected=std::exp(-.5/(h*h))/(2*std::pow(2*std::acos(-1.),1.5)*h*h*h);
            EXPECT_FLOAT_EQ(reference->Densities[0],float(expected));
            EXPECT_FLOAT_EQ(reference->Densities[1],float(expected));
        }
    }
    // Normal exp(-708) times the normalization would be double-subnormal.
    const std::vector<glm::vec3> product{{0,0,0},{3762,0,0}};
    const auto result=PC::EstimateKernelDensity(product,{.Bandwidth=100});
    ASSERT_TRUE(result);EXPECT_EQ(result->Densities,(std::vector<float>{0,0}));
}

TEST(KernelDensity, DoubleReferenceRejectsUnrepresentablePublishedBandwidth)
{
    const float maximum=std::numeric_limits<float>::max();
    const std::vector<glm::vec3> points{{-maximum,0,0},{maximum,0,0}};
    // Double can represent these distances, but the public bandwidth remains float.
    EXPECT_FALSE(PC::EstimateKernelDensity(points));
    EXPECT_FALSE(PC::EstimateKernelDensityFromNeighbors(points,{std::vector<std::uint32_t>{0,1,1,0}},{}));
}
