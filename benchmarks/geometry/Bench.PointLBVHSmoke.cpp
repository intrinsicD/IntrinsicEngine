#include "Bench.PointLBVHSmoke.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>
#include <optional>
import Geometry.PointLBVH;
import Geometry.PointCloud.Normals;
import Geometry.PointCloud.Consolidation;
namespace Intrinsic::Bench::Geometry
{
    PointLBVHSmokeResult RunPointLBVHSmoke()
    {
        std::vector<glm::vec3> points;
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    points.push_back(glm::vec3(x, y, z));
        PointLBVHSmokeResult result;
        for (int run = -1; run < 8; ++run)
        {
            auto start = std::chrono::steady_clock::now();
            ::Geometry::PointLBVH::Index tree;
            if (!tree.Build(points))
            {
                ++result.Mismatches;
                return result;
            }
            std::vector<::Geometry::PointLBVH::Neighbor> neighbors;
            for (auto p : points)
                neighbors.push_back(tree.Nearest(p + glm::vec3(.1f, .2f, .3f)));
            if (run >= 0)
                result.RuntimeMilliseconds += std::chrono::duration<double, std::milli>(
                                                  std::chrono::steady_clock::now() - start)
                                                  .count() /
                                              8.0;
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                auto oracle = ::Geometry::PointLBVH::NearestReference(
                    points, points[i] + glm::vec3(.1f, .2f, .3f));
                result.Mismatches += neighbors[i].Index != oracle.Index;
                result.MaxDistanceError = std::max(
                    result.MaxDistanceError,
                    double(std::abs(neighbors[i].SquaredDistance - oracle.SquaredDistance)));
            }
        }
        return result;
    }
    PointLBVHKnnSmokeResult RunPointLBVHKnnSmoke()
    {
        namespace LB = ::Geometry::PointLBVH;
        namespace N = ::Geometry::PointCloud::Normals;
        std::vector<glm::vec3> points;
        for(int y=0;y<16;++y) for(int x=0;x<32;++x)
            points.push_back({float(x),float(y),.01f*x*x+.02f*y*y});
        PointLBVHKnnSmokeResult result;
        for(int run=-1;run<4;++run)
        {
            auto measure = [&](auto&& operation, double& milliseconds) {
                const auto start=std::chrono::steady_clock::now();
                operation();
                if(run>=0) milliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/4.;
            };
            LB::Index index;
            measure([&] { if(!index.Build(points)) ++result.Mismatches; },result.BuildMilliseconds);
            std::vector<std::vector<LB::Neighbor>> reference,actual;
            measure([&] { for(std::uint32_t i=0;i<points.size();++i) reference.push_back(LB::KNearestReference(points,points[i],16,i)); },result.ReferenceMilliseconds);
            measure([&] { for(std::uint32_t i=0;i<points.size();++i) actual.push_back(index.KNearest(points[i],16,i)); },result.WarmMilliseconds);
            for(std::size_t i=0;i<reference.size();++i)
            {
                if(actual[i].size()!=reference[i].size()){++result.Mismatches;continue;}
                for(std::size_t j=0;j<reference[i].size();++j)
                {
                    result.Mismatches+=actual[i][j].Index!=reference[i][j].Index;
                    result.MaxDistanceError=std::max(result.MaxDistanceError,double(std::abs(actual[i][j].SquaredDistance-reference[i][j].SquaredDistance)));
                }
            }
            std::optional<N::EstimateResult> baseline,estimate;
            measure([&] {baseline=N::Estimate(points);},result.NormalReferenceMilliseconds);
            measure([&] {estimate=N::Estimate(points,index);},result.NormalLbvhMilliseconds);
            if(!baseline || !estimate){++result.Mismatches;continue;}
            for(std::size_t i=0;i<points.size();++i) for(int axis=0;axis<3;++axis)
                result.MaxNormalError=std::max(result.MaxNormalError,double(std::abs(baseline->Normals[i][axis]-estimate->Normals[i][axis])));
        }
        return result;
    }
} // namespace Intrinsic::Bench::Geometry

namespace Intrinsic::Bench::Geometry
{
    PointLBVHSmokeResult RunLopLBVHSmoke()
    {
        namespace C = ::Geometry::PointCloud::Consolidation;
        std::vector<glm::vec3> source;
        for(int y=0;y<16;++y) for(int x=0;x<16;++x)
            source.emplace_back(x*.05f,y*.05f,.01f*std::sin(float(x*3+y*7)));
        C::Params params{.Method=C::LopStrategy{},.SupportRadius=.2,.RepulsionWeight=.2,
            .MaxIterations=3,.ConvergenceTolerance=0,.TargetPointCount=64,.Seed=257};
        const auto reference=C::Consolidate(source,params);
        ::Geometry::PointLBVH::Index index;
        PointLBVHSmokeResult result;
        if(!index.Build(source) || reference.Positions.size()!=64) {result.Mismatches=1;return result;}
        for(int run=-1;run<4;++run)
        {
            const auto start=std::chrono::steady_clock::now();
            const auto projected=C::ConsolidateLopWithIndex(source,index,params);
            if(run>=0) result.RuntimeMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/4;
            if(projected.State!=reference.State || projected.Positions.size()!=reference.Positions.size()) {++result.Mismatches;continue;}
            for(std::size_t i=0;i<reference.Positions.size();++i)
                result.MaxDistanceError=std::max(result.MaxDistanceError,double(glm::length(projected.Positions[i]-reference.Positions[i])));
            if(projected.Diagnostics.AttractionContributionCount!=reference.Diagnostics.AttractionContributionCount ||
               projected.Diagnostics.RepulsionContributionCount!=reference.Diagnostics.RepulsionContributionCount) ++result.Mismatches;
        }
        return result;
    }
}
