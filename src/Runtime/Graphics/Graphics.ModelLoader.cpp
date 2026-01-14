module;
// TinyGLTF headers
#include <charconv>
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <algorithm>
#include <expected>
#include <cctype>
#include <unordered_map>
#include <variant>

module Graphics:ModelLoader.Impl;
import :ModelLoader;
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
        Core::Log::Info("Recalculated normals for vertices.");
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
            Core::Log::Info("Generated Planar UVs for {} vertices (Axis: {})", mesh.Positions.size(), flatAxis);
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

        [[nodiscard]] static bool IsColorByteBased(PlyScalarType t) { return t == PlyScalarType::UInt8 || t == PlyScalarType::Int8; }
    }

    static bool LoadPLY(const std::string& path, GeometryCpuData& outData)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        std::string line;
        PlyFormat format = PlyFormat::Ascii;

        PlyElement vertexElement{};
        PlyElement faceElement{};
        PlyElement* currentElement = nullptr;

        // Quick lookup of vertex property indices
        int idxX = -1, idxY = -1, idxZ = -1;
        int idxNX = -1, idxNY = -1, idxNZ = -1;
        int idxR = -1, idxG = -1, idxB = -1;
        int idxS = -1, idxT = -1;

        bool headerEnded = false;

        // 1. Header Parse
        while (std::getline(file, line))
        {
            if (line == "end_header")
            {
                headerEnded = true;
                break;
            }

            // skip comments/empty
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string token;
            ss >> token;
            if (token.empty()) continue;

            if (token == "format")
            {
                std::string fmt;
                ss >> fmt;
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

                if (type == "vertex")
                {
                    vertexElement = PlyElement{};
                    vertexElement.Name = "vertex";
                    vertexElement.Count = count;
                    currentElement = &vertexElement;

                    // reset indices
                    idxX = idxY = idxZ = -1;
                    idxNX = idxNY = idxNZ = -1;
                    idxR = idxG = idxB = -1;
                    idxS = idxT = -1;
                }
                else if (type == "face")
                {
                    faceElement = PlyElement{};
                    faceElement.Name = "face";
                    faceElement.Count = count;
                    currentElement = &faceElement;
                }
                else
                {
                    // Still parse properties to skip header correctly, but we ignore body for unknown elements.
                    currentElement = nullptr;
                }
            }
            else if (token == "property")
            {
                if (!currentElement) continue;

                std::string typeOrList;
                ss >> typeOrList;

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

                    prop.Name = name;
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
                    prop.Name = name;
                    prop.IsList = false;
                    prop.ScalarType = *scalarTy;
                }

                const int propIndex = static_cast<int>(currentElement->Properties.size());
                currentElement->Properties.push_back(prop);

                if (currentElement == &vertexElement)
                {
                    const std::string& n = currentElement->Properties.back().Name;
                    if (n == "x") idxX = propIndex;
                    else if (n == "y") idxY = propIndex;
                    else if (n == "z") idxZ = propIndex;
                    else if (n == "nx") idxNX = propIndex;
                    else if (n == "ny") idxNY = propIndex;
                    else if (n == "nz") idxNZ = propIndex;
                    else if (n == "red" || n == "r") idxR = propIndex;
                    else if (n == "green" || n == "g") idxG = propIndex;
                    else if (n == "blue" || n == "b") idxB = propIndex;
                    else if (n == "s" || n == "u" || n == "texture_u") idxS = propIndex;
                    else if (n == "t" || n == "v" || n == "texture_v") idxT = propIndex;
                }
            }
        }

        if (!headerEnded) return false;
        if (vertexElement.Count == 0)
        {
            Core::Log::Error("PLY: Missing vertex element or vertex count == 0");
            return false;
        }

        // Setup binary stride/offsets for vertex fixed-size properties.
        if (format != PlyFormat::Ascii)
        {
            size_t offset = 0;
            for (auto& p : vertexElement.Properties)
            {
                if (p.IsList)
                {
                    Core::Log::Error("PLY: vertex element contains 'list' property '{}', unsupported in binary loader", p.Name);
                    return false;
                }
                p.ByteOffset = offset;
                offset += PlyScalarSizeBytes(p.ScalarType);
            }
            vertexElement.BinaryStrideBytes = offset;

            // Face element: we only support a single list property (vertex_indices / vertex_index)
            size_t listProps = 0;
            for (const auto& p : faceElement.Properties)
            {
                if (p.IsList) listProps++;
            }
            if (faceElement.Count > 0 && listProps == 0)
            {
                Core::Log::Error("PLY: face element has no list property; cannot read indices");
                return false;
            }
        }

        const bool hasNormals = (idxNX >= 0 && idxNY >= 0 && idxNZ >= 0);
        const bool hasUVs = (idxS >= 0 && idxT >= 0);
        const bool hasColors = (idxR >= 0 && idxG >= 0 && idxB >= 0);

        outData.Positions.resize(vertexElement.Count);
        outData.Normals.resize(vertexElement.Count, glm::vec3(0, 1, 0));
        outData.Aux.resize(vertexElement.Count, glm::vec4(1));

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
                    if (r > 1.0f || g > 1.0f || b > 1.0f)
                    {
                        r /= 255.0f;
                        g /= 255.0f;
                        b /= 255.0f;
                    }
                    outData.Aux[i] = glm::vec4(r, g, b, 1.0f);
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
            const bool fileIsLittle = (format == PlyFormat::BinaryLittleEndian);

            // Read all vertex bytes in one go (fast path).
            {
                const size_t totalBytes = vertexElement.Count * vertexElement.BinaryStrideBytes;
                std::vector<std::byte> vertexBlob(totalBytes);
                file.read(reinterpret_cast<char*>(vertexBlob.data()), static_cast<std::streamsize>(vertexBlob.size()));
                if (!file)
                {
                    Core::Log::Error("PLY: Failed to read binary vertex blob ({} bytes)", totalBytes);
                    return false;
                }

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

                // Required: x/y/z
                if (idxX < 0 || idxY < 0 || idxZ < 0)
                {
                    Core::Log::Error("PLY: Missing required x/y/z properties");
                    return false;
                }

                const PlyProperty& px = vertexElement.Properties[(size_t)idxX];
                const PlyProperty& py = vertexElement.Properties[(size_t)idxY];
                const PlyProperty& pz = vertexElement.Properties[(size_t)idxZ];

                const PlyProperty* pnx = (idxNX >= 0) ? &vertexElement.Properties[(size_t)idxNX] : nullptr;
                const PlyProperty* pny = (idxNY >= 0) ? &vertexElement.Properties[(size_t)idxNY] : nullptr;
                const PlyProperty* pnz = (idxNZ >= 0) ? &vertexElement.Properties[(size_t)idxNZ] : nullptr;

                const PlyProperty* pr = (idxR >= 0) ? &vertexElement.Properties[(size_t)idxR] : nullptr;
                const PlyProperty* pg = (idxG >= 0) ? &vertexElement.Properties[(size_t)idxG] : nullptr;
                const PlyProperty* pb = (idxB >= 0) ? &vertexElement.Properties[(size_t)idxB] : nullptr;

                const PlyProperty* ps = (idxS >= 0) ? &vertexElement.Properties[(size_t)idxS] : nullptr;
                const PlyProperty* pt = (idxT >= 0) ? &vertexElement.Properties[(size_t)idxT] : nullptr;

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

                        // Heuristic: if byte-based or values > 1, normalize.
                        const bool byteBased = IsColorByteBased(pr->ScalarType) && IsColorByteBased(pg->ScalarType) && IsColorByteBased(pb->ScalarType);
                        float rf = (float)rd;
                        float gf = (float)gd;
                        float bf = (float)bd;
                        if (byteBased || rf > 1.0f || gf > 1.0f || bf > 1.0f)
                        {
                            rf /= 255.0f;
                            gf /= 255.0f;
                            bf /= 255.0f;
                        }
                        outData.Aux[i] = glm::vec4(rf, gf, bf, 1.0f);
                    }

                    if (ps && pt)
                    {
                        outData.Aux[i].x = (float)readFromBlobAsDouble(v, *ps);
                        outData.Aux[i].y = (float)readFromBlobAsDouble(v, *pt);
                    }
                }
            }

            // Faces come after the vertex blob in the file.
            if (faceElement.Count > 0)
            {
                outData.Topology = PrimitiveTopology::Triangles;

                // Find indices list property
                const PlyProperty* indicesProp = nullptr;
                for (const auto& p : faceElement.Properties)
                {
                    if (p.IsList && (p.Name == "vertex_indices" || p.Name == "vertex_index" || p.Name == "indices"))
                    {
                        indicesProp = &p;
                        break;
                    }
                }
                if (!indicesProp)
                {
                    // accept first list property
                    for (const auto& p : faceElement.Properties)
                    {
                        if (p.IsList)
                        {
                            indicesProp = &p;
                            break;
                        }
                    }
                }

                if (!indicesProp)
                {
                    Core::Log::Error("PLY: face element has no list property for indices");
                    return false;
                }

                for (size_t f = 0; f < faceElement.Count; ++f)
                {
                    const auto maybeCountU64 = ReadScalarAsU64(file, indicesProp->ListCountType, fileIsLittle);
                    if (!maybeCountU64)
                    {
                        Core::Log::Error("PLY: Failed reading face list count");
                        return false;
                    }

                    const size_t count = static_cast<size_t>(*maybeCountU64);
                    if (count < 2)
                    {
                        // still need to skip any malformed data
                        for (size_t k = 0; k < count; ++k)
                        {
                            (void)ReadScalarAsU64(file, indicesProp->ListElementType, fileIsLittle);
                        }
                        continue;
                    }

                    std::vector<uint32_t> faceIndices(count);
                    for (size_t k = 0; k < count; ++k)
                    {
                        const auto maybeIdx = ReadScalarAsU64(file, indicesProp->ListElementType, fileIsLittle);
                        if (!maybeIdx)
                        {
                            Core::Log::Error("PLY: Failed reading face index {}", k);
                            return false;
                        }
                        faceIndices[k] = static_cast<uint32_t>(*maybeIdx);
                    }

                    // triangulate fan (works for triangles/quads/n-gons)
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

                // 2. Accessors Setup
                const float* positionBuffer = nullptr;
                const float* normalsBuffer = nullptr;
                const float* texCoordsBuffer = nullptr;
                size_t vertexCount = 0;

                auto GetBuffer = [&](const char* attrName) -> const float*
                {
                    if (primitive.attributes.find(attrName) == primitive.attributes.end()) return nullptr;
                    const auto& accessor = model.accessors[primitive.attributes.at(attrName)];
                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buffer = model.buffers[view.buffer];

                    vertexCount = accessor.count; // Set count based on Position (primary attribute)
                    return reinterpret_cast<const float*>(&buffer.data[view.byteOffset + accessor.byteOffset]);
                };

                positionBuffer = GetBuffer("POSITION");
                normalsBuffer = GetBuffer("NORMAL");
                texCoordsBuffer = GetBuffer("TEXCOORD_0");

                if (!positionBuffer || vertexCount == 0) continue;

                // 3. Populate Vectors (SoA)
                meshData.Positions.resize(vertexCount);
                meshData.Normals.resize(vertexCount);
                meshData.Aux.resize(vertexCount);

                for (size_t i = 0; i < vertexCount; i++)
                {
                    meshData.Positions[i] = glm::make_vec3(&positionBuffer[i * 3]);

                    if (normalsBuffer)
                    {
                        meshData.Normals[i] = glm::make_vec3(&normalsBuffer[i * 3]);
                    }
                    else
                    {
                        meshData.Normals[i] = glm::vec3(0, 1, 0);
                    }

                    if (texCoordsBuffer)
                    {
                        glm::vec2 uv = glm::make_vec2(&texCoordsBuffer[i * 2]);
                        meshData.Aux[i] = glm::vec4(uv.x, uv.y, 0.0f, 0.0f);
                    }
                    else
                    {
                        meshData.Aux[i] = glm::vec4(0.0f);
                    }
                }

                // 4. Indices
                if (primitive.indices >= 0)
                {
                    const auto& accessor = model.accessors[primitive.indices];
                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buffer = model.buffers[view.buffer];
                    const uint8_t* data = &buffer.data[view.byteOffset + accessor.byteOffset];

                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    {
                        const auto* buf = reinterpret_cast<const uint32_t*>(data);
                        meshData.Indices.assign(buf, buf + accessor.count);
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        const auto* buf = reinterpret_cast<const uint16_t*>(data);
                        for (size_t i = 0; i < accessor.count; ++i) meshData.Indices.push_back(buf[i]);
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        const auto* buf = reinterpret_cast<const uint8_t*>(data);
                        for (size_t i = 0; i < accessor.count; ++i) meshData.Indices.push_back(buf[i]);
                    }
                }

                hasNormals = normalsBuffer != nullptr;

                if (!hasNormals)
                {
                    RecalculateNormals(meshData);
                }
                if (!texCoordsBuffer) GenerateUVs(meshData);

                outMeshes.push_back(std::move(meshData));
            }
        }
        return true;
    }

    std::expected<ModelLoadResult, AssetError> ModelLoader::LoadAsync(
        std::shared_ptr<RHI::VulkanDevice> device,
        RHI::TransferManager& transferManager,
        GeometryStorage& geometryStorage,
        const std::string& filepath)
    {
        if (!device)
            return std::unexpected(AssetError::InvalidData);

        std::string fullPath = Core::Filesystem::GetAssetPath(filepath);
        std::string ext = std::filesystem::path(fullPath).extension().string();
        for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        auto model = std::make_shared<Model>(geometryStorage, device);
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
                if (meshData.Indices.empty())
                {
                    primitiveBounds.reserve(meshData.Positions.size() / 3);
                    for (size_t i = 0; i < meshData.Positions.size(); i += 3)
                    {
                        auto aabb = Geometry::AABB{meshData.Positions[i], meshData.Positions[i]};
                        aabb = Geometry::Union(aabb, meshData.Positions[i + 1]);
                        aabb = Geometry::Union(aabb, meshData.Positions[i + 2]);
                        primitiveBounds.push_back(aabb);
                    }
                }
                else
                {
                    primitiveBounds.reserve(meshData.Indices.size() / 3);
                    for (size_t i = 0; i < meshData.Indices.size(); i += 3)
                    {
                        const uint32_t i0 = meshData.Indices[i];
                        const uint32_t i1 = meshData.Indices[i + 1];
                        const uint32_t i2 = meshData.Indices[i + 2];
                        auto aabb = Geometry::AABB{meshData.Positions[i0], meshData.Positions[i0]};
                        aabb = Geometry::Union(aabb, meshData.Positions[i1]);
                        aabb = Geometry::Union(aabb, meshData.Positions[i2]);
                        primitiveBounds.push_back(aabb);
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

            Core::Log::Info("Loaded {} ({} submeshes)", filepath, model->Size());
            return ModelLoadResult{ model, latestToken };
        }

        return std::unexpected(AssetError::InvalidData);
    }
}
