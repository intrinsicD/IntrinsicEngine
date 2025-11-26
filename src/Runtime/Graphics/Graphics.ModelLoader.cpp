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

        // Iterate over all meshes in the file
        for (const auto& gltfMesh : model.meshes)
        {
            // Iterate over primitives (sub-meshes)
            for (const auto& primitive : gltfMesh.primitives)
            {
                // We only support triangles
                if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

                std::vector<RHI::Vertex> vertices;
                std::vector<uint32_t> indices;

                // --- 1. Get Attributes (Pos, Normal, UV) ---
                const float* positionBuffer = nullptr;
                const float* normalsBuffer = nullptr;
                const float* texCoordsBuffer = nullptr;
                size_t vertexCount = 0;

                // Positions
                if (primitive.attributes.find("POSITION") != primitive.attributes.end())
                {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("POSITION")];
                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                    positionBuffer = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.
                        byteOffset]);
                    vertexCount = accessor.count;
                }

                // Normals
                if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
                {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("NORMAL")];
                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                    normalsBuffer = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.
                        byteOffset]);
                }

                // TexCoords
                if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
                {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                    texCoordsBuffer = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.
                        byteOffset]);
                }

                // Assemble Vertices
                for (size_t v = 0; v < vertexCount; v++)
                {
                    RHI::Vertex vertex{};

                    if (positionBuffer)
                    {
                        vertex.pos = glm::make_vec3(&positionBuffer[v * 3]);
                    }

                    if (normalsBuffer)
                    {
                        vertex.normal = glm::make_vec3(&normalsBuffer[v * 3]);
                    }
                    else
                    {
                        vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default up
                    }

                    if (texCoordsBuffer)
                    {
                        vertex.texCoord = glm::make_vec2(&texCoordsBuffer[v * 2]);
                    }

                    vertices.push_back(vertex);
                }

                // --- 2. Get Indices ---
                if (primitive.indices >= 0)
                {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

                    const void* dataPtr = &buffer.data[bufferView.byteOffset + accessor.byteOffset];

                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    {
                        const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
                        for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
                        for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
                        for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                    }
                }

                // Create Mesh
                resultModel->Meshes.push_back(std::make_shared<Mesh>(device, vertices, indices));
            }
        }

        Core::Log::Info("Loaded GLTF: {} with {} meshes", filepath, resultModel->Size());
        return resultModel;
    }
}
