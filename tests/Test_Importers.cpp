// =============================================================================
// Test_Importers — Per-format importer contract tests.
//
// Tests each importer's Load() interface with synthetic byte data to verify:
// - Correct geometry extraction (positions, normals, indices)
// - Error handling for empty/malformed input
// - Extension registration
//
// Target: IntrinsicTests (requires IntrinsicRuntime for Graphics module).
// =============================================================================

#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import Graphics;

using namespace Graphics;

// Helper: convert a string to a byte span for Load().
static std::span<const std::byte> ToBytes(const std::string& s)
{
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

// Helper: extract the MeshImportData from an ImportResult.
static const MeshImportData& AsMesh(const ImportResult& result)
{
    return std::get<MeshImportData>(result);
}

// Default LoadContext with no I/O backend.
static LoadContext MakeCtx(std::string_view path = "test.obj")
{
    LoadContext ctx{};
    ctx.SourcePath = path;
    return ctx;
}

static IAssetLoader* FindBuiltinLoader(std::string_view extension)
{
    static auto registry = [] {
        auto value = std::make_unique<IORegistry>();
        RegisterBuiltinLoaders(*value);
        return value;
    }();

    return registry->FindLoader(extension);
}

// =============================================================================
// OBJ Importer
// =============================================================================

TEST(Importer_OBJ, LoadsTriangle)
{
    auto* loader = FindBuiltinLoader(".obj");
    ASSERT_NE(loader, nullptr);
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";

    auto result = loader->Load(ToBytes(obj), MakeCtx("test.obj"));
    ASSERT_TRUE(result.has_value()) << "OBJ load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_GE(meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshes[0].Indices.size(), 3u);
}

TEST(Importer_OBJ, LoadsQuadFanTriangulated)
{
    auto* loader = FindBuiltinLoader(".obj");
    ASSERT_NE(loader, nullptr);
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "f 1 2 3 4\n";

    auto result = loader->Load(ToBytes(obj), MakeCtx("test.obj"));
    ASSERT_TRUE(result.has_value());

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    // Quad → 2 triangles → 6 indices
    EXPECT_EQ(meshes[0].Indices.size(), 6u);
}

TEST(Importer_OBJ, EmptyInputReturnsError)
{
    auto* loader = FindBuiltinLoader(".obj");
    ASSERT_NE(loader, nullptr);
    const std::string empty;
    auto result = loader->Load(ToBytes(empty), MakeCtx("test.obj"));
    // Empty OBJ should either error or produce empty meshes.
    if (result.has_value())
    {
        const auto& meshes = AsMesh(*result).Meshes;
        // Either no meshes or meshes with no geometry.
        for (const auto& m : meshes)
            EXPECT_TRUE(m.Positions.empty());
    }
}

TEST(Importer_OBJ, ExtensionsRegistered)
{
    auto* loader = FindBuiltinLoader(".obj");
    ASSERT_NE(loader, nullptr);
    auto exts = loader->Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".obj") found = true;
    EXPECT_TRUE(found) << "OBJ loader must register .obj extension";
}

TEST(Importer_OBJ, VertexNormals)
{
    auto* loader = FindBuiltinLoader(".obj");
    ASSERT_NE(loader, nullptr);
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "f 1//1 2//1 3//1\n";

    auto result = loader->Load(ToBytes(obj), MakeCtx("test.obj"));
    ASSERT_TRUE(result.has_value());

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_FALSE(meshes[0].Normals.empty());
}

// =============================================================================
// OFF Importer
// =============================================================================

TEST(Importer_OFF, LoadsTriangle)
{
    auto* loader = FindBuiltinLoader(".off");
    ASSERT_NE(loader, nullptr);
    const std::string off =
        "OFF\n"
        "3 1 0\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "3 0 1 2\n";

    auto result = loader->Load(ToBytes(off), MakeCtx("test.off"));
    ASSERT_TRUE(result.has_value()) << "OFF load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshes[0].Indices.size(), 3u);
}

TEST(Importer_OFF, InvalidHeaderReturnsError)
{
    auto* loader = FindBuiltinLoader(".off");
    ASSERT_NE(loader, nullptr);
    const std::string bad = "NOT_OFF\n3 1 0\n";
    auto result = loader->Load(ToBytes(bad), MakeCtx("test.off"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_OFF, ExtensionsRegistered)
{
    auto* loader = FindBuiltinLoader(".off");
    ASSERT_NE(loader, nullptr);
    auto exts = loader->Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".off") found = true;
    EXPECT_TRUE(found);
}

// =============================================================================
// XYZ Importer (Point Cloud)
// =============================================================================

TEST(Importer_XYZ, LoadsPoints)
{
    auto* loader = FindBuiltinLoader(".xyz");
    ASSERT_NE(loader, nullptr);
    const std::string xyz =
        "1.0 2.0 3.0\n"
        "4.0 5.0 6.0\n"
        "7.0 8.0 9.0\n";

    auto result = loader->Load(ToBytes(xyz), MakeCtx("test.xyz"));
    ASSERT_TRUE(result.has_value()) << "XYZ load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
}

TEST(Importer_XYZ, EmptyInputProducesNoGeometry)
{
    auto* loader = FindBuiltinLoader(".xyz");
    ASSERT_NE(loader, nullptr);
    const std::string empty;
    auto result = loader->Load(ToBytes(empty), MakeCtx("test.xyz"));
    if (result.has_value())
    {
        const auto& meshes = AsMesh(*result).Meshes;
        for (const auto& m : meshes)
            EXPECT_TRUE(m.Positions.empty());
    }
}

TEST(Importer_XYZ, ExtensionsRegistered)
{
    auto* loader = FindBuiltinLoader(".xyz");
    ASSERT_NE(loader, nullptr);
    auto exts = loader->Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".xyz") found = true;
    EXPECT_TRUE(found);
}

// =============================================================================
// TGF Importer (Graph)
// =============================================================================

TEST(Importer_TGF, LoadsSimpleGraph)
{
    auto* loader = FindBuiltinLoader(".tgf");
    ASSERT_NE(loader, nullptr);
    const std::string tgf =
        "1 NodeA\n"
        "2 NodeB\n"
        "3 NodeC\n"
        "#\n"
        "1 2 edge1\n"
        "2 3 edge2\n";

    auto result = loader->Load(ToBytes(tgf), MakeCtx("test.tgf"));
    ASSERT_TRUE(result.has_value()) << "TGF load failed";
}

TEST(Importer_TGF, ExtensionsRegistered)
{
    auto* loader = FindBuiltinLoader(".tgf");
    ASSERT_NE(loader, nullptr);
    auto exts = loader->Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".tgf") found = true;
    EXPECT_TRUE(found);
}

// =============================================================================
// STL Importer
// =============================================================================

TEST(Importer_STL, LoadsASCIITriangle)
{
    auto* loader = FindBuiltinLoader(".stl");
    ASSERT_NE(loader, nullptr);
    const std::string stl =
        "solid test\n"
        "facet normal 0 0 1\n"
        "  outer loop\n"
        "    vertex 0 0 0\n"
        "    vertex 1 0 0\n"
        "    vertex 0 1 0\n"
        "  endloop\n"
        "endfacet\n"
        "endsolid test\n";

    auto result = loader->Load(ToBytes(stl), MakeCtx("test.stl"));
    ASSERT_TRUE(result.has_value()) << "STL load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_GE(meshes[0].Positions.size(), 3u);
}

TEST(Importer_STL, ExtensionsRegistered)
{
    auto* loader = FindBuiltinLoader(".stl");
    ASSERT_NE(loader, nullptr);
    auto exts = loader->Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".stl") found = true;
    EXPECT_TRUE(found);
}

// =============================================================================
// PLY Importer
// =============================================================================

TEST(Importer_PLY, LoadsASCIITriangle)
{
    auto* loader = FindBuiltinLoader(".ply");
    ASSERT_NE(loader, nullptr);
    const std::string ply =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 1\n"
        "property list uchar uint vertex_indices\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "3 0 1 2\n";

    auto result = loader->Load(ToBytes(ply), MakeCtx("test.ply"));
    ASSERT_TRUE(result.has_value()) << "PLY ASCII load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshes[0].Indices.size(), 3u);
    EXPECT_EQ(meshes[0].Topology, PrimitiveTopology::Triangles);
}

TEST(Importer_PLY, LoadsASCIIPointCloud)
{
    auto* loader = FindBuiltinLoader(".ply");
    ASSERT_NE(loader, nullptr);
    const std::string ply =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 4\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "1.0 2.0 3.0\n"
        "4.0 5.0 6.0\n"
        "7.0 8.0 9.0\n"
        "10.0 11.0 12.0\n";

    auto result = loader->Load(ToBytes(ply), MakeCtx("test.ply"));
    ASSERT_TRUE(result.has_value()) << "PLY point cloud load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 4u);
    EXPECT_EQ(meshes[0].Topology, PrimitiveTopology::Points);
    EXPECT_TRUE(meshes[0].Indices.empty());
}

TEST(Importer_PLY, LoadsASCIIWithNormalsAndColors)
{
    auto* loader = FindBuiltinLoader(".ply");
    ASSERT_NE(loader, nullptr);
    const std::string ply =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property float nx\n"
        "property float ny\n"
        "property float nz\n"
        "property uchar red\n"
        "property uchar green\n"
        "property uchar blue\n"
        "end_header\n"
        "0 0 0 0 0 1 255 0 0\n"
        "1 0 0 0 0 1 0 255 0\n"
        "0 1 0 0 0 1 0 0 255\n";

    auto result = loader->Load(ToBytes(ply), MakeCtx("test.ply"));
    ASSERT_TRUE(result.has_value()) << "PLY normals+colors load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
    EXPECT_FALSE(meshes[0].Normals.empty());
    EXPECT_FALSE(meshes[0].Aux.empty());
}

TEST(Importer_PLY, LoadsASCIIQuadTriangulated)
{
    auto* loader = FindBuiltinLoader(".ply");
    ASSERT_NE(loader, nullptr);
    const std::string ply =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 4\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 1\n"
        "property list uchar uint vertex_indices\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "1 1 0\n"
        "0 1 0\n"
        "4 0 1 2 3\n";

    auto result = loader->Load(ToBytes(ply), MakeCtx("test.ply"));
    ASSERT_TRUE(result.has_value());

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    // Quad → 2 triangles → 6 indices
    EXPECT_EQ(meshes[0].Indices.size(), 6u);
}

// Helper: write a little-endian value into a byte vector.
template <typename T>
static void AppendLE(std::vector<std::byte>& buf, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    for (size_t i = 0; i < sizeof(T); ++i)
        buf.push_back(bytes[i]);
}

TEST(Importer_PLY, LoadsBinaryLittleEndianTriangle)
{
    // Build a binary LE PLY with 3 vertices (float x,y,z) and 1 triangle face.
    const std::string header =
        "ply\n"
        "format binary_little_endian 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 1\n"
        "property list uchar uint vertex_indices\n"
        "end_header\n";

    std::vector<std::byte> data(header.size());
    std::memcpy(data.data(), header.data(), header.size());

    // 3 vertices: (0,0,0), (1,0,0), (0,1,0)
    AppendLE<float>(data, 0.0f); AppendLE<float>(data, 0.0f); AppendLE<float>(data, 0.0f);
    AppendLE<float>(data, 1.0f); AppendLE<float>(data, 0.0f); AppendLE<float>(data, 0.0f);
    AppendLE<float>(data, 0.0f); AppendLE<float>(data, 1.0f); AppendLE<float>(data, 0.0f);

    // 1 face: count=3, indices 0,1,2
    AppendLE<uint8_t>(data, 3);
    AppendLE<uint32_t>(data, 0); AppendLE<uint32_t>(data, 1); AppendLE<uint32_t>(data, 2);

    auto* loader = FindBuiltinLoader(".ply");
    ASSERT_NE(loader, nullptr);
    auto result = loader->Load(std::span<const std::byte>(data), MakeCtx("test.ply"));
    ASSERT_TRUE(result.has_value()) << "PLY binary LE load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshes[0].Indices.size(), 3u);
    EXPECT_EQ(meshes[0].Topology, PrimitiveTopology::Triangles);
}

TEST(Importer_PLY, MissingEndHeaderReturnsError)
{
    auto* loader = FindBuiltinLoader(".ply");
    ASSERT_NE(loader, nullptr);
    const std::string bad =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 3\n"
        "property float x\n";
    // No end_header
    auto result = loader->Load(ToBytes(bad), MakeCtx("test.ply"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_PLY, InvalidFormatReturnsError)
{
    auto* loader = FindBuiltinLoader(".ply");
    ASSERT_NE(loader, nullptr);
    const std::string bad =
        "ply\n"
        "format unknown_format 1.0\n"
        "element vertex 1\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n"
        "0 0 0\n";
    auto result = loader->Load(ToBytes(bad), MakeCtx("test.ply"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_PLY, ExtensionsRegistered)
{
    auto* loader = FindBuiltinLoader(".ply");
    ASSERT_NE(loader, nullptr);
    auto exts = loader->Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".ply") found = true;
    EXPECT_TRUE(found) << "PLY loader must register .ply extension";
}

// =============================================================================
// PCD Importer (Point Cloud)
// =============================================================================

TEST(Importer_PCD, LoadsASCIIPoints)
{
    auto* loader = FindBuiltinLoader(".pcd");
    ASSERT_NE(loader, nullptr);
    const std::string pcd =
        "# .PCD v0.7 - Point Cloud Data\n"
        "FIELDS x y z\n"
        "SIZE 4 4 4\n"
        "TYPE F F F\n"
        "COUNT 1 1 1\n"
        "WIDTH 3\n"
        "HEIGHT 1\n"
        "POINTS 3\n"
        "DATA ascii\n"
        "1.0 2.0 3.0\n"
        "4.0 5.0 6.0\n"
        "7.0 8.0 9.0\n";

    auto result = loader->Load(ToBytes(pcd), MakeCtx("test.pcd"));
    ASSERT_TRUE(result.has_value()) << "PCD ASCII load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshes[0].Topology, PrimitiveTopology::Points);
}

TEST(Importer_PCD, LoadsASCIIWithSeparateRGBChannels)
{
    auto* loader = FindBuiltinLoader(".pcd");
    ASSERT_NE(loader, nullptr);
    const std::string pcd =
        "FIELDS x y z r g b\n"
        "SIZE 4 4 4 1 1 1\n"
        "TYPE F F F U U U\n"
        "COUNT 1 1 1 1 1 1\n"
        "WIDTH 2\n"
        "HEIGHT 1\n"
        "POINTS 2\n"
        "DATA ascii\n"
        "1.0 2.0 3.0 255 0 0\n"
        "4.0 5.0 6.0 0 255 0\n";

    auto result = loader->Load(ToBytes(pcd), MakeCtx("test.pcd"));
    ASSERT_TRUE(result.has_value()) << "PCD ASCII with colors load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 2u);
    EXPECT_FALSE(meshes[0].Aux.empty());
}

TEST(Importer_PCD, LoadsBinaryPoints)
{
    // Build a binary PCD with 2 points: x,y,z as float32.
    const std::string header =
        "FIELDS x y z\n"
        "SIZE 4 4 4\n"
        "TYPE F F F\n"
        "COUNT 1 1 1\n"
        "WIDTH 2\n"
        "HEIGHT 1\n"
        "POINTS 2\n"
        "DATA binary\n";

    std::vector<std::byte> data(header.size());
    std::memcpy(data.data(), header.data(), header.size());

    // Point 1: (1.0, 2.0, 3.0)
    AppendLE<float>(data, 1.0f); AppendLE<float>(data, 2.0f); AppendLE<float>(data, 3.0f);
    // Point 2: (4.0, 5.0, 6.0)
    AppendLE<float>(data, 4.0f); AppendLE<float>(data, 5.0f); AppendLE<float>(data, 6.0f);

    auto* loader = FindBuiltinLoader(".pcd");
    ASSERT_NE(loader, nullptr);
    auto result = loader->Load(std::span<const std::byte>(data), MakeCtx("test.pcd"));
    ASSERT_TRUE(result.has_value()) << "PCD binary load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 2u);
}

TEST(Importer_PCD, MissingFieldsReturnsError)
{
    auto* loader = FindBuiltinLoader(".pcd");
    ASSERT_NE(loader, nullptr);
    // Missing FIELDS header line
    const std::string bad =
        "SIZE 4 4 4\n"
        "TYPE F F F\n"
        "COUNT 1 1 1\n"
        "POINTS 1\n"
        "DATA ascii\n"
        "1.0 2.0 3.0\n";
    auto result = loader->Load(ToBytes(bad), MakeCtx("test.pcd"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_PCD, MissingXYZFieldsReturnsError)
{
    auto* loader = FindBuiltinLoader(".pcd");
    ASSERT_NE(loader, nullptr);
    // Has fields but not x/y/z
    const std::string bad =
        "FIELDS a b c\n"
        "SIZE 4 4 4\n"
        "TYPE F F F\n"
        "COUNT 1 1 1\n"
        "POINTS 1\n"
        "DATA ascii\n"
        "1.0 2.0 3.0\n";
    auto result = loader->Load(ToBytes(bad), MakeCtx("test.pcd"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_PCD, UnsupportedEncodingReturnsError)
{
    auto* loader = FindBuiltinLoader(".pcd");
    ASSERT_NE(loader, nullptr);
    const std::string bad =
        "FIELDS x y z\n"
        "SIZE 4 4 4\n"
        "TYPE F F F\n"
        "COUNT 1 1 1\n"
        "POINTS 1\n"
        "DATA binary_compressed\n"
        "dummy\n";
    auto result = loader->Load(ToBytes(bad), MakeCtx("test.pcd"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_PCD, ExtensionsRegistered)
{
    auto* loader = FindBuiltinLoader(".pcd");
    ASSERT_NE(loader, nullptr);
    auto exts = loader->Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".pcd") found = true;
    EXPECT_TRUE(found) << "PCD loader must register .pcd extension";
}

// =============================================================================
// GLTF/GLB Importer
// =============================================================================

// Helper: build a minimal valid GLB (glTF Binary) with a single triangle.
// GLB layout: 12-byte header + JSON chunk + BIN chunk.
static std::vector<std::byte> BuildMinimalGLB()
{
    // Buffer: 3 positions (vec3) + 3 indices (uint16, padded to 4-byte alignment)
    // Positions: 3 * 12 = 36 bytes
    // Indices: 3 * 2 = 6 bytes, padded to 8 bytes
    // Total bin: 44 bytes

    // Build the binary buffer
    std::vector<std::byte> binBuf;
    // 3 positions
    AppendLE<float>(binBuf, 0.0f); AppendLE<float>(binBuf, 0.0f); AppendLE<float>(binBuf, 0.0f);
    AppendLE<float>(binBuf, 1.0f); AppendLE<float>(binBuf, 0.0f); AppendLE<float>(binBuf, 0.0f);
    AppendLE<float>(binBuf, 0.0f); AppendLE<float>(binBuf, 1.0f); AppendLE<float>(binBuf, 0.0f);
    // 3 indices (uint16)
    AppendLE<uint16_t>(binBuf, 0); AppendLE<uint16_t>(binBuf, 1); AppendLE<uint16_t>(binBuf, 2);
    // Pad to 4-byte alignment
    AppendLE<uint16_t>(binBuf, 0);

    const uint32_t binSize = static_cast<uint32_t>(binBuf.size()); // 44

    // Build JSON
    std::string json = R"({
  "asset": {"version": "2.0"},
  "buffers": [{"byteLength": )" + std::to_string(binSize) + R"(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "max": [1.0, 1.0, 0.0], "min": [0.0, 0.0, 0.0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "mode": 4}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})";

    // Pad JSON to 4-byte alignment with spaces
    while (json.size() % 4 != 0) json += ' ';
    const uint32_t jsonSize = static_cast<uint32_t>(json.size());

    // GLB header: magic(4) + version(4) + length(4)
    // JSON chunk: length(4) + type(4) + data
    // BIN chunk: length(4) + type(4) + data
    const uint32_t totalLength = 12 + 8 + jsonSize + 8 + binSize;

    std::vector<std::byte> glb;
    glb.reserve(totalLength);

    // GLB header
    AppendLE<uint32_t>(glb, 0x46546C67); // magic "glTF"
    AppendLE<uint32_t>(glb, 2);           // version
    AppendLE<uint32_t>(glb, totalLength);

    // JSON chunk
    AppendLE<uint32_t>(glb, jsonSize);
    AppendLE<uint32_t>(glb, 0x4E4F534A); // "JSON"
    for (char c : json)
        glb.push_back(static_cast<std::byte>(c));

    // BIN chunk
    AppendLE<uint32_t>(glb, binSize);
    AppendLE<uint32_t>(glb, 0x004E4942); // "BIN\0"
    glb.insert(glb.end(), binBuf.begin(), binBuf.end());

    return glb;
}

TEST(Importer_GLTF, LoadsGLBTriangle)
{
    auto* loader = FindBuiltinLoader(".glb");
    ASSERT_NE(loader, nullptr);
    const auto glb = BuildMinimalGLB();

    auto result = loader->Load(std::span<const std::byte>(glb), MakeCtx("test.glb"));
    ASSERT_TRUE(result.has_value()) << "GLB load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshes[0].Indices.size(), 3u);
    EXPECT_EQ(meshes[0].Topology, PrimitiveTopology::Triangles);
}

TEST(Importer_GLTF, EmptyInputReturnsError)
{
    auto* loader = FindBuiltinLoader(".glb");
    ASSERT_NE(loader, nullptr);
    const std::string empty;
    auto result = loader->Load(ToBytes(empty), MakeCtx("test.glb"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_GLTF, InvalidBytesReturnsError)
{
    auto* loader = FindBuiltinLoader(".gltf");
    ASSERT_NE(loader, nullptr);
    const std::string garbage = "this is not valid gltf or glb data at all";
    auto result = loader->Load(ToBytes(garbage), MakeCtx("test.gltf"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_GLTF, ExtensionsRegistered)
{
    auto* loader = FindBuiltinLoader(".gltf");
    ASSERT_NE(loader, nullptr);
    auto exts = loader->Extensions();
    ASSERT_GE(exts.size(), 2u);
    bool foundGltf = false, foundGlb = false;
    for (auto ext : exts)
    {
        if (ext == ".gltf") foundGltf = true;
        if (ext == ".glb") foundGlb = true;
    }
    EXPECT_TRUE(foundGltf) << "GLTF loader must register .gltf extension";
    EXPECT_TRUE(foundGlb) << "GLTF loader must register .glb extension";
}

// =============================================================================
// IAssetLoader interface contract: FormatName is non-empty.
// =============================================================================

TEST(Importer_Contract, FormatNamesAreNonEmpty)
{
    constexpr std::string_view kExtensions[] = {
        ".obj", ".off", ".xyz", ".tgf", ".stl", ".ply", ".pcd", ".gltf", ".glb"
    };

    for (std::string_view ext : kExtensions)
    {
        auto* loader = FindBuiltinLoader(ext);
        ASSERT_NE(loader, nullptr) << "Missing built-in loader for " << ext;
        EXPECT_FALSE(loader->FormatName().empty()) << ext;
    }
}

TEST(Importer_Contract, ExtensionsStartWithDot)
{
    auto checkDot = [](IAssetLoader& loader) {
        for (auto ext : loader.Extensions())
            EXPECT_EQ(ext[0], '.') << "Extension must start with '.' in " << loader.FormatName();
    };

    constexpr std::string_view kExtensions[] = {
        ".obj", ".off", ".xyz", ".tgf", ".stl", ".ply", ".pcd", ".gltf"
    };

    for (std::string_view ext : kExtensions)
    {
        auto* loader = FindBuiltinLoader(ext);
        ASSERT_NE(loader, nullptr) << "Missing built-in loader for " << ext;
        checkDot(*loader);
    }
}
