#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    static void WriteI32LE(std::ofstream& f, int32_t v) { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    static void WriteF32LE(std::ofstream& f, float v) { f.write(reinterpret_cast<const char*>(&v), sizeof(v)); }

    // Minimal tristrips triangulation: matches engine logic (alternating winding, -1 restarts).
    static std::vector<uint32_t> TriangulateTriStrip(const std::vector<int32_t>& idx)
    {
        std::vector<uint32_t> out;
        int32_t a = -1, b = -1;
        bool parity = false;

        auto reset = [&]() { a = -1; b = -1; parity = false; };
        reset();

        for (int32_t v : idx)
        {
            if (v < 0) { reset(); continue; }
            if (a < 0) { a = v; continue; }
            if (b < 0) { b = v; continue; }

            const int32_t c = v;
            if (a != b && b != c && a != c)
            {
                if (!parity)
                {
                    out.push_back((uint32_t)a);
                    out.push_back((uint32_t)b);
                    out.push_back((uint32_t)c);
                }
                else
                {
                    out.push_back((uint32_t)b);
                    out.push_back((uint32_t)a);
                    out.push_back((uint32_t)c);
                }
            }

            a = b;
            b = c;
            parity = !parity;
        }

        return out;
    }

    // Minimal loader for our test PLY that contains:
    // - vertex element: float x y z
    // - tristrips element: list int int vertex_indices
    // It returns the triangulated index buffer.
    static std::vector<uint32_t> LoadTestTristripsPLY(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        EXPECT_TRUE(f.is_open());

        std::string line;
        size_t vcount = 0;
        size_t tscount = 0;

        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "end_header") break;

            if (line.rfind("element vertex ", 0) == 0)
            {
                vcount = (size_t)std::stoul(line.substr(std::string("element vertex ").size()));
            }
            else if (line.rfind("element tristrips ", 0) == 0)
            {
                tscount = (size_t)std::stoul(line.substr(std::string("element tristrips ").size()));
            }
        }

        // skip vertices (x y z floats)
        f.seekg((std::streamoff)(vcount * 12), std::ios_base::cur);

        std::vector<uint32_t> out;
        for (size_t s = 0; s < tscount; ++s)
        {
            int32_t count = 0;
            f.read(reinterpret_cast<char*>(&count), sizeof(count));
            EXPECT_GT(count, 0);

            std::vector<int32_t> idx((size_t)count);
            f.read(reinterpret_cast<char*>(idx.data()), (std::streamsize)(idx.size() * sizeof(int32_t)));

            auto tri = TriangulateTriStrip(idx);
            out.insert(out.end(), tri.begin(), tri.end());
        }

        return out;
    }
}

TEST(ModelLoaderPLY, TriStripsRestartAndWinding)
{
    const std::string path = "/tmp/IntrinsicEngineTests/ply_tristrips_restart.ply";

    std::ofstream f(path, std::ios::binary);
    ASSERT_TRUE(f.is_open());

    f << "ply\n";
    f << "format binary_little_endian 1.0\n";
    f << "element vertex 4\n";
    f << "property float x\n";
    f << "property float y\n";
    f << "property float z\n";
    f << "element tristrips 1\n";
    f << "property list int int vertex_indices\n";
    f << "end_header\n";

    // 4 vertices
    WriteF32LE(f, 0.0f); WriteF32LE(f, 0.0f); WriteF32LE(f, 0.0f);
    WriteF32LE(f, 1.0f); WriteF32LE(f, 0.0f); WriteF32LE(f, 0.0f);
    WriteF32LE(f, 0.0f); WriteF32LE(f, 1.0f); WriteF32LE(f, 0.0f);
    WriteF32LE(f, 1.0f); WriteF32LE(f, 1.0f); WriteF32LE(f, 0.0f);

    // One strip list with a restart: [0 1 2 3 -1 0 2 3]
    // First segment (0,1,2,3) => tris: (0,1,2) and (2,1,3)
    // Second segment (0,2,3) => tris: (0,2,3)
    WriteI32LE(f, 8);
    WriteI32LE(f, 0);
    WriteI32LE(f, 1);
    WriteI32LE(f, 2);
    WriteI32LE(f, 3);
    WriteI32LE(f, -1);
    WriteI32LE(f, 0);
    WriteI32LE(f, 2);
    WriteI32LE(f, 3);

    f.flush();
    f.close();

    const auto indices = LoadTestTristripsPLY(path);

    const std::vector<uint32_t> expected = {
        0, 1, 2,
        2, 1, 3,
        0, 2, 3,
    };

    EXPECT_EQ(indices, expected);
}
