#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <cstring>
#include <vector>
#include <glm/glm.hpp>

import Core;
import Core.IOBackend;
import Graphics;
import Geometry;

using namespace Core::IO;
using namespace Graphics;

// =============================================================================
// I/O Backend Tests
// =============================================================================

TEST(FileIOBackend, ReadExistingFile)
{
    FileIOBackend backend;

    // Find any file that exists in the source tree
    std::string path = std::string(ENGINE_ROOT_DIR) + "/CMakeLists.txt";
    IORequest req;
    req.Path = path;

    auto result = backend.Read(req);
    ASSERT_TRUE(result.has_value()) << "Expected to read " << path;
    EXPECT_GT(result->Data.size(), 0u);
}

TEST(FileIOBackend, ReadNonexistentFile)
{
    FileIOBackend backend;
    IORequest req;
    req.Path = "/nonexistent/path/to/file.bin";

    auto result = backend.Read(req);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::FileNotFound);
}

TEST(FileIOBackend, ReadWithOffset)
{
    FileIOBackend backend;
    std::string path = std::string(ENGINE_ROOT_DIR) + "/CMakeLists.txt";

    // Read whole file
    IORequest reqFull;
    reqFull.Path = path;
    auto fullResult = backend.Read(reqFull);
    ASSERT_TRUE(fullResult.has_value());
    ASSERT_GT(fullResult->Data.size(), 10u);

    // Read from offset 5, size 5
    IORequest reqPartial;
    reqPartial.Path = path;
    reqPartial.Offset = 5;
    reqPartial.Size = 5;
    auto partialResult = backend.Read(reqPartial);
    ASSERT_TRUE(partialResult.has_value());
    ASSERT_EQ(partialResult->Data.size(), 5u);

    // Verify bytes match
    for (size_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ(partialResult->Data[i], fullResult->Data[i + 5]);
    }
}

TEST(FileIOBackend, ReadEmptyPath)
{
    FileIOBackend backend;
    IORequest req;
    req.Path = "";

    auto result = backend.Read(req);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidPath);
}

// =============================================================================
// AssetId Tests
// =============================================================================

TEST(AssetId, FromPath_DifferentPaths)
{
    auto id1 = AssetId::FromPath("models/Duck.glb");
    auto id2 = AssetId::FromPath("models/Bunny.obj");
    auto id3 = AssetId::FromPath("models/Duck.glb");

    EXPECT_NE(id1, id2);
    EXPECT_EQ(id1, id3);
    EXPECT_TRUE(id1.IsValid());
    EXPECT_TRUE(id2.IsValid());
}

TEST(AssetId, FromPath_Empty)
{
    auto id = AssetId::FromPath("");
    // FNV-1a of empty string is the offset basis, which is non-zero
    // but we don't require it to be invalid
    // Just verify it's deterministic
    auto id2 = AssetId::FromPath("");
    EXPECT_EQ(id, id2);
}

// =============================================================================
// IORegistry Tests
// =============================================================================

// Mock loader for testing registry mechanics
class MockLoader final : public IAssetLoader
{
public:
    explicit MockLoader(std::string_view name, std::vector<std::string_view> exts)
        : m_Name(name), m_Extensions(std::move(exts)) {}

    [[nodiscard]] std::string_view FormatName() const override { return m_Name; }
    [[nodiscard]] std::span<const std::string_view> Extensions() const override { return m_Extensions; }

    [[nodiscard]] std::expected<ImportResult, AssetError> Load(
        std::span<const std::byte> data,
        const LoadContext& /*ctx*/) override
    {
        // Return a mesh with a single vertex for each byte in the input
        GeometryCpuData mesh;
        mesh.Topology = PrimitiveTopology::Points;
        mesh.Positions.resize(data.size());
        mesh.Normals.resize(data.size());
        mesh.Aux.resize(data.size());

        MeshImportData result;
        result.Meshes.push_back(std::move(mesh));
        return ImportResult{std::move(result)};
    }

private:
    std::string m_Name;
    std::vector<std::string_view> m_Extensions;
};

TEST(IORegistry, RegisterLoaderSucceeds)
{
    IORegistry registry;
    auto loader = std::make_unique<MockLoader>("Test", std::vector<std::string_view>{".test"});
    EXPECT_TRUE(registry.RegisterLoader(std::move(loader)));
}

TEST(IORegistry, DuplicateExtensionRejected)
{
    IORegistry registry;
    auto loader1 = std::make_unique<MockLoader>("Test1", std::vector<std::string_view>{".dup"});
    auto loader2 = std::make_unique<MockLoader>("Test2", std::vector<std::string_view>{".dup"});

    EXPECT_TRUE(registry.RegisterLoader(std::move(loader1)));
    EXPECT_FALSE(registry.RegisterLoader(std::move(loader2)));
}

TEST(IORegistry, FindLoaderByExtension)
{
    IORegistry registry;
    auto loader = std::make_unique<MockLoader>("TestFmt", std::vector<std::string_view>{".abc"});
    auto* raw = loader.get();
    registry.RegisterLoader(std::move(loader));

    EXPECT_EQ(registry.FindLoader(".abc"), raw);
    EXPECT_EQ(registry.FindLoader(".ABC"), raw); // Case-insensitive
    EXPECT_EQ(registry.FindLoader(".xyz"), nullptr); // Not registered here
}

TEST(IORegistry, CanImportReturnsTrueForRegistered)
{
    IORegistry registry;
    auto loader = std::make_unique<MockLoader>("Test", std::vector<std::string_view>{".can"});
    registry.RegisterLoader(std::move(loader));

    EXPECT_TRUE(registry.CanImport(".can"));
    EXPECT_TRUE(registry.CanImport(".CAN"));
    EXPECT_FALSE(registry.CanImport(".cannot"));
}

TEST(IORegistry, ImportReturnsUnsupportedFormatForUnknownExt)
{
    IORegistry registry;
    FileIOBackend backend;

    auto result = registry.Import("/some/file.unknown", backend);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AssetError::UnsupportedFormat);
}

TEST(IORegistry, MultiExtensionLoader)
{
    IORegistry registry;
    auto loader = std::make_unique<MockLoader>("Multi", std::vector<std::string_view>{".ext1", ".ext2"});
    auto* raw = loader.get();
    registry.RegisterLoader(std::move(loader));

    EXPECT_EQ(registry.FindLoader(".ext1"), raw);
    EXPECT_EQ(registry.FindLoader(".ext2"), raw);
}

TEST(IORegistry, GetSupportedImportExtensions)
{
    IORegistry registry;
    auto loader = std::make_unique<MockLoader>("Test", std::vector<std::string_view>{".a", ".b"});
    registry.RegisterLoader(std::move(loader));

    auto exts = registry.GetSupportedImportExtensions();
    EXPECT_EQ(exts.size(), 2u);
}

TEST(IORegistry, RegisterBuiltinLoadersPopulatesAll)
{
    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    // Built-in mesh/point-cloud formats and point-cloud aliases should be importable.
    EXPECT_TRUE(registry.CanImport(".obj"));
    EXPECT_TRUE(registry.CanImport(".ply"));
    EXPECT_TRUE(registry.CanImport(".xyz"));
    EXPECT_TRUE(registry.CanImport(".pts"));
    EXPECT_TRUE(registry.CanImport(".xyzrgb"));
    EXPECT_TRUE(registry.CanImport(".txt"));
    EXPECT_TRUE(registry.CanImport(".pcd"));
    EXPECT_TRUE(registry.CanImport(".tgf"));
    EXPECT_TRUE(registry.CanImport(".gltf"));
    EXPECT_TRUE(registry.CanImport(".glb"));
    EXPECT_TRUE(registry.CanImport(".stl"));
    EXPECT_TRUE(registry.CanImport(".off"));
}

// =============================================================================
// Loader Integration Tests (in-memory bytes — proves I/O-agnostic)
// =============================================================================

TEST(OBJLoader, ParseCubeFromBytes)
{
    // Minimal OBJ: a triangle
    const char* objText =
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "f 1 2 3\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(objText), std::strlen(objText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".obj");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "OBJ parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Triangles);
}

TEST(PLYLoader, ParseAsciiFromBytes)
{
    const char* plyText =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 1\n"
        "property list uchar int vertex_indices\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "3 0 1 2\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(plyText), std::strlen(plyText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".ply");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "PLY parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Triangles);
}

TEST(PLYLoader, ParsePointCloudFromBytes)
{
    const char* plyText =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 4\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "0 0 1\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(plyText), std::strlen(plyText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".ply");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "PLY point cloud parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 4u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Points);
}

TEST(XYZLoader, ParseFromBytes)
{
    const char* xyzText =
        "1.0 2.0 3.0\n"
        "4.0 5.0 6.0\n"
        "7.0 8.0 9.0\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(xyzText), std::strlen(xyzText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".xyz");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "XYZ parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Points);
}

TEST(XYZLoader, SupportsPointCloudAliasesAndPTSLayout)
{
    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* xyzLoader = registry.FindLoader(".xyz");
    ASSERT_NE(xyzLoader, nullptr);
    EXPECT_EQ(registry.FindLoader(".pts"), xyzLoader);
    EXPECT_EQ(registry.FindLoader(".xyzrgb"), xyzLoader);
    EXPECT_EQ(registry.FindLoader(".TXT"), xyzLoader);

    const char* ptsText =
        "2\n"
        "-0.984375 1.000000 -0.98437 1.0 1.0 1.0 1.0\n"
        "-0.984375 1.000000 -1.00000 0.5 64 128 255\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(ptsText), std::strlen(ptsText));

    LoadContext ctx{};
    auto result = xyzLoader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "PTS parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    ASSERT_EQ(meshImport->Meshes[0].Positions.size(), 2u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Points);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[0].x, 1.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[0].y, 1.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[0].z, 1.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[1].x, 64.0f / 255.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[1].y, 128.0f / 255.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[1].z, 1.0f, 1e-6f);
}

TEST(XYZLoader, SkipsLHScanLineMarkers)
{
    const char* xyzText =
        "3\n"
        "LH1\n"
        "1.0 2.0 3.0\n"
        "LH2\n"
        "4.0 5.0 6.0\n"
        "LH12\n"
        "7.0 8.0 9.0\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(xyzText), std::strlen(xyzText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".xyz");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "XYZ parse with LH markers failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    ASSERT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Points);
    EXPECT_FLOAT_EQ(meshImport->Meshes[0].Positions[0].x, 1.0f);
    EXPECT_FLOAT_EQ(meshImport->Meshes[0].Positions[1].y, 5.0f);
    EXPECT_FLOAT_EQ(meshImport->Meshes[0].Positions[2].z, 9.0f);
}

TEST(XYZLoader, ParsesSemicolonDelimitedRows)
{
    const char* xyzText =
        "LH1\n"
        "20.549438;\t-744.746521;\t-4.371309;\n"
        "20.428139;\t-744.684082;\t-4.322818;\n"
        "20.328897;\t-744.662109;\t-4.313331;\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(xyzText), std::strlen(xyzText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".xyz");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "Semicolon-delimited XYZ parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    ASSERT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Points);
    EXPECT_NEAR(meshImport->Meshes[0].Positions[0].x, 20.549438f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Positions[0].y, -744.746521f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Positions[0].z, -4.371309f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Positions[2].x, 20.328897f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Positions[2].y, -744.662109f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Positions[2].z, -4.313331f, 1e-6f);
}

TEST(PCDLoader, ParseAsciiFromBytes)
{
    const char* pcdText =
        "# .PCD v0.7 - Point Cloud Data file format\n"
        "VERSION 0.7\n"
        "FIELDS x y z r g b\n"
        "SIZE 4 4 4 4 4 4\n"
        "TYPE F F F F F F\n"
        "COUNT 1 1 1 1 1 1\n"
        "WIDTH 2\n"
        "HEIGHT 1\n"
        "POINTS 2\n"
        "DATA ascii\n"
        "0.0 1.0 2.0 255 0 0\n"
        "3.0 4.0 5.0 0 255 0\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pcdText), std::strlen(pcdText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".pcd");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "PCD parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 2u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Points);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[0].x, 1.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[0].y, 0.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[1].y, 1.0f, 1e-6f);
}

TEST(PCDLoader, ParseBinaryFromBytes)
{
    const char* header =
        "# .PCD v0.7 - Point Cloud Data file format\n"
        "VERSION 0.7\n"
        "FIELDS x y z r g b\n"
        "SIZE 4 4 4 1 1 1\n"
        "TYPE F F F U U U\n"
        "COUNT 1 1 1 1 1 1\n"
        "WIDTH 2\n"
        "HEIGHT 1\n"
        "POINTS 2\n"
        "DATA binary\n";

    std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(header),
                                 reinterpret_cast<const std::byte*>(header) + std::strlen(header));

    auto appendScalar = [&bytes](const auto& value)
    {
        const auto* raw = reinterpret_cast<const std::byte*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(value));
    };
    auto appendColor = [&bytes](std::uint8_t value)
    {
        bytes.push_back(static_cast<std::byte>(value));
    };
    auto appendPoint = [&](float x, float y, float z, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        appendScalar(x);
        appendScalar(y);
        appendScalar(z);
        appendColor(r);
        appendColor(g);
        appendColor(b);
    };

    appendPoint(0.0f, 1.0f, 2.0f, 255u, 0u, 0u);
    appendPoint(3.0f, 4.0f, 5.0f, 0u, 255u, 128u);

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".pcd");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "Binary PCD parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 2u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Points);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[0].x, 1.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[0].y, 0.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[1].y, 1.0f, 1e-6f);
    EXPECT_NEAR(meshImport->Meshes[0].Aux[1].z, 128.0f / 255.0f, 1e-6f);
}

// =============================================================================
// IORegistry::Import integration test (full pipeline: backend + registry)
// =============================================================================

// =============================================================================
// STL Loader Tests
// =============================================================================

TEST(STLLoader, ParseBinaryFromBytes)
{
    // Construct a minimal binary STL in memory: 1 triangle
    // Binary format: 80-byte header + 4-byte triangle count + 50 bytes per triangle
    std::vector<std::byte> data(80 + 4 + 50, std::byte{0});

    // Triangle count = 1
    uint32_t triCount = 1;
    std::memcpy(data.data() + 80, &triCount, 4);

    // Normal (0,0,1)
    float normal[3] = {0.0f, 0.0f, 1.0f};
    std::memcpy(data.data() + 84, normal, 12);

    // Vertex 0: (0,0,0)
    float v0[3] = {0.0f, 0.0f, 0.0f};
    std::memcpy(data.data() + 96, v0, 12);

    // Vertex 1: (1,0,0)
    float v1[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(data.data() + 108, v1, 12);

    // Vertex 2: (0,1,0)
    float v2[3] = {0.0f, 1.0f, 0.0f};
    std::memcpy(data.data() + 120, v2, 12);

    // Attribute byte count = 0
    uint16_t attr = 0;
    std::memcpy(data.data() + 132, &attr, 2);

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".stl");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(data, ctx);
    ASSERT_TRUE(result.has_value()) << "Binary STL parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Triangles);
}

TEST(STLLoader, ParseAsciiFromBytes)
{
    const char* stlText =
        "solid test\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1 0 0\n"
        "      vertex 0 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid test\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(stlText), std::strlen(stlText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".stl");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "ASCII STL parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 3u);
}

TEST(STLLoader, VertexDeduplication)
{
    // Two triangles sharing an edge: (0,0,0)-(1,0,0) is shared
    // Without dedup: 6 vertices. With dedup: 4 unique vertices.
    std::vector<std::byte> data(80 + 4 + 100, std::byte{0});

    uint32_t triCount = 2;
    std::memcpy(data.data() + 80, &triCount, 4);

    // Triangle 1: (0,0,0), (1,0,0), (0,1,0)
    float n1[3] = {0.0f, 0.0f, 1.0f};
    float t1v0[3] = {0.0f, 0.0f, 0.0f};
    float t1v1[3] = {1.0f, 0.0f, 0.0f};
    float t1v2[3] = {0.0f, 1.0f, 0.0f};
    uint16_t attr = 0;
    std::size_t off = 84;
    std::memcpy(data.data() + off, n1, 12); off += 12;
    std::memcpy(data.data() + off, t1v0, 12); off += 12;
    std::memcpy(data.data() + off, t1v1, 12); off += 12;
    std::memcpy(data.data() + off, t1v2, 12); off += 12;
    std::memcpy(data.data() + off, &attr, 2); off += 2;

    // Triangle 2: (0,0,0), (1,0,0), (0.5,0,-1)
    float n2[3] = {0.0f, 0.0f, -1.0f};
    float t2v0[3] = {0.0f, 0.0f, 0.0f};
    float t2v1[3] = {1.0f, 0.0f, 0.0f};
    float t2v2[3] = {0.5f, 0.0f, -1.0f};
    std::memcpy(data.data() + off, n2, 12); off += 12;
    std::memcpy(data.data() + off, t2v0, 12); off += 12;
    std::memcpy(data.data() + off, t2v1, 12); off += 12;
    std::memcpy(data.data() + off, t2v2, 12); off += 12;
    std::memcpy(data.data() + off, &attr, 2);

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".stl");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(data, ctx);
    ASSERT_TRUE(result.has_value());

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 4u); // 4 unique, not 6
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 6u);   // 2 triangles * 3
}

TEST(STLLoader, EmptyReturnsInvalidData)
{
    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".stl");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    std::span<const std::byte> empty;
    auto result = loader->Load(empty, ctx);
    EXPECT_FALSE(result.has_value());
}

TEST(STLLoader, AutoDetectsBinaryWithSolidHeader)
{
    // Binary STL that happens to start with "solid" in the header
    std::vector<std::byte> data(80 + 4 + 50, std::byte{0});

    // Write "solid" into the header
    const char* solidStr = "solid fake";
    std::memcpy(data.data(), solidStr, std::strlen(solidStr));

    // Set triangle count = 1 (binary size match: 80 + 4 + 1*50 = 134)
    uint32_t triCount = 1;
    std::memcpy(data.data() + 80, &triCount, 4);

    // Fill in a valid triangle
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float v0[3] = {0.0f, 0.0f, 0.0f};
    float v1[3] = {1.0f, 0.0f, 0.0f};
    float v2[3] = {0.0f, 1.0f, 0.0f};
    uint16_t attr = 0;
    std::memcpy(data.data() + 84, normal, 12);
    std::memcpy(data.data() + 96, v0, 12);
    std::memcpy(data.data() + 108, v1, 12);
    std::memcpy(data.data() + 120, v2, 12);
    std::memcpy(data.data() + 132, &attr, 2);

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".stl");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(data, ctx);
    ASSERT_TRUE(result.has_value()) << "Binary STL with 'solid' header should parse correctly";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
}

// =============================================================================
// OFF Loader Tests
// =============================================================================

TEST(OFFLoader, ParseBasicTriangle)
{
    const char* offText =
        "OFF\n"
        "3 1 0\n"
        "0.0 0.0 0.0\n"
        "1.0 0.0 0.0\n"
        "0.0 1.0 0.0\n"
        "3 0 1 2\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(offText), std::strlen(offText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".off");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "OFF parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Triangles);
}

TEST(OFFLoader, ParseQuadFaces)
{
    const char* offText =
        "OFF\n"
        "4 1 0\n"
        "0.0 0.0 0.0\n"
        "1.0 0.0 0.0\n"
        "1.0 1.0 0.0\n"
        "0.0 1.0 0.0\n"
        "4 0 1 2 3\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(offText), std::strlen(offText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".off");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value());

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    // A quad is fan-triangulated into 2 triangles = 6 indices
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 6u);
}

TEST(OFFLoader, ParseCOFF)
{
    const char* offText =
        "COFF\n"
        "3 1 0\n"
        "0.0 0.0 0.0 255 0 0 255\n"
        "1.0 0.0 0.0 0 255 0 255\n"
        "0.0 1.0 0.0 0 0 255 255\n"
        "3 0 1 2\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(offText), std::strlen(offText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".off");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "COFF parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
}

TEST(OFFLoader, EmptyReturnsInvalidData)
{
    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".off");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    std::span<const std::byte> empty;
    auto result = loader->Load(empty, ctx);
    EXPECT_FALSE(result.has_value());
}

TEST(OFFLoader, PolygonFaces)
{
    // Pentagon (5 vertices, 1 face) should be triangulated into 3 triangles
    const char* offText =
        "OFF\n"
        "5 1 0\n"
        "1.0 0.0 0.0\n"
        "0.309 0.951 0.0\n"
        "-0.809 0.588 0.0\n"
        "-0.809 -0.588 0.0\n"
        "0.309 -0.951 0.0\n"
        "5 0 1 2 3 4\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(offText), std::strlen(offText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".off");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value());

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    // Pentagon → 3 triangles → 9 indices
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 9u);
}

TEST(IORegistryImport, FullPipelineWithGLB)
{
    FileIOBackend backend;
    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    std::string path = std::string(ENGINE_ROOT_DIR) + "/assets/models/Duck.glb";

    // Check if file exists first
    IORequest checkReq;
    checkReq.Path = path;
    checkReq.Size = 1;
    auto checkResult = backend.Read(checkReq);
    if (!checkResult.has_value())
    {
        GTEST_SKIP() << "Duck.glb not found, skipping";
        return;
    }

    auto result = registry.Import(path, backend);
    ASSERT_TRUE(result.has_value()) << "Import failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    EXPECT_GT(meshImport->Meshes.size(), 0u);
}

namespace
{
    Geometry::Halfedge::Mesh BuildHalfedgeMesh(const Graphics::GeometryCpuData& cpu)
    {
        Geometry::Halfedge::Mesh mesh;
        std::vector<Geometry::VertexHandle> verts;
        verts.reserve(cpu.Positions.size());
        for (const auto& p : cpu.Positions)
        {
            verts.push_back(mesh.AddVertex(p));
        }

        bool buildOk = true;
        for (std::size_t i = 0; i + 2 < cpu.Indices.size(); i += 3)
        {
            const auto maybeFace = mesh.AddTriangle(verts[cpu.Indices[i]], verts[cpu.Indices[i + 1]], verts[cpu.Indices[i + 2]]);
            if (!maybeFace.has_value())
            {
                ADD_FAILURE() << "Failed to build duck triangle " << (i / 3);
                buildOk = false;
                break;
            }
        }
        EXPECT_TRUE(buildOk);
        return mesh;
    }

    void ExtractTriangleSoup(Geometry::Halfedge::Mesh& mesh,
                             std::vector<glm::vec3>& positions,
                             std::vector<uint32_t>& indices)
    {
        mesh.GarbageCollection();
        positions.clear();
        indices.clear();
        positions.reserve(mesh.VertexCount());
        indices.reserve(mesh.FaceCount() * 3);

        std::vector<uint32_t> vMap(mesh.VerticesSize(), 0u);
        uint32_t next = 0;
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
            ASSERT_FALSE(mesh.IsDeleted(v));
            vMap[vi] = next++;
            positions.push_back(mesh.Position(v));
        }

        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(fi)};
            ASSERT_FALSE(mesh.IsDeleted(f));
            const auto h0 = mesh.Halfedge(f);
            const auto h1 = mesh.NextHalfedge(h0);
            const auto h2 = mesh.NextHalfedge(h1);
            const auto v0 = mesh.ToVertex(h0);
            const auto v1 = mesh.ToVertex(h1);
            const auto v2 = mesh.ToVertex(h2);
            ASSERT_TRUE(mesh.IsValid(v0));
            ASSERT_TRUE(mesh.IsValid(v1));
            ASSERT_TRUE(mesh.IsValid(v2));
            indices.push_back(vMap[v0.Index]);
            indices.push_back(vMap[v1.Index]);
            indices.push_back(vMap[v2.Index]);
        }
    }
}

TEST(IORegistryImport, DuckRepeatedSimplificationWorkflow)
{
    FileIOBackend backend;
    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    std::string path = std::string(ENGINE_ROOT_DIR) + "/assets/models/Duck.glb";

    IORequest checkReq;
    checkReq.Path = path;
    checkReq.Size = 1;
    auto checkResult = backend.Read(checkReq);
    if (!checkResult.has_value())
    {
        GTEST_SKIP() << "Duck.glb not found, skipping";
        return;
    }

    auto result = registry.Import(path, backend);
    ASSERT_TRUE(result.has_value()) << "Import failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_FALSE(meshImport->Meshes.empty());

    auto mesh = BuildHalfedgeMesh(meshImport->Meshes.front());
    ASSERT_GT(mesh.FaceCount(), 4000u);

    Geometry::Simplification::SimplificationParams first;
    first.TargetFaces = 4000;
    first.PreserveBoundary = false;
    auto firstResult = Geometry::Simplification::Simplify(mesh, first);
    ASSERT_TRUE(firstResult.has_value());

    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    ExtractTriangleSoup(mesh, positions, indices);

    Graphics::GeometryCpuData rebuiltCpu;
    rebuiltCpu.Positions = positions;
    rebuiltCpu.Indices = indices;
    auto rebuilt = BuildHalfedgeMesh(rebuiltCpu);

    Geometry::Simplification::SimplificationParams second;
    second.TargetFaces = 3000;
    second.PreserveBoundary = false;
    auto secondResult = Geometry::Simplification::Simplify(rebuilt, second);
    ASSERT_TRUE(secondResult.has_value());

    ExtractTriangleSoup(rebuilt, positions, indices);
    EXPECT_EQ(indices.size(), rebuilt.FaceCount() * 3);
}
