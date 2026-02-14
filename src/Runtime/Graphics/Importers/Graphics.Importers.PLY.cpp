module;
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

module Graphics:Importers.PLY.Impl;
import :Importers.PLY;
import :IORegistry;
import :Geometry;
import :AssetErrors;
import Core.Logging;
import Geometry;

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".ply" };

        enum class PlyFormat { Ascii, BinaryLittleEndian, BinaryBigEndian };

        enum class PlyScalarType { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64 };

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
            PlyScalarType ScalarType = PlyScalarType::Float32;
            PlyScalarType ListCountType = PlyScalarType::UInt8;
            PlyScalarType ListElementType = PlyScalarType::UInt32;
            size_t ByteOffset = 0;
        };

        struct PlyElement
        {
            std::string Name;
            size_t Count = 0;
            std::vector<PlyProperty> Properties;
            size_t BinaryStrideBytes = 0;
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

        // Byte-buffer reader (replaces ifstream-based ReadRaw)
        struct ByteReader
        {
            const std::byte* Data = nullptr;
            size_t Size = 0;
            size_t Pos = 0;

            [[nodiscard]] bool HasBytes(size_t n) const { return Pos + n <= Size; }

            template <typename T>
            [[nodiscard]] bool ReadRaw(T& out)
            {
                if (!HasBytes(sizeof(T))) return false;
                std::memcpy(&out, Data + Pos, sizeof(T));
                Pos += sizeof(T);
                return true;
            }

            [[nodiscard]] bool Skip(size_t bytes)
            {
                if (!HasBytes(bytes)) return false;
                Pos += bytes;
                return true;
            }
        };

        [[nodiscard]] static std::optional<uint64_t> ReadScalarAsU64(ByteReader& reader, PlyScalarType type, bool fileIsLittleEndian)
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
            case PlyScalarType::Int8:   { int8_t v{};   if (!reader.ReadRaw(v)) return std::nullopt; return static_cast<uint64_t>(static_cast<int64_t>(v)); }
            case PlyScalarType::UInt8:  { uint8_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; return static_cast<uint64_t>(v); }
            case PlyScalarType::Int16:  { int16_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return static_cast<uint64_t>(static_cast<int64_t>(v)); }
            case PlyScalarType::UInt16: { uint16_t v{}; if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return static_cast<uint64_t>(v); }
            case PlyScalarType::Int32:  { int32_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return static_cast<uint64_t>(static_cast<int64_t>(v)); }
            case PlyScalarType::UInt32: { uint32_t v{}; if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return static_cast<uint64_t>(v); }
            case PlyScalarType::Float32:{ float v{};    if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return static_cast<uint64_t>(static_cast<double>(v)); }
            case PlyScalarType::Float64:{ double v{};   if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return static_cast<uint64_t>(v); }
            }
            return std::nullopt;
        }

        [[nodiscard]] static std::optional<int64_t> ReadScalarAsI64(ByteReader& reader, PlyScalarType type, bool fileIsLittleEndian)
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
            case PlyScalarType::Int8:   { int8_t v{};   if (!reader.ReadRaw(v)) return std::nullopt; return (int64_t)v; }
            case PlyScalarType::UInt8:  { uint8_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; return (int64_t)v; }
            case PlyScalarType::Int16:  { int16_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return (int64_t)v; }
            case PlyScalarType::UInt16: { uint16_t v{}; if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return (int64_t)v; }
            case PlyScalarType::Int32:  { int32_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return (int64_t)v; }
            case PlyScalarType::UInt32: { uint32_t v{}; if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwap(v); return (int64_t)v; }
            case PlyScalarType::Float32:
            case PlyScalarType::Float64:
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsColorByteBased(PlyScalarType t)
        {
            return t == PlyScalarType::UInt8 || t == PlyScalarType::Int8;
        }

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

        [[nodiscard]] bool SkipBinaryProperty(ByteReader& reader, const PlyProperty& prop, bool fileIsLittleEndian)
        {
            if (!prop.IsList)
                return reader.Skip(PlyScalarSizeBytes(prop.ScalarType));

            const auto maybeCount = ReadScalarAsU64(reader, prop.ListCountType, fileIsLittleEndian);
            if (!maybeCount) return false;
            const size_t count = static_cast<size_t>(*maybeCount);
            const size_t elemBytes = PlyScalarSizeBytes(prop.ListElementType);
            return reader.Skip(count * elemBytes);
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
                if (score > bestScore) { bestScore = score; best = &p; }
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
                if (idx < 0) { state.Reset(); continue; }
                if (state.Prev[0] < 0) { state.Prev[0] = idx; continue; }
                if (state.Prev[1] < 0) { state.Prev[1] = idx; continue; }

                const int64_t a = state.Prev[0], b = state.Prev[1], c = idx;
                if (a != b && b != c && a != c)
                {
                    if (!state.Parity) { outIndices.push_back((uint32_t)a); outIndices.push_back((uint32_t)b); outIndices.push_back((uint32_t)c); }
                    else { outIndices.push_back((uint32_t)b); outIndices.push_back((uint32_t)a); outIndices.push_back((uint32_t)c); }
                }
                state.Prev[0] = state.Prev[1];
                state.Prev[1] = idx;
                state.Parity = !state.Parity;
            }
        }

        static float ParseFloat(std::string_view sv)
        {
            float val = 0.0f;
            std::from_chars(sv.data(), sv.data() + sv.size(), val);
            return val;
        }

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
    }

    std::span<const std::string_view> PLYLoader::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<ImportResult, AssetError> PLYLoader::Load(
        std::span<const std::byte> data,
        const LoadContext& /*ctx*/)
    {
        // Wrap bytes into a string_view for header parsing, ByteReader for binary body
        std::string_view fullText(reinterpret_cast<const char*>(data.data()), data.size());

        PlyFormat format = PlyFormat::Ascii;
        PlyElement vertexElement{};
        PlyElement faceElement{};
        std::vector<PlyElement> elementsInOrder;
        PlyElement* currentElement = nullptr;

        int idxX = -1, idxY = -1, idxZ = -1;
        int idxNX = -1, idxNY = -1, idxNZ = -1;
        int idxR = -1, idxG = -1, idxB = -1;
        int idxA = -1;
        int idxS = -1, idxT = -1;

        bool headerEnded = false;
        size_t headerEndPos = 0;

        // 1. Parse header line-by-line from string_view
        {
            std::istringstream headerStream{std::string{fullText}};
            std::string line;

            while (std::getline(headerStream, line))
            {
                line = TrimRight(line);

                if (line == "end_header")
                {
                    headerEnded = true;
                    headerEndPos = static_cast<size_t>(headerStream.tellg());
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
                    else return std::unexpected(AssetError::DecodeFailed);
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
                        if (!countTy || !elemTy) return std::unexpected(AssetError::DecodeFailed);
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
                        if (!scalarTy) return std::unexpected(AssetError::DecodeFailed);
                        prop.Name = ToLower(name);
                        prop.IsList = false;
                        prop.ScalarType = *scalarTy;
                    }

                    currentElement->Properties.push_back(prop);

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
        }

        if (!headerEnded) return std::unexpected(AssetError::DecodeFailed);
        if (vertexElement.Count == 0) return std::unexpected(AssetError::InvalidData);

        const bool hasNormals = (idxNX >= 0 && idxNY >= 0 && idxNZ >= 0);
        const bool hasUVs = (idxS >= 0 && idxT >= 0);
        const bool hasColors = (idxR >= 0 && idxG >= 0 && idxB >= 0);
        const bool hasAlpha = (idxA >= 0);

        GeometryCpuData outData;
        outData.Positions.resize(vertexElement.Count);
        outData.Normals.resize(vertexElement.Count, glm::vec3(0, 1, 0));
        outData.Aux.resize(vertexElement.Count, glm::vec4(1));

        const bool isBinary = (format != PlyFormat::Ascii);
        const bool fileIsLittle = (format == PlyFormat::BinaryLittleEndian);

        // For binary, skip any trailing linebreaks after "end_header\n"
        size_t bodyStart = headerEndPos;
        if (isBinary)
        {
            while (bodyStart < data.size())
            {
                char c = static_cast<char>(data[bodyStart]);
                if (c == '\r' || c == '\n') { bodyStart++; continue; }
                break;
            }
        }

        // Setup binary stride/offsets for vertex
        bool vertexHasList = false;
        if (isBinary)
        {
            size_t offset = 0;
            for (auto& p : vertexElement.Properties)
            {
                if (p.IsList) { vertexHasList = true; break; }
                p.ByteOffset = offset;
                offset += PlyScalarSizeBytes(p.ScalarType);
            }
            vertexElement.BinaryStrideBytes = vertexHasList ? 0 : offset;
        }

        std::string connectivityElementName;

        // 2. Body Parse
        if (format == PlyFormat::Ascii)
        {
            // Parse from the remaining text after the header
            std::string_view bodyText = fullText.substr(headerEndPos);
            std::istringstream bodyStream{std::string{bodyText}};
            std::string line;

            // Vertex lines
            for (size_t i = 0; i < vertexElement.Count; ++i)
            {
                std::getline(bodyStream, line);
                std::vector<std::string_view> tokens = Split(line, ' ');
                std::erase_if(tokens, [](std::string_view s) { return s.empty(); });

                if (idxX >= 0 && (size_t)idxX < tokens.size()) outData.Positions[i].x = ParseFloat(tokens[(size_t)idxX]);
                if (idxY >= 0 && (size_t)idxY < tokens.size()) outData.Positions[i].y = ParseFloat(tokens[(size_t)idxY]);
                if (idxZ >= 0 && (size_t)idxZ < tokens.size()) outData.Positions[i].z = ParseFloat(tokens[(size_t)idxZ]);

                if (hasNormals)
                {
                    if ((size_t)idxNX < tokens.size()) outData.Normals[i].x = ParseFloat(tokens[(size_t)idxNX]);
                    if ((size_t)idxNY < tokens.size()) outData.Normals[i].y = ParseFloat(tokens[(size_t)idxNY]);
                    if ((size_t)idxNZ < tokens.size()) outData.Normals[i].z = ParseFloat(tokens[(size_t)idxNZ]);
                }

                if (hasColors)
                {
                    float r = ParseFloat(tokens[(size_t)idxR]);
                    float g = ParseFloat(tokens[(size_t)idxG]);
                    float b = ParseFloat(tokens[(size_t)idxB]);
                    float a = hasAlpha ? ParseFloat(tokens[(size_t)idxA]) : 255.0f;
                    if (r > 1.0f || g > 1.0f || b > 1.0f || a > 1.0f)
                    { r /= 255.0f; g /= 255.0f; b /= 255.0f; a /= 255.0f; }
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
                    std::getline(bodyStream, line);
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
            // Binary parsing using ByteReader
            ByteReader reader;
            reader.Data = data.data();
            reader.Size = data.size();
            reader.Pos = bodyStart;

            auto readFromBlobAsDouble = [&](const std::byte* base, const PlyProperty& p) -> double
            {
                const bool needSwap = (HostIsLittleEndian() != fileIsLittle);
                const std::byte* ptr = base + p.ByteOffset;

                switch (p.ScalarType)
                {
                case PlyScalarType::Int8:    { int8_t v;   std::memcpy(&v, ptr, 1); return (double)v; }
                case PlyScalarType::UInt8:   { uint8_t v;  std::memcpy(&v, ptr, 1); return (double)v; }
                case PlyScalarType::Int16:   { int16_t v;  std::memcpy(&v, ptr, 2); if (needSwap) v = ByteSwap(v); return (double)v; }
                case PlyScalarType::UInt16:  { uint16_t v; std::memcpy(&v, ptr, 2); if (needSwap) v = ByteSwap(v); return (double)v; }
                case PlyScalarType::Int32:   { int32_t v;  std::memcpy(&v, ptr, 4); if (needSwap) v = ByteSwap(v); return (double)v; }
                case PlyScalarType::UInt32:  { uint32_t v; std::memcpy(&v, ptr, 4); if (needSwap) v = ByteSwap(v); return (double)v; }
                case PlyScalarType::Float32: { float v;    std::memcpy(&v, ptr, 4); if (needSwap) v = ByteSwap(v); return (double)v; }
                case PlyScalarType::Float64: { double v;   std::memcpy(&v, ptr, 8); if (needSwap) v = ByteSwap(v); return v; }
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

            for (const PlyElement& elem : elementsInOrder)
            {
                if (elem.Count == 0) continue;

                if (elem.Name == "vertex")
                {
                    if (!vertexHasList && vertexElement.BinaryStrideBytes > 0)
                    {
                        // Fast path: read vertex blob then decode known offsets
                        const size_t totalBytes = vertexElement.Count * vertexElement.BinaryStrideBytes;
                        if (!reader.HasBytes(totalBytes))
                            return std::unexpected(AssetError::DecodeFailed);

                        const std::byte* base = reader.Data + reader.Pos;
                        reader.Pos += totalBytes;

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
                                float rf = (float)rd, gf = (float)gd, bf = (float)bd, af = (float)ad;
                                if (byteBased || rf > 1.0f || gf > 1.0f || bf > 1.0f || af > 1.0f)
                                { rf /= 255.0f; gf /= 255.0f; bf /= 255.0f; af /= 255.0f; }
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
                        // Slow path: property-by-property per vertex
                        for (size_t i = 0; i < vertexElement.Count; ++i)
                        {
                            glm::vec3 pos{0};
                            glm::vec3 nrm{0, 1, 0};
                            glm::vec2 uv{0};
                            glm::vec4 rgba{1, 1, 1, 1};
                            bool gotColor = false;

                            for (size_t pIndex = 0; pIndex < vertexElement.Properties.size(); ++pIndex)
                            {
                                const PlyProperty& p = vertexElement.Properties[pIndex];
                                if (p.IsList) { if (!SkipBinaryProperty(reader, p, fileIsLittle)) return std::unexpected(AssetError::DecodeFailed); continue; }

                                auto readScalar = [&]() -> std::optional<double>
                                {
                                    auto maybeSwapInner = [&](auto v)
                                    {
                                        using V = decltype(v);
                                        if constexpr (sizeof(V) == 1) return v;
                                        const bool needSwap = (HostIsLittleEndian() != fileIsLittle);
                                        return needSwap ? ByteSwap(v) : v;
                                    };

                                    switch (p.ScalarType)
                                    {
                                    case PlyScalarType::Int8:    { int8_t v{};   if (!reader.ReadRaw(v)) return std::nullopt; return (double)v; }
                                    case PlyScalarType::UInt8:   { uint8_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; return (double)v; }
                                    case PlyScalarType::Int16:   { int16_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwapInner(v); return (double)v; }
                                    case PlyScalarType::UInt16:  { uint16_t v{}; if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwapInner(v); return (double)v; }
                                    case PlyScalarType::Int32:   { int32_t v{};  if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwapInner(v); return (double)v; }
                                    case PlyScalarType::UInt32:  { uint32_t v{}; if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwapInner(v); return (double)v; }
                                    case PlyScalarType::Float32: { float v{};    if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwapInner(v); return (double)v; }
                                    case PlyScalarType::Float64: { double v{};   if (!reader.ReadRaw(v)) return std::nullopt; v = maybeSwapInner(v); return v; }
                                    }
                                    return std::nullopt;
                                };

                                const auto maybeVal = readScalar();
                                if (!maybeVal) return std::unexpected(AssetError::DecodeFailed);

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
                                float rf = rgba.r, gf = rgba.g, bf = rgba.b;
                                float af = hasAlpha ? rgba.a : 255.0f;
                                if (rf > 1.0f || gf > 1.0f || bf > 1.0f || af > 1.0f)
                                { rf /= 255.0f; gf /= 255.0f; bf /= 255.0f; af /= 255.0f; }
                                outData.Aux[i] = glm::vec4(rf, gf, bf, af);
                            }
                            if (hasUVs) { outData.Aux[i].x = uv.x; outData.Aux[i].y = uv.y; }
                        }
                    }
                }
                else if (elem.Name == "face")
                {
                    if (faceElement.Count == 0) { outData.Topology = PrimitiveTopology::Points; continue; }
                    outData.Topology = PrimitiveTopology::Triangles;
                    if (!faceIndicesProp) return std::unexpected(AssetError::DecodeFailed);

                    for (size_t f = 0; f < faceElement.Count; ++f)
                    {
                        std::vector<uint32_t> faceIndices;
                        for (const auto& p : faceElement.Properties)
                        {
                            if (&p == faceIndicesProp)
                            {
                                const auto maybeCountU64 = ReadScalarAsU64(reader, p.ListCountType, fileIsLittle);
                                if (!maybeCountU64) return std::unexpected(AssetError::DecodeFailed);
                                const size_t count = static_cast<size_t>(*maybeCountU64);
                                faceIndices.resize(count);
                                for (size_t k = 0; k < count; ++k)
                                {
                                    const auto maybeIdx = ReadScalarAsU64(reader, p.ListElementType, fileIsLittle);
                                    if (!maybeIdx) return std::unexpected(AssetError::DecodeFailed);
                                    faceIndices[k] = static_cast<uint32_t>(*maybeIdx);
                                }
                            }
                            else
                            {
                                if (!SkipBinaryProperty(reader, p, fileIsLittle))
                                    return std::unexpected(AssetError::DecodeFailed);
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
                    const PlyProperty* stripProp = nullptr;
                    for (const auto& p : elem.Properties)
                    {
                        if (p.IsList) { stripProp = &p; break; }
                    }
                    if (!stripProp) return std::unexpected(AssetError::DecodeFailed);

                    outData.Topology = PrimitiveTopology::Triangles;
                    connectivityElementName = "tristrips";

                    for (size_t s = 0; s < elem.Count; ++s)
                    {
                        std::vector<int64_t> strip;
                        for (const auto& p : elem.Properties)
                        {
                            if (&p == stripProp)
                            {
                                const auto maybeCountU64 = ReadScalarAsU64(reader, p.ListCountType, fileIsLittle);
                                if (!maybeCountU64) return std::unexpected(AssetError::DecodeFailed);
                                const size_t count = static_cast<size_t>(*maybeCountU64);
                                strip.resize(count);
                                for (size_t k = 0; k < count; ++k)
                                {
                                    const auto maybeIdxI64 = ReadScalarAsI64(reader, p.ListElementType, fileIsLittle);
                                    if (!maybeIdxI64) return std::unexpected(AssetError::DecodeFailed);
                                    strip[k] = *maybeIdxI64;
                                }
                            }
                            else
                            {
                                if (!SkipBinaryProperty(reader, p, fileIsLittle))
                                    return std::unexpected(AssetError::DecodeFailed);
                            }
                        }
                        AppendTriStripAsTriangles(strip, outData.Indices);
                    }
                }
                else
                {
                    // Unknown element: skip
                    for (size_t i = 0; i < elem.Count; ++i)
                    {
                        for (const auto& p : elem.Properties)
                        {
                            if (!SkipBinaryProperty(reader, p, fileIsLittle))
                                return std::unexpected(AssetError::DecodeFailed);
                        }
                    }
                }
            }

            if (faceElement.Count == 0 && connectivityElementName.empty())
                outData.Topology = PrimitiveTopology::Points;
        }

        if (!hasNormals && outData.Topology == PrimitiveTopology::Triangles)
        {
            Geometry::MeshUtils::CalculateNormals(outData.Positions, outData.Indices, outData.Normals);
        }
        if (!hasUVs)
        {
            Geometry::MeshUtils::GenerateUVs(outData.Positions, outData.Aux);
        }

        MeshImportData result;
        result.Meshes.push_back(std::move(outData));
        return ImportResult{std::move(result)};
    }
}
