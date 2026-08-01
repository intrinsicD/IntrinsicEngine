// METHOD-019 — frozen LOP-family optimized CPU comparison declaration.
#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kLopFamilyComparisonBenchmarkId =
        "geometry.lop_family.comparison.smoke";
    inline constexpr const char* kLopFamilyComparisonMethod =
        "geometry.lop_family.backend_comparison";
    inline constexpr const char* kLopFamilyComparisonDataset =
        "builtin.lop_family.comparison.v1";
    inline constexpr std::size_t kLopFamilyComparisonWarmupPairs = 1u;
    inline constexpr std::size_t kLopFamilyComparisonMeasuredPairs = 5u;
    inline constexpr double kLopFamilyUsefulRuntimeRatioMax = 0.80;
    inline constexpr double kLopFamilyRmsDeltaMax = 1.0e-6;
    inline constexpr double kLopFamilyLinfDeltaMax = 2.0e-6;

    struct LopFamilyStrategyComparison
    {
        std::string Strategy{};
        std::string ReferenceStatus{};
        std::string OptimizedStatus{};
        std::array<double, kLopFamilyComparisonMeasuredPairs>
            ReferenceRuntimeSamplesMilliseconds{};
        std::array<double, kLopFamilyComparisonMeasuredPairs>
            OptimizedRuntimeSamplesMilliseconds{};
        std::array<double, kLopFamilyComparisonMeasuredPairs>
            PairedRuntimeRatios{};
        double ReferenceMedianRuntimeMilliseconds{0.0};
        double OptimizedMedianRuntimeMilliseconds{0.0};
        double MedianRuntimeRatio{0.0};
        double PositionRmsDelta{0.0};
        double PositionLinfDelta{0.0};
        double NormalRmsDelta{0.0};
        double NormalLinfDelta{0.0};
        std::size_t InputPointCount{0u};
        std::size_t OutputPointCount{0u};
        bool StateMatched{false};
        bool OutputShapeMatched{false};
        bool ReferenceIdentityMatched{false};
        bool OptimizedIdentityMatched{false};
        bool OptimizedUsedFallback{false};
        bool ReferenceDeterministic{false};
        bool OptimizedDeterministic{false};
        bool ParityPassed{false};
        bool AccelerationPassed{false};
        bool Adopted{false};
    };

    struct LopFamilyComparisonMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        std::array<LopFamilyStrategyComparison, 4u> Strategies{};
        std::size_t EvaluatedStrategyCount{0u};
        std::size_t AdoptedStrategyCount{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] LopFamilyComparisonMetrics RunLopFamilyComparisonSmoke();
}
