module;
// TinyGLTF headers
#include <charconv>
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

// Optional: enable verbose import-time logging.
// #define INTRINSIC_MODELLOADER_VERBOSE 1

module Graphics:ModelLoader.Impl;
import :ModelLoader;
import :AssetErrors;
import :Model;
import :IORegistry;
import Core.IOBackend;
import Core.Filesystem;
import Core.Logging;
import RHI;
import :Geometry;
import Geometry;

namespace Graphics
{
    // --- Helpers ---

    struct VertexKey
    {
        int p = -1, n = -1, t = -1;
        bool operator==(const VertexKey& other) const { return p == other.p && n == other.n && t == other.t; }
    };

    struct VertexKeyHash
    {
        size_t operator()(const VertexKey& k) const
        {
            return std::hash<int>()(k.p) ^ (std::hash<int>()(k.n) << 1) ^ (std::hash<int>()(k.t) << 2);
        }
    };

    // Fast float parsing from string_view
    static float ParseFloat(std::string_view sv)
    {
        float val = 0.0f;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    // Split string by delimiter
    static std::vector<std::string_view> Split(std::string_view str, const char delimiter)
    {
        std::vector<std::string_view> result;
        size_t first = 0;
        while (first < str.size())
        {
            const auto second = str.find(delimiter, first);
            if (first != second)
                result.emplace_back(str.substr(first, second - first));
            if (second == std::string_view::npos) break;
            first = second + 1;
        }
        return result;
    }

    inline void RecalculateNormals(GeometryCpuData& mesh)
    {
        if (mesh.Topology != PrimitiveTopology::Triangles) return;

        Geometry::MeshUtils::CalculateNormals(mesh.Positions, mesh.Indices, mesh.Normals);
#if defined(INTRINSIC_MODELLOADER_VERBOSE)
        Core::Log::Info("Recalculated normals for vertices.");
#endif
    }


    inline void GenerateUVs(GeometryCpuData& mesh)
    {
        auto flatAxis = Geometry::MeshUtils::GenerateUVs(mesh.Positions, mesh.Aux);

        if (flatAxis == -1)
        {
            Core::Log::Warn("Failed to generate UVs: Mesh has no vertices.");
        }
        else
        {
#if defined(INTRINSIC_MODELLOADER_VERBOSE)
            Core::Log::Info("Generated Planar UVs for {} vertices (Axis: {})", mesh.Positions.size(), flatAxis);
#endif
        }
    }

    // --- Bulk Data Loading Helper ---
    // Reads from a generic GLTF accessor into a destination vector.
    // Optimizes for memcpy when types match and stride is packed.
    template <typename DstT, typename SrcT>
    void LoadBuffer(std::vector<DstT>& outBuffer,
                    const tinygltf::Model& model,
                    const tinygltf::Accessor& accessor,
                    size_t count)
    {
        const auto& view = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[view.buffer];

        const uint8_t* srcData = &buffer.data[view.byteOffset + accessor.byteOffset];

        // GLTF spec: byteStride of 0 implies tightly packed.
        // If > 0, it is the byte distance between start of attributes.
        const size_t srcStride = view.byteStride == 0 ? sizeof(SrcT) : view.byteStride;

        outBuffer.resize(count);

        // Optimization: Bulk copy if types match and data is tightly packed.
        if constexpr (std::is_same_v<DstT, SrcT>)
        {
            if (srcStride == sizeof(DstT))
            {
                std::memcpy(outBuffer.data(), srcData, count * sizeof(DstT));
                return;
            }
        }

        // Fallback: Stride-aware loop / type conversion.
        DstT* dstPtr = outBuffer.data();
        for (size_t i = 0; i < count; ++i)
        {
            const SrcT* elem = reinterpret_cast<const SrcT*>(srcData + i * srcStride);
            if constexpr (std::is_same_v<DstT, SrcT>)
            {
                dstPtr[i] = *elem;
            }
            else
            {
                dstPtr[i] = static_cast<DstT>(*elem);
            }
        }
    }
}

    // --- Format Parsers ---

    // NOTE: Legacy per-format parsers previously lived here (LoadOBJ/LoadPLY/LoadXYZ/LoadTGF/LoadGLTF).
    // They were superseded by the IORegistry + *Loader implementations and were unused.
    // We rely on the registry path to avoid duplicated parsing logic.
