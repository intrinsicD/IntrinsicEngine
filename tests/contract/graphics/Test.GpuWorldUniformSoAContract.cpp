#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace
{
    [[nodiscard]] std::filesystem::path RepoRoot()
    {
        return std::filesystem::path{__FILE__}
            .parent_path()
            .parent_path()
            .parent_path()
            .parent_path();
    }

    [[nodiscard]] std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream{path};
        return {std::istreambuf_iterator<char>{stream},
                std::istreambuf_iterator<char>{}};
    }
}

TEST(GpuWorldUniformSoAContract, DormantAlternateStoragePlanningSurfaceIsAbsent)
{
    constexpr std::array<std::string_view, 6> kSources{{
        "src/graphics/renderer/Graphics.GpuWorld.cppm",
        "src/graphics/renderer/Graphics.GpuWorld.cpp",
        "src/graphics/renderer/Graphics.GeometryResidency.cppm",
        "src/graphics/renderer/Graphics.GeometryResidency.cpp",
        "src/runtime/Runtime.GeometryPlanBuilders.cppm",
        "src/runtime/Runtime.RenderExtraction.Geometry.cpp",
    }};
    constexpr std::array<std::string_view, 7> kDormantTokens{{
        "StaticInterleaved" "AoS",
        "GeometryStorage" "Hint",
        "GeometryStorage" "Lane",
        "GeometryStorage" "Plan",
        "GeometryStorage" "Promotion",
        "PlanGeometry" "Storage",
        "Storage" "Lane",
    }};

    for (const std::string_view source : kSources)
    {
        const std::string content = ReadFile(RepoRoot() / source);
        ASSERT_FALSE(content.empty()) << source;
        for (const std::string_view token : kDormantTokens)
        {
            EXPECT_EQ(content.find(token), std::string::npos)
                << source << " still names dormant token " << token;
        }
    }
}

TEST(GpuWorldUniformSoAContract, VertexFetchProbeRemainsNonAdoptionEvidence)
{
    const std::string manifest = ReadFile(
        RepoRoot() /
        "benchmarks/rendering/manifests/rendering_vertex_fetch_layout_smoke.yaml");
    const std::string benchmarkReadme =
        ReadFile(RepoRoot() / "benchmarks/rendering/README.md");

    ASSERT_FALSE(manifest.empty());
    ASSERT_FALSE(benchmarkReadme.empty());
    EXPECT_NE(manifest.find("probe_layout: interleaved_aos"),
              std::string::npos);
    EXPECT_NE(benchmarkReadme.find("adoption_claim=false"),
              std::string::npos);
}
