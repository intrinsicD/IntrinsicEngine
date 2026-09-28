module;
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
module Extrinsic.Runtime.PointSamplingConfig;

import Geometry.PointSampling;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace PS = Geometry::PointSampling;
        using FT = ConfigFieldType;
        static_assert(std::uint8_t(PointSamplingMethod::Tournament) == std::uint8_t(PS::Method::Tournament) &&
                      std::uint8_t(PointSamplingMethod::LazyGreedy) == std::uint8_t(PS::Method::LazyGreedy) &&
                      std::uint8_t(PointSamplingMethod::SampleElimination) == std::uint8_t(PS::Method::SampleElimination) &&
                      std::uint8_t(PointSamplingMethod::ProgressivePoisson) == std::uint8_t(PS::Method::ProgressivePoisson) &&
                      std::uint8_t(PointSamplingPriority::CoverageGain) == std::uint8_t(PS::GreedyPriority::CoverageGain) &&
                      std::uint8_t(PointSamplingPoissonSelection::FeaturePriority) ==
                          std::uint8_t(PS::PoissonCellSelection::FeaturePriority));

        constexpr std::array<std::string_view, 8> kMethodNames{
            "Random (seeded permutation)", "Farthest point (exact, hole sieve)", "Progressive Poisson disk",
            "Coupled sieve (eta-relaxed farthest point)", "Flat greedy (beta-greedy batches)",
            "Sample elimination (Yuksel)", "Lazy greedy (beta-greedy batches)", "Tournament (hierarchy baseline)"};
        constexpr std::array<std::string_view, 3> kPriorityNames{"Random", "Clearance", "Coverage gain"};
        constexpr std::array<std::string_view, 4> kSelectionNames{"Bounded (fast)", "Exhaustive (HAPDS)",
                                                                  "Best of candidates", "Feature priority"};

        struct Field
        {
            std::string_view Suffix;
            ConfigFieldSpec Spec;
        };
        // Names are filled per prefix; the spec's Name is replaced by prefix + suffix.
        const std::array kFields{
            Field{"method", {.Type = FT::Enum, .Description = "Sampling method that picks the points.", .EnumNames = kMethodNames}},
            Field{"seed", {.Type = FT::UInt, .Description = "Seed of the random permutation and the greedy priorities."}},
            Field{"eta", {.Type = FT::Float, .Description = "Coupled sieve: relaxation; 1 is exact farthest point, smaller admits larger batches.", .Min = 0, .Max = 1, .ExclusiveMin = true}},
            Field{"candidate_cap", {.Type = FT::UInt, .Description = "Coupled sieve: points per batch (1 when eta is 1).", .Min = 1, .Max = 32}},
            Field{"beta", {.Type = FT::Float, .Description = "Flat and lazy greedy: approximation; every sample keeps clearance >= largest / beta (1 is exact).", .Min = 1, .Max = 10}},
            Field{"batch_priority", {.Type = FT::Enum, .Description = "Flat greedy: which conflicting candidate wins a batch.", .EnumNames = kPriorityNames}},
            Field{"batch_ordering", {.Type = FT::Enum, .Description = "Flat greedy: order of a batch's samples.", .EnumNames = kPriorityNames}},
            Field{"void_density", {.Type = FT::Bool, .Description = "Lazy greedy: prefer candidates in emptier regions (else random)."}},
            Field{"poisson_selection", {.Type = FT::Enum, .Description = "Progressive Poisson: how a cell picks among its candidates.", .EnumNames = kSelectionNames}},
            Field{"poisson_retries", {.Type = FT::UInt, .Description = "Progressive Poisson, bounded: extra contenders per cell on the coarse levels.", .Min = 0, .Max = 8}},
            Field{"poisson_repair_levels", {.Type = FT::UInt, .Description = "Progressive Poisson, bounded: coarsest levels that use the retries.", .Min = 0, .Max = 32}},
            Field{"poisson_budget", {.Type = FT::UInt, .Description = "Progressive Poisson, best of candidates: candidates inspected per cell.", .Min = 1, .Max = 32}},
            Field{"poisson_random_phases", {.Type = FT::Bool, .Description = "Progressive Poisson: visit each level's cell phases in a seeded order."}},
            Field{"poisson_balanced", {.Type = FT::Bool, .Description = "Progressive Poisson: spatially balanced order within each level (else shuffled)."}},
            Field{"poisson_grid_width", {.Type = FT::UInt, .Description = "Progressive Poisson: cells per side at the coarsest level.", .Min = 1, .Max = 1024}},
            Field{"poisson_max_levels", {.Type = FT::UInt, .Description = "Progressive Poisson: hierarchy depth.", .Min = 1, .Max = 32}},
            Field{"elimination_alpha", {.Type = FT::Float, .Description = "Sample elimination: weight exponent.", .Min = 0, .Max = 64, .ExclusiveMin = true}},
            Field{"elimination_radius", {.Type = FT::Float, .Description = "Sample elimination: weight radius in world units; 0 derives it from the bounding box.", .Min = 0}},
            Field{"manifold_dimension", {.Type = FT::UInt, .Description = "Sample elimination: 2 for surface samples, 3 for volumes.", .Min = 2, .Max = 3}},
        };

        struct Block
        {
            std::deque<std::string> Names;
            std::vector<ConfigFieldSpec> Specs;
        };
    }

    std::span<const ConfigFieldSpec> PointSamplingFieldSpecs(const std::string_view prefix)
    {
        static std::mutex mutex;
        static std::map<std::string, Block, std::less<>> blocks;
        std::scoped_lock lock{mutex};
        auto it = blocks.find(prefix);
        if (it == blocks.end())
        {
            Block block;
            for (const Field& field : kFields)
            {
                block.Names.push_back(std::string(prefix) + std::string(field.Suffix));
                ConfigFieldSpec spec = field.Spec;
                spec.Name = block.Names.back();
                block.Specs.push_back(spec);
            }
            it = blocks.emplace(std::string(prefix), std::move(block)).first;
        }
        return it->second.Specs;
    }

    std::optional<std::string> ValidatePointSamplingConfig(const PointSamplingConfig& config)
    {
        if (config.Method == PointSamplingMethod::CoupledSieve && config.Eta == 1.0 && config.CandidateCap != 1u)
            return "The coupled sieve with eta 1 is exact farthest point and needs a candidate cap of 1.";
        if (config.Method == PointSamplingMethod::ProgressivePoisson &&
            config.PoissonSelection == PointSamplingPoissonSelection::FeaturePriority)
            return "Feature-priority Poisson sampling needs per-point scores; choose another cell selection here.";
        return std::nullopt;
    }

    std::string_view DisplayName(const PointSamplingMethod method) noexcept
    {
        const auto index = std::size_t(method);
        return index < kMethodNames.size() ? kMethodNames[index] : std::string_view{"unknown"};
    }

    Geometry::PointSampling::Params ToPointSamplingParams(const PointSamplingConfig& c, const std::span<const double> weights,
                                                          const std::span<const float> scores)
    {
        PS::PoissonSettings poisson{};
        poisson.GridWidth = c.PoissonGridWidth;
        poisson.MaxLevels = c.PoissonMaxLevels;
        poisson.Selection = PS::PoissonCellSelection(c.PoissonSelection);
        poisson.MaxCellRetries = c.PoissonRetries;
        poisson.RepairCoarseLevels = c.PoissonRepairLevels;
        poisson.CandidateBudget = c.PoissonBudget;
        poisson.RandomizePhaseOrder = c.PoissonRandomPhases;
        poisson.Ordering = c.PoissonBalanced ? PS::PoissonOrdering::SpatiallyBalanced : PS::PoissonOrdering::RandomShuffle;
        poisson.ComputeSplatRadii = false; // consumers take the order only
        return PS::Params{.Method = PS::Method(c.Method), .Seed = c.Seed, .Weights = weights, .Poisson = poisson,
                          .PriorityScores = scores, .Eta = c.Eta, .CandidateCap = c.CandidateCap, .Beta = c.Beta,
                          .BatchPriority = PS::GreedyPriority(c.BatchPriority),
                          .BatchOrdering = PS::GreedyPriority(c.BatchOrdering), .GreedySeed = c.Seed ^ 0x6d2b79f5u,
                          .LazyBatchPriority = c.VoidDensity ? PS::LazyPriority::VoidDensity : PS::LazyPriority::Random,
                          .EliminationAlpha = c.EliminationAlpha, .EliminationRadius = c.EliminationRadius,
                          .ManifoldDimension = c.ManifoldDimension};
    }
}
