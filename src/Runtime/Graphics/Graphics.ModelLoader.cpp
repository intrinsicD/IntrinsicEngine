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
import Core;
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

    // --- Format Parsers ---

    static bool LoadOBJ(const std::string& path, GeometryCpuData& outData)
    {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::vector<glm::vec3> tempPos;
        std::vector<glm::vec3> tempNorm;
        std::vector<glm::vec2> tempUV;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVertices;

        outData.Topology = PrimitiveTopology::Triangles; // Default
        std::string line;
        bool hasNormals = false;
        bool hasUVs = false;

        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string type;
            ss >> type;

            if (type == "v")
            {
                glm::vec3 v;
                ss >> v.x >> v.y >> v.z;
                tempPos.push_back(v);
            }
            else if (type == "vn")
            {
                glm::vec3 vn;
                ss >> vn.x >> vn.y >> vn.z;
                tempNorm.push_back(vn);
                hasNormals = true;
            }
            else if (type == "vt")
            {
                glm::vec2 vt;
                ss >> vt.x >> vt.y;
                tempUV.push_back(vt);
                hasUVs = true;
            }
            else if (type == "f")
            {
                std::string vertexStr;
                std::vector<uint32_t> faceIndices;

                while (ss >> vertexStr)
                {
                    VertexKey key;
                    size_t s1 = vertexStr.find('/');
                    size_t s2 = vertexStr.find('/', s1 + 1);

                    // Pos index
                    std::from_chars(vertexStr.data(),
                                    vertexStr.data() + (s1 == std::string::npos ? vertexStr.size() : s1), key.p);
                    key.p--; // OBJ 1-based

                    if (s1 != std::string::npos)
                    {
                        // Tex Index
                        if (s2 != std::string::npos && s2 - s1 > 1)
                        {
                            std::from_chars(vertexStr.data() + s1 + 1, vertexStr.data() + s2, key.t);
                            key.t--;
                        }
                        // Norm Index
                        if (s2 != std::string::npos && s2 + 1 < vertexStr.size())
                        {
                            std::from_chars(vertexStr.data() + s2 + 1, vertexStr.data() + vertexStr.size(), key.n);
                            key.n--;
                        }
                    }

                    if (uniqueVertices.find(key) == uniqueVertices.end())
                    {
                        auto idx = static_cast<uint32_t>(outData.Positions.size());
                        uniqueVertices[key] = idx;

                        outData.Positions.push_back(tempPos[key.p]);
                        outData.Normals.push_back((key.n >= 0 && static_cast<size_t>(key.n) < tempNorm.size())
                                                      ? tempNorm[key.n]
                                                      : glm::vec3(0, 1, 0));
                        glm::vec2 uv = (key.t >= 0 && static_cast<size_t>(key.t) < tempUV.size()) ? tempUV[key.t] : glm::vec2(0, 0);
                        outData.Aux.emplace_back(uv.x, uv.y, 0, 0);
                    }
                    faceIndices.push_back(uniqueVertices[key]);
                }

                // Triangulate Fan
                for (size_t i = 1; i < faceIndices.size() - 1; ++i)
                {
                    outData.Indices.push_back(faceIndices[0]);
                    outData.Indices.push_back(faceIndices[i]);
                    outData.Indices.push_back(faceIndices[i + 1]);
                }
            }
            else if (type == "l")
            {
                outData.Topology = PrimitiveTopology::Lines;
                // Line parsing logic similar to faces but usually just 2 indices
                // Simplified for brevity
            }
        }

        if (!hasNormals)
        {
            RecalculateNormals(outData);
        }
        if (!hasUVs)
        {
            GenerateUVs(outData);
        }
        return true;
    }

    namespace
    {
        enum class PlyFormat
        {
            Ascii,
            BinaryLittleEndian,
            BinaryBigEndian
        };

        enum class PlyScalarType
        {
            Int8,
            UInt8,
            Int16,
            UInt16,
            Int32,
            UInt32,
            Float32,
            Float64
        };

        [[nodiscard]] constexpr size_t PlyScalarSizeBytes(PlyScalarType t)
        {
            switch (t)
            {
            case PlyScalarType::Int8:
            case PlyScalarType::UInt8: return 1;
            case PlyScalarType::Int16:
            case PlyScalarType::UInt16: return 2;
            case PlyScalarType::Int32:
            case PlyScalarType::UInt32:
            case PlyScalarType::Float32: return 4;
            case PlyScalarType::Float64: return 8;
            }
            return 0;
        }

        [[nodiscard]] static std::optional<PlyScalarType> PlyScalarTypeFromToken(std::string_view token)
        {
            // PLY type tokens (common): char/uchar/short/ushort/int/uint/float/double
            // Also allow aliases: int8/uint8/int16/uint16/int32/uint32/float32/float64
            auto lower = std::string(token);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            if (lower == "char" || lower == "int8") return PlyScalarType::Int8;
            if (lower == "uchar" || lower == "uint8" || lower == "uchar8") return PlyScalarType::UInt8;
            if (lower == "short" || lower == "int16") return PlyScalarType::Int16;
            if (lower == "ushort" || lower == "uint16") return PlyScalarType::UInt16;
            if (lower == "int" || lower == "int32") return PlyScalarType::Int32;
            if (lower == "uint" || lower == "uint32") return PlyScalarType::UInt32;
            if (lower == "float" || lower == "float32") return PlyScalarType::Float32;
            if (lower == "double" || lower == "float64") return PlyScalarType::Float64;

            return std::nullopt;
        }

        struct PlyProperty
        {
            std::string Name;
            bool IsList = false;
            PlyScalarType ScalarType = PlyScalarType::Float32; // for non-list
            PlyScalarType ListCountType = PlyScalarType::UInt8;
            PlyScalarType ListElementType = PlyScalarType::UInt32;
            size_t ByteOffset = 0; // Only meaningful for non-list in binary vertex layout
        };

        struct PlyElement
        {
            std::string Name;
            size_t Count = 0;
            std::vector<PlyProperty> Properties;
            size_t BinaryStrideBytes = 0; // Only for fixed-size (non-list) properties
        };

        [[nodiscard]] constexpr bool HostIsLittleEndian()
        {
            return std::endian::native == std::endian::little;
        }

        template <typename T>
        [[nodiscard]] static T ByteSwap(T v)
        {
            static_assert(std::is_trivially_copyable_v<T>);

            std::array<std::byte, sizeof(T)> bytes{};
            std::memcpy(bytes.data(), &v, sizeof(T));
            std::reverse(bytes.begin(), bytes.end());
            std::memcpy(&v, bytes.data(), sizeof(T));
            return v;
        }

        template <typename T>
        [[nodiscard]] static bool ReadRaw(std::ifstream& file, T& out)
        {
            file.read(reinterpret_cast<char*>(&out), sizeof(T));
            return static_cast<bool>(file);
        }

        [[nodiscard]] static std::optional<uint64_t> ReadScalarAsU64(std::ifstream& file, PlyScalarType type, bool fileIsLittleEndian)
        {
            auto maybeSwap = [&](auto v)
            {
                using V = decltype(v);
                if constexpr (sizeof(V) == 1) return v;
                const bool needSwap = (HostIsLittleEndian() != fileIsLittleEndian);
                return needSwap ? ByteSwap(v) : v;
            };

            switch (type)
            {
            case PlyScalarType::Int8:
            {
                int8_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                return static_cast<uint64_t>(static_cast<int64_t>(v));
            }
            case PlyScalarType::UInt8:
            {
                uint8_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                return static_cast<uint64_t>(v);
            }
            case PlyScalarType::Int16:
            {
                int16_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return static_cast<uint64_t>(static_cast<int64_t>(v));
            }
            case PlyScalarType::UInt16:
            {
                uint16_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return static_cast<uint64_t>(v);
            }
            case PlyScalarType::Int32:
            {
                int32_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return static_cast<uint64_t>(static_cast<int64_t>(v));
            }
            case PlyScalarType::UInt32:
            {
                uint32_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return static_cast<uint64_t>(v);
            }
            case PlyScalarType::Float32:
            {
                float v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return static_cast<uint64_t>(static_cast<double>(v));
            }
            case PlyScalarType::Float64:
            {
                double v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return static_cast<uint64_t>(v);
            }
            }
            return std::nullopt;
        }

        [[nodiscard]] static std::optional<int64_t> ReadScalarAsI64(std::ifstream& file, PlyScalarType type, bool fileIsLittleEndian)
        {
            auto maybeSwap = [&](auto v)
            {
                using V = decltype(v);
                if constexpr (sizeof(V) == 1) return v;
                const bool needSwap = (HostIsLittleEndian() != fileIsLittleEndian);
                return needSwap ? ByteSwap(v) : v;
            };

            switch (type)
            {
            case PlyScalarType::Int8:
            {
                int8_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                return (int64_t)v;
            }
            case PlyScalarType::UInt8:
            {
                uint8_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                return (int64_t)v;
            }
            case PlyScalarType::Int16:
            {
                int16_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return (int64_t)v;
            }
            case PlyScalarType::UInt16:
            {
                uint16_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return (int64_t)v;
            }
            case PlyScalarType::Int32:
            {
                int32_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return (int64_t)v;
            }
            case PlyScalarType::UInt32:
            {
                uint32_t v{};
                if (!ReadRaw(file, v)) return std::nullopt;
                v = maybeSwap(v);
                return (int64_t)v;
            }
            case PlyScalarType::Float32:
            case PlyScalarType::Float64:
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsColorByteBased(PlyScalarType t) { return t == PlyScalarType::UInt8 || t == PlyScalarType::Int8; }

        [[nodiscard]] std::string TrimRight(std::string s)
        {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            return s;
        }

        [[nodiscard]] std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        [[nodiscard]] bool PlyScalarIsIntegerLike(PlyScalarType t)
        {
            switch (t)
            {
            case PlyScalarType::Int8:
            case PlyScalarType::UInt8:
            case PlyScalarType::Int16:
            case PlyScalarType::UInt16:
            case PlyScalarType::Int32:
            case PlyScalarType::UInt32: return true;
            case PlyScalarType::Float32:
            case PlyScalarType::Float64: return false;
            }
            return false;
        }

        [[nodiscard]] bool SkipBytes(std::ifstream& file, size_t bytes)
        {
            if (bytes == 0) return true;
            file.seekg(static_cast<std::streamoff>(bytes), std::ios_base::cur);
            return static_cast<bool>(file);
        }

        [[nodiscard]] bool SkipBinaryProperty(std::ifstream& file, const PlyProperty& prop, bool fileIsLittleEndian)
        {
            if (!prop.IsList)
            {
                return SkipBytes(file, PlyScalarSizeBytes(prop.ScalarType));
            }

            const auto maybeCount = ReadScalarAsU64(file, prop.ListCountType, fileIsLittleEndian);
            if (!maybeCount) return false;
            const size_t count = static_cast<size_t>(*maybeCount);
            const size_t elemBytes = PlyScalarSizeBytes(prop.ListElementType);
            return SkipBytes(file, count * elemBytes);
        }

        [[nodiscard]] bool ConsumeBinaryLineBreaksAfterHeader(std::ifstream& file)
        {
            for (;;)
            {
                const int c = file.peek();
                if (c == '\r' || c == '\n')
                {
                    file.get();
                    continue;
                }
                break;
            }
            return static_cast<bool>(file);
        }

        [[nodiscard]] const PlyProperty* ChooseFaceIndexListProperty(const PlyElement& faceElement)
        {
            const PlyProperty* best = nullptr;
            int bestScore = -1;

            for (const auto& p : faceElement.Properties)
            {
                if (!p.IsList) continue;

                const std::string nameLower = ToLower(p.Name);
                int score = 0;

                if (nameLower == "vertex_indices" || nameLower == "vertex_index") score += 100;
                if (nameLower.find("vertex") != std::string::npos) score += 20;
                if (nameLower.find("index") != std::string::npos || nameLower.find("indices") != std::string::npos) score += 20;
                if (nameLower == "indices") score += 10;

                if (PlyScalarIsIntegerLike(p.ListElementType)) score += 10;
                else score -= 50;

                if (p.ListElementType == PlyScalarType::UInt32 || p.ListElementType == PlyScalarType::Int32) score += 2;

                if (score > bestScore)
                {
                    bestScore = score;
                    best = &p;
                }
            }

            return best;
        }

        struct TriStripState
        {
            std::array<int64_t, 2> Prev = { -1, -1 };
            bool Parity = false;

            void Reset() { Prev = { -1, -1 }; Parity = false; }
        };

        void AppendTriStripAsTriangles(const std::vector<int64_t>& strip, std::vector<uint32_t>& outIndices)
        {
            TriStripState state;

            for (const int64_t idx : strip)
            {
                if (idx < 0)
                {
                    // Restart marker (-1)
                    state.Reset();
                    continue;
                }

                if (state.Prev[0] < 0)
                {
                    state.Prev[0] = idx;
                    continue;
                }
                if (state.Prev[1] < 0)
                {
                    state.Prev[1] = idx;
                    continue;
                }

                const int64_t a = state.Prev[0];
                const int64_t b = state.Prev[1];
                const int64_t c = idx;

                // Skip degenerate triangles
                if (a != b && b != c && a != c)
                {
                    if (!state.Parity)
                    {
                        outIndices.push_back(static_cast<uint32_t>(a));
                        outIndices.push_back(static_cast<uint32_t>(b));
                        outIndices.push_back(static_cast<uint32_t>(c));
                    }
                    else
                    {
                        outIndices.push_back(static_cast<uint32_t>(b));
                        outIndices.push_back(static_cast<uint32_t>(a));
                        outIndices.push_back(static_cast<uint32_t>(c));
                    }
                }

                state.Prev[0] = state.Prev[1];
                state.Prev[1] = idx;
                state.Parity = !state.Parity;
            }
        }
    }

    static bool LoadPLY(const std::string& path, GeometryCpuData& outData)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        std::string line;
        PlyFormat format = PlyFormat::Ascii;

        PlyElement vertexElement{};
        PlyElement faceElement{};

        // Header order elements (for robust binary skipping)
        std::vector<PlyElement> elementsInOrder;
        PlyElement* currentElement = nullptr;

        // Quick lookup of vertex property indices
        int idxX = -1, idxY = -1, idxZ = -1;
        int idxNX = -1, idxNY = -1, idxNZ = -1;
        int idxR = -1, idxG = -1, idxB = -1;
        int idxA = -1;
        int idxS = -1, idxT = -1;

        bool headerEnded = false;

        // 1. Header Parse
        while (std::getline(file, line))
        {
            line = TrimRight(line);

            if (line == "end_header")
            {
                headerEnded = true;
                break;
            }

            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string token;
            ss >> token;
            if (token.empty()) continue;

            if (token == "format")
            {
                std::string fmt;
                ss >> fmt;
                fmt = ToLower(fmt);
                if (fmt == "ascii") format = PlyFormat::Ascii;
                else if (fmt == "binary_little_endian") format = PlyFormat::BinaryLittleEndian;
                else if (fmt == "binary_big_endian") format = PlyFormat::BinaryBigEndian;
                else
                {
                    Core::Log::Error("PLY: Unsupported format token: {}", fmt);
                    return false;
                }
            }
            else if (token == "element")
            {
                std::string type;
                size_t count = 0;
                ss >> type >> count;
                type = ToLower(type);

                elementsInOrder.emplace_back();
                elementsInOrder.back().Name = type;
                elementsInOrder.back().Count = count;
                currentElement = &elementsInOrder.back();

                if (type == "vertex")
                {
                    vertexElement = PlyElement{};
                    vertexElement.Name = "vertex";
                    vertexElement.Count = count;

                    // reset indices
                    idxX = idxY = idxZ = -1;
                    idxNX = idxNY = idxNZ = -1;
                    idxR = idxG = idxB = -1;
                    idxA = -1;
                    idxS = idxT = -1;
                }
                else if (type == "face")
                {
                    faceElement = PlyElement{};
                    faceElement.Name = "face";
                    faceElement.Count = count;
                }
            }
            else if (token == "property")
            {
                if (!currentElement) continue;

                std::string typeOrList;
                ss >> typeOrList;
                typeOrList = ToLower(typeOrList);

                PlyProperty prop{};

                if (typeOrList == "list")
                {
                    std::string countTypeTok, elemTypeTok, name;
                    ss >> countTypeTok >> elemTypeTok >> name;

                    const auto countTy = PlyScalarTypeFromToken(countTypeTok);
                    const auto elemTy = PlyScalarTypeFromToken(elemTypeTok);
                    if (!countTy || !elemTy)
                    {
                        Core::Log::Error("PLY: Unsupported list types: {} {}", countTypeTok, elemTypeTok);
                        return false;
                    }

                    prop.Name = ToLower(name);
                    prop.IsList = true;
                    prop.ListCountType = *countTy;
                    prop.ListElementType = *elemTy;
                }
                else
                {
                    std::string name;
                    ss >> name;
                    const auto scalarTy = PlyScalarTypeFromToken(typeOrList);
                    if (!scalarTy)
                    {
                        Core::Log::Error("PLY: Unsupported scalar type: {}", typeOrList);
                        return false;
                    }
                    prop.Name = ToLower(name);
                    prop.IsList = false;
                    prop.ScalarType = *scalarTy;
                }

                currentElement->Properties.push_back(prop);

                // Keep dedicated copies for vertex/face for compatibility with old code paths
                if (currentElement->Name == "vertex")
                {
                    vertexElement.Properties.push_back(prop);
                    const int vIndex = static_cast<int>(vertexElement.Properties.size()) - 1;

                    const std::string& n = vertexElement.Properties.back().Name;
                    if (n == "x") idxX = vIndex;
                    else if (n == "y") idxY = vIndex;
                    else if (n == "z") idxZ = vIndex;
                    else if (n == "nx" || n == "normal_x" || n == "n_x") idxNX = vIndex;
                    else if (n == "ny" || n == "normal_y" || n == "n_y") idxNY = vIndex;
                    else if (n == "nz" || n == "normal_z" || n == "n_z") idxNZ = vIndex;
                    else if (n == "red" || n == "r") idxR = vIndex;
                    else if (n == "green" || n == "g") idxG = vIndex;
                    else if (n == "blue" || n == "b") idxB = vIndex;
                    else if (n == "alpha" || n == "a") idxA = vIndex;
                    else if (n == "s" || n == "u" || n == "texture_u" || n == "texcoord_u" || n == "u0") idxS = vIndex;
                    else if (n == "t" || n == "v" || n == "texture_v" || n == "texcoord_v" || n == "v0") idxT = vIndex;
                }
                else if (currentElement->Name == "face")
                {
                    faceElement.Properties.push_back(prop);
                }
            }
        }

        if (!headerEnded) return false;
        if (vertexElement.Count == 0)
        {
            Core::Log::Error("PLY: Missing vertex element or vertex count == 0");
            return false;
        }

        // Compute feature flags once, early. Used by both ASCII + binary decoding.
        const bool hasNormals = (idxNX >= 0 && idxNY >= 0 && idxNZ >= 0);
        const bool hasUVs = (idxS >= 0 && idxT >= 0);
        const bool hasColors = (idxR >= 0 && idxG >= 0 && idxB >= 0);
        const bool hasAlpha = (idxA >= 0);

        // Allocate output buffers before decoding.
        outData.Positions.resize(vertexElement.Count);
        outData.Normals.resize(vertexElement.Count, glm::vec3(0, 1, 0));
        outData.Aux.resize(vertexElement.Count, glm::vec4(1));

        const bool isBinary = (format != PlyFormat::Ascii);
        const bool fileIsLittle = (format == PlyFormat::BinaryLittleEndian);
        if (isBinary)
        {
            if (!ConsumeBinaryLineBreaksAfterHeader(file))
                return false;
        }

        // Setup binary stride/offsets for vertex fixed-size properties (fast path only if no list props).
        bool vertexHasList = false;
        if (isBinary)
        {
            size_t offset = 0;
            for (auto& p : vertexElement.Properties)
            {
                if (p.IsList)
                {
                    vertexHasList = true;
                    break;
                }
                p.ByteOffset = offset;
                const size_t sz = PlyScalarSizeBytes(p.ScalarType);
                if (sz == 0)
                {
                    Core::Log::Error("PLY: Unsupported/invalid scalar size for vertex property '{}'", p.Name);
                    return false;
                }
                offset += sz;
            }
            vertexElement.BinaryStrideBytes = vertexHasList ? 0 : offset;

            if (!vertexHasList)
            {
                if (vertexElement.BinaryStrideBytes == 0)
                {
                    Core::Log::Error("PLY: Invalid binary vertex stride (0)");
                    return false;
                }

                // Quick sanity check: x/y/z must be within stride.
                auto inStride = [&](int idx) -> bool
                {
                    if (idx < 0) return false;
                    const auto& p = vertexElement.Properties[(size_t)idx];
                    return (p.ByteOffset + PlyScalarSizeBytes(p.ScalarType)) <= vertexElement.BinaryStrideBytes;
                };

                if (!inStride(idxX) || !inStride(idxY) || !inStride(idxZ))
                {
                    Core::Log::Error("PLY: Invalid x/y/z offsets for binary vertex layout");
                    return false;
                }
            }

            // Face: must have at least one list property to form indices
            if (faceElement.Count > 0)
            {
                const PlyProperty* idxProp = ChooseFaceIndexListProperty(faceElement);
                if (!idxProp)
                {
                    Core::Log::Error("PLY: face element has no usable list property for indices");
                    return false;
                }
            }
        }

        // Track connectivity source if present (face or tristrips)
        std::string connectivityElementName;

        // 2. Body Parse
        if (format == PlyFormat::Ascii)
        {
            // Vertex lines
            for (size_t i = 0; i < vertexElement.Count; ++i)
            {
                std::getline(file, line);
                std::vector<std::string_view> tokens = Split(line, ' ');
                std::erase_if(tokens, [](std::string_view s) { return s.empty(); });

                if (idxX >= 0) outData.Positions[i].x = ParseFloat(tokens[(size_t)idxX]);
                if (idxY >= 0) outData.Positions[i].y = ParseFloat(tokens[(size_t)idxY]);
                if (idxZ >= 0) outData.Positions[i].z = ParseFloat(tokens[(size_t)idxZ]);

                if (hasNormals)
                {
                    outData.Normals[i].x = ParseFloat(tokens[(size_t)idxNX]);
                    outData.Normals[i].y = ParseFloat(tokens[(size_t)idxNY]);
                    outData.Normals[i].z = ParseFloat(tokens[(size_t)idxNZ]);
                }

                if (hasColors)
                {
                    float r = ParseFloat(tokens[(size_t)idxR]);
                    float g = ParseFloat(tokens[(size_t)idxG]);
                    float b = ParseFloat(tokens[(size_t)idxB]);
                    float a = hasAlpha ? ParseFloat(tokens[(size_t)idxA]) : 255.0f;
                    if (r > 1.0f || g > 1.0f || b > 1.0f || a > 1.0f)
                    {
                        r /= 255.0f;
                        g /= 255.0f;
                        b /= 255.0f;
                        a /= 255.0f;
                    }
                    outData.Aux[i] = glm::vec4(r, g, b, a);
                }

                if (hasUVs)
                {
                    outData.Aux[i].x = ParseFloat(tokens[(size_t)idxS]);
                    outData.Aux[i].y = ParseFloat(tokens[(size_t)idxT]);
                }
            }

            // Faces
            if (faceElement.Count > 0)
            {
                outData.Topology = PrimitiveTopology::Triangles;
                for (size_t i = 0; i < faceElement.Count; ++i)
                {
                    std::getline(file, line);
                    std::stringstream ss(line);
                    int count;
                    ss >> count;
                    std::vector<uint32_t> faceIndices((size_t)count);
                    for (int k = 0; k < count; ++k) ss >> faceIndices[(size_t)k];

                    for (size_t k = 1; k + 1 < faceIndices.size(); ++k)
                    {
                        outData.Indices.push_back(faceIndices[0]);
                        outData.Indices.push_back(faceIndices[k]);
                        outData.Indices.push_back(faceIndices[k + 1]);
                    }
                }
            }
            else
            {
                outData.Topology = PrimitiveTopology::Points;
            }
        }
        else
        {
            // Robust binary element iteration (prevents desync when extra elements exist).

            auto readFromBlobAsDouble = [&](const std::byte* base, const PlyProperty& p) -> double
            {
                const bool needSwap = (HostIsLittleEndian() != fileIsLittle);
                const std::byte* ptr = base + p.ByteOffset;

                switch (p.ScalarType)
                {
                case PlyScalarType::Int8:
                {
                    int8_t v;
                    std::memcpy(&v, ptr, 1);
                    return (double)v;
                }
                case PlyScalarType::UInt8:
                {
                    uint8_t v;
                    std::memcpy(&v, ptr, 1);
                    return (double)v;
                }
                case PlyScalarType::Int16:
                {
                    int16_t v;
                    std::memcpy(&v, ptr, 2);
                    if (needSwap) v = ByteSwap(v);
                    return (double)v;
                }
                case PlyScalarType::UInt16:
                {
                    uint16_t v;
                    std::memcpy(&v, ptr, 2);
                    if (needSwap) v = ByteSwap(v);
                    return (double)v;
                }
                case PlyScalarType::Int32:
                {
                    int32_t v;
                    std::memcpy(&v, ptr, 4);
                    if (needSwap) v = ByteSwap(v);
                    return (double)v;
                }
                case PlyScalarType::UInt32:
                {
                    uint32_t v;
                    std::memcpy(&v, ptr, 4);
                    if (needSwap) v = ByteSwap(v);
                    return (double)v;
                }
                case PlyScalarType::Float32:
                {
                    float v;
                    std::memcpy(&v, ptr, 4);
                    if (needSwap) v = ByteSwap(v);
                    return (double)v;
                }
                case PlyScalarType::Float64:
                {
                    double v;
                    std::memcpy(&v, ptr, 8);
                    if (needSwap) v = ByteSwap(v);
                    return v;
                }
                }
                return 0.0;
            };

            const PlyProperty& px = vertexElement.Properties[(size_t)idxX];
            const PlyProperty& py = vertexElement.Properties[(size_t)idxY];
            const PlyProperty& pz = vertexElement.Properties[(size_t)idxZ];

            const PlyProperty* pnx = (idxNX >= 0) ? &vertexElement.Properties[(size_t)idxNX] : nullptr;
            const PlyProperty* pny = (idxNY >= 0) ? &vertexElement.Properties[(size_t)idxNY] : nullptr;
            const PlyProperty* pnz = (idxNZ >= 0) ? &vertexElement.Properties[(size_t)idxNZ] : nullptr;

            const PlyProperty* pr = (idxR >= 0) ? &vertexElement.Properties[(size_t)idxR] : nullptr;
            const PlyProperty* pg = (idxG >= 0) ? &vertexElement.Properties[(size_t)idxG] : nullptr;
            const PlyProperty* pb = (idxB >= 0) ? &vertexElement.Properties[(size_t)idxB] : nullptr;
            const PlyProperty* pa = (idxA >= 0) ? &vertexElement.Properties[(size_t)idxA] : nullptr;

            const PlyProperty* ps = (idxS >= 0) ? &vertexElement.Properties[(size_t)idxS] : nullptr;
            const PlyProperty* pt = (idxT >= 0) ? &vertexElement.Properties[(size_t)idxT] : nullptr;

            const PlyProperty* faceIndicesProp = ChooseFaceIndexListProperty(faceElement);

            // Process elements in header order
            for (const PlyElement& elem : elementsInOrder)
            {
                if (elem.Count == 0) continue;

                if (elem.Name == "vertex")
                {
                    if (!vertexHasList)
                    {
                        // Fast path: read blob then decode known offsets.
                        const size_t totalBytes = vertexElement.Count * vertexElement.BinaryStrideBytes;
                        std::vector<std::byte> vertexBlob(totalBytes);
                        file.read(reinterpret_cast<char*>(vertexBlob.data()), static_cast<std::streamsize>(vertexBlob.size()));
                        if (!file)
                        {
                            Core::Log::Error("PLY: Failed to read binary vertex blob ({} bytes)", totalBytes);
                            return false;
                        }

                        const std::byte* base = vertexBlob.data();
                        for (size_t i = 0; i < vertexElement.Count; ++i)
                        {
                            const std::byte* v = base + i * vertexElement.BinaryStrideBytes;

                            outData.Positions[i] = glm::vec3(
                                (float)readFromBlobAsDouble(v, px),
                                (float)readFromBlobAsDouble(v, py),
                                (float)readFromBlobAsDouble(v, pz));

                            if (pnx && pny && pnz)
                            {
                                outData.Normals[i] = glm::vec3(
                                    (float)readFromBlobAsDouble(v, *pnx),
                                    (float)readFromBlobAsDouble(v, *pny),
                                    (float)readFromBlobAsDouble(v, *pnz));
                            }

                            if (pr && pg && pb)
                            {
                                const double rd = readFromBlobAsDouble(v, *pr);
                                const double gd = readFromBlobAsDouble(v, *pg);
                                const double bd = readFromBlobAsDouble(v, *pb);
                                const double ad = pa ? readFromBlobAsDouble(v, *pa) : 255.0;

                                const bool byteBased = IsColorByteBased(pr->ScalarType) && IsColorByteBased(pg->ScalarType) && IsColorByteBased(pb->ScalarType)
                                                       && (!pa || IsColorByteBased(pa->ScalarType));
                                float rf = (float)rd;
                                float gf = (float)gd;
                                float bf = (float)bd;
                                float af = (float)ad;
                                if (byteBased || rf > 1.0f || gf > 1.0f || bf > 1.0f || af > 1.0f)
                                {
                                    rf /= 255.0f;
                                    gf /= 255.0f;
                                    bf /= 255.0f;
                                    af /= 255.0f;
                                }
                                outData.Aux[i] = glm::vec4(rf, gf, bf, af);
                            }

                            if (ps && pt)
                            {
                                outData.Aux[i].x = (float)readFromBlobAsDouble(v, *ps);
                                outData.Aux[i].y = (float)readFromBlobAsDouble(v, *pt);
                            }
                        }
                    }
                    else
                    {
                        // Slow but robust: per-vertex parse/skip property-by-property.
                        // NOTE: list properties in vertex are skipped; we still decode x/y/z/etc scalars.
                        for (size_t i = 0; i < vertexElement.Count; ++i)
                        {
                            // Temporary storage for scalar values we care about
                            glm::vec3 pos{0};
                            glm::vec3 nrm{0, 1, 0};
                            glm::vec2 uv{0};
                            glm::vec4 rgba{1, 1, 1, 1};
                            bool gotColor = false;

                            for (size_t pIndex = 0; pIndex < vertexElement.Properties.size(); ++pIndex)
                            {
                                const PlyProperty& p = vertexElement.Properties[pIndex];
                                if (p.IsList)
                                {
                                    if (!SkipBinaryProperty(file, p, fileIsLittle)) return false;
                                    continue;
                                }

                                // read scalar for selective properties
                                auto readScalar = [&]() -> std::optional<double>
                                {
                                    // reuse ReadScalarAsU64 for ints, but use float/double read to keep precision
                                    // We'll implement small reads inline here.
                                    switch (p.ScalarType)
                                    {
                                    case PlyScalarType::Int8:
                                    {
                                        int8_t v{};
                                        if (!ReadRaw(file, v)) return std::nullopt;
                                        return (double)v;
                                    }
                                    case PlyScalarType::UInt8:
                                    {
                                        uint8_t v{};
                                        if (!ReadRaw(file, v)) return std::nullopt;
                                        return (double)v;
                                    }
                                    case PlyScalarType::Int16:
                                    {
                                        int16_t v{};
                                        if (!ReadRaw(file, v)) return std::nullopt;
                                        if (HostIsLittleEndian() != fileIsLittle) v = ByteSwap(v);
                                        return (double)v;
                                    }
                                    case PlyScalarType::UInt16:
                                    {
                                        uint16_t v{};
                                        if (!ReadRaw(file, v)) return std::nullopt;
                                        if (HostIsLittleEndian() != fileIsLittle) v = ByteSwap(v);
                                        return (double)v;
                                    }
                                    case PlyScalarType::Int32:
                                    {
                                        int32_t v{};
                                        if (!ReadRaw(file, v)) return std::nullopt;
                                        if (HostIsLittleEndian() != fileIsLittle) v = ByteSwap(v);
                                        return (double)v;
                                    }
                                    case PlyScalarType::UInt32:
                                    {
                                        uint32_t v{};
                                        if (!ReadRaw(file, v)) return std::nullopt;
                                        if (HostIsLittleEndian() != fileIsLittle) v = ByteSwap(v);
                                        return (double)v;
                                    }
                                    case PlyScalarType::Float32:
                                    {
                                        float v{};
                                        if (!ReadRaw(file, v)) return std::nullopt;
                                        if (HostIsLittleEndian() != fileIsLittle) v = ByteSwap(v);
                                        return (double)v;
                                    }
                                    case PlyScalarType::Float64:
                                    {
                                        double v{};
                                        if (!ReadRaw(file, v)) return std::nullopt;
                                        if (HostIsLittleEndian() != fileIsLittle) v = ByteSwap(v);
                                        return v;
                                    }
                                    }
                                    return std::nullopt;
                                };

                                const auto maybeVal = readScalar();
                                if (!maybeVal) return false;

                                if ((int)pIndex == idxX) pos.x = (float)(*maybeVal);
                                else if ((int)pIndex == idxY) pos.y = (float)(*maybeVal);
                                else if ((int)pIndex == idxZ) pos.z = (float)(*maybeVal);
                                else if ((int)pIndex == idxNX) nrm.x = (float)(*maybeVal);
                                else if ((int)pIndex == idxNY) nrm.y = (float)(*maybeVal);
                                else if ((int)pIndex == idxNZ) nrm.z = (float)(*maybeVal);
                                else if ((int)pIndex == idxS) uv.x = (float)(*maybeVal);
                                else if ((int)pIndex == idxT) uv.y = (float)(*maybeVal);
                                else if ((int)pIndex == idxR) { rgba.r = (float)(*maybeVal); gotColor = true; }
                                else if ((int)pIndex == idxG) rgba.g = (float)(*maybeVal);
                                else if ((int)pIndex == idxB) rgba.b = (float)(*maybeVal);
                                else if ((int)pIndex == idxA) rgba.a = (float)(*maybeVal);
                            }

                            outData.Positions[i] = pos;
                            if (hasNormals) outData.Normals[i] = nrm;

                            if (hasColors && gotColor)
                            {
                                float rf = rgba.r;
                                float gf = rgba.g;
                                float bf = rgba.b;
                                float af = hasAlpha ? rgba.a : 255.0f;
                                if (rf > 1.0f || gf > 1.0f || bf > 1.0f || af > 1.0f)
                                {
                                    rf /= 255.0f;
                                    gf /= 255.0f;
                                    bf /= 255.0f;
                                    af /= 255.0f;
                                }
                                outData.Aux[i] = glm::vec4(rf, gf, bf, af);
                            }

                            if (hasUVs)
                            {
                                outData.Aux[i].x = uv.x;
                                outData.Aux[i].y = uv.y;
                            }
                        }
                    }
                }
                else if (elem.Name == "face")
                {
                    if (faceElement.Count == 0)
                    {
                        outData.Topology = PrimitiveTopology::Points;
                        continue;
                    }

                    outData.Topology = PrimitiveTopology::Triangles;

                    if (!faceIndicesProp)
                    {
                        Core::Log::Error("PLY: face element has no usable list property for indices");
                        return false;
                    }

                    for (size_t f = 0; f < faceElement.Count; ++f)
                    {
                        std::vector<uint32_t> faceIndices;

                        for (const auto& p : faceElement.Properties)
                        {
                            if (&p == faceIndicesProp)
                            {
                                const auto maybeCountU64 = ReadScalarAsU64(file, p.ListCountType, fileIsLittle);
                                if (!maybeCountU64)
                                {
                                    Core::Log::Error("PLY: Failed reading face list count");
                                    return false;
                                }

                                const size_t count = static_cast<size_t>(*maybeCountU64);
                                faceIndices.resize(count);
                                for (size_t k = 0; k < count; ++k)
                                {
                                    const auto maybeIdx = ReadScalarAsU64(file, p.ListElementType, fileIsLittle);
                                    if (!maybeIdx)
                                    {
                                        Core::Log::Error("PLY: Failed reading face index {}", k);
                                        return false;
                                    }
                                    faceIndices[k] = static_cast<uint32_t>(*maybeIdx);
                                }
                            }
                            else
                            {
                                if (!SkipBinaryProperty(file, p, fileIsLittle))
                                {
                                    Core::Log::Error("PLY: Failed skipping face property '{}'", p.Name);
                                    return false;
                                }
                            }
                        }

                        if (faceIndices.size() >= 3)
                        {
                            for (size_t k = 1; k + 1 < faceIndices.size(); ++k)
                            {
                                outData.Indices.push_back(faceIndices[0]);
                                outData.Indices.push_back(faceIndices[k]);
                                outData.Indices.push_back(faceIndices[k + 1]);
                            }
                        }
                    }
                }
                else if (elem.Name == "tristrips")
                {
                    // Common in some scanners / VCGLIB / custom exporters.
                    // Typical header:
                    // element tristrips 1
                    // property list int int vertex_indices

                    const PlyProperty* stripProp = nullptr;
                    for (const auto& p : elem.Properties)
                    {
                        if (p.IsList)
                        {
                            stripProp = &p;
                            break;
                        }
                    }

                    if (!stripProp)
                    {
                        Core::Log::Error("PLY: tristrips element has no list property");
                        return false;
                    }

                    outData.Topology = PrimitiveTopology::Triangles;
                    connectivityElementName = "tristrips";

                    for (size_t s = 0; s < elem.Count; ++s)
                    {
                        std::vector<int64_t> strip;

                        for (const auto& p : elem.Properties)
                        {
                            if (&p == stripProp)
                            {
                                const auto maybeCountU64 = ReadScalarAsU64(file, p.ListCountType, fileIsLittle);
                                if (!maybeCountU64)
                                {
                                    Core::Log::Error("PLY: Failed reading tristrips list count");
                                    return false;
                                }

                                const size_t count = static_cast<size_t>(*maybeCountU64);
                                strip.resize(count);
                                for (size_t k = 0; k < count; ++k)
                                {
                                    const auto maybeIdxI64 = ReadScalarAsI64(file, p.ListElementType, fileIsLittle);
                                    if (!maybeIdxI64)
                                    {
                                        Core::Log::Error("PLY: Failed reading tristrips index {}", k);
                                        return false;
                                    }
                                    strip[k] = *maybeIdxI64;
                                }
                            }
                            else
                            {
                                if (!SkipBinaryProperty(file, p, fileIsLittle))
                                {
                                    Core::Log::Error("PLY: Failed skipping tristrips property '{}'", p.Name);
                                    return false;
                                }
                            }
                        }

                        AppendTriStripAsTriangles(strip, outData.Indices);
                    }
                }
                else
                {
                    // Unknown element: skip its payload safely.
                    for (size_t i = 0; i < elem.Count; ++i)
                    {
                        for (const auto& p : elem.Properties)
                        {
                            if (!SkipBinaryProperty(file, p, fileIsLittle))
                            {
                                Core::Log::Error("PLY: Failed skipping element '{}' property '{}'", elem.Name, p.Name);
                                return false;
                            }
                        }
                    }
                }
            }

            if (faceElement.Count == 0 && connectivityElementName.empty())
            {
                outData.Topology = PrimitiveTopology::Points;
            }
        }

        if (!hasNormals)
        {
            RecalculateNormals(outData);
        }
        if (!hasUVs)
        {
            GenerateUVs(outData);
        }
        return true;
    }

    static bool LoadXYZ(const std::string& path, GeometryCpuData& outData)
    {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        outData.Topology = PrimitiveTopology::Points;
        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            outData.Positions.push_back(p);
            outData.Normals.emplace_back(0, 1, 0);

            // Check for colors
            if (!ss.eof())
            {
                float r, g, b;
                ss >> r >> g >> b;
                outData.Aux.emplace_back(r, g, b, 1.0f);
            }
            else
            {
                outData.Aux.emplace_back(1.0f);
            }
        }

        GenerateUVs(outData);
        return true;
    }

    static bool LoadTGF(const std::string& path, GeometryCpuData& outData)
    {
        // Trivial Graph Format
        // NODEID Label
        // #
        // FromID ToID
        std::ifstream file(path);
        if (!file.is_open()) return false;

        outData.Topology = PrimitiveTopology::Lines;
        std::string line;
        bool parsingEdges = false;
        std::unordered_map<int, uint32_t> idMap;

        while (std::getline(file, line))
        {
            if (line.empty()) continue;
            if (line[0] == '#')
            {
                parsingEdges = true;
                continue;
            }

            std::stringstream ss(line);
            if (!parsingEdges)
            {
                int id;
                ss >> id;
                // TGF doesn't have coordinates usually, only topology.
                // We'll assign random positions or require a sidecar file?
                // For this research engine, let's assume valid lines contain ID X Y Z
                // EXTENSION: "Extended TGF" with coords
                glm::vec3 p{0.0f};
                if (!ss.eof()) ss >> p.x >> p.y >> p.z;

                auto idx = static_cast<uint32_t>(outData.Positions.size());
                idMap[id] = idx;
                outData.Positions.push_back(p);
                outData.Normals.emplace_back(0, 1, 0);
                outData.Aux.emplace_back(1);
            }
            else
            {
                int from, to;
                ss >> from >> to;
                if (idMap.count(from) && idMap.count(to))
                {
                    outData.Indices.push_back(idMap[from]);
                    outData.Indices.push_back(idMap[to]);
                }
            }
        }
        return true;
    }

    // --- GLTF Adapter ---
    static bool LoadGLTF(const std::string& fullPath, std::vector<GeometryCpuData>& outMeshes)
    {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;
        bool ret = fullPath.ends_with(".glb")
                       ? loader.LoadBinaryFromFile(&model, &err, &warn, fullPath)
                       : loader.LoadASCIIFromFile(&model, &err, &warn, fullPath);

        if (!warn.empty()) Core::Log::Warn("GLTF: {}", warn);
        if (!ret) return false;

        for (const auto& gltfMesh : model.meshes)
        {
            for (const auto& primitive : gltfMesh.primitives)
            {
                GeometryCpuData meshData;
                bool hasNormals = false;

                // 1. Topology Mapping
                switch (primitive.mode)
                {
                case TINYGLTF_MODE_POINTS: meshData.Topology = PrimitiveTopology::Points;
                    break;
                case TINYGLTF_MODE_LINE:
                case TINYGLTF_MODE_LINE_LOOP:
                case TINYGLTF_MODE_LINE_STRIP: meshData.Topology = PrimitiveTopology::Lines;
                    break;
                case TINYGLTF_MODE_TRIANGLES:
                case TINYGLTF_MODE_TRIANGLE_STRIP:
                case TINYGLTF_MODE_TRIANGLE_FAN: meshData.Topology = PrimitiveTopology::Triangles;
                    break;
                default: continue; // Unsupported topology
                }

                // 2. Attributes
                auto posIt = primitive.attributes.find("POSITION");
                if (posIt == primitive.attributes.end()) continue;

                const auto& posAccessor = model.accessors[posIt->second];
                const size_t vertexCount = posAccessor.count;

                // --- FAST PATH: Positions ---
                LoadBuffer<glm::vec3, glm::vec3>(meshData.Positions, model, posAccessor, vertexCount);

                // --- FAST PATH: Normals ---
                auto normIt = primitive.attributes.find("NORMAL");
                if (normIt != primitive.attributes.end())
                {
                    LoadBuffer<glm::vec3, glm::vec3>(meshData.Normals, model, model.accessors[normIt->second], vertexCount);
                    hasNormals = true;
                }
                else
                {
                    meshData.Normals.resize(vertexCount, glm::vec3(0, 1, 0));
                }

                // --- UVs (Aux) ---
                // Requires packing vec2 -> vec4, so we cannot bulk copy.
                meshData.Aux.resize(vertexCount, glm::vec4(0.0f));
                auto uvIt = primitive.attributes.find("TEXCOORD_0");
                if (uvIt != primitive.attributes.end())
                {
                    const auto& uvAccessor = model.accessors[uvIt->second];
                    const auto& uvView = model.bufferViews[uvAccessor.bufferView];
                    const auto& uvBuffer = model.buffers[uvView.buffer];
                    const uint8_t* uvData = &uvBuffer.data[uvView.byteOffset + uvAccessor.byteOffset];
                    const size_t uvStride = uvView.byteStride == 0 ? sizeof(glm::vec2) : uvView.byteStride;

                    for (size_t i = 0; i < vertexCount; ++i)
                    {
                        const glm::vec2* uv = reinterpret_cast<const glm::vec2*>(uvData + i * uvStride);
                        meshData.Aux[i] = glm::vec4(uv->x, uv->y, 0.0f, 0.0f);
                    }
                }

                // 3. Indices
                if (primitive.indices >= 0)
                {
                    const auto& accessor = model.accessors[primitive.indices];
                    const size_t indexCount = accessor.count;

                    switch (accessor.componentType)
                    {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        LoadBuffer<uint32_t, uint32_t>(meshData.Indices, model, accessor, indexCount);
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        LoadBuffer<uint32_t, uint16_t>(meshData.Indices, model, accessor, indexCount);
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        LoadBuffer<uint32_t, uint8_t>(meshData.Indices, model, accessor, indexCount);
                        break;
                    default:
                        Core::Log::Error("GLTF: Unsupported index component type: {}", accessor.componentType);
                        break;
                    }
                }

                if (!hasNormals)
                {
                    RecalculateNormals(meshData);
                }
                if (uvIt == primitive.attributes.end()) GenerateUVs(meshData);

                outMeshes.push_back(std::move(meshData));
            }
        }
        return true;
    }

    std::expected<ModelLoadResult, AssetError> ModelLoader::LoadAsync(
        std::shared_ptr<RHI::VulkanDevice> device,
        RHI::TransferManager& transferManager,
        GeometryPool& geometryStorage,
        const std::string& filepath)
    {
        if (!device)
            return std::unexpected(AssetError::InvalidData);

        std::string fullPath = Core::Filesystem::GetAssetPath(filepath);
        std::string ext = std::filesystem::path(fullPath).extension().string();
        for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        auto model = std::make_unique<Model>(geometryStorage, device);
        std::vector<GeometryCpuData> cpuMeshes;
        bool success = false;

        // Reuse the existing parsers
        if (ext == ".obj")
        {
            GeometryCpuData data;
            success = LoadOBJ(fullPath, data);
            if (success) cpuMeshes.push_back(std::move(data));
        }
        else if (ext == ".ply")
        {
            GeometryCpuData data;
            success = LoadPLY(fullPath, data);
            if (success) cpuMeshes.push_back(std::move(data));
        }
        else if (ext == ".xyz" || ext == ".pcd")
        {
            GeometryCpuData data;
            success = LoadXYZ(fullPath, data);
            if (success) cpuMeshes.push_back(std::move(data));
        }
        else if (ext == ".tgf")
        {
            GeometryCpuData data;
            success = LoadTGF(fullPath, data);
            if (success) cpuMeshes.push_back(std::move(data));
        }
        else if (ext == ".gltf" || ext == ".glb")
        {
            success = LoadGLTF(fullPath, cpuMeshes);
        }
        else
        {
            return std::unexpected(AssetError::UnsupportedFormat);
        }

        if (success && !cpuMeshes.empty())
        {
            RHI::TransferToken latestToken = {0};

            for (auto& meshData : cpuMeshes)
            {
                MeshSegment segment;
                segment.Name = "Mesh_" + std::to_string(model->Meshes.size());

                // 1. Physics / Collision Logic (CPU)
                segment.CollisionGeometry = std::make_shared<GeometryCollisionData>();
                auto aabbs = Geometry::Convert(meshData.Positions);
                segment.CollisionGeometry->LocalAABB = Geometry::Union(aabbs);

                std::vector<Geometry::AABB> primitiveBounds;

                // For point clouds, each point is its own primitive (no triangles).
                if (meshData.Topology == PrimitiveTopology::Points)
                {
                    primitiveBounds.reserve(meshData.Positions.size());
                    for (const auto& pos : meshData.Positions)
                    {
                        primitiveBounds.push_back(Geometry::AABB{pos, pos});
                    }
                }
                else if (meshData.Indices.empty())
                {
                    // Non-indexed triangle mesh
                    primitiveBounds.reserve(meshData.Positions.size() / 3);
                    for (size_t i = 0; i + 2 < meshData.Positions.size(); i += 3)
                    {
                        auto aabb = Geometry::AABB{meshData.Positions[i], meshData.Positions[i]};
                        aabb = Geometry::Union(aabb, meshData.Positions[i + 1]);
                        aabb = Geometry::Union(aabb, meshData.Positions[i + 2]);
                        primitiveBounds.push_back(aabb);
                    }
                }
                else
                {
                    // Indexed triangle mesh
                    primitiveBounds.reserve(meshData.Indices.size() / 3);
                    for (size_t i = 0; i + 2 < meshData.Indices.size(); i += 3)
                    {
                        const uint32_t i0 = meshData.Indices[i];
                        const uint32_t i1 = meshData.Indices[i + 1];
                        const uint32_t i2 = meshData.Indices[i + 2];
                        if (i0 < meshData.Positions.size() && i1 < meshData.Positions.size() && i2 < meshData.Positions.size())
                        {
                            auto aabb = Geometry::AABB{meshData.Positions[i0], meshData.Positions[i0]};
                            aabb = Geometry::Union(aabb, meshData.Positions[i1]);
                            aabb = Geometry::Union(aabb, meshData.Positions[i2]);
                            primitiveBounds.push_back(aabb);
                        }
                    }
                }

                if (!segment.CollisionGeometry->LocalOctree.Build(primitiveBounds, Geometry::Octree::SplitPolicy{}, 16, 8))
                {
                    Core::Log::Warn("Failed to build collision octree for mesh segment");
                }

                segment.CollisionGeometry->Positions = std::move(meshData.Positions);
                segment.CollisionGeometry->Indices = std::move(meshData.Indices);

                // 2. Upload to GPU (Async)
                GeometryUploadRequest uploadReq;
                uploadReq.Positions = segment.CollisionGeometry->Positions;
                uploadReq.Indices = segment.CollisionGeometry->Indices;
                uploadReq.Normals = meshData.Normals;
                uploadReq.Aux = meshData.Aux;
                uploadReq.Topology = meshData.Topology;

                auto [gpuData, token] = GeometryGpuData::CreateAsync(device, transferManager, uploadReq);
                latestToken = token; // Tokens are monotonic; keeping the last one is enough

                segment.Handle = geometryStorage.Add(std::move(gpuData));
                model->Meshes.emplace_back(std::make_shared<MeshSegment>(segment));
            }

#if defined(INTRINSIC_MODELLOADER_VERBOSE)
            Core::Log::Info("Loaded {} ({} submeshes)", filepath, model->Size());
#endif
            return ModelLoadResult{ std::move(model), latestToken };
        }

        return std::unexpected(AssetError::InvalidData);
    }
}
