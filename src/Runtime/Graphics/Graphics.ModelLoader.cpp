module;
// TinyGLTF headers
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <memory>

module Runtime.Graphics.ModelLoader;
import Core.Logging;
import Core.Filesystem;
import Runtime.RHI.Types;
import Runtime.Graphics.Geometry;

namespace Runtime::Graphics
{
    std::shared_ptr<Model> ModelLoader::Load(std::shared_ptr<RHI::VulkanDevice> device,
                                             const std::string& filepath)
    {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        std::string fullPath = Core::Filesystem::GetAssetPath(filepath);

        bool ret = false;
        if (fullPath.ends_with(".glb"))
        {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, fullPath);
        }
        else
        {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, fullPath);
        }

        if (!warn.empty()) Core::Log::Warn("GLTF Warning: {}", warn);
        if (!err.empty()) Core::Log::Error("GLTF Error: {}", err);
        if (!ret) return {};

        auto resultModel = std::make_shared<Model>();

        for (const auto& gltfMesh : model.meshes)
        {
            for (const auto& primitive : gltfMesh.primitives)
            {
                GeometryCpuData cpuData;

                // --- 1. Accessors ---
                const float* positionBuffer = nullptr;
                const float* normalsBuffer = nullptr;
                const float* texCoordsBuffer = nullptr;
                size_t vertexCount = 0;

                // Helper to get buffer pointer
                auto GetBuffer = [&](const char* attrName) -> const float* {
                    if (primitive.attributes.find(attrName) == primitive.attributes.end()) return nullptr;
                    const auto& accessor = model.accessors[primitive.attributes.at(attrName)];
                    const auto& view = model.bufferViews[accessor.bufferView];
                    vertexCount = accessor.count; // Assume synced counts
                    return reinterpret_cast<const float*>(&model.buffers[view.buffer].data[view.byteOffset + accessor.byteOffset]);
                };

                positionBuffer = GetBuffer("POSITION");
                normalsBuffer = GetBuffer("NORMAL");
                texCoordsBuffer = GetBuffer("TEXCOORD_0");

                if (!positionBuffer) continue;

                // --- 2. Populate Vectors (SoA) ---
                cpuData.Positions.resize(vertexCount);
                cpuData.Normals.resize(vertexCount);
                cpuData.Aux.resize(vertexCount);

                // Bulk Copy if possible, or loop for safety/swizzling
                for (size_t i = 0; i < vertexCount; i++)
                {
                    // Positions
                    cpuData.Positions[i] = glm::make_vec3(&positionBuffer[i * 3]);

                    // Normals (Default up if missing)
                    if (normalsBuffer) cpuData.Normals[i] = glm::make_vec3(&normalsBuffer[i * 3]);
                    else cpuData.Normals[i] = glm::vec3(0, 1, 0);

                    // Aux (UV in xy, zw unused for now)
                    if (texCoordsBuffer) {
                        glm::vec2 uv = glm::make_vec2(&texCoordsBuffer[i * 2]);
                        cpuData.Aux[i] = glm::vec4(uv.x, uv.y, 0.0f, 0.0f);
                    } else {
                        cpuData.Aux[i] = glm::vec4(0.0f);
                    }
                }

                // --- 3. Indices ---
                if (primitive.indices >= 0) {
                     const auto& accessor = model.accessors[primitive.indices];
                     const auto& view = model.bufferViews[accessor.bufferView];
                     const auto& buffer = model.buffers[view.buffer];
                     const uint8_t* data = &buffer.data[view.byteOffset + accessor.byteOffset];

                     if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                         const uint32_t* buf = reinterpret_cast<const uint32_t*>(data);
                         cpuData.Indices.assign(buf, buf + accessor.count);
                     } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                         const uint16_t* buf = reinterpret_cast<const uint16_t*>(data);
                         for(size_t i=0; i<accessor.count; ++i) cpuData.Indices.push_back(buf[i]);
                     } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                         const uint8_t* buf = reinterpret_cast<const uint8_t*>(data);
                         for(size_t i=0; i<accessor.count; ++i) cpuData.Indices.push_back(buf[i]);
                     }
                }

                // --- 4. Upload ---
                // Model now needs to hold GeometryGpuData instead of Mesh
                // We update the Model struct definition in step F.
                resultModel->Meshes.push_back(std::make_shared<GeometryGpuData>(device, cpuData.ToUploadRequest()));
            }
        }
        return resultModel;
    }
}
