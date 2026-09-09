#include "Bench.RegistrationSpatialSmoke.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>
#include <random>
#include <vector>
import Geometry.Registration;
import Geometry.PointLBVH;
namespace Intrinsic::Bench::Geometry
{
    RegistrationSpatialSmokeResult RunRegistrationSpatialSmoke()
    {
        namespace Reg=::Geometry::Registration;
        RegistrationSpatialSmokeResult result;
        std::mt19937 random(917); std::uniform_real_distribution<float> dist(-5,5);
        std::vector<glm::vec3> target,source;
        for(int i=0;i<1025;++i){target.push_back({dist(random),dist(random),dist(random)});source.push_back(target.back()+glm::vec3(.1f,-.15f,.2f));}
        Reg::RegistrationParams params{.Variant=Reg::ICPVariant::PointToPoint,.InlierRatio=1.};
        for(int run=-1;run<8;++run)
        {
            auto start=std::chrono::steady_clock::now();
            const auto reference=Reg::AlignICP(source,target,{},params);
            const auto referenceEnd=std::chrono::steady_clock::now();
            ::Geometry::PointLBVH::Index index;
            if(!index.Build(target)){++result.Failures;return result;}
            const Reg::NearestQuery query=[&](auto queries,auto ids){
                for(std::size_t i=0;i<queries.size();++i)ids[i]=index.Nearest(queries[i]).Index;
                return true;
            };
            const auto cold=Reg::AlignICPWithQueries(source,target,{},params,query);
            const auto coldEnd=std::chrono::steady_clock::now();
            const auto warm=Reg::AlignICPWithQueries(source,target,{},params,query);
            const auto warmEnd=std::chrono::steady_clock::now();
            if(!reference||!cold||!warm){++result.Failures;return result;}
            for(int i=0;i<4;++i)for(int j=0;j<4;++j)
                result.MaxTransformError=std::max({result.MaxTransformError,
                    std::abs(reference->Transform[i][j]-cold->Transform[i][j]),
                    std::abs(reference->Transform[i][j]-warm->Transform[i][j])});
            if(run>=0)
            {
                result.ReferenceMilliseconds+=std::chrono::duration<double,std::milli>(referenceEnd-start).count()/8;
                result.ColdMilliseconds+=std::chrono::duration<double,std::milli>(coldEnd-referenceEnd).count()/8;
                result.WarmMilliseconds+=std::chrono::duration<double,std::milli>(warmEnd-coldEnd).count()/8;
            }
        }
        return result;
    }
}
