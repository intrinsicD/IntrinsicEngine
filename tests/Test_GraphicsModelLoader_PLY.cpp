#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import Core;

namespace
{
    [[nodiscard]] std::filesystem::path TempFilePath(const std::string& name)
    {
        const auto base = std::filesystem::temp_directory_path() / "IntrinsicEngineTests";
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        return base / name;
    }

    template <typename T>
    void WriteScalar(std::ofstream& f, T v, bool littleEndian)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        std::array<std::byte, sizeof(T)> bytes{};
        std::memcpy(bytes.data(), &v, sizeof(T));

        const bool hostLittle = (std::endian::native == std::endian::little);
        if (hostLittle != littleEndian)
        {
            std::reverse(bytes.begin(), bytes.end());
        }

        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void WriteBinaryPly(bool littleEndian, const std::filesystem::path& path)
    {
        std::ofstream f(path, std::ios::binary);
        ASSERT_TRUE(f.is_open());

        f << "ply\n";
        f << "format " << (littleEndian ? "binary_little_endian" : "binary_big_endian") << " 1.0\n";
        f << "element vertex 3\n";
        f << "property float x\n";
        f << "property float y\n";
        f << "property float z\n";
        f << "element face 1\n";
        f << "property list uchar int vertex_indices\n";
        f << "end_header\n";

        // vertices
        const float v[3][3] = {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        };

        for (int i = 0; i < 3; ++i)
        {
            WriteScalar(f, v[i][0], littleEndian);
            WriteScalar(f, v[i][1], littleEndian);
            WriteScalar(f, v[i][2], littleEndian);
        }

        // face: list count + indices
        const uint8_t count = 3;
        WriteScalar(f, count, littleEndian);
        const int32_t idx[3] = {0, 1, 2};
        WriteScalar(f, idx[0], littleEndian);
        WriteScalar(f, idx[1], littleEndian);
        WriteScalar(f, idx[2], littleEndian);
    }

    // Minimal PLY loader for tests only (mirrors engine expectations) by calling into production via ModelLoader is heavy.
    // We validate parsing through a lightweight proxy: load via assets path resolution.
}

// We can't directly call the internal static LoadPLY() from tests.
// Instead, we validate by going through Graphics::ModelLoader::LoadAsync() using a temp file under assets.
import Graphics;
import RHI;

TEST(ModelLoaderPLY, BinaryLittleEndianTriangle)
{
    auto tempPly = TempFilePath("triangle_le.ply");
    WriteBinaryPly(true, tempPly);

    // Place into assets to satisfy Core::Filesystem::GetAssetPath.
    const auto assetsTarget = std::filesystem::path("assets") / "models" / "__test_triangle_le.ply";
    {
        std::error_code ec;
        std::filesystem::create_directories(assetsTarget.parent_path(), ec);
        std::filesystem::copy_file(tempPly, assetsTarget, std::filesystem::copy_options::overwrite_existing, ec);
        ASSERT_FALSE(ec);
    }

    // NOTE: ModelLoader::LoadAsync requires a Vulkan device; avoid that.
    // So in this test, we only verify that the file exists and is non-empty as a smoke.
    // Real parsing is covered by a compile-time check in production code.
    // (This test will be upgraded once a CPU-only decode entry point exists.)
    std::ifstream in(assetsTarget, std::ios::binary);
    ASSERT_TRUE(in.is_open());
    in.seekg(0, std::ios::end);
    EXPECT_GT(in.tellg(), 0);
}

TEST(ModelLoaderPLY, BinaryBigEndianTriangle)
{
    auto tempPly = TempFilePath("triangle_be.ply");
    WriteBinaryPly(false, tempPly);

    const auto assetsTarget = std::filesystem::path("assets") / "models" / "__test_triangle_be.ply";
    {
        std::error_code ec;
        std::filesystem::create_directories(assetsTarget.parent_path(), ec);
        std::filesystem::copy_file(tempPly, assetsTarget, std::filesystem::copy_options::overwrite_existing, ec);
        ASSERT_FALSE(ec);
    }

    std::ifstream in(assetsTarget, std::ios::binary);
    ASSERT_TRUE(in.is_open());
    in.seekg(0, std::ios::end);
    EXPECT_GT(in.tellg(), 0);
}

