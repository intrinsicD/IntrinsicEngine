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

module Runtime.Graphics.ModelLoader;
import Core.Logging;
import Core.Filesystem;
import Runtime.RHI.Types;
import Runtime.Graphics.Geometry;

namespace Runtime::Graphics
{
    // --- Helpers ---

    struct VertexKey {
        int p = -1, n = -1, t = -1;
        bool operator==(const VertexKey& other) const { return p==other.p && n==other.n && t==other.t; }
    };
    struct VertexKeyHash {
        size_t operator()(const VertexKey& k) const {
             return std::hash<int>()(k.p) ^ (std::hash<int>()(k.n) << 1) ^ (std::hash<int>()(k.t) << 2);
        }
    };

    // Fast float parsing from string_view
    static float ParseFloat(std::string_view sv) {
        float val = 0.0f;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    // Split string by delimiter
    static std::vector<std::string_view> Split(std::string_view str, char delimiter) {
        std::vector<std::string_view> result;
        size_t first = 0;
        while (first < str.size()) {
            const auto second = str.find(delimiter, first);
            if (first != second)
                result.emplace_back(str.substr(first, second - first));
            if (second == std::string_view::npos) break;
            first = second + 1;
        }
        return result;
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

        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string type;
            ss >> type;

            if (type == "v") {
                glm::vec3 v; ss >> v.x >> v.y >> v.z;
                tempPos.push_back(v);
            } else if (type == "vn") {
                glm::vec3 vn; ss >> vn.x >> vn.y >> vn.z;
                tempNorm.push_back(vn);
            } else if (type == "vt") {
                glm::vec2 vt; ss >> vt.x >> vt.y;
                tempUV.push_back(vt);
            } else if (type == "f") {
                std::string vertexStr;
                std::vector<uint32_t> faceIndices;

                while (ss >> vertexStr) {
                    VertexKey key;
                    size_t s1 = vertexStr.find('/');
                    size_t s2 = vertexStr.find('/', s1 + 1);

                    // Pos index
                    std::from_chars(vertexStr.data(), vertexStr.data() + (s1 == std::string::npos ? vertexStr.size() : s1), key.p);
                    key.p--; // OBJ 1-based

                    if (s1 != std::string::npos) {
                        // Tex Index
                        if (s2 != std::string::npos && s2 - s1 > 1) {
                             std::from_chars(vertexStr.data() + s1 + 1, vertexStr.data() + s2, key.t);
                             key.t--;
                        }
                        // Norm Index
                        if (s2 != std::string::npos && s2 + 1 < vertexStr.size()) {
                             std::from_chars(vertexStr.data() + s2 + 1, vertexStr.data() + vertexStr.size(), key.n);
                             key.n--;
                        }
                    }

                    if (uniqueVertices.find(key) == uniqueVertices.end()) {
                        uint32_t idx = (uint32_t)outData.Positions.size();
                        uniqueVertices[key] = idx;

                        outData.Positions.push_back(tempPos[key.p]);
                        outData.Normals.push_back((key.n >= 0 && key.n < tempNorm.size()) ? tempNorm[key.n] : glm::vec3(0,1,0));
                        glm::vec2 uv = (key.t >= 0 && key.t < tempUV.size()) ? tempUV[key.t] : glm::vec2(0,0);
                        outData.Aux.emplace_back(uv.x, uv.y, 0, 0);
                    }
                    faceIndices.push_back(uniqueVertices[key]);
                }

                // Triangulate Fan
                for (size_t i = 1; i < faceIndices.size() - 1; ++i) {
                    outData.Indices.push_back(faceIndices[0]);
                    outData.Indices.push_back(faceIndices[i]);
                    outData.Indices.push_back(faceIndices[i+1]);
                }
            } else if (type == "l") {
                outData.Topology = PrimitiveTopology::Lines;
                // Line parsing logic similar to faces but usually just 2 indices
                // Simplified for brevity
            }
        }
        return true;
    }

    // PLY Property Mapping
    struct PlyProp { std::string name; int offset; }; // simplified

    static bool LoadPLY(const std::string& path, GeometryCpuData& outData)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        std::string line;
        bool binary = false;
        size_t vertexCount = 0;
        size_t faceCount = 0;

        // Property Indices (-1 = not found)
        int idxX = -1, idxY = -1, idxZ = -1;
        int idxNX = -1, idxNY = -1, idxNZ = -1;
        int idxR = -1, idxG = -1, idxB = -1;

        int propCounter = 0;
        bool headerEnded = false;
        bool inVertex = false;

        // 1. Header Parse
        while (std::getline(file, line)) {
            if (line == "end_header") { headerEnded = true; break; }
            std::stringstream ss(line);
            std::string token; ss >> token;

            if (token == "format") {
                std::string fmt; ss >> fmt;
                if (fmt == "binary_little_endian") binary = true;
            } else if (token == "element") {
                std::string type; ss >> type;
                if (type == "vertex") { ss >> vertexCount; inVertex = true; propCounter = 0; }
                else if (type == "face") { ss >> faceCount; inVertex = false; }
            } else if (token == "property" && inVertex) {
                std::string type, name; ss >> type >> name;
                if (name == "x") idxX = propCounter;
                if (name == "y") idxY = propCounter;
                if (name == "z") idxZ = propCounter;
                if (name == "nx") idxNX = propCounter;
                if (name == "ny") idxNY = propCounter;
                if (name == "nz") idxNZ = propCounter;
                if (name == "red") idxR = propCounter;
                if (name == "green") idxG = propCounter;
                if (name == "blue") idxB = propCounter;
                propCounter++;
            }
        }

        if (!headerEnded) return false;

        outData.Positions.resize(vertexCount);
        outData.Normals.resize(vertexCount, glm::vec3(0,1,0));
        outData.Aux.resize(vertexCount, glm::vec4(1)); // Default white color

        // 2. Body Parse
        if (binary) {
            // Assume all properties are float (4 bytes) or uchar (1 byte) for colors.
            // This is a simplified binary loader. A full one requires tracking types per property.
            // For robustness, let's fallback to "Support ASCII PLY only" or implement full type tracking.
            // Given the complexity constraints, we'll implement ASCII PLY fully and minimal binary.
            // ... (Binary implementation omitted for brevity, assuming ASCII for research/text files)
            Core::Log::Warn("Binary PLY not fully implemented in this sample. Use ASCII PLY.");
            return false;
        } else {
            // ASCII
            for (size_t i = 0; i < vertexCount; ++i) {
                std::getline(file, line);
                std::vector<std::string_view> tokens = Split(line, ' ');

                // Remove empty tokens caused by multiple spaces
                std::erase_if(tokens, [](std::string_view s) { return s.empty(); });

                if (idxX >= 0) outData.Positions[i].x = ParseFloat(tokens[idxX]);
                if (idxY >= 0) outData.Positions[i].y = ParseFloat(tokens[idxY]);
                if (idxZ >= 0) outData.Positions[i].z = ParseFloat(tokens[idxZ]);

                if (idxNX >= 0) outData.Normals[i].x = ParseFloat(tokens[idxNX]);
                if (idxNY >= 0) outData.Normals[i].y = ParseFloat(tokens[idxNY]);
                if (idxNZ >= 0) outData.Normals[i].z = ParseFloat(tokens[idxNZ]);

                if (idxR >= 0 && idxG >= 0 && idxB >= 0) {
                     // PLY colors are usually 0-255 int or 0-1 float. Simple heuristic:
                     float r = ParseFloat(tokens[idxR]);
                     if (r > 1.0f) { // Assume 0-255
                         outData.Aux[i] = glm::vec4(
                             r / 255.0f,
                             ParseFloat(tokens[idxG]) / 255.0f,
                             ParseFloat(tokens[idxB]) / 255.0f,
                             1.0f
                         );
                     } else {
                         outData.Aux[i] = glm::vec4(r, ParseFloat(tokens[idxG]), ParseFloat(tokens[idxB]), 1.0f);
                     }
                }
            }

            // Faces
            if (faceCount > 0) {
                outData.Topology = PrimitiveTopology::Triangles;
                for (size_t i = 0; i < faceCount; ++i) {
                    std::getline(file, line);
                    std::stringstream ss(line);
                    int count; ss >> count;
                    std::vector<uint32_t> faceIndices(count);
                    for(int k=0; k<count; ++k) ss >> faceIndices[k];

                    // Triangulate
                    for (size_t k = 1; k < faceIndices.size() - 1; ++k) {
                        outData.Indices.push_back(faceIndices[0]);
                        outData.Indices.push_back(faceIndices[k]);
                        outData.Indices.push_back(faceIndices[k+1]);
                    }
                }
            } else {
                outData.Topology = PrimitiveTopology::Points;
            }
        }
        return true;
    }

    static bool LoadXYZ(const std::string& path, GeometryCpuData& outData)
    {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        outData.Topology = PrimitiveTopology::Points;
        std::string line;
        while(std::getline(file, line)) {
            if(line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            outData.Positions.push_back(p);
            outData.Normals.push_back({0,1,0});

            // Check for colors
            if (!ss.eof()) {
                float r,g,b;
                ss >> r >> g >> b;
                outData.Aux.emplace_back(r, g, b, 1.0f);
            } else {
                outData.Aux.emplace_back(1.0f);
            }
        }
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

        while(std::getline(file, line)) {
            if(line.empty()) continue;
            if(line[0] == '#') { parsingEdges = true; continue; }

            std::stringstream ss(line);
            if (!parsingEdges) {
                int id; ss >> id;
                // TGF doesn't have coordinates usually, only topology.
                // We'll assign random positions or require a sidecar file?
                // For this research engine, let's assume valid lines contain ID X Y Z
                // EXTENSION: "Extended TGF" with coords
                glm::vec3 p{0.0f};
                if (!ss.eof()) ss >> p.x >> p.y >> p.z;

                uint32_t idx = (uint32_t)outData.Positions.size();
                idMap[id] = idx;
                outData.Positions.push_back(p);
                outData.Normals.push_back({0,1,0});
                outData.Aux.emplace_back(1);
            } else {
                int from, to; ss >> from >> to;
                if (idMap.count(from) && idMap.count(to)) {
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

         for (const auto& gltfMesh : model.meshes) {
            for (const auto& primitive : gltfMesh.primitives) {
                 GeometryCpuData meshData;

                 // 1. Topology Mapping
                 switch(primitive.mode) {
                     case TINYGLTF_MODE_POINTS: meshData.Topology = PrimitiveTopology::Points; break;
                     case TINYGLTF_MODE_LINE:
                     case TINYGLTF_MODE_LINE_LOOP:
                     case TINYGLTF_MODE_LINE_STRIP: meshData.Topology = PrimitiveTopology::Lines; break;
                     case TINYGLTF_MODE_TRIANGLES:
                     case TINYGLTF_MODE_TRIANGLE_STRIP:
                     case TINYGLTF_MODE_TRIANGLE_FAN: meshData.Topology = PrimitiveTopology::Triangles; break;
                     default: continue; // Unsupported topology
                 }

                 // 2. Accessors Setup
                 const float* positionBuffer = nullptr;
                 const float* normalsBuffer = nullptr;
                 const float* texCoordsBuffer = nullptr;
                 size_t vertexCount = 0;

                 auto GetBuffer = [&](const char* attrName) -> const float* {
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

                    if (normalsBuffer) {
                        meshData.Normals[i] = glm::make_vec3(&normalsBuffer[i * 3]);
                    } else {
                        meshData.Normals[i] = glm::vec3(0, 1, 0);
                    }

                    if (texCoordsBuffer) {
                        glm::vec2 uv = glm::make_vec2(&texCoordsBuffer[i * 2]);
                        meshData.Aux[i] = glm::vec4(uv.x, uv.y, 0.0f, 0.0f);
                    } else {
                        meshData.Aux[i] = glm::vec4(0.0f);
                    }
                }

                // 4. Indices
                if (primitive.indices >= 0) {
                     const auto& accessor = model.accessors[primitive.indices];
                     const auto& view = model.bufferViews[accessor.bufferView];
                     const auto& buffer = model.buffers[view.buffer];
                     const uint8_t* data = &buffer.data[view.byteOffset + accessor.byteOffset];

                     if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                         const uint32_t* buf = reinterpret_cast<const uint32_t*>(data);
                         meshData.Indices.assign(buf, buf + accessor.count);
                     } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                         const uint16_t* buf = reinterpret_cast<const uint16_t*>(data);
                         for(size_t i=0; i<accessor.count; ++i) meshData.Indices.push_back(buf[i]);
                     } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                         const uint8_t* buf = reinterpret_cast<const uint8_t*>(data);
                         for(size_t i=0; i<accessor.count; ++i) meshData.Indices.push_back(buf[i]);
                     }
                }

                outMeshes.push_back(std::move(meshData));
            }
         }
         return true;
    }

    std::shared_ptr<Model> ModelLoader::Load(std::shared_ptr<RHI::VulkanDevice> device,
                                             const std::string& filepath)
    {
        std::string fullPath = Core::Filesystem::GetAssetPath(filepath);
        std::string ext = std::filesystem::path(fullPath).extension().string();
        // ToLower
        for (auto& c : ext) c = tolower(c);

        auto model = std::make_shared<Model>();
        std::vector<GeometryCpuData> cpuMeshes;
        bool success = false;

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
            // Treating simple PCD like XYZ
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
            Core::Log::Error("Unsupported format: {}", ext);
            return model;
        }

        if (success && !cpuMeshes.empty())
        {
            for (const auto& meshData : cpuMeshes)
            {
                model->Meshes.push_back(std::make_shared<GeometryGpuData>(device, meshData.ToUploadRequest()));
            }
            Core::Log::Info("Loaded {} ({} submeshes)", filepath, model->Size());
        }
        else
        {
            Core::Log::Error("Failed to load geometry: {}", filepath);
        }

        return model;
    }
}
