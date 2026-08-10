// METHOD-038 — opt-in METHOD-037 baseline stage profiler.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/resource.h>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.CurvatureSegmentation;
import Geometry.Properties;

namespace
{
    namespace Segment = Geometry::CurvatureSegmentation;

    struct CohortSpec
    {
        std::string_view Token{};
        std::uint32_t Rows{0u};
        std::uint32_t Columns{0u};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
    };

    enum class SelectionMode : std::uint8_t
    {
        Fixed,
        Automatic,
    };

    struct Fixture
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<double> K1{};
        std::vector<double> K2{};
        std::vector<std::uint32_t> ExpectedFaceRegime{};
        bool Valid{true};
    };

    struct CandidateProfile
    {
        std::uint32_t ComponentCount{0u};
        std::uint32_t Iterations{0u};
        double FitMilliseconds{0.0};
        bool Selected{false};
    };

    struct VariantProfile
    {
        std::string_view Name{};
        bool Succeeded{false};
        std::size_t FaceCount{0u};
        std::size_t DualEdgeCount{0u};
        std::uint32_t SelectedComponentCount{0u};
        std::uint32_t SpatialIterations{0u};
        double MisclassifiedFaceFraction{1.0};
        Segment::CurvatureSegmentationStageTimings MedianTimings{};
        std::vector<CandidateProfile> Candidates{};
    };

    struct CohortProfile
    {
        CohortSpec Spec{};
        SelectionMode Mode{SelectionMode::Fixed};
        VariantProfile Uniform{};
        VariantProfile Anisotropic{};
        std::uint64_t PeakWorkingSetBytes{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] std::string ResolveCommit()
    {
        const char* value = std::getenv("GIT_COMMIT");
        return value != nullptr && value[0] != '\0'
            ? std::string{value}
            : std::string{"unknown"};
    }

    [[nodiscard]] std::uint64_t PeakWorkingSetBytes() noexcept
    {
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0)
            return 0u;
        // Linux reports ru_maxrss in KiB.
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
    }

    [[nodiscard]] double Median(std::vector<double> values)
    {
        if (values.empty())
            return 0.0;
        const std::size_t middle = values.size() / 2u;
        std::nth_element(
            values.begin(), values.begin() + middle, values.end());
        const double upper = values[middle];
        if ((values.size() & 1u) != 0u)
            return upper;
        const double lower = *std::max_element(
            values.begin(), values.begin() + middle);
        return 0.5 * (lower + upper);
    }

    [[nodiscard]] Fixture MakeGridFixture(
        const CohortSpec& spec,
        const bool anisotropic)
    {
        Fixture fixture{};
        const std::size_t vertexCount =
            static_cast<std::size_t>(spec.Rows + 1u) *
            static_cast<std::size_t>(spec.Columns + 1u);
        const std::size_t faceCount =
            2u * static_cast<std::size_t>(spec.Rows) *
            static_cast<std::size_t>(spec.Columns);
        fixture.Mesh.Reserve(vertexCount, 2u * faceCount, faceCount);
        fixture.K1.resize(vertexCount);
        fixture.K2.resize(vertexCount);

        std::vector<Geometry::VertexHandle> vertices;
        vertices.reserve(vertexCount);
        for (std::uint32_t row = 0u; row <= spec.Rows; ++row)
        {
            const double v = static_cast<double>(row) /
                static_cast<double>(spec.Rows);
            for (std::uint32_t column = 0u;
                 column <= spec.Columns;
                 ++column)
            {
                const double u = static_cast<double>(column) /
                    static_cast<double>(spec.Columns);
                // Both variants sample the same unit square. Squared u spacing
                // creates a nonuniform/anisotropic triangulation without
                // changing the embedded reference surface.
                const double x = anisotropic ? u * u : u;
                const Geometry::VertexHandle vertex =
                    fixture.Mesh.AddVertex(glm::vec3{
                        static_cast<float>(x),
                        static_cast<float>(v),
                        0.0f,
                    });
                vertices.push_back(vertex);
                const bool rightRegime = x >= 0.5;
                fixture.K1[vertex.Index] = rightRegime ? 3.0 : -2.0;
                fixture.K2[vertex.Index] = rightRegime ? 1.0 : -4.0;
            }
        }

        fixture.ExpectedFaceRegime.reserve(faceCount);
        const auto vertexAt = [&](const std::uint32_t row,
                                  const std::uint32_t column)
        {
            return vertices[
                static_cast<std::size_t>(row) *
                    static_cast<std::size_t>(spec.Columns + 1u) +
                column];
        };
        const auto addFace = [&](const Geometry::VertexHandle a,
                                 const Geometry::VertexHandle b,
                                 const Geometry::VertexHandle c)
        {
            const auto face = fixture.Mesh.AddTriangle(a, b, c);
            if (!face.has_value())
            {
                fixture.Valid = false;
                return;
            }
            if (fixture.ExpectedFaceRegime.size() <= face->Index)
            {
                fixture.ExpectedFaceRegime.resize(
                    static_cast<std::size_t>(face->Index) + 1u, 0u);
            }
            const double centroidX =
                (static_cast<double>(fixture.Mesh.Position(a).x) +
                 static_cast<double>(fixture.Mesh.Position(b).x) +
                 static_cast<double>(fixture.Mesh.Position(c).x)) /
                3.0;
            fixture.ExpectedFaceRegime[face->Index] =
                centroidX >= 0.5 ? 1u : 0u;
        };

        for (std::uint32_t row = 0u; row < spec.Rows; ++row)
        {
            for (std::uint32_t column = 0u;
                 column < spec.Columns;
                 ++column)
            {
                const Geometry::VertexHandle v00 = vertexAt(row, column);
                const Geometry::VertexHandle v10 =
                    vertexAt(row, column + 1u);
                const Geometry::VertexHandle v01 =
                    vertexAt(row + 1u, column);
                const Geometry::VertexHandle v11 =
                    vertexAt(row + 1u, column + 1u);
                const bool alternate =
                    ((row + column + (anisotropic ? 1u : 0u)) & 1u) != 0u;
                if (alternate)
                {
                    addFace(v00, v10, v01);
                    addFace(v10, v11, v01);
                }
                else
                {
                    addFace(v00, v10, v11);
                    addFace(v00, v11, v01);
                }
                if (!fixture.Valid)
                    return fixture;
            }
        }
        fixture.Valid &= fixture.Mesh.FaceCount() == faceCount;
        return fixture;
    }

    [[nodiscard]] double MisclassifiedFaceFraction(
        const std::vector<std::uint32_t>& labels,
        const std::vector<std::uint32_t>& expected)
    {
        if (labels.size() != expected.size() || labels.empty())
            return 1.0;
        std::size_t direct = 0u;
        std::size_t swapped = 0u;
        for (std::size_t face = 0u; face < labels.size(); ++face)
        {
            direct += labels[face] == expected[face] ? 1u : 0u;
            swapped += labels[face] == 1u - expected[face] ? 1u : 0u;
        }
        return 1.0 - static_cast<double>(std::max(direct, swapped)) /
            static_cast<double>(labels.size());
    }

    [[nodiscard]] Segment::CurvatureSegmentationParams MakeParams(
        const SelectionMode mode)
    {
        Segment::CurvatureSegmentationParams params{};
        params.SelectionMode = mode == SelectionMode::Fixed
            ? Segment::ComponentSelectionMode::FixedCount
            : Segment::ComponentSelectionMode::Automatic;
        params.FixedComponentCount = 2u;
        params.AutomaticMinComponents = 1u;
        params.AutomaticMaxComponents = 4u;
        params.AutomaticFitTolerance = 0.12;
        params.MaxSpatialIterations = 12u;
        params.MinimumRegionFaces = 1u;
        params.Seed = 17u;
        return params;
    }

    [[nodiscard]] VariantProfile RunVariant(
        const CohortSpec& spec,
        const SelectionMode mode,
        const bool anisotropic)
    {
        VariantProfile profile{};
        profile.Name = anisotropic ? "anisotropic" : "uniform";
        Fixture fixture = MakeGridFixture(spec, anisotropic);
        profile.FaceCount = fixture.Mesh.FaceCount();
        if (!fixture.Valid)
            return profile;

        const Segment::CurvatureSegmentationParams params =
            MakeParams(mode);
        for (std::uint32_t iteration = 0u;
             iteration < spec.WarmupIterations;
             ++iteration)
        {
            const auto warmup = Segment::Segment(
                fixture.Mesh, fixture.K1, fixture.K2, params);
            if (!warmup.Succeeded())
                return profile;
        }

        std::vector<double> aggregation;
        std::vector<double> fitting;
        std::vector<double> unary;
        std::vector<double> dualGraph;
        std::vector<double> spatial;
        std::vector<double> connectivity;
        std::vector<double> total;
        aggregation.reserve(spec.MeasuredIterations);
        fitting.reserve(spec.MeasuredIterations);
        unary.reserve(spec.MeasuredIterations);
        dualGraph.reserve(spec.MeasuredIterations);
        spatial.reserve(spec.MeasuredIterations);
        connectivity.reserve(spec.MeasuredIterations);
        total.reserve(spec.MeasuredIterations);

        Segment::CurvatureSegmentationResult last{};
        for (std::uint32_t iteration = 0u;
             iteration < spec.MeasuredIterations;
             ++iteration)
        {
            Segment::CurvatureSegmentationResult result = Segment::Segment(
                fixture.Mesh, fixture.K1, fixture.K2, params);
            if (!result.Succeeded())
                return profile;
            const auto& timings = result.Diagnostics.Timings;
            aggregation.push_back(
                timings.FaceAggregationAndNormalizationMilliseconds);
            fitting.push_back(timings.GmmFittingMilliseconds);
            unary.push_back(timings.UnaryConstructionMilliseconds);
            dualGraph.push_back(
                timings.DualGraphConstructionMilliseconds);
            spatial.push_back(timings.SpatialOptimizationMilliseconds);
            connectivity.push_back(
                timings.ConnectivityCleanupAndPublicationMilliseconds);
            total.push_back(timings.TotalMilliseconds);
            last = std::move(result);
        }

        profile.MedianTimings.FaceAggregationAndNormalizationMilliseconds =
            Median(std::move(aggregation));
        profile.MedianTimings.GmmFittingMilliseconds =
            Median(std::move(fitting));
        profile.MedianTimings.UnaryConstructionMilliseconds =
            Median(std::move(unary));
        profile.MedianTimings.DualGraphConstructionMilliseconds =
            Median(std::move(dualGraph));
        profile.MedianTimings.SpatialOptimizationMilliseconds =
            Median(std::move(spatial));
        profile.MedianTimings.ConnectivityCleanupAndPublicationMilliseconds =
            Median(std::move(connectivity));
        profile.MedianTimings.TotalMilliseconds = Median(std::move(total));
        profile.DualEdgeCount = last.Diagnostics.DualEdgeCount;
        profile.SelectedComponentCount =
            last.Diagnostics.SelectedComponentCount;
        profile.SpatialIterations = last.Diagnostics.SpatialIterations;
        profile.MisclassifiedFaceFraction = MisclassifiedFaceFraction(
            last.FaceComponents, fixture.ExpectedFaceRegime);
        for (const auto& candidate : last.Diagnostics.Candidates)
        {
            profile.Candidates.push_back(CandidateProfile{
                .ComponentCount = candidate.ComponentCount,
                .Iterations = candidate.Iterations,
                .FitMilliseconds = candidate.FitMilliseconds,
                .Selected = candidate.Selected,
            });
        }
        profile.Succeeded = profile.SelectedComponentCount == 2u &&
            profile.MisclassifiedFaceFraction <= 0.02;
        return profile;
    }

    [[nodiscard]] CohortProfile RunCohort(
        const CohortSpec& spec,
        const SelectionMode mode)
    {
        CohortProfile profile{};
        profile.Spec = spec;
        profile.Mode = mode;
        profile.Uniform = RunVariant(spec, mode, false);
        profile.Anisotropic = RunVariant(spec, mode, true);
        profile.PeakWorkingSetBytes = PeakWorkingSetBytes();
        profile.Succeeded =
            profile.Uniform.Succeeded && profile.Anisotropic.Succeeded;
        return profile;
    }

    [[nodiscard]] std::string BenchmarkId(const CohortProfile& profile)
    {
        return "geometry.curvature_segmentation.reference.profile." +
            std::string{profile.Spec.Token} + "." +
            (profile.Mode == SelectionMode::Fixed
                ? "fixed"
                : "automatic");
    }

    void EmitCandidates(
        std::ostringstream& out,
        const std::vector<CandidateProfile>& candidates)
    {
        out << '[';
        for (std::size_t index = 0u; index < candidates.size(); ++index)
        {
            if (index != 0u)
                out << ',';
            const CandidateProfile& candidate = candidates[index];
            out << "{\"component_count\":" << candidate.ComponentCount
                << ",\"iterations\":" << candidate.Iterations
                << ",\"fit_ms\":" << candidate.FitMilliseconds
                << ",\"selected\":"
                << (candidate.Selected ? "true" : "false") << '}';
        }
        out << ']';
    }

    void EmitVariant(
        std::ostringstream& out,
        const VariantProfile& profile)
    {
        const auto& timings = profile.MedianTimings;
        out << "{\"name\":\"" << profile.Name << "\""
            << ",\"face_count\":" << profile.FaceCount
            << ",\"dual_edge_count\":" << profile.DualEdgeCount
            << ",\"selected_component_count\":"
            << profile.SelectedComponentCount
            << ",\"spatial_iterations\":"
            << profile.SpatialIterations
            << ",\"misclassified_face_fraction\":"
            << profile.MisclassifiedFaceFraction
            << ",\"face_aggregation_ms\":"
            << timings.FaceAggregationAndNormalizationMilliseconds
            << ",\"gmm_fitting_ms\":"
            << timings.GmmFittingMilliseconds
            << ",\"unary_construction_ms\":"
            << timings.UnaryConstructionMilliseconds
            << ",\"dual_graph_construction_ms\":"
            << timings.DualGraphConstructionMilliseconds
            << ",\"spatial_optimization_ms\":"
            << timings.SpatialOptimizationMilliseconds
            << ",\"connectivity_publication_ms\":"
            << timings.ConnectivityCleanupAndPublicationMilliseconds
            << ",\"segmentation_total_ms\":"
            << timings.TotalMilliseconds << ",\"candidates\":";
        EmitCandidates(out, profile.Candidates);
        out << '}';
    }

    [[nodiscard]] std::string EmitResult(
        const CohortProfile& profile,
        const std::string& commit)
    {
        const std::string benchmarkId = BenchmarkId(profile);
        const double runtime = std::max(
            profile.Uniform.MedianTimings.TotalMilliseconds,
            profile.Anisotropic.MedianTimings.TotalMilliseconds);
        const double qualityError = std::max(
            profile.Uniform.MisclassifiedFaceFraction,
            profile.Anisotropic.MisclassifiedFaceFraction);
        const std::size_t populationCount = std::max(
            profile.Uniform.SelectedComponentCount,
            profile.Anisotropic.SelectedComponentCount);
        std::ostringstream out;
        out << std::fixed << std::setprecision(9)
            << "{\n"
            << "  \"benchmark_id\": \"" << benchmarkId << "\",\n"
            << "  \"method\": \"geometry.curvature_segmentation\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \"builtin.curvature_regime_grid_pair.v1\",\n"
            << "  \"commit\": \"" << commit << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": " << runtime << ",\n"
            << "    \"memory_peak_bytes\": "
            << profile.PeakWorkingSetBytes << ",\n"
            << "    \"quality_error_l2\": " << qualityError << ",\n"
            << "    \"population_count\": " << populationCount << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicCurvatureSegmentationProfile\",\n"
            << "    \"implementation_version\": \"cpu_reference_v1\",\n"
            << "    \"quality_error_l2_unit\": "
            << "\"misclassified_face_fraction\",\n"
            << "    \"aggregation\": \"max_of_variant_medians\",\n"
            << "    \"variants\": [";
        EmitVariant(out, profile.Uniform);
        out << ',';
        EmitVariant(out, profile.Anisotropic);
        out << "]\n"
            << "  },\n"
            << "  \"status\": \""
            << (profile.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";
        return out.str();
    }

    [[nodiscard]] bool WriteResult(
        const std::filesystem::path& outputRoot,
        const CohortProfile& profile,
        const std::string& commit)
    {
        std::error_code error;
        std::filesystem::create_directories(outputRoot, error);
        if (error)
            return false;
        const std::filesystem::path path =
            outputRoot / (BenchmarkId(profile) + ".json");
        std::ofstream output{path, std::ios::trunc};
        if (!output.is_open())
            return false;
        output << EmitResult(profile, commit);
        return output.good();
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: IntrinsicCurvatureSegmentationProfile "
                     "<output-directory>\n";
        return 2;
    }

    const char* cohortEnvironment =
        std::getenv("INTRINSIC_CURVATURE_PROFILE_COHORT");
    const std::string_view cohort =
        cohortEnvironment == nullptr ? "smoke" : cohortEnvironment;
    constexpr CohortSpec smoke{
        .Token = "10k",
        .Rows = 50u,
        .Columns = 100u,
        .WarmupIterations = 1u,
        .MeasuredIterations = 3u,
    };
    constexpr std::array<CohortSpec, 2u> heavy{{
        {
            .Token = "100k",
            .Rows = 200u,
            .Columns = 250u,
            .WarmupIterations = 1u,
            .MeasuredIterations = 3u,
        },
        {
            .Token = "1m",
            .Rows = 500u,
            .Columns = 1000u,
            .WarmupIterations = 0u,
            .MeasuredIterations = 1u,
        },
    }};

    std::vector<CohortSpec> selected;
    if (cohort == "smoke")
        selected.push_back(smoke);
    else if (cohort == "heavy")
        selected.assign(heavy.begin(), heavy.end());
    else
    {
        std::cerr << "INTRINSIC_CURVATURE_PROFILE_COHORT must be "
                     "smoke or heavy\n";
        return 2;
    }

    const std::filesystem::path outputRoot{argv[1]};
    const std::string commit = ResolveCommit();
    bool allPassed = true;
    for (const CohortSpec& spec : selected)
    {
        for (const SelectionMode mode :
             {SelectionMode::Fixed, SelectionMode::Automatic})
        {
            const CohortProfile profile = RunCohort(spec, mode);
            if (!WriteResult(outputRoot, profile, commit))
            {
                std::cerr << "failed to write " << BenchmarkId(profile)
                          << '\n';
                return 1;
            }
            std::cout << "Wrote " << BenchmarkId(profile) << '\n';
            allPassed &= profile.Succeeded;
        }
    }
    return allPassed ? 0 : 1;
}
