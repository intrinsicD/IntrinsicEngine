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

module Graphics.Importers.GLTF;
import Graphics.IORegistry;
import Graphics.Geometry;
import Graphics.AssetErrors;
import Graphics.Model;
import Core.IOBackend;
import Core.Logging;
import Geometry.MeshUtils;

#include "Graphics.Importers.PostProcess.hpp"

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
            // bufferView == -1 means the accessor has no dense buffer (glTF spec: zero-initialized,
            // or the data lives in a Draco blob that we don't yet decode). Fill with defaults.
            if (accessor.bufferView < 0)
            {
                outBuffer.assign(count, DstT{});
                return;
            }

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

            for (size_t i = 0; i < count; ++i)
            {
                SrcT elem{};
                std::memcpy(&elem, srcData + i * srcStride, sizeof(SrcT));
                if constexpr (std::is_same_v<DstT, SrcT>)
                    outBuffer[i] = elem;
                else
                    outBuffer[i] = static_cast<DstT>(elem);
            }
        }

        [[nodiscard]] float DecodeAccessorComponentAsFloat(const tinygltf::Accessor& accessor,
                                                          const uint8_t* src)
        {
            switch (accessor.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
            {
                float value = 0.0f;
                std::memcpy(&value, src, sizeof(float));
                return value;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                uint16_t value = 0;
                std::memcpy(&value, src, sizeof(uint16_t));
                return accessor.normalized ? static_cast<float>(value) / 65535.0f
                                            : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_SHORT:
            {
                int16_t value = 0;
                std::memcpy(&value, src, sizeof(int16_t));
                if (!accessor.normalized) return static_cast<float>(value);
                constexpr float kDenom = 32767.0f;
                return glm::clamp(static_cast<float>(value) / kDenom, -1.0f, 1.0f);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                uint8_t value = 0;
                std::memcpy(&value, src, sizeof(uint8_t));
                return accessor.normalized ? static_cast<float>(value) / 255.0f
                                           : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_BYTE:
            {
                int8_t value = 0;
                std::memcpy(&value, src, sizeof(int8_t));
                if (!accessor.normalized) return static_cast<float>(value);
                constexpr float kDenom = 127.0f;
                return glm::clamp(static_cast<float>(value) / kDenom, -1.0f, 1.0f);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                uint32_t value = 0;
                std::memcpy(&value, src, sizeof(uint32_t));
                return accessor.normalized ? static_cast<float>(value) / 4294967295.0f
                                           : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_INT:
            {
                int32_t value = 0;
                std::memcpy(&value, src, sizeof(int32_t));
                if (!accessor.normalized) return static_cast<float>(value);
                constexpr float kDenom = 2147483647.0f;
                return glm::clamp(static_cast<float>(value) / kDenom, -1.0f, 1.0f);
            }
            default:
                return 0.0f;
            }
        }

        void LoadVec2Buffer(std::vector<glm::vec2>& outBuffer,
                            const tinygltf::Model& model,
                            const tinygltf::Accessor& accessor,
                            size_t count)
        {
            if (accessor.bufferView < 0)
            {
                outBuffer.assign(count, glm::vec2(0.0f));
                return;
            }

            const auto& view = model.bufferViews[accessor.bufferView];
            const auto& buffer = model.buffers[view.buffer];
            const uint8_t* srcData = &buffer.data[view.byteOffset + accessor.byteOffset];
            const size_t srcStride = view.byteStride == 0
                ? static_cast<size_t>(tinygltf::GetComponentSizeInBytes(accessor.componentType)) * 2u
                : view.byteStride;

            outBuffer.resize(count);
            for (size_t i = 0; i < count; ++i)
            {
                const uint8_t* elem = srcData + i * srcStride;
                outBuffer[i] = glm::vec2(
                    DecodeAccessorComponentAsFloat(accessor, elem + 0 * tinygltf::GetComponentSizeInBytes(accessor.componentType)),
                    DecodeAccessorComponentAsFloat(accessor, elem + 1 * tinygltf::GetComponentSizeInBytes(accessor.componentType)));
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

        // Reject only required extensions that we truly cannot satisfy in-engine.
        for (const auto& ext : model.extensionsRequired)
        {
            if (ext == "KHR_draco_mesh_compression")
            {
                Core::Log::Info("GLTF: '{}' uses Draco mesh compression; decoding natively.",
                                ctx.SourcePath);
                continue;
            }
            Core::Log::Warn("GLTF: '{}' uses required extension '{}' — partial or no support.",
                            ctx.SourcePath, ext);
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
                    std::vector<glm::vec2> uvs;
                    LoadVec2Buffer(uvs, model, uvAccessor, vertexCount);

                    for (size_t i = 0; i < uvs.size(); ++i)
                        meshData.Aux[i] = glm::vec4(uvs[i], 0.0f, 0.0f);
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

                if (!Importers::ApplyGeometryImportPostProcess(
                        meshData,
                        hasNormals,
                        uvIt != primitive.attributes.end(),
                        Geometry::MeshUtils::CalculateNormals,
                        Geometry::MeshUtils::GenerateUVs))
                {
                    continue;
                }

                meshes.push_back(std::move(meshData));
            }
        }

        if (meshes.empty())
            return std::unexpected(AssetError::InvalidData);

        MeshImportData result;
        result.Meshes = std::move(meshes);

        result.EmbeddedImages.reserve(model.images.size());
        for (const auto& image : model.images)
        {
            if (image.width <= 0 || image.height <= 0 || image.image.empty())
                continue;

            ImportedTextureImage out{};
            out.Name = image.name;
            out.Width = image.width;
            out.Height = image.height;
            out.Components = image.component;
            out.Bits = image.bits;
            out.PixelType = image.pixel_type;
            out.AsIs = image.as_is;
            out.Pixels.resize(image.image.size());
            std::memcpy(out.Pixels.data(), image.image.data(), image.image.size());
            result.EmbeddedImages.push_back(std::move(out));
        }

        auto resolveTextureImage = [&model](int textureIndex) -> int
        {
            if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
                return -1;
            const int source = model.textures[textureIndex].source;
            if (source < 0 || source >= static_cast<int>(model.images.size()))
                return -1;
            return source;
        };

        result.EmbeddedMaterials.reserve(model.materials.size());
        for (const auto& material : model.materials)
        {
            ImportedMaterialTextureRefs out{};
            out.Name = material.name;
            if (material.pbrMetallicRoughness.baseColorFactor.size() == 4)
            {
                out.BaseColorFactor = glm::vec4(
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[0]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[1]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[2]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[3]));
            }
            out.MetallicFactor = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
            out.RoughnessFactor = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);
            out.BaseColorImage = resolveTextureImage(material.pbrMetallicRoughness.baseColorTexture.index);
            out.MetallicRoughnessImage = resolveTextureImage(material.pbrMetallicRoughness.metallicRoughnessTexture.index);
            out.NormalImage = resolveTextureImage(material.normalTexture.index);
            out.OcclusionImage = resolveTextureImage(material.occlusionTexture.index);
            result.EmbeddedMaterials.push_back(std::move(out));
        }

        return ImportResult{std::move(result)};
    }
}
