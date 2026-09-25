#include "Bench.PropertySmoothingSmoke.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>
import Geometry.Smoothing;
namespace Intrinsic::Bench::Geometry
{
    PropertySmoothingSmokeMetrics RunPropertySmoothingSmoke()
    {
        namespace S = ::Geometry::Smoothing;
        constexpr std::size_t n = 128;
        const double frequency = 2 * std::numbers::pi * 8 / n;
        std::vector<double> values(n);
        std::vector<S::PropertyEdge> edges;
        for (std::size_t i = 0; i < n; ++i)
        { values[i] = std::cos(frequency * i); edges.push_back({i,(i+1)%n,1}); }
        PropertySmoothingSmokeMetrics metrics{.Succeeded=true};
        const auto tick = [&] {
            for (auto method : {S::PropertyFilter::Averaging,S::PropertyFilter::SpectralHeat,S::PropertyFilter::Taubin,S::PropertyFilter::Implicit})
                for (auto laplacian : {S::PropertyLaplacian::RandomWalk,S::PropertyLaplacian::Combinatorial})
                {
                    S::PropertyFilterParams p{.Method=method,.Laplacian=laplacian};
                    const auto result = S::FilterProperty(values,1,edges,p);
                    if (!result.Success) { metrics.Succeeded=false; continue; }
                    const double lambda = 1-std::cos(frequency);
                    const double gain = method == S::PropertyFilter::SpectralHeat
                        ? std::exp(-lambda*(laplacian == S::PropertyLaplacian::RandomWalk ? 1 : 2))
                        : method == S::PropertyFilter::Implicit
                        ? 1/(1+p.TimeStep*lambda*(laplacian == S::PropertyLaplacian::RandomWalk ? 1 : 2))
                        : (1-p.Lambda*lambda)*(method == S::PropertyFilter::Taubin ? 1-p.Mu*lambda : 1);
                    for (std::size_t i=0;i<n;++i) metrics.MaxError=std::max(metrics.MaxError,std::abs(result.Values[i]-gain*values[i]));
                }
        };
        tick();
        const auto start = std::chrono::steady_clock::now();
        for (int i=0;i<8;++i) tick();
        metrics.RuntimeMilliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/8;
        metrics.Succeeded = metrics.Succeeded && metrics.MaxError <= 1e-11;
        return metrics;
    }
}
