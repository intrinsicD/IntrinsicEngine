module;
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module Graphics:Importers.GLTF.Impl;
import :Importers.GLTF;
import :IORegistry;
import :Geometry;
import :AssetErrors;
import Core.IOBackend;
import Core.Logging;
import Geometry;

namespace Graphics
{
    namespace
    {
        static constexpr std::string_view s_Extensions[] = { ".gltf", ".glb" };

        // --- Bulk Data Loading Helper ---
        template <typename DstT, typename SrcT>
        void LoadBuffer(std::vector<DstT>& outBuffer,
                        const tinygltf::Model& model,
                        const tinygltf::Accessor& accessor,
                        size_t count)
        {
            const auto& view = model.bufferViews[accessor.bufferView];
            const auto& buffer = model.buffers[view.buffer];
            const uint8_t* srcData = &buffer.data[view.byteOffset + accessor.byteOffset];
            const size_t srcStride = view.byteStride == 0 ? sizeof(SrcT) : view.byteStride;

            outBuffer.resize(count);

            if constexpr (std::is_same_v<DstT, SrcT>)
            {
                if (srcStride == sizeof(DstT))
                {
                    std::memcpy(outBuffer.data(), srcData, count * sizeof(DstT));
                    return;
                }
            }

            DstT* dstPtr = outBuffer.data();
            for (size_t i = 0; i < count; ++i)
            {
                const SrcT* elem = reinterpret_cast<const SrcT*>(srcData + i * srcStride);
                if constexpr (std::is_same_v<DstT, SrcT>)
                    dstPtr[i] = *elem;
                else
                    dstPtr[i] = static_cast<DstT>(*elem);
            }
        }

        // Custom FsCallbacks that route through IIOBackend for loading external resources
        struct IOBackendFsContext
        {
            Core::IO::IIOBackend* Backend = nullptr;
            std::string BasePath;
        };

        bool CustomFileExists(const std::string& abs_filename, void* user_data)
        {
            auto* ctx = static_cast<IOBackendFsContext*>(user_data);
            if (!ctx || !ctx->Backend) return false;

            Core::IO::IORequest req;
            req.Path = abs_filename;
            req.Size = 1; // Just check if we can read 1 byte
            auto result = ctx->Backend->Read(req);
            return result.has_value();
        }

        std::string CustomExpandFilePath(const std::string& filepath, void* user_data)
        {
            auto* ctx = static_cast<IOBackendFsContext*>(user_data);
            if (!ctx) return filepath;

            // If already absolute, return as-is
            namespace fs = std::filesystem;
            if (fs::path(filepath).is_absolute()) return filepath;

            // Resolve relative to base path
            return (fs::path(ctx->BasePath) / filepath).string();
        }

        bool CustomReadWholeFile(std::vector<unsigned char>* out,
                                 std::string* err,
                                 const std::string& filepath,
                                 void* user_data)
        {
            auto* ctx = static_cast<IOBackendFsContext*>(user_data);
            if (!ctx || !ctx->Backend)
            {
                if (err) *err = "No I/O backend available";
                return false;
            }

            Core::IO::IORequest req;
            req.Path = filepath;
            auto result = ctx->Backend->Read(req);
            if (!result)
            {
                if (err) *err = "Failed to read file: " + filepath;
                return false;
            }

            out->resize(result->Data.size());
            std::memcpy(out->data(), result->Data.data(), result->Data.size());
            return true;
        }

        bool CustomWriteWholeFile(std::string* /* err */,
                                  const std::string& /* filepath */,
                                  const std::vector<unsigned char>& /* contents */,
                                  void* /* user_data */)
        {
            return false; // Not supported in Phase 0
        }
    }

    std::span<const std::string_view> GLTFLoader::Extensions() const
    {
        return s_Extensions;
    }

    std::expected<ImportResult, AssetError> GLTFLoader::Load(
        std::span<const std::byte> data,
        const LoadContext& ctx)
    {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        // Setup custom FsCallbacks so tiny_gltf routes external resource reads
        // through the I/O backend
        IOBackendFsContext fsCtx;
        fsCtx.Backend = ctx.Backend;
        fsCtx.BasePath = std::string(ctx.BasePath);

        tinygltf::FsCallbacks callbacks{};
        callbacks.FileExists = CustomFileExists;
        callbacks.ExpandFilePath = CustomExpandFilePath;
        callbacks.ReadWholeFile = CustomReadWholeFile;
        callbacks.WriteWholeFile = CustomWriteWholeFile;
        callbacks.user_data = &fsCtx;
        loader.SetFsCallbacks(callbacks);

        // Detect GLB vs glTF from magic bytes or source path
        bool isGlb = false;
        if (data.size() >= 4)
        {
            // GLB magic: 0x46546C67 ("glTF" in little-endian)
            uint32_t magic = 0;
            std::memcpy(&magic, data.data(), 4);
            isGlb = (magic == 0x46546C67);
        }

        bool ret;
        if (isGlb)
        {
            ret = loader.LoadBinaryFromMemory(
                &model, &err, &warn,
                reinterpret_cast<const unsigned char*>(data.data()),
                static_cast<unsigned int>(data.size()));
        }
        else
        {
            std::string jsonStr(reinterpret_cast<const char*>(data.data()), data.size());

            // For .gltf, we need to provide the base dir so tiny_gltf can resolve
            // relative URIs for buffers/images
            std::string baseDir = std::string(ctx.BasePath);
            if (!baseDir.empty() && baseDir.back() != '/' && baseDir.back() != '\\')
                baseDir += '/';

            ret = loader.LoadASCIIFromString(
                &model, &err, &warn,
                jsonStr.c_str(),
                static_cast<unsigned int>(jsonStr.size()),
                baseDir);
        }

        if (!warn.empty()) Core::Log::Warn("GLTF: {}", warn);
        if (!ret)
        {
            if (!err.empty()) Core::Log::Error("GLTF: {}", err);
            return std::unexpected(AssetError::DecodeFailed);
        }

        std::vector<GeometryCpuData> meshes;

        for (const auto& gltfMesh : model.meshes)
        {
            for (const auto& primitive : gltfMesh.primitives)
            {
                GeometryCpuData meshData;
                bool hasNormals = false;

                switch (primitive.mode)
                {
                case TINYGLTF_MODE_POINTS:
                    meshData.Topology = PrimitiveTopology::Points; break;
                case TINYGLTF_MODE_LINE:
                case TINYGLTF_MODE_LINE_LOOP:
                case TINYGLTF_MODE_LINE_STRIP:
                    meshData.Topology = PrimitiveTopology::Lines; break;
                case TINYGLTF_MODE_TRIANGLES:
                case TINYGLTF_MODE_TRIANGLE_STRIP:
                case TINYGLTF_MODE_TRIANGLE_FAN:
                    meshData.Topology = PrimitiveTopology::Triangles; break;
                default: continue;
                }

                auto posIt = primitive.attributes.find("POSITION");
                if (posIt == primitive.attributes.end()) continue;

                const auto& posAccessor = model.accessors[posIt->second];
                const size_t vertexCount = posAccessor.count;

                LoadBuffer<glm::vec3, glm::vec3>(meshData.Positions, model, posAccessor, vertexCount);

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

                if (primitive.indices >= 0)
                {
                    const auto& accessor = model.accessors[primitive.indices];
                    const size_t indexCount = accessor.count;

                    switch (accessor.componentType)
                    {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        LoadBuffer<uint32_t, uint32_t>(meshData.Indices, model, accessor, indexCount); break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        LoadBuffer<uint32_t, uint16_t>(meshData.Indices, model, accessor, indexCount); break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        LoadBuffer<uint32_t, uint8_t>(meshData.Indices, model, accessor, indexCount); break;
                    default:
                        Core::Log::Error("GLTF: Unsupported index component type: {}", accessor.componentType);
                        break;
                    }
                }

                if (!hasNormals && meshData.Topology == PrimitiveTopology::Triangles)
                {
                    Geometry::MeshUtils::CalculateNormals(meshData.Positions, meshData.Indices, meshData.Normals);
                }
                if (uvIt == primitive.attributes.end())
                {
                    Geometry::MeshUtils::GenerateUVs(meshData.Positions, meshData.Aux);
                }

                meshes.push_back(std::move(meshData));
            }
        }

        if (meshes.empty())
            return std::unexpected(AssetError::InvalidData);

        MeshImportData result;
        result.Meshes = std::move(meshes);
        return ImportResult{std::move(result)};
    }
}
