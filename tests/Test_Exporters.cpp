#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import Core;
import Core.IOBackend;
import Graphics;

using namespace Core::IO;
using namespace Graphics;

// =============================================================================
// Helper: create a simple triangle GeometryCpuData
// =============================================================================
static GeometryCpuData MakeTriangle()
{
    GeometryCpuData data;
    data.Topology = PrimitiveTopology::Triangles;
    data.Positions = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    };
    data.Normals = {
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };
    data.Indices = { 0, 1, 2 };
    return data;
}

// Helper: create a two-triangle quad
static GeometryCpuData MakeQuad()
{
    GeometryCpuData data;
    data.Topology = PrimitiveTopology::Triangles;
    data.Positions = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    };
    data.Normals = {
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };
    data.Indices = { 0, 1, 2, 0, 2, 3 };
    return data;
}

// Helper: create point cloud data
static GeometryCpuData MakePointCloud()
{
    GeometryCpuData data;
    data.Topology = PrimitiveTopology::Points;
    data.Positions = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };
    return data;
}

// =============================================================================
// OBJ Exporter Tests
// =============================================================================

TEST(OBJExporter, ExportTriangleProducesValidOutput)
{
    IORegistry registry;
    RegisterBuiltinExporters(registry);

    auto* exporter = registry.FindExporter(".obj");
    ASSERT_NE(exporter, nullptr);

    auto data = MakeTriangle();
    auto result = exporter->Export(data);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->size(), 0u);

    // Parse the text output
    std::string text(reinterpret_cast<const char*>(result->data()), result->size());

    // Count v lines and f lines
    int vCount = 0, vnCount = 0, fCount = 0;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.size() >= 2 && line[0] == 'v' && line[1] == ' ') ++vCount;
        else if (line.size() >= 3 && line[0] == 'v' && line[1] == 'n' && line[2] == ' ') ++vnCount;
        else if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ') ++fCount;
    }

    EXPECT_EQ(vCount, 3);
    EXPECT_EQ(vnCount, 3);
    EXPECT_EQ(fCount, 1);
}

// =============================================================================
// PLY Exporter Tests
// =============================================================================

TEST(PLYExporter, ExportBinaryRoundTrip)
{
    IORegistry registry;
    RegisterBuiltinLoaders(registry);
    RegisterBuiltinExporters(registry);

    auto* exporter = registry.FindExporter(".ply");
    ASSERT_NE(exporter, nullptr);

    auto original = MakeTriangle();
    auto exported = exporter->Export(original, ExportOptions{.Binary = true});
    ASSERT_TRUE(exported.has_value());

    // Re-import the exported bytes
    auto* loader = registry.FindLoader(".ply");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx;
    ctx.SourcePath = "test.ply";
    auto imported = loader->Load(*exported, ctx);
    ASSERT_TRUE(imported.has_value());

    auto& meshData = std::get<MeshImportData>(*imported);
    ASSERT_EQ(meshData.Meshes.size(), 1u);
    EXPECT_EQ(meshData.Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshData.Meshes[0].Indices.size(), 3u);

    // Compare positions (should be exact for binary float round-trip)
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ(meshData.Meshes[0].Positions[i].x, original.Positions[i].x);
        EXPECT_FLOAT_EQ(meshData.Meshes[0].Positions[i].y, original.Positions[i].y);
        EXPECT_FLOAT_EQ(meshData.Meshes[0].Positions[i].z, original.Positions[i].z);
    }
}

TEST(PLYExporter, ExportAsciiRoundTrip)
{
    IORegistry registry;
    RegisterBuiltinLoaders(registry);
    RegisterBuiltinExporters(registry);

    auto* exporter = registry.FindExporter(".ply");
    ASSERT_NE(exporter, nullptr);

    auto original = MakeTriangle();
    auto exported = exporter->Export(original, ExportOptions{.Binary = false});
    ASSERT_TRUE(exported.has_value());

    auto* loader = registry.FindLoader(".ply");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx;
    ctx.SourcePath = "test.ply";
    auto imported = loader->Load(*exported, ctx);
    ASSERT_TRUE(imported.has_value());

    auto& meshData = std::get<MeshImportData>(*imported);
    ASSERT_EQ(meshData.Meshes.size(), 1u);
    EXPECT_EQ(meshData.Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshData.Meshes[0].Indices.size(), 3u);

    // ASCII round-trip may have epsilon differences
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_NEAR(meshData.Meshes[0].Positions[i].x, original.Positions[i].x, 1e-4f);
        EXPECT_NEAR(meshData.Meshes[0].Positions[i].y, original.Positions[i].y, 1e-4f);
        EXPECT_NEAR(meshData.Meshes[0].Positions[i].z, original.Positions[i].z, 1e-4f);
    }
}

TEST(PLYExporter, PointCloudNoFaceElement)
{
    IORegistry registry;
    RegisterBuiltinExporters(registry);

    auto* exporter = registry.FindExporter(".ply");
    ASSERT_NE(exporter, nullptr);

    auto data = MakePointCloud();
    auto result = exporter->Export(data, ExportOptions{.Binary = false});
    ASSERT_TRUE(result.has_value());

    std::string text(reinterpret_cast<const char*>(result->data()), result->size());
    EXPECT_TRUE(text.find("element vertex 4") != std::string::npos);
    EXPECT_TRUE(text.find("element face") == std::string::npos);
}

TEST(PLYExporter, ExportWithNormalsPreserved)
{
    IORegistry registry;
    RegisterBuiltinLoaders(registry);
    RegisterBuiltinExporters(registry);

    auto* exporter = registry.FindExporter(".ply");
    ASSERT_NE(exporter, nullptr);

    auto original = MakeTriangle();
    auto exported = exporter->Export(original, ExportOptions{.Binary = true});
    ASSERT_TRUE(exported.has_value());

    auto* loader = registry.FindLoader(".ply");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx;
    ctx.SourcePath = "test.ply";
    auto imported = loader->Load(*exported, ctx);
    ASSERT_TRUE(imported.has_value());

    auto& meshData = std::get<MeshImportData>(*imported);
    ASSERT_EQ(meshData.Meshes.size(), 1u);
    EXPECT_EQ(meshData.Meshes[0].Normals.size(), 3u);

    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ(meshData.Meshes[0].Normals[i].x, original.Normals[i].x);
        EXPECT_FLOAT_EQ(meshData.Meshes[0].Normals[i].y, original.Normals[i].y);
        EXPECT_FLOAT_EQ(meshData.Meshes[0].Normals[i].z, original.Normals[i].z);
    }
}

// =============================================================================
// STL Exporter Tests
// =============================================================================

TEST(STLExporter, ExportBinaryCorrectSize)
{
    IORegistry registry;
    RegisterBuiltinExporters(registry);

    auto* exporter = registry.FindExporter(".stl");
    ASSERT_NE(exporter, nullptr);

    auto data = MakeQuad(); // 2 triangles
    auto result = exporter->Export(data, ExportOptions{.Binary = true});
    ASSERT_TRUE(result.has_value());

    // Binary STL: 80 header + 4 count + 50 * 2 triangles = 184
    EXPECT_EQ(result->size(), 184u);

    // Verify triangle count in header
    uint32_t triCount = 0;
    std::memcpy(&triCount, result->data() + 80, sizeof(uint32_t));
    EXPECT_EQ(triCount, 2u);
}

TEST(STLExporter, NonTriangleTopologyReturnsError)
{
    IORegistry registry;
    RegisterBuiltinExporters(registry);

    auto* exporter = registry.FindExporter(".stl");
    ASSERT_NE(exporter, nullptr);

    auto data = MakePointCloud();
    auto result = exporter->Export(data);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AssetError::InvalidData);
}

// =============================================================================
// Registry Tests
// =============================================================================

TEST(IORegistryExport, FindExporterByExtension)
{
    IORegistry registry;
    RegisterBuiltinExporters(registry);

    EXPECT_NE(registry.FindExporter(".obj"), nullptr);
    EXPECT_NE(registry.FindExporter(".ply"), nullptr);
    EXPECT_NE(registry.FindExporter(".stl"), nullptr);
    EXPECT_EQ(registry.FindExporter(".unknown"), nullptr);
}

TEST(IORegistryExport, RegisterBuiltinExportersPopulatesAll)
{
    IORegistry registry;
    RegisterBuiltinExporters(registry);

    EXPECT_TRUE(registry.CanExport(".obj"));
    EXPECT_TRUE(registry.CanExport(".ply"));
    EXPECT_TRUE(registry.CanExport(".stl"));
    EXPECT_FALSE(registry.CanExport(".xyz")); // no XYZ exporter

    auto extensions = registry.GetSupportedExportExtensions();
    EXPECT_EQ(extensions.size(), 3u);
}

TEST(IORegistryExport, ExportViaRegistryConvenienceMethod)
{
    IORegistry registry;
    RegisterBuiltinLoaders(registry);
    RegisterBuiltinExporters(registry);

    FileIOBackend backend;
    auto data = MakeTriangle();

    // Export to temp file
    std::string tmpPath = "/tmp/intrinsic_test_export.ply";
    auto exportResult = registry.Export(tmpPath, backend, data, ExportOptions{.Binary = true});
    ASSERT_TRUE(exportResult.has_value()) << "Export failed";

    // Import it back
    auto importResult = registry.Import(tmpPath, backend);
    ASSERT_TRUE(importResult.has_value()) << "Re-import failed";

    auto& meshData = std::get<MeshImportData>(*importResult);
    ASSERT_EQ(meshData.Meshes.size(), 1u);
    EXPECT_EQ(meshData.Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshData.Meshes[0].Indices.size(), 3u);

    // Clean up
    std::filesystem::remove(tmpPath);
}
