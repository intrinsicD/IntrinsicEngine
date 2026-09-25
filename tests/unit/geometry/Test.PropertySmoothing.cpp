#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Geometry.Smoothing;
namespace S = Geometry::Smoothing;
TEST(PropertySmoothing, AnalyticHeatSpectrumAndDisconnectedConstants)
{
    const std::vector<double> values{1,0,7};
    const std::vector<S::PropertyEdge> edges{{0,1,2}};
    for (auto laplacian : {S::PropertyLaplacian::RandomWalk, S::PropertyLaplacian::Combinatorial})
    {
        S::PropertyFilterParams p{.Method=S::PropertyFilter::SpectralHeat, .Laplacian=laplacian, .HeatTime=0.7};
        const auto result = S::FilterProperty(values,1,edges,p);
        ASSERT_TRUE(result.Success) << result.Diagnostic;
        const double factor = std::exp(-0.7 * (laplacian == S::PropertyLaplacian::RandomWalk ? 2 : 4));
        EXPECT_NEAR(result.Values[0],(1+factor)/2,1e-12);
        EXPECT_NEAR(result.Values[1],(1-factor)/2,1e-12);
        EXPECT_EQ(result.Values[2],7);
    }
}
TEST(PropertySmoothing, AveragingIsSimultaneousAcrossAllChannels)
{
    const std::vector<double> values{0,2,4,6, 2,4,6,8};
    const std::vector<S::PropertyEdge> edges{{0,1,1}};
    const auto result = S::FilterProperty(values,4,edges,{});
    ASSERT_TRUE(result.Success);
    EXPECT_EQ(result.Values, (std::vector<double>{1,3,5,7,1,3,5,7}));
}
TEST(PropertySmoothing, TaubinPolynomialAndBilateralEdgePreservation)
{
    const std::vector<double> values{0,10};
    const std::vector<S::PropertyEdge> edges{{0,1,1}};
    S::PropertyFilterParams p{.Method=S::PropertyFilter::Taubin, .Lambda=0.25, .Mu=-0.3};
    auto result = S::FilterProperty(values,1,edges,p);
    ASSERT_TRUE(result.Success);
    EXPECT_NEAR(result.Values[0],1,1e-12);
    EXPECT_NEAR(result.Values[1],9,1e-12);
    p.Method = S::PropertyFilter::Bilateral;
    p.RangeSigma = 0.1;
    result = S::FilterProperty(values,1,edges,p);
    ASSERT_TRUE(result.Success);
    EXPECT_EQ(result.Values,values);
}
TEST(PropertySmoothing, NeighborhoodRetainsCoincidentPeersAndSymmetrizes)
{
    const std::vector<glm::vec3> points{{0,0,0},{0,0,0},{2,0,0}};
    const auto edges = S::BuildPropertyNeighborhood(points,1,S::PropertyWeight::Uniform,1);
    ASSERT_TRUE(edges);
    ASSERT_EQ(edges->size(),2u);
    EXPECT_EQ((*edges)[0].A,0u); EXPECT_EQ((*edges)[0].B,1u);
    EXPECT_EQ((*edges)[1].A,0u); EXPECT_EQ((*edges)[1].B,2u);
}
TEST(PropertySmoothing, NeighborhoodMergesMutualPairsInPairOrder)
{
    const std::vector<glm::vec3> points{{0,0,0},{1,0,0},{3,0,0},{6,0,0}};
    const auto edges = S::BuildPropertyNeighborhood(points,2,S::PropertyWeight::InverseDistance,1);
    ASSERT_TRUE(edges);
    // Union of 2-NN: {0,1},{0,2},{1,2},{1,3},{2,3}; every mutual pair appears once.
    const std::vector<std::pair<std::size_t,std::size_t>> expected{{0,1},{0,2},{1,2},{1,3},{2,3}};
    ASSERT_EQ(edges->size(),expected.size());
    for (std::size_t e=0;e<expected.size();++e)
    {
        EXPECT_EQ((*edges)[e].A,expected[e].first); EXPECT_EQ((*edges)[e].B,expected[e].second);
        EXPECT_DOUBLE_EQ((*edges)[e].Weight,1.0/(points[expected[e].second].x-points[expected[e].first].x));
    }
}
TEST(PropertySmoothing, InvalidInputsReturnNoPartialValues)
{
    const std::vector<double> values{1,std::numeric_limits<double>::quiet_NaN()};
    auto result = S::FilterProperty(values,1,{},{});
    EXPECT_FALSE(result.Success); EXPECT_TRUE(result.Values.empty());
    const std::vector<double> finite{1,2};
    const std::vector<S::PropertyEdge> invalid{{0,2,1}};
    EXPECT_FALSE(S::FilterProperty(finite,1,invalid,{}).Success);
    S::PropertyFilterParams p; p.Iterations = 0;
    EXPECT_FALSE(S::FilterProperty(finite,1,{},p).Success);
}
TEST(PropertySmoothing, SparseHeatMatchesIrregularGraphAnalyticStationaryLimit)
{
    const std::vector<double> values{1,0,0};
    const std::vector<S::PropertyEdge> edges{{0,1,1},{1,2,1}};
    for(auto laplacian : {S::PropertyLaplacian::RandomWalk,S::PropertyLaplacian::Combinatorial})
    {
        const auto result=S::FilterProperty(values,1,edges,{.Method=S::PropertyFilter::SpectralHeat,.Laplacian=laplacian,.HeatTime=100});
        ASSERT_TRUE(result.Success);
        for(auto value : result.Values) EXPECT_NEAR(value,laplacian==S::PropertyLaplacian::RandomWalk ? 0.25 : 1.0/3,1e-11);
    }
}

TEST(PropertySmoothing, ImplicitAnalyticGainAllChannelsAndLargeSteps)
{
    const std::vector<S::PropertyEdge> edges{{0,1,2}};
    for (auto laplacian : {S::PropertyLaplacian::RandomWalk,S::PropertyLaplacian::Combinatorial})
        for (double dt : {0.01,1.0,10000.0})
            for (std::size_t channels=1;channels<=4;++channels)
            {
                std::vector<double> values(3*channels,7.);
                for (std::size_t c=0;c<channels;++c) { values[c]=double(c); values[channels+c]=double(c)+2; }
                S::PropertyFilterParams p{.Method=S::PropertyFilter::Implicit,.Laplacian=laplacian,.Iterations=3};
                p.TimeStep=dt; p.SolverTolerance=1e-12;
                const auto result=S::FilterProperty(values,channels,edges,p);
                ASSERT_TRUE(result.Success) << result.Diagnostic;
                const double gain=std::pow(1+dt*(laplacian==S::PropertyLaplacian::RandomWalk?2:4),-3);
                for (std::size_t c=0;c<channels;++c)
                {
                    EXPECT_NEAR(result.Values[c],double(c)+1-gain,1e-9);
                    EXPECT_NEAR(result.Values[channels+c],double(c)+1+gain,1e-9);
                    EXPECT_EQ(result.Values[2*channels+c],7.);
                }
            }
}

TEST(PropertySmoothing, ImplicitDirichletEliminatesBoundaryInsideSolve)
{
    // Two free unknowns; solve the independent 2x2 reduced system explicitly.
    const std::vector<double> values{2,9,-3,5};
    const std::vector<S::PropertyEdge> edges{{0,1,2},{1,2,3},{2,3,4}};
    const std::vector<std::size_t> fixed{0,3};
    const std::vector<double> masses{1,2,3,4};
    for (auto laplacian : {S::PropertyLaplacian::RandomWalk,S::PropertyLaplacian::Combinatorial,S::PropertyLaplacian::LumpedMass})
    {
        S::PropertyFilterParams p{.Method=S::PropertyFilter::Implicit,.Laplacian=laplacian};
        p.TimeStep=0.7; p.SolverTolerance=1e-12;
        const double m1=laplacian==S::PropertyLaplacian::RandomWalk?5:laplacian==S::PropertyLaplacian::LumpedMass?2:1;
        const double m2=laplacian==S::PropertyLaplacian::RandomWalk?7:laplacian==S::PropertyLaplacian::LumpedMass?3:1;
        const double a=m1+3.5, b=-2.1, d=m2+4.9;
        const double r1=m1*9+2.8, r2=m2*(-3)+14;
        const auto result=S::FilterProperty(values,1,edges,p,fixed,masses);
        ASSERT_TRUE(result.Success) << result.Diagnostic;
        EXPECT_EQ(result.Values[0],2); EXPECT_EQ(result.Values[3],5);
        EXPECT_NEAR(result.Values[1],(d*r1-b*r2)/(a*d-b*b),1e-11);
        EXPECT_NEAR(result.Values[2],(a*r2-b*r1)/(a*d-b*b),1e-11);
    }
}

TEST(PropertySmoothing, ImplicitFailureHasNoPartialValues)
{
    const std::vector<double> values{0,7,1,9};
    const std::vector<S::PropertyEdge> edges{{0,1,1},{1,2,3},{2,3,2}};
    S::PropertyFilterParams p{.Method=S::PropertyFilter::Implicit};
    p.Solver=S::PropertySolver::ConjugateGradient;
    p.MaxSolverIterations=1; p.SolverTolerance=1e-14;
    auto result=S::FilterProperty(values,1,edges,p);
    EXPECT_FALSE(result.Success); EXPECT_TRUE(result.Values.empty());
    p.Solver=S::PropertySolver::Direct;
    result=S::FilterProperty(values,1,edges,p);
    EXPECT_TRUE(result.Success) << "direct solves ignore the CG iteration cap";
    p.Solver=S::PropertySolver(2);
    EXPECT_FALSE(S::FilterProperty(values,1,edges,p).Success);
    p.Solver=S::PropertySolver::Direct;
    p.MaxSolverIterations=2000;
    for (double dt : {0.,-1.,std::numeric_limits<double>::infinity()})
    { p.TimeStep=dt; EXPECT_FALSE(S::FilterProperty(values,1,edges,p).Success); }
    p.TimeStep=1;
    const std::vector<std::size_t> bad{0,0};
    EXPECT_FALSE(S::FilterProperty(values,1,edges,p,bad).Success);
    p.Laplacian=S::PropertyLaplacian::LumpedMass;
    EXPECT_FALSE(S::FilterProperty(values,1,edges,p).Success);
}

TEST(PropertySmoothing, FixedRowsApplyToEveryFilter)
{
    const std::vector<double> values{0,4,0};
    const std::vector<S::PropertyEdge> edges{{0,1,1},{1,2,1}};
    const std::vector<std::size_t> fixed{0,2};
    for (auto method : {S::PropertyFilter::Averaging,S::PropertyFilter::SpectralHeat,S::PropertyFilter::Taubin,S::PropertyFilter::Bilateral,S::PropertyFilter::Implicit})
    {
        S::PropertyFilterParams p{.Method=method,.Iterations=3};
        const auto result=S::FilterProperty(values,1,edges,p,fixed);
        ASSERT_TRUE(result.Success);
        EXPECT_EQ(result.Values[0],0); EXPECT_EQ(result.Values[2],0);
        EXPECT_LT(result.Values[1],4);
    }
}
TEST(PropertySmoothing, ImplicitDirectMatchesConjugateGradientReference)
{
    // Irregular 5x5 grid graph with varied weights, masses, pinned rows and an isolated row.
    constexpr std::size_t side=5, count=side*side+1, channels=3;
    std::vector<S::PropertyEdge> edges;
    for (std::size_t y=0;y<side;++y)
        for (std::size_t x=0;x<side;++x)
        {
            const std::size_t i=y*side+x;
            if (x+1<side) edges.push_back({i,i+1,0.5+double((i*7)%5)});
            if (y+1<side) edges.push_back({i,i+side,0.25+double((i*3)%4)});
        }
    std::vector<double> values(count*channels), masses(count);
    for (std::size_t i=0;i<count;++i)
    {
        masses[i]=0.3+double((i*11)%7)/3.0;
        for (std::size_t c=0;c<channels;++c) values[i*channels+c]=std::sin(double(i*channels+c));
    }
    const std::vector<std::size_t> fixed{0,side-1,side*side-1};
    for (auto laplacian : {S::PropertyLaplacian::RandomWalk,S::PropertyLaplacian::Combinatorial,S::PropertyLaplacian::LumpedMass})
        for (double dt : {0.05,3.0,500.0})
            for (bool pinned : {false,true})
            {
                SCOPED_TRACE("laplacian "+std::to_string(int(laplacian))+" dt "+std::to_string(dt)+" pinned "+std::to_string(pinned));
                S::PropertyFilterParams p{.Method=S::PropertyFilter::Implicit,.Laplacian=laplacian,.Iterations=2};
                p.TimeStep=dt; p.SolverTolerance=1e-13; p.MaxSolverIterations=10000;
                const std::span<const std::size_t> rows=pinned ? std::span<const std::size_t>(fixed) : std::span<const std::size_t>{};
                p.Solver=S::PropertySolver::Direct;
                const auto direct=S::FilterProperty(values,channels,edges,p,rows,masses);
                p.Solver=S::PropertySolver::ConjugateGradient;
                const auto reference=S::FilterProperty(values,channels,edges,p,rows,masses);
                ASSERT_TRUE(direct.Success) << direct.Diagnostic;
                ASSERT_TRUE(reference.Success) << reference.Diagnostic;
                EXPECT_EQ(direct.Diagnostic,"cpu_sparse_cholesky");
                EXPECT_EQ(reference.Diagnostic,"cpu_reference");
                EXPECT_EQ(direct.OperatorApplications,p.Iterations*channels);
                for (std::size_t j=0;j<values.size();++j) EXPECT_NEAR(direct.Values[j],reference.Values[j],1e-9) << j;
                for (auto row : rows)
                    for (std::size_t c=0;c<channels;++c) EXPECT_EQ(direct.Values[row*channels+c],values[row*channels+c]);
                for (std::size_t c=0;c<channels;++c) EXPECT_EQ(direct.Values[(count-1)*channels+c],values[(count-1)*channels+c]);
            }
}
