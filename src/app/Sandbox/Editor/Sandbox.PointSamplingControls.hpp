// Widgets of a prefixed Runtime::PointSamplingConfig block (RUNTIME-289), shared by every
// panel that chooses points. Include after Sandbox.PanelSupport.hpp in a unit that imports
// Extrinsic.Runtime.PointSamplingConfig; shows only the chosen method's own fields.
#pragma once

extern "C++"
{
namespace Extrinsic::Sandbox::Editor
{
    inline bool DrawPointSamplingControls(const char* id, const std::span<const Runtime::ConfigFieldSpec> fields,
                                          const std::string_view prefix, Runtime::PointSamplingConfig& c,
                                          const Runtime::PointSamplingConfig& d)
    {
        using M = Runtime::PointSamplingMethod;
        const auto name = [&](const char* suffix) { return std::string(prefix) + suffix; };
        const auto label = [&](const char* text) { return std::string(text) + "##" + id; };
        bool changed = DrawSpecEnumCombo(label("Method").c_str(), fields, name("method"), c.Method, d.Method);
        const auto seed = [&] { changed |= DrawSpecInputUInt(label("Seed").c_str(), fields, name("seed"), c.Seed, d.Seed); };
        switch (c.Method)
        {
        case M::Random: seed(); break;
        case M::FarthestPoint:
        case M::Tournament: break;
        case M::CoupledSieve:
            changed |= DrawSpecInputDouble(label("Eta").c_str(), fields, name("eta"), c.Eta, d.Eta, "%.3f");
            changed |= DrawSpecInputUInt(label("Batch cap").c_str(), fields, name("candidate_cap"), c.CandidateCap, d.CandidateCap);
            break;
        case M::FlatGreedy:
            changed |= DrawSpecInputDouble(label("Beta").c_str(), fields, name("beta"), c.Beta, d.Beta, "%.3f");
            changed |= DrawSpecEnumCombo(label("Batch priority").c_str(), fields, name("batch_priority"), c.BatchPriority,
                                         d.BatchPriority);
            changed |= DrawSpecEnumCombo(label("Batch order").c_str(), fields, name("batch_ordering"), c.BatchOrdering,
                                         d.BatchOrdering);
            seed();
            break;
        case M::LazyGreedy:
            changed |= DrawSpecInputDouble(label("Beta").c_str(), fields, name("beta"), c.Beta, d.Beta, "%.3f");
            DrawSpecCheckbox(label("Void density").c_str(), fields, name("void_density"), c.VoidDensity, d.VoidDensity, changed);
            seed();
            break;
        case M::ProgressivePoisson:
            changed |= DrawSpecEnumCombo(label("Cell selection").c_str(), fields, name("poisson_selection"),
                                         c.PoissonSelection, d.PoissonSelection);
            if (c.PoissonSelection == Runtime::PointSamplingPoissonSelection::Bounded)
            {
                changed |= DrawSpecInputUInt(label("Retries").c_str(), fields, name("poisson_retries"), c.PoissonRetries,
                                             d.PoissonRetries);
                changed |= DrawSpecInputUInt(label("Repair levels").c_str(), fields, name("poisson_repair_levels"),
                                             c.PoissonRepairLevels, d.PoissonRepairLevels);
            }
            if (c.PoissonSelection == Runtime::PointSamplingPoissonSelection::BestOfCandidates)
                changed |= DrawSpecInputUInt(label("Candidates").c_str(), fields, name("poisson_budget"), c.PoissonBudget,
                                             d.PoissonBudget);
            DrawSpecCheckbox(label("Random phase order").c_str(), fields, name("poisson_random_phases"),
                             c.PoissonRandomPhases, d.PoissonRandomPhases, changed);
            DrawSpecCheckbox(label("Spatially balanced").c_str(), fields, name("poisson_balanced"), c.PoissonBalanced,
                             d.PoissonBalanced, changed);
            changed |= DrawSpecInputUInt(label("Grid width").c_str(), fields, name("poisson_grid_width"), c.PoissonGridWidth,
                                         d.PoissonGridWidth);
            changed |= DrawSpecInputUInt(label("Levels").c_str(), fields, name("poisson_max_levels"), c.PoissonMaxLevels,
                                         d.PoissonMaxLevels);
            break;
        case M::SampleElimination:
            changed |= DrawSpecInputDouble(label("Weight exponent").c_str(), fields, name("elimination_alpha"),
                                           c.EliminationAlpha, d.EliminationAlpha, "%.2f");
            changed |= DrawSpecInputDouble(label("Weight radius").c_str(), fields, name("elimination_radius"),
                                           c.EliminationRadius, d.EliminationRadius, "%.4g");
            changed |= DrawSpecInputUInt(label("Manifold dimension").c_str(), fields, name("manifold_dimension"),
                                         c.ManifoldDimension, d.ManifoldDimension);
            break;
        }
        return changed;
    }
}
}
