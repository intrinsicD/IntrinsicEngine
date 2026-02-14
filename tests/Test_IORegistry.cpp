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

import Core;
import Core.IOBackend;
import Graphics;

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

    // All 5 built-in formats should be importable
    EXPECT_TRUE(registry.CanImport(".obj"));
    EXPECT_TRUE(registry.CanImport(".ply"));
    EXPECT_TRUE(registry.CanImport(".xyz"));
    EXPECT_TRUE(registry.CanImport(".pcd"));
    EXPECT_TRUE(registry.CanImport(".tgf"));
    EXPECT_TRUE(registry.CanImport(".gltf"));
    EXPECT_TRUE(registry.CanImport(".glb"));
    EXPECT_FALSE(registry.CanImport(".stl")); // Not yet implemented
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

TEST(TGFLoader, ParseFromBytes)
{
    const char* tgfText =
        "1 0.0 0.0 0.0\n"
        "2 1.0 0.0 0.0\n"
        "3 0.0 1.0 0.0\n"
        "#\n"
        "1 2\n"
        "2 3\n";

    std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(tgfText), std::strlen(tgfText));

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".tgf");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    auto result = loader->Load(bytes, ctx);
    ASSERT_TRUE(result.has_value()) << "TGF parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    ASSERT_EQ(meshImport->Meshes.size(), 1u);
    EXPECT_EQ(meshImport->Meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshImport->Meshes[0].Indices.size(), 4u); // 2 edges * 2 indices
    EXPECT_EQ(meshImport->Meshes[0].Topology, PrimitiveTopology::Lines);
}

TEST(GLTFLoader, ParseGLBFromFile)
{
    // Read Duck.glb via FileIOBackend, then pass bytes to loader
    FileIOBackend backend;
    std::string path = std::string(ENGINE_ROOT_DIR) + "/assets/models/Duck.glb";

    IORequest req;
    req.Path = path;
    auto readResult = backend.Read(req);

    // Skip test if file doesn't exist (CI environment might not have assets)
    if (!readResult.has_value())
    {
        GTEST_SKIP() << "Duck.glb not found, skipping";
        return;
    }

    IORegistry registry;
    RegisterBuiltinLoaders(registry);

    auto* loader = registry.FindLoader(".glb");
    ASSERT_NE(loader, nullptr);

    LoadContext ctx{};
    ctx.SourcePath = path;
    // BasePath for GLB is the directory containing the file
    std::string baseDir = std::filesystem::path(path).parent_path().string();
    ctx.BasePath = baseDir;
    ctx.Backend = &backend;

    auto result = loader->Load(readResult->Data, ctx);
    ASSERT_TRUE(result.has_value()) << "GLB parse failed";

    auto* meshImport = std::get_if<MeshImportData>(&*result);
    ASSERT_NE(meshImport, nullptr);
    EXPECT_GT(meshImport->Meshes.size(), 0u);

    // Duck.glb should have triangle topology and non-empty vertex data
    for (const auto& mesh : meshImport->Meshes)
    {
        EXPECT_GT(mesh.Positions.size(), 0u);
        EXPECT_EQ(mesh.Topology, PrimitiveTopology::Triangles);
    }
}

// =============================================================================
// IORegistry::Import integration test (full pipeline: backend + registry)
// =============================================================================

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
