module;
// TinyGLTF headers
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <memory>
#include <cstring>

module Runtime.Graphics.ModelLoader;
import Core.Logging;
import Core.Filesystem;
import Runtime.RHI.Types;

namespace Runtime::Graphics
{
    std::vector<std::unique_ptr<Mesh>> ModelLoader::Load(RHI::VulkanDevice& device,
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

        std::vector<std::unique_ptr<Mesh>> resultMeshes;

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

                auto validateAccessor = [&](const tinygltf::Accessor& accessor, int expectedType, int expectedComponentType,
                                            size_t expectedElementSize) -> bool
                {
                    if (accessor.type != expectedType || accessor.componentType != expectedComponentType)
                    {
                        Core::Log::Error("Unsupported accessor layout: type {} component {}", accessor.type,
                                         accessor.componentType);
                        return false;
                    }

                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

                    size_t stride = bufferView.byteStride == 0 ? expectedElementSize : bufferView.byteStride;
                    if (stride != expectedElementSize)
                    {
                        Core::Log::Error("Unexpected stride {} for accessor; expected {}", stride, expectedElementSize);
                        return false;
                    }

                    const size_t start = bufferView.byteOffset + accessor.byteOffset;
                    const size_t end = start + stride * accessor.count;
                    if (end > buffer.data.size())
                    {
                        Core::Log::Error("Accessor out of bounds (end {}, buffer size {})", end, buffer.data.size());
                        return false;
                    }

                    return true;
                };

                // --- 1. Get Attributes (Pos, Normal, UV) ---
                const float* positionBuffer = nullptr;
                const float* normalsBuffer = nullptr;
                const float* texCoordsBuffer = nullptr;
                size_t vertexCount = 0;

                // Positions
                if (primitive.attributes.find("POSITION") != primitive.attributes.end())
                {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("POSITION")];
                    if (!validateAccessor(accessor, TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_FLOAT,
                                          sizeof(float) * 3))
                    {
                        continue;
                    }

                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                    positionBuffer = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.
                        byteOffset);
                    vertexCount = accessor.count;
                }

                // Normals
                if (primitive.attributes.find("NORMAL") != primitive.attributes.end())
                {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("NORMAL")];
                    if (!validateAccessor(accessor, TINYGLTF_TYPE_VEC3, TINYGLTF_COMPONENT_TYPE_FLOAT,
                                          sizeof(float) * 3))
                    {
                        continue;
                    }

                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                    normalsBuffer = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.
                        byteOffset);
                }

                // TexCoords
                if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end())
                {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                    if (!validateAccessor(accessor, TINYGLTF_TYPE_VEC2, TINYGLTF_COMPONENT_TYPE_FLOAT,
                                          sizeof(float) * 2))
                    {
                        continue;
                    }

                    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
                    texCoordsBuffer = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.
                        byteOffset);
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

                    if (accessor.type != TINYGLTF_TYPE_SCALAR)
                    {
                        Core::Log::Error("Index accessor must be scalar type");
                        continue;
                    }

                    size_t indexSize = 0;
                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    {
                        indexSize = sizeof(uint32_t);
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        indexSize = sizeof(uint16_t);
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        indexSize = sizeof(uint8_t);
                    }
                    else
                    {
                        Core::Log::Error("Unsupported index component type {}", accessor.componentType);
                        continue;
                    }

                    size_t stride = bufferView.byteStride == 0 ? indexSize : bufferView.byteStride;
                    const size_t start = bufferView.byteOffset + accessor.byteOffset;
                    const size_t end = start + stride * accessor.count;
                    if (stride != indexSize || end > buffer.data.size())
                    {
                        Core::Log::Error("Invalid index buffer stride {} or bounds (end {}, size {})", stride, end,
                                         buffer.data.size());
                        continue;
                    }

                    const void* dataPtr = buffer.data.data() + start;
                    if (indexSize == sizeof(uint32_t))
                    {
                        const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
                        for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                    }
                    else if (indexSize == sizeof(uint16_t))
                    {
                        const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
                        for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                    }
                    else
                    {
                        const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
                        for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                    }
                }

                // Create Mesh
                resultMeshes.push_back(std::make_unique<Mesh>(device, vertices, indices));
            }
        }

        Core::Log::Info("Loaded GLTF: {} with {} meshes", filepath, resultMeshes.size());
        return resultMeshes;
    }
}
