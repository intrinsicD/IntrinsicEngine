#include <gtest/gtest.h>
#include <algorithm>
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
    Rows Complete(std::span<const glm::vec3> p,float radius)
    {
        Rows rows;
        for(std::uint32_t i=0;i<p.size();++i)
        {
            for(std::uint32_t j=0;j<p.size();++j)
            {const auto d=p[j]-p[i];if(j!=i && glm::dot(d,d)<=radius*radius)rows.Indices.push_back(j);}
            rows.Offsets.push_back(std::uint32_t(rows.Indices.size()));
        }
        return rows;
    }
}
TEST(DescriptorAnalysis, AnalyticPlanarHistogramsAndRequestedOrder)
{
    const std::vector<glm::vec3> points{{0,0,0},{1,0,0},{0,1,0},{1,1,0}};
    const std::vector<glm::vec3> normals(4,glm::vec3{0,0,2});
    const std::array<std::uint32_t,3> queries{3,0,3};
    auto result=F::ComputeDescriptors(points,normals,queries,{.FeatureRadius=2});ASSERT_TRUE(result);
    EXPECT_EQ(result->SourceIndices,(std::vector<std::uint32_t>{3,0,3}));EXPECT_EQ(result->Dimension,33u);
    for(auto row=0u;row<result->Count;++row)for(auto bin=0u;bin<33;++bin)
        EXPECT_FLOAT_EQ(result->Row(row)[bin],bin%11==5?100.f:0.f);
    auto empty=F::ComputeDescriptors(points,normals,{}, {.FeatureRadius=.1f});ASSERT_TRUE(empty);
    for(auto value:empty->Data)EXPECT_FLOAT_EQ(value,0);
}
TEST(DescriptorAnalysis, CompleteRowsMatchReferenceWithLowestIdCapsAndAutomaticRadius)
{
    std::vector<glm::vec3> points,normals;
    for(int i=0;i<7;++i)for(int j=0;j<7;++j)
    {points.push_back({.2f*i,.2f*j,.13f*std::sin(float(i+j))});normals.push_back({.1f*i,.15f*j,1});}
    points.push_back(points[0]);normals.push_back(normals[0]);
    for(float radius:{0.f,.6f})for(auto cap:{0u,1u,8u})
    {
        F::DescriptorParams p{.FeatureRadius=radius,.MaxNeighbors=cap};
        auto scale=F::ResolveDescriptorScale(points,p);ASSERT_TRUE(scale);
        if(radius==0)EXPECT_FLOAT_EQ(scale->FeatureRadius,5*scale->MeanSpacing);
        auto rows=Complete(points,scale->FeatureRadius);
        auto reference=F::ComputeDescriptors(points,normals,{},p);ASSERT_TRUE(reference);
        auto supplied=F::ComputeDescriptorsFromNeighbors(points,normals,{},p,*scale,rows);ASSERT_TRUE(supplied);
        EXPECT_EQ(supplied->Data,reference->Data);EXPECT_EQ(supplied->SourceIndices,reference->SourceIndices);
        if(cap)
        {
            Rows prefix;
            for(std::size_t i=0;i<points.size();++i)
            {
                const auto first=rows.Offsets[i],last=rows.Offsets[i+1];
                const auto count=std::min(cap,last-first);
                prefix.Indices.insert(prefix.Indices.end(),rows.Indices.begin()+first,rows.Indices.begin()+first+count);
                prefix.Offsets.push_back(std::uint32_t(prefix.Indices.size()));
            }
            auto bounded=F::ComputeDescriptorsFromNeighbors(points,normals,{},p,*scale,prefix);ASSERT_TRUE(bounded);
            EXPECT_EQ(bounded->Data,reference->Data);
        }
        for(auto i=0u;i<reference->Count;++i)for(auto b=0u;b<3;++b)
        {double sum=0;for(auto j=0u;j<11;++j)sum+=reference->Row(i)[b*11+j];EXPECT_NEAR(sum,cap==1 && i==49?0:100,2e-5);}
    }
}
TEST(DescriptorAnalysis, RejectsInvalidNormalsScaleAndMalformedNeighborhoods)
{
    const std::vector<glm::vec3> points{{0,0,0},{1,0,0},{0,1,0}};
    const std::vector<glm::vec3> normals(3,glm::vec3{0,0,1});
    F::DescriptorParams p{.FeatureRadius=2};auto scale=F::ResolveDescriptorScale(points,p);ASSERT_TRUE(scale);
    for(auto invalid:{glm::vec3{0},glm::vec3{std::numeric_limits<float>::quiet_NaN(),0,1}})
    {auto n=normals;n[1]=invalid;EXPECT_FALSE(F::ComputeDescriptors(points,n,{},p));}
    EXPECT_FALSE(F::ComputeDescriptors(points,std::span(normals).first(2),{},p));
    const std::array<std::uint32_t,1> invalidQuery{3};EXPECT_FALSE(F::ComputeDescriptors(points,normals,invalidQuery,p));
    for(float radius:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::max(),std::numeric_limits<float>::denorm_min()})
    {auto bad=p;bad.FeatureRadius=radius;EXPECT_FALSE(F::ComputeDescriptors(points,normals,{},bad));}
    const auto original=Complete(points,2);auto rows=original;
    rows.Offsets.pop_back();EXPECT_FALSE(F::ComputeDescriptorsFromNeighbors(points,normals,{},p,*scale,rows));
    rows=original;rows.Offsets[1]=99;EXPECT_FALSE(F::ComputeDescriptorsFromNeighbors(points,normals,{},p,*scale,rows));
    rows=original;rows.Indices[0]=0;EXPECT_FALSE(F::ComputeDescriptorsFromNeighbors(points,normals,{},p,*scale,rows));
    rows=original;rows.Indices[1]=rows.Indices[0];EXPECT_FALSE(F::ComputeDescriptorsFromNeighbors(points,normals,{},p,*scale,rows));
    rows=original;rows.Indices[0]=99;EXPECT_FALSE(F::ComputeDescriptorsFromNeighbors(points,normals,{},p,*scale,rows));
    auto moved=points;moved[1].x=10;EXPECT_FALSE(F::ComputeDescriptorsFromNeighbors(moved,normals,{},p,*scale,original));
    auto badScale=*scale;badScale.FeatureRadius=1;EXPECT_FALSE(F::ComputeDescriptorsFromNeighbors(points,normals,{},p,badScale,original));
}
TEST(DescriptorAnalysis, CloudCompactsDeletedNonfiniteRowsAndRemapsQueries)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.AddPoint({0,0,0});cloud.AddPoint({1,0,0});cloud.AddPoint({0,1,0});cloud.AddPoint({2,2,2});
    cloud.EnableNormals();for(auto& normal:cloud.Normals())normal={0,0,1};
    cloud.DeletePoint(Geometry::VertexHandle{3});cloud.Positions()[3].x=std::numeric_limits<float>::quiet_NaN();
    cloud.Normals()[3]={0,0,0};const std::array<std::uint32_t,3> queries{2,0,2};
    auto result=F::ComputeDescriptors(cloud,queries,{.FeatureRadius=2});ASSERT_TRUE(result);
    EXPECT_EQ(result->SourceIndices,(std::vector<std::uint32_t>{2,0,2}));
    const std::array<std::uint32_t,1> deleted{3};EXPECT_FALSE(F::ComputeDescriptors(cloud,deleted));
    cloud.Normals()[1]={0,0,0};EXPECT_FALSE(F::ComputeDescriptors(cloud,{}));
}
