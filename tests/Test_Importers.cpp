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

// =============================================================================
// OBJ Importer
// =============================================================================

TEST(Importer_OBJ, LoadsTriangle)
{
    OBJLoader loader;
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";

    auto result = loader.Load(ToBytes(obj), MakeCtx("test.obj"));
    ASSERT_TRUE(result.has_value()) << "OBJ load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_GE(meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshes[0].Indices.size(), 3u);
}

TEST(Importer_OBJ, LoadsQuadFanTriangulated)
{
    OBJLoader loader;
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "f 1 2 3 4\n";

    auto result = loader.Load(ToBytes(obj), MakeCtx("test.obj"));
    ASSERT_TRUE(result.has_value());

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    // Quad → 2 triangles → 6 indices
    EXPECT_EQ(meshes[0].Indices.size(), 6u);
}

TEST(Importer_OBJ, EmptyInputReturnsError)
{
    OBJLoader loader;
    const std::string empty;
    auto result = loader.Load(ToBytes(empty), MakeCtx("test.obj"));
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
    OBJLoader loader;
    auto exts = loader.Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".obj") found = true;
    EXPECT_TRUE(found) << "OBJ loader must register .obj extension";
}

TEST(Importer_OBJ, VertexNormals)
{
    OBJLoader loader;
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "f 1//1 2//1 3//1\n";

    auto result = loader.Load(ToBytes(obj), MakeCtx("test.obj"));
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
    OFFLoader loader;
    const std::string off =
        "OFF\n"
        "3 1 0\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "3 0 1 2\n";

    auto result = loader.Load(ToBytes(off), MakeCtx("test.off"));
    ASSERT_TRUE(result.has_value()) << "OFF load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
    EXPECT_EQ(meshes[0].Indices.size(), 3u);
}

TEST(Importer_OFF, InvalidHeaderReturnsError)
{
    OFFLoader loader;
    const std::string bad = "NOT_OFF\n3 1 0\n";
    auto result = loader.Load(ToBytes(bad), MakeCtx("test.off"));
    EXPECT_FALSE(result.has_value());
}

TEST(Importer_OFF, ExtensionsRegistered)
{
    OFFLoader loader;
    auto exts = loader.Extensions();
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
    XYZLoader loader;
    const std::string xyz =
        "1.0 2.0 3.0\n"
        "4.0 5.0 6.0\n"
        "7.0 8.0 9.0\n";

    auto result = loader.Load(ToBytes(xyz), MakeCtx("test.xyz"));
    ASSERT_TRUE(result.has_value()) << "XYZ load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Positions.size(), 3u);
}

TEST(Importer_XYZ, EmptyInputProducesNoGeometry)
{
    XYZLoader loader;
    const std::string empty;
    auto result = loader.Load(ToBytes(empty), MakeCtx("test.xyz"));
    if (result.has_value())
    {
        const auto& meshes = AsMesh(*result).Meshes;
        for (const auto& m : meshes)
            EXPECT_TRUE(m.Positions.empty());
    }
}

TEST(Importer_XYZ, ExtensionsRegistered)
{
    XYZLoader loader;
    auto exts = loader.Extensions();
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
    TGFLoader loader;
    const std::string tgf =
        "1 NodeA\n"
        "2 NodeB\n"
        "3 NodeC\n"
        "#\n"
        "1 2 edge1\n"
        "2 3 edge2\n";

    auto result = loader.Load(ToBytes(tgf), MakeCtx("test.tgf"));
    ASSERT_TRUE(result.has_value()) << "TGF load failed";
}

TEST(Importer_TGF, ExtensionsRegistered)
{
    TGFLoader loader;
    auto exts = loader.Extensions();
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
    STLLoader loader;
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

    auto result = loader.Load(ToBytes(stl), MakeCtx("test.stl"));
    ASSERT_TRUE(result.has_value()) << "STL load failed";

    const auto& meshes = AsMesh(*result).Meshes;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_GE(meshes[0].Positions.size(), 3u);
}

TEST(Importer_STL, ExtensionsRegistered)
{
    STLLoader loader;
    auto exts = loader.Extensions();
    ASSERT_FALSE(exts.empty());
    bool found = false;
    for (auto ext : exts)
        if (ext == ".stl") found = true;
    EXPECT_TRUE(found);
}

// =============================================================================
// IAssetLoader interface contract: FormatName is non-empty.
// =============================================================================

TEST(Importer_Contract, FormatNamesAreNonEmpty)
{
    OBJLoader obj;
    OFFLoader off;
    XYZLoader xyz;
    TGFLoader tgf;
    STLLoader stl;

    EXPECT_FALSE(obj.FormatName().empty());
    EXPECT_FALSE(off.FormatName().empty());
    EXPECT_FALSE(xyz.FormatName().empty());
    EXPECT_FALSE(tgf.FormatName().empty());
    EXPECT_FALSE(stl.FormatName().empty());
}

TEST(Importer_Contract, ExtensionsStartWithDot)
{
    auto checkDot = [](IAssetLoader& loader) {
        for (auto ext : loader.Extensions())
            EXPECT_EQ(ext[0], '.') << "Extension must start with '.' in " << loader.FormatName();
    };

    OBJLoader obj; checkDot(obj);
    OFFLoader off; checkDot(off);
    XYZLoader xyz; checkDot(xyz);
    TGFLoader tgf; checkDot(tgf);
    STLLoader stl; checkDot(stl);
}
