#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

import Extrinsic.Runtime.MethodFigureExport;

namespace Runtime = Extrinsic::Runtime;

namespace
{
    [[nodiscard]] Runtime::FigureMetricBundle MakeMetricBundle()
    {
        Runtime::FigureMetricSeries rdf{};
        rdf.Name = "rdf";
        rdf.XLabel = "radius";
        rdf.YLabel = "g_r";
        rdf.XUnit = "world";
        rdf.YUnit = "unitless";
        rdf.X = {0.125, 0.25};
        rdf.Y = {0.0, 1.5};
        rdf.Summaries = {
            Runtime::NamedFigureExportValue{"cv", 0.125, "unitless"},
            Runtime::NamedFigureExportValue{"sample_count", std::uint64_t{64u}, "count"},
        };

        return Runtime::FigureMetricBundle{
            .DatasetId = "builtin.jittered_grid",
            .MethodId = "geometry.progressive_poisson.reference",
            .BackendId = "cpu.reference",
            .RunId = "run-0001",
            .Series = {rdf},
            .Summaries = {
                Runtime::NamedFigureExportValue{"coverage", 0.875, "fraction"},
            },
        };
    }

    [[nodiscard]] Runtime::FigureRunManifest MakeManifest()
    {
        return Runtime::FigureRunManifest{
            .DatasetId = "builtin.jittered_grid",
            .MethodId = "geometry.progressive_poisson.reference",
            .BackendId = "cpu.reference",
            .RunId = "run-0001",
            .EngineVersionStamp = "test-engine@abcdef0",
            .PointCount = 64u,
            .SamplerConfig = {
                Runtime::NamedFigureExportValue{"shuffle_within_levels", true, ""},
                Runtime::NamedFigureExportValue{"dimension", std::uint64_t{2u}, ""},
                Runtime::NamedFigureExportValue{"radius_alpha", 0.5, ""},
            },
            .Seeds = {
                Runtime::NamedFigureExportValue{"shuffle_seed", std::uint64_t{17u}, ""},
                Runtime::NamedFigureExportValue{"grid_origin_seed", std::uint64_t{11u}, ""},
            },
            .Artifacts = {
                Runtime::NamedFigureExportValue{"metrics_json", std::string{"metrics.json"}, "path"},
            },
        };
    }

    [[nodiscard]] std::filesystem::path TempDir(const std::string& name)
    {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("intrinsic_method_figure_export_" + name);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
        return path;
    }

    [[nodiscard]] std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream out;
        out << file.rdbuf();
        return out.str();
    }
}

TEST(MethodFigureExport, MetricBundleCsvAndJsonUseStableColumnsAndFormatting)
{
    const Runtime::FigureMetricBundle bundle = MakeMetricBundle();

    const Runtime::FigureExportTextResult csv =
        Runtime::SerializeMetricBundleCsv(bundle);
    ASSERT_TRUE(csv.Succeeded()) << csv.Diagnostic;
    EXPECT_EQ(csv.Text,
              "schema,kind,series,key,x,y,value,unit\n"
              "intrinsic.method_figure_export.metric_bundle.csv.v1,series,rdf,,"
              "1.25000000000000000e-01,0.00000000000000000e+00,,\n"
              "intrinsic.method_figure_export.metric_bundle.csv.v1,series,rdf,,"
              "2.50000000000000000e-01,1.50000000000000000e+00,,\n"
              "intrinsic.method_figure_export.metric_bundle.csv.v1,summary,rdf,cv,,,"
              "1.25000000000000000e-01,unitless\n"
              "intrinsic.method_figure_export.metric_bundle.csv.v1,summary,rdf,sample_count,,,"
              "64,count\n"
              "intrinsic.method_figure_export.metric_bundle.csv.v1,summary,bundle,coverage,,,"
              "8.75000000000000000e-01,fraction\n");

    const Runtime::FigureExportTextResult json =
        Runtime::SerializeMetricBundleJson(bundle);
    ASSERT_TRUE(json.Succeeded()) << json.Diagnostic;

    const nlohmann::json parsed = nlohmann::json::parse(json.Text, nullptr, false);
    ASSERT_FALSE(parsed.is_discarded());
    EXPECT_EQ(parsed["schema"], "intrinsic.method_figure_export.metric_bundle.json.v1");
    EXPECT_EQ(parsed["dataset_id"], "builtin.jittered_grid");
    EXPECT_EQ(parsed["series"][0]["name"], "rdf");
    EXPECT_DOUBLE_EQ(parsed["series"][0]["samples"][1]["x"].get<double>(), 0.25);
    EXPECT_DOUBLE_EQ(parsed["series"][0]["samples"][1]["y"].get<double>(), 1.5);
    EXPECT_EQ(parsed["series"][0]["summaries"][0]["key"], "cv");
    EXPECT_EQ(parsed["summaries"][0]["key"], "coverage");
}

TEST(MethodFigureExport, WritersCommitMetricFilesAtomically)
{
    const std::filesystem::path dir = TempDir("metric_files");
    const Runtime::FigureMetricBundle bundle = MakeMetricBundle();

    const Runtime::FigureExportResult csv =
        Runtime::WriteMetricBundleCsv((dir / "metrics.csv").string(), bundle);
    const Runtime::FigureExportResult json =
        Runtime::WriteMetricBundleJson((dir / "metrics.json").string(), bundle);

    ASSERT_TRUE(csv.Succeeded()) << csv.Diagnostic;
    ASSERT_TRUE(json.Succeeded()) << json.Diagnostic;
    EXPECT_GT(csv.BytesWritten, 0u);
    EXPECT_GT(json.BytesWritten, 0u);
    EXPECT_FALSE(std::filesystem::exists(dir / "metrics.csv.tmp"));
    EXPECT_FALSE(std::filesystem::exists(dir / "metrics.json.tmp"));
    EXPECT_EQ(ReadText(dir / "metrics.csv"), Runtime::SerializeMetricBundleCsv(bundle).Text);

    const nlohmann::json parsed = nlohmann::json::parse(ReadText(dir / "metrics.json"), nullptr, false);
    ASSERT_FALSE(parsed.is_discarded());
    EXPECT_EQ(parsed["run_id"], "run-0001");
}

TEST(MethodFigureExport, RunManifestSortsKeysAndIsByteIdentical)
{
    Runtime::FigureRunManifest manifest = MakeManifest();
    Runtime::FigureRunManifest reordered = manifest;
    std::reverse(reordered.SamplerConfig.begin(), reordered.SamplerConfig.end());
    std::reverse(reordered.Seeds.begin(), reordered.Seeds.end());

    const Runtime::FigureExportTextResult first =
        Runtime::SerializeRunManifestJson(manifest);
    const Runtime::FigureExportTextResult second =
        Runtime::SerializeRunManifestJson(reordered);
    ASSERT_TRUE(first.Succeeded()) << first.Diagnostic;
    ASSERT_TRUE(second.Succeeded()) << second.Diagnostic;
    EXPECT_EQ(first.Text, second.Text);

    const nlohmann::json parsed = nlohmann::json::parse(first.Text, nullptr, false);
    ASSERT_FALSE(parsed.is_discarded());
    EXPECT_EQ(parsed["schema"], "intrinsic.method_figure_export.run_manifest.json.v1");
    EXPECT_EQ(parsed["point_count"], 64u);
    EXPECT_EQ(parsed["sampler_config"]["dimension"], 2u);
    EXPECT_DOUBLE_EQ(parsed["sampler_config"]["radius_alpha"].get<double>(), 0.5);
    EXPECT_EQ(parsed["sampler_config"]["shuffle_within_levels"], true);
    EXPECT_EQ(parsed["seeds"]["grid_origin_seed"], 11u);
}

TEST(MethodFigureExport, PointSetCsvAndPlyUseStablePointColumns)
{
    const std::vector<Runtime::FigurePointRecord> points{
        Runtime::FigurePointRecord{
            .Position = {1.0, 2.0, 0.0},
            .Level = 3u,
            .Phase = 1u,
            .SplatRadius = 0.03125,
        },
        Runtime::FigurePointRecord{
            .Position = {-1.0, 0.5, 4.0},
            .Level = 4u,
            .Phase = 2u,
            .SplatRadius = 0.0625,
        },
    };

    const Runtime::FigureExportTextResult csv = Runtime::SerializePointSetCsv(points);
    ASSERT_TRUE(csv.Succeeded()) << csv.Diagnostic;
    EXPECT_EQ(csv.Text,
              "schema,x,y,z,level,phase,splat_radius\n"
              "intrinsic.method_figure_export.point_set.csv.v1,"
              "1.00000000000000000e+00,2.00000000000000000e+00,0.00000000000000000e+00,"
              "3,1,3.12500000000000000e-02\n"
              "intrinsic.method_figure_export.point_set.csv.v1,"
              "-1.00000000000000000e+00,5.00000000000000000e-01,4.00000000000000000e+00,"
              "4,2,6.25000000000000000e-02\n");

    const Runtime::FigureExportTextResult ply = Runtime::SerializePointSetPly(points);
    ASSERT_TRUE(ply.Succeeded()) << ply.Diagnostic;
    EXPECT_NE(ply.Text.find("comment schema intrinsic.method_figure_export.point_set.ply.v1"),
              std::string::npos);
    EXPECT_NE(ply.Text.find("property uint level"), std::string::npos);
    EXPECT_NE(ply.Text.find("-1.00000000000000000e+00 5.00000000000000000e-01"),
              std::string::npos);
}

TEST(MethodFigureExport, InvalidInputsAndUnwritableTargetsFailClosed)
{
    Runtime::FigureMetricBundle bundle = MakeMetricBundle();
    bundle.Series.front().Y.pop_back();
    const Runtime::FigureExportTextResult mismatch =
        Runtime::SerializeMetricBundleCsv(bundle);
    EXPECT_FALSE(mismatch.Succeeded());
    EXPECT_EQ(mismatch.Status, Runtime::FigureExportStatus::SizeMismatch);

    Runtime::FigureRunManifest manifest = MakeManifest();
    manifest.SamplerConfig.push_back(
        Runtime::NamedFigureExportValue{"radius_alpha", 0.75, ""});
    const Runtime::FigureExportTextResult duplicate =
        Runtime::SerializeRunManifestJson(manifest);
    EXPECT_FALSE(duplicate.Succeeded());
    EXPECT_EQ(duplicate.Status, Runtime::FigureExportStatus::InvalidArgument);

    const std::filesystem::path dir = TempDir("unwritable_target");
    const Runtime::FigureExportResult targetIsDirectory =
        Runtime::WriteMetricBundleCsv(dir.string(), MakeMetricBundle());
    EXPECT_FALSE(targetIsDirectory.Succeeded());
    EXPECT_EQ(targetIsDirectory.Status, Runtime::FigureExportStatus::InvalidPath);
    EXPECT_FALSE(std::filesystem::exists(dir.string() + ".tmp"));
}
