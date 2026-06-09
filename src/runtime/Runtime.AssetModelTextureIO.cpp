module;

#include <stb_image.h>
#include <tiny_gltf.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.AssetModelTextureIO;

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTextureIOBridge;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Core.Error;
import Geometry.HalfedgeMesh.IO;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Assets = Extrinsic::Assets;

        struct AccessorView
        {
            const unsigned char* Data{nullptr};
            std::size_t Count{0};
            std::size_t Stride{0};
            std::size_t ComponentSize{0};
            std::size_t ComponentCount{0};
            int ComponentType{0};
            bool Normalized{false};
        };

        [[nodiscard]] Core::Result Combine(Core::Result lhs, const Core::Result& rhs)
        {
            if (!lhs.has_value())
            {
                return lhs;
            }
            return rhs;
        }

        [[nodiscard]] bool HasExtension(
            const std::string_view path,
            const std::string_view extension) noexcept
        {
            if (path.size() < extension.size())
            {
                return false;
            }
            const std::string_view tail = path.substr(path.size() - extension.size());
            for (std::size_t i = 0u; i < extension.size(); ++i)
            {
                const char lhs = tail[i] >= 'A' && tail[i] <= 'Z'
                    ? static_cast<char>(tail[i] - 'A' + 'a')
                    : tail[i];
                const char rhs = extension[i] >= 'A' && extension[i] <= 'Z'
                    ? static_cast<char>(extension[i] - 'A' + 'a')
                    : extension[i];
                if (lhs != rhs)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] Assets::AssetFileFormat GuessTextureFormat(
            const std::string_view uri,
            const std::string_view mimeType) noexcept
        {
            if (mimeType == "image/png")
            {
                return Assets::AssetFileFormat::PNG;
            }
            if (mimeType == "image/jpeg" || mimeType == "image/jpg")
            {
                return Assets::AssetFileFormat::JPEG;
            }
            if (mimeType == "image/bmp")
            {
                return Assets::AssetFileFormat::BMP;
            }
            if (mimeType == "image/ktx" || mimeType == "image/ktx2")
            {
                return Assets::AssetFileFormat::KTX;
            }
            if (HasExtension(uri, ".png"))
            {
                return Assets::AssetFileFormat::PNG;
            }
            if (HasExtension(uri, ".jpg") || HasExtension(uri, ".jpeg"))
            {
                return Assets::AssetFileFormat::JPEG;
            }
            if (HasExtension(uri, ".tga"))
            {
                return Assets::AssetFileFormat::TGA;
            }
            if (HasExtension(uri, ".bmp"))
            {
                return Assets::AssetFileFormat::BMP;
            }
            if (HasExtension(uri, ".hdr"))
            {
                return Assets::AssetFileFormat::HDR;
            }
            if (HasExtension(uri, ".ktx") || HasExtension(uri, ".ktx2"))
            {
                return Assets::AssetFileFormat::KTX;
            }
            return Assets::AssetFileFormat::Unknown;
        }

        [[nodiscard]] Assets::AssetModelResourceKind ResourceKindForPath(
            const std::string_view path) noexcept
        {
            if (HasExtension(path, ".png") || HasExtension(path, ".jpg")
                || HasExtension(path, ".jpeg") || HasExtension(path, ".tga")
                || HasExtension(path, ".bmp") || HasExtension(path, ".hdr")
                || HasExtension(path, ".ktx") || HasExtension(path, ".ktx2"))
            {
                return Assets::AssetModelResourceKind::Image;
            }
            return Assets::AssetModelResourceKind::Buffer;
        }

        [[nodiscard]] std::string ResolveExternalPath(
            const Assets::AssetModelTextureIORequest& request,
            const std::string_view path)
        {
            const std::filesystem::path candidate{std::string(path)};
            if (candidate.is_absolute() || request.BasePath.empty())
            {
                return candidate.string();
            }
            return (std::filesystem::path(request.BasePath) / candidate)
                .lexically_normal()
                .string();
        }

        void RecordExternalReadDiagnostic(
            std::vector<Assets::AssetModelExternalResourceDiagnostic>& diagnostics,
            const std::string& uri,
            const Assets::AssetModelResourceStatus status,
            const Core::ErrorCode error,
            std::string message = {})
        {
            diagnostics.push_back(Assets::AssetModelExternalResourceDiagnostic{
                .ResourceKind = ResourceKindForPath(uri),
                .Status = status,
                .Error = error,
                .Uri = uri,
                .Message = std::move(message),
            });
        }

        struct GltfFsContext
        {
            const Assets::AssetModelTextureIORequest* Request{nullptr};
            std::vector<Assets::AssetModelExternalResourceDiagnostic>* Diagnostics{nullptr};
        };

        bool GltfFileExists(const std::string& filename, void* userData)
        {
            const auto* context = static_cast<const GltfFsContext*>(userData);
            if (context == nullptr || context->Request == nullptr
                || !context->Request->ReadExternalResource)
            {
                return false;
            }

            const std::string resolved = ResolveExternalPath(*context->Request, filename);
            auto read = context->Request->ReadExternalResource(resolved);
            if (!read.has_value() && context->Diagnostics != nullptr)
            {
                RecordExternalReadDiagnostic(
                    *context->Diagnostics,
                    resolved,
                    Assets::AssetModelResourceStatus::FileReadFailed,
                    read.error(),
                    "tinygltf external resource existence check failed");
            }
            return read.has_value();
        }

        std::string GltfExpandFilePath(const std::string& filename, void* userData)
        {
            const auto* context = static_cast<const GltfFsContext*>(userData);
            if (context == nullptr || context->Request == nullptr)
            {
                return filename;
            }
            return ResolveExternalPath(*context->Request, filename);
        }

        bool GltfReadWholeFile(
            std::vector<unsigned char>* out,
            std::string* err,
            const std::string& filename,
            void* userData)
        {
            const auto* context = static_cast<const GltfFsContext*>(userData);
            if (context == nullptr || context->Request == nullptr
                || !context->Request->ReadExternalResource)
            {
                if (err != nullptr)
                {
                    *err = "no promoted asset I/O callback is available";
                }
                return false;
            }

            const std::string resolved = ResolveExternalPath(*context->Request, filename);
            auto read = context->Request->ReadExternalResource(resolved);
            if (!read.has_value())
            {
                if (err != nullptr)
                {
                    *err = "promoted asset I/O failed to read " + resolved;
                }
                if (context->Diagnostics != nullptr)
                {
                    RecordExternalReadDiagnostic(
                        *context->Diagnostics,
                        resolved,
                        Assets::AssetModelResourceStatus::FileReadFailed,
                        read.error(),
                        "tinygltf external resource read failed");
                }
                return false;
            }

            out->resize(read->Bytes.size());
            if (!read->Bytes.empty())
            {
                std::memcpy(out->data(), read->Bytes.data(), read->Bytes.size());
            }
            if (context->Diagnostics != nullptr)
            {
                RecordExternalReadDiagnostic(
                    *context->Diagnostics,
                    resolved,
                    Assets::AssetModelResourceStatus::Ready,
                    Core::ErrorCode::Success);
            }
            return true;
        }

        bool GltfGetFileSizeInBytes(
            std::size_t* out,
            std::string* err,
            const std::string& filename,
            void* userData)
        {
            const auto* context = static_cast<const GltfFsContext*>(userData);
            if (context == nullptr || context->Request == nullptr
                || !context->Request->ReadExternalResource)
            {
                if (err != nullptr)
                {
                    *err = "no promoted asset I/O callback is available";
                }
                return false;
            }

            const std::string resolved = ResolveExternalPath(*context->Request, filename);
            auto read = context->Request->ReadExternalResource(resolved);
            if (!read.has_value())
            {
                if (err != nullptr)
                {
                    *err = "promoted asset I/O failed to size " + resolved;
                }
                return false;
            }
            if (out != nullptr)
            {
                *out = read->Bytes.size();
            }
            return true;
        }

        bool GltfWriteWholeFile(
            std::string*,
            const std::string&,
            const std::vector<unsigned char>&,
            void*)
        {
            return false;
        }

        [[nodiscard]] bool SourceLooksLikeGlb(
            const Assets::AssetModelTextureIORequest& request) noexcept
        {
            if (request.Route.Format == Assets::AssetFileFormat::GLB)
            {
                return true;
            }
            if (request.SourceBytes.size() < 4u)
            {
                return false;
            }
            std::uint32_t magic = 0u;
            std::memcpy(&magic, request.SourceBytes.data(), sizeof(magic));
            return magic == 0x46546C67u;
        }

        [[nodiscard]] Core::Expected<tinygltf::Model> LoadGltfModel(
            const Assets::AssetModelTextureIORequest& request,
            std::vector<Assets::AssetModelExternalResourceDiagnostic>& diagnostics)
        {
            if (request.SourceBytes.empty()
                || request.SourceBytes.size()
                    > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max()))
            {
                return Core::Err<tinygltf::Model>(Core::ErrorCode::AssetInvalidData);
            }

            tinygltf::TinyGLTF loader;
            tinygltf::Model model;
            std::string error;
            std::string warning;

            GltfFsContext context{.Request = &request, .Diagnostics = &diagnostics};
            tinygltf::FsCallbacks callbacks{};
            callbacks.FileExists = GltfFileExists;
            callbacks.ExpandFilePath = GltfExpandFilePath;
            callbacks.ReadWholeFile = GltfReadWholeFile;
            callbacks.WriteWholeFile = GltfWriteWholeFile;
            callbacks.GetFileSizeInBytes = GltfGetFileSizeInBytes;
            callbacks.user_data = &context;
            loader.SetFsCallbacks(callbacks);

            bool loaded = false;
            const auto* bytes = reinterpret_cast<const unsigned char*>(request.SourceBytes.data());
            const auto byteCount = static_cast<unsigned int>(request.SourceBytes.size());
            if (SourceLooksLikeGlb(request))
            {
                loaded = loader.LoadBinaryFromMemory(
                    &model,
                    &error,
                    &warning,
                    bytes,
                    byteCount);
            }
            else
            {
                std::string json{
                    reinterpret_cast<const char*>(request.SourceBytes.data()),
                    request.SourceBytes.size()};
                std::string baseDir = request.BasePath;
                if (!baseDir.empty() && baseDir.back() != '/' && baseDir.back() != '\\')
                {
                    baseDir.push_back('/');
                }
                loaded = loader.LoadASCIIFromString(
                    &model,
                    &error,
                    &warning,
                    json.c_str(),
                    static_cast<unsigned int>(json.size()),
                    baseDir);
            }

            if (!loaded)
            {
                return Core::Err<tinygltf::Model>(Core::ErrorCode::AssetDecodeFailed);
            }
            return model;
        }

        [[nodiscard]] Core::Expected<AccessorView> MakeAccessorView(
            const tinygltf::Model& model,
            const int accessorIndex,
            const int requiredType)
        {
            if (accessorIndex < 0
                || accessorIndex >= static_cast<int>(model.accessors.size()))
            {
                return Core::Err<AccessorView>(Core::ErrorCode::OutOfRange);
            }
            const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
            if (requiredType != 0 && accessor.type != requiredType)
            {
                return Core::Err<AccessorView>(Core::ErrorCode::InvalidFormat);
            }
            if (accessor.bufferView < 0
                || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
            {
                return Core::Err<AccessorView>(Core::ErrorCode::InvalidFormat);
            }

            const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
            if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
            {
                return Core::Err<AccessorView>(Core::ErrorCode::InvalidFormat);
            }
            const tinygltf::Buffer& buffer = model.buffers[view.buffer];

            const int32_t componentSizeSigned = tinygltf::GetComponentSizeInBytes(
                static_cast<std::uint32_t>(accessor.componentType));
            const int32_t componentCountSigned = tinygltf::GetNumComponentsInType(
                static_cast<std::uint32_t>(accessor.type));
            if (componentSizeSigned <= 0 || componentCountSigned <= 0)
            {
                return Core::Err<AccessorView>(Core::ErrorCode::InvalidFormat);
            }

            const std::size_t componentSize = static_cast<std::size_t>(componentSizeSigned);
            const std::size_t componentCount = static_cast<std::size_t>(componentCountSigned);
            const std::size_t packedStride = componentSize * componentCount;
            const std::size_t stride = view.byteStride == 0u ? packedStride : view.byteStride;
            const std::size_t start = view.byteOffset + accessor.byteOffset;
            if (stride < packedStride || start > buffer.data.size())
            {
                return Core::Err<AccessorView>(Core::ErrorCode::InvalidFormat);
            }
            if (accessor.count > 0u)
            {
                const std::size_t lastOffset = start + stride * (accessor.count - 1u);
                if (lastOffset < start || lastOffset + packedStride < lastOffset
                    || lastOffset + packedStride > buffer.data.size())
                {
                    return Core::Err<AccessorView>(Core::ErrorCode::OutOfRange);
                }
            }

            return AccessorView{
                .Data = buffer.data.data() + start,
                .Count = accessor.count,
                .Stride = stride,
                .ComponentSize = componentSize,
                .ComponentCount = componentCount,
                .ComponentType = accessor.componentType,
                .Normalized = accessor.normalized,
            };
        }

        [[nodiscard]] float DecodeAccessorFloat(
            const int componentType,
            const bool normalized,
            const unsigned char* source) noexcept
        {
            switch (componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
            {
                float value = 0.0f;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                std::uint16_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_SHORT:
            {
                std::int16_t value = 0;
                std::memcpy(&value, source, sizeof(value));
                if (!normalized)
                {
                    return static_cast<float>(value);
                }
                return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                std::uint8_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_BYTE:
            {
                std::int8_t value = 0;
                std::memcpy(&value, source, sizeof(value));
                if (!normalized)
                {
                    return static_cast<float>(value);
                }
                return std::clamp(static_cast<float>(value) / 127.0f, -1.0f, 1.0f);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                std::uint32_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return normalized ? static_cast<float>(value) / 4294967295.0f : static_cast<float>(value);
            }
            default:
                return 0.0f;
            }
        }

        [[nodiscard]] Core::Expected<std::vector<glm::vec3>> LoadVec3Accessor(
            const tinygltf::Model& model,
            const int accessorIndex)
        {
            auto view = MakeAccessorView(model, accessorIndex, TINYGLTF_TYPE_VEC3);
            if (!view.has_value())
            {
                return Core::Err<std::vector<glm::vec3>>(view.error());
            }

            std::vector<glm::vec3> values(view->Count, glm::vec3(0.0f));
            for (std::size_t i = 0u; i < view->Count; ++i)
            {
                const unsigned char* element = view->Data + i * view->Stride;
                values[i] = glm::vec3(
                    DecodeAccessorFloat(view->ComponentType, view->Normalized, element),
                    DecodeAccessorFloat(view->ComponentType, view->Normalized, element + view->ComponentSize),
                    DecodeAccessorFloat(view->ComponentType, view->Normalized, element + 2u * view->ComponentSize));
            }
            return values;
        }

        [[nodiscard]] Core::Expected<std::vector<glm::vec2>> LoadVec2Accessor(
            const tinygltf::Model& model,
            const int accessorIndex)
        {
            auto view = MakeAccessorView(model, accessorIndex, TINYGLTF_TYPE_VEC2);
            if (!view.has_value())
            {
                return Core::Err<std::vector<glm::vec2>>(view.error());
            }

            std::vector<glm::vec2> values(view->Count, glm::vec2(0.0f));
            for (std::size_t i = 0u; i < view->Count; ++i)
            {
                const unsigned char* element = view->Data + i * view->Stride;
                values[i] = glm::vec2(
                    DecodeAccessorFloat(view->ComponentType, view->Normalized, element),
                    DecodeAccessorFloat(view->ComponentType, view->Normalized, element + view->ComponentSize));
            }
            return values;
        }

        [[nodiscard]] std::uint32_t DecodeIndex(
            const int componentType,
            const unsigned char* source) noexcept
        {
            switch (componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                std::uint32_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                std::uint16_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                std::uint8_t value = 0u;
                std::memcpy(&value, source, sizeof(value));
                return value;
            }
            default:
                return std::numeric_limits<std::uint32_t>::max();
            }
        }

        [[nodiscard]] Core::Expected<std::vector<std::uint32_t>> LoadIndexAccessor(
            const tinygltf::Model& model,
            const int accessorIndex)
        {
            auto view = MakeAccessorView(model, accessorIndex, TINYGLTF_TYPE_SCALAR);
            if (!view.has_value())
            {
                return Core::Err<std::vector<std::uint32_t>>(view.error());
            }
            if (view->ComponentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT
                && view->ComponentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT
                && view->ComponentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
            {
                return Core::Err<std::vector<std::uint32_t>>(Core::ErrorCode::InvalidFormat);
            }

            std::vector<std::uint32_t> indices(view->Count, 0u);
            for (std::size_t i = 0u; i < view->Count; ++i)
            {
                indices[i] = DecodeIndex(view->ComponentType, view->Data + i * view->Stride);
            }
            return indices;
        }

        [[nodiscard]] Core::Expected<std::vector<std::uint32_t>> PrimitiveIndices(
            const tinygltf::Model& model,
            const tinygltf::Primitive& primitive,
            const std::size_t vertexCount)
        {
            if (primitive.indices >= 0)
            {
                return LoadIndexAccessor(model, primitive.indices);
            }
            if (vertexCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                return Core::Err<std::vector<std::uint32_t>>(Core::ErrorCode::OutOfRange);
            }
            std::vector<std::uint32_t> indices(vertexCount, 0u);
            for (std::size_t i = 0u; i < vertexCount; ++i)
            {
                indices[i] = static_cast<std::uint32_t>(i);
            }
            return indices;
        }

        [[nodiscard]] Core::Expected<std::vector<std::vector<std::uint32_t>>> BuildTriangleFaces(
            const tinygltf::Primitive& primitive,
            std::vector<std::uint32_t> indices)
        {
            std::vector<std::vector<std::uint32_t>> faces{};
            const int mode = primitive.mode < 0 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
            switch (mode)
            {
            case TINYGLTF_MODE_TRIANGLES:
                if (indices.size() % 3u != 0u)
                {
                    return Core::Err<std::vector<std::vector<std::uint32_t>>>(Core::ErrorCode::InvalidFormat);
                }
                faces.reserve(indices.size() / 3u);
                for (std::size_t i = 0u; i < indices.size(); i += 3u)
                {
                    faces.push_back({indices[i], indices[i + 1u], indices[i + 2u]});
                }
                return faces;
            case TINYGLTF_MODE_TRIANGLE_STRIP:
                if (indices.size() < 3u)
                {
                    return Core::Err<std::vector<std::vector<std::uint32_t>>>(Core::ErrorCode::InvalidFormat);
                }
                faces.reserve(indices.size() - 2u);
                for (std::size_t i = 0u; i + 2u < indices.size(); ++i)
                {
                    if ((i % 2u) == 0u)
                    {
                        faces.push_back({indices[i], indices[i + 1u], indices[i + 2u]});
                    }
                    else
                    {
                        faces.push_back({indices[i + 1u], indices[i], indices[i + 2u]});
                    }
                }
                return faces;
            case TINYGLTF_MODE_TRIANGLE_FAN:
                if (indices.size() < 3u)
                {
                    return Core::Err<std::vector<std::vector<std::uint32_t>>>(Core::ErrorCode::InvalidFormat);
                }
                faces.reserve(indices.size() - 2u);
                for (std::size_t i = 1u; i + 1u < indices.size(); ++i)
                {
                    faces.push_back({indices[0], indices[i], indices[i + 1u]});
                }
                return faces;
            default:
                return Core::Err<std::vector<std::vector<std::uint32_t>>>(Core::ErrorCode::AssetUnsupportedFormat);
            }
        }

        [[nodiscard]] Core::Expected<Geometry::MeshIO::MeshIOResult> BuildMeshPayload(
            const tinygltf::Model& model,
            const tinygltf::Primitive& primitive,
            const std::string& sourcePath,
            const std::string& basePath)
        {
            const auto positionIt = primitive.attributes.find("POSITION");
            if (positionIt == primitive.attributes.end())
            {
                return Core::Err<Geometry::MeshIO::MeshIOResult>(Core::ErrorCode::AssetInvalidData);
            }

            auto positions = LoadVec3Accessor(model, positionIt->second);
            if (!positions.has_value() || positions->empty())
            {
                return Core::Err<Geometry::MeshIO::MeshIOResult>(
                    positions.has_value() ? Core::ErrorCode::AssetInvalidData : positions.error());
            }

            auto indices = PrimitiveIndices(model, primitive, positions->size());
            if (!indices.has_value())
            {
                return Core::Err<Geometry::MeshIO::MeshIOResult>(indices.error());
            }
            auto faces = BuildTriangleFaces(primitive, std::move(*indices));
            if (!faces.has_value() || faces->empty())
            {
                return Core::Err<Geometry::MeshIO::MeshIOResult>(
                    faces.has_value() ? Core::ErrorCode::AssetInvalidData : faces.error());
            }

            Geometry::MeshIO::MeshIOResult result{};
            result.SourcePath = sourcePath;
            result.BasePath = basePath;
            result.Vertices.Resize(positions->size());
            auto pointProperty = result.Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3(0.0f));
            for (std::size_t i = 0u; i < positions->size(); ++i)
            {
                pointProperty[i] = (*positions)[i];
            }

            const auto normalIt = primitive.attributes.find("NORMAL");
            if (normalIt != primitive.attributes.end())
            {
                auto normals = LoadVec3Accessor(model, normalIt->second);
                if (normals.has_value() && normals->size() == positions->size())
                {
                    auto normalProperty = result.Vertices.GetOrAdd<glm::vec3>(
                        "v:normal",
                        glm::vec3(0.0f, 1.0f, 0.0f));
                    for (std::size_t i = 0u; i < normals->size(); ++i)
                    {
                        normalProperty[i] = (*normals)[i];
                    }
                }
            }

            const auto uvIt = primitive.attributes.find("TEXCOORD_0");
            if (uvIt != primitive.attributes.end())
            {
                auto uvs = LoadVec2Accessor(model, uvIt->second);
                if (uvs.has_value() && uvs->size() == positions->size())
                {
                    auto uvProperty = result.Vertices.GetOrAdd<glm::vec2>(
                        "v:texcoord",
                        glm::vec2(0.0f));
                    for (std::size_t i = 0u; i < uvs->size(); ++i)
                    {
                        uvProperty[i] = (*uvs)[i];
                    }
                }
            }

            result.Faces.Resize(faces->size());
            auto faceProperty = result.Faces.GetOrAdd<std::vector<std::uint32_t>>(
                "f:vertices",
                {});
            for (std::size_t i = 0u; i < faces->size(); ++i)
            {
                faceProperty[i] = std::move((*faces)[i]);
            }
            return result;
        }

        [[nodiscard]] Core::Expected<Assets::AssetTexture2DPayload> MakeTinyGltfImagePayload(
            const tinygltf::Image& image,
            const Assets::AssetFileFormat format,
            const std::string& fallbackSourcePath)
        {
            if (!Assets::IsSupportedTextureImportFormat(format))
            {
                return Core::Err<Assets::AssetTexture2DPayload>(
                    Core::ErrorCode::AssetUnsupportedFormat);
            }
            if (image.width <= 0 || image.height <= 0 || image.component <= 0
                || image.component > 4 || image.bits != 8 || image.image.empty())
            {
                return Core::Err<Assets::AssetTexture2DPayload>(Core::ErrorCode::AssetInvalidData);
            }

            const std::size_t width = static_cast<std::size_t>(image.width);
            const std::size_t height = static_cast<std::size_t>(image.height);
            const std::size_t components = static_cast<std::size_t>(image.component);
            const std::size_t texelCount = width * height;
            if (height != 0u && width > std::numeric_limits<std::size_t>::max() / height)
            {
                return Core::Err<Assets::AssetTexture2DPayload>(Core::ErrorCode::OutOfRange);
            }
            if (texelCount > std::numeric_limits<std::size_t>::max() / 4u
                || image.image.size() < texelCount * components)
            {
                return Core::Err<Assets::AssetTexture2DPayload>(Core::ErrorCode::OutOfRange);
            }

            std::vector<std::byte> pixels(texelCount * 4u, std::byte{0});
            for (std::size_t i = 0u; i < texelCount; ++i)
            {
                const unsigned char* src = image.image.data() + i * components;
                unsigned char* dst = reinterpret_cast<unsigned char*>(pixels.data() + i * 4u);
                dst[0] = src[0];
                dst[1] = components > 1u ? src[1] : src[0];
                dst[2] = components > 2u ? src[2] : src[0];
                dst[3] = components > 3u ? src[3] : 0xFFu;
            }

            return Assets::AssetTexture2DPayload{
                .Metadata = Assets::AssetTexture2DMetadata{
                    .Width = static_cast<std::uint32_t>(image.width),
                    .Height = static_cast<std::uint32_t>(image.height),
                    .Components = 4u,
                    .PixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm,
                    .ColorSpace = Assets::AssetTextureColorSpace::SRGB,
                    .SourceKind = image.uri.empty() || image.uri.starts_with("data:")
                        ? Assets::AssetTextureSourceKind::Embedded
                        : Assets::AssetTextureSourceKind::ExternalFile,
                    .SourceFormat = format,
                    .SourcePath = image.uri.empty() ? fallbackSourcePath : image.uri,
                    .DebugName = image.name,
                },
                .PixelBytes = std::move(pixels),
            };
        }

        template <class TextureInfoT>
        [[nodiscard]] Assets::AssetModelTextureReference ResolveTextureReference(
            const tinygltf::Model& model,
            const std::span<const std::uint32_t> imageToPayload,
            const TextureInfoT& textureInfo)
        {
            if (textureInfo.index < 0
                || textureInfo.index >= static_cast<int>(model.textures.size()))
            {
                return {};
            }
            const int source = model.textures[textureInfo.index].source;
            if (source < 0 || source >= static_cast<int>(imageToPayload.size()))
            {
                return {};
            }
            const std::uint32_t payloadIndex = imageToPayload[static_cast<std::size_t>(source)];
            if (payloadIndex == Assets::kInvalidAssetModelIndex)
            {
                return {};
            }
            return Assets::AssetModelTextureReference{
                .ImageIndex = payloadIndex,
                .Uri = model.images[static_cast<std::size_t>(source)].uri,
            };
        }

        [[nodiscard]] Assets::AssetModelMaterialPayload BuildMaterialPayload(
            const tinygltf::Model& model,
            const tinygltf::Material& material,
            const std::span<const std::uint32_t> imageToPayload)
        {
            Assets::AssetModelMaterialPayload payload{};
            payload.Name = material.name;
            if (material.pbrMetallicRoughness.baseColorFactor.size() == 4u)
            {
                for (std::size_t i = 0u; i < 4u; ++i)
                {
                    payload.BaseColorFactor[i] = static_cast<float>(
                        material.pbrMetallicRoughness.baseColorFactor[i]);
                }
            }
            payload.MetallicFactor = static_cast<float>(
                material.pbrMetallicRoughness.metallicFactor);
            payload.RoughnessFactor = static_cast<float>(
                material.pbrMetallicRoughness.roughnessFactor);
            payload.BaseColorTexture = ResolveTextureReference(
                model,
                imageToPayload,
                material.pbrMetallicRoughness.baseColorTexture);
            payload.MetallicRoughnessTexture = ResolveTextureReference(
                model,
                imageToPayload,
                material.pbrMetallicRoughness.metallicRoughnessTexture);
            payload.NormalTexture = ResolveTextureReference(
                model,
                imageToPayload,
                material.normalTexture);
            payload.OcclusionTexture = ResolveTextureReference(
                model,
                imageToPayload,
                material.occlusionTexture);
            return payload;
        }

        [[nodiscard]] Core::Expected<Assets::AssetModelScenePayload> BuildScenePayload(
            tinygltf::Model model,
            const Assets::AssetModelTextureIORequest& request,
            std::vector<Assets::AssetModelExternalResourceDiagnostic> diagnostics)
        {
            Assets::AssetModelScenePayload payload{};
            payload.SourcePath = request.Path;
            payload.ExternalResourceDiagnostics = std::move(diagnostics);

            std::vector<std::uint32_t> imageToPayload(
                model.images.size(),
                Assets::kInvalidAssetModelIndex);
            for (std::size_t i = 0u; i < model.images.size(); ++i)
            {
                const tinygltf::Image& image = model.images[i];
                const Assets::AssetFileFormat format = GuessTextureFormat(
                    image.uri,
                    image.mimeType);
                auto imagePayload = MakeTinyGltfImagePayload(image, format, request.Path);
                if (imagePayload.has_value())
                {
                    imageToPayload[i] = static_cast<std::uint32_t>(payload.EmbeddedImages.size());
                    payload.EmbeddedImages.push_back(std::move(*imagePayload));
                }
            }

            payload.Materials.reserve(model.materials.size());
            for (const tinygltf::Material& material : model.materials)
            {
                payload.Materials.push_back(BuildMaterialPayload(model, material, imageToPayload));
            }

            for (std::size_t meshIndex = 0u; meshIndex < model.meshes.size(); ++meshIndex)
            {
                const tinygltf::Mesh& mesh = model.meshes[meshIndex];
                for (std::size_t primitiveIndex = 0u;
                     primitiveIndex < mesh.primitives.size();
                     ++primitiveIndex)
                {
                    const tinygltf::Primitive& primitive = mesh.primitives[primitiveIndex];
                    auto meshPayload = BuildMeshPayload(
                        model,
                        primitive,
                        request.Path,
                        request.BasePath);
                    if (!meshPayload.has_value())
                    {
                        if (meshPayload.error() == Core::ErrorCode::AssetUnsupportedFormat)
                        {
                            continue;
                        }
                        return Core::Err<Assets::AssetModelScenePayload>(meshPayload.error());
                    }

                    const std::uint32_t geometryIndex = static_cast<std::uint32_t>(
                        payload.GeometryPayloads.size());
                    const std::size_t vertexCount = meshPayload->Vertices.Size();
                    const auto faceVertices = meshPayload->Faces.Get<std::vector<std::uint32_t>>(
                        "f:vertices");
                    std::uint32_t indexCount = 0u;
                    if (faceVertices)
                    {
                        for (const std::vector<std::uint32_t>& face : faceVertices.Vector())
                        {
                            indexCount += static_cast<std::uint32_t>(face.size());
                        }
                    }

                    payload.GeometryPayloads.push_back(Assets::AssetGeometryPayload::Make(
                        Assets::AssetPayloadKind::Mesh,
                        std::move(*meshPayload),
                        "Geometry::MeshIO::MeshIOResult"));
                    payload.Primitives.push_back(Assets::AssetModelPrimitivePayload{
                        .Name = mesh.name.empty()
                            ? "mesh-" + std::to_string(meshIndex) + "-primitive-" + std::to_string(primitiveIndex)
                            : mesh.name,
                        .GeometryKind = Assets::AssetPayloadKind::Mesh,
                        .GeometryPayloadIndex = geometryIndex,
                        .MaterialIndex = primitive.material >= 0
                            ? static_cast<std::uint32_t>(primitive.material)
                            : Assets::kInvalidAssetModelIndex,
                        .VertexCount = static_cast<std::uint32_t>(vertexCount),
                        .IndexCount = indexCount,
                    });
                }
            }

            if (payload.Primitives.empty())
            {
                return Core::Err<Assets::AssetModelScenePayload>(Core::ErrorCode::AssetInvalidData);
            }
            return payload;
        }

        [[nodiscard]] Core::Expected<Assets::AssetTexture2DPayload> DecodeStbTexture(
            const Assets::AssetModelTextureIORequest& request)
        {
            if (request.SourceBytes.empty()
                || request.SourceBytes.size()
                    > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return Core::Err<Assets::AssetTexture2DPayload>(Core::ErrorCode::AssetInvalidData);
            }

            int width = 0;
            int height = 0;
            int components = 0;
            const auto* source = reinterpret_cast<const stbi_uc*>(request.SourceBytes.data());
            const int sourceSize = static_cast<int>(request.SourceBytes.size());
            if (request.Route.Format == Assets::AssetFileFormat::HDR)
            {
                float* pixels = stbi_loadf_from_memory(
                    source,
                    sourceSize,
                    &width,
                    &height,
                    &components,
                    STBI_rgb_alpha);
                if (pixels == nullptr || width <= 0 || height <= 0)
                {
                    if (pixels != nullptr)
                    {
                        stbi_image_free(pixels);
                    }
                    return Core::Err<Assets::AssetTexture2DPayload>(Core::ErrorCode::AssetDecodeFailed);
                }

                const std::size_t byteCount = static_cast<std::size_t>(width)
                    * static_cast<std::size_t>(height)
                    * 4u
                    * sizeof(float);
                std::vector<std::byte> bytes(byteCount, std::byte{0});
                std::memcpy(bytes.data(), pixels, byteCount);
                stbi_image_free(pixels);
                return Assets::AssetTexture2DPayload{
                    .Metadata = Assets::AssetTexture2DMetadata{
                        .Width = static_cast<std::uint32_t>(width),
                        .Height = static_cast<std::uint32_t>(height),
                        .Components = 4u,
                        .PixelFormat = Assets::AssetTexturePixelFormat::Rgba32Float,
                        .ColorSpace = Assets::AssetTextureColorSpace::Linear,
                        .SourceKind = Assets::AssetTextureSourceKind::ExternalFile,
                        .SourceFormat = request.Route.Format,
                        .SourcePath = request.Path,
                    },
                    .PixelBytes = std::move(bytes),
                };
            }

            stbi_uc* pixels = stbi_load_from_memory(
                source,
                sourceSize,
                &width,
                &height,
                &components,
                STBI_rgb_alpha);
            if (pixels == nullptr || width <= 0 || height <= 0)
            {
                if (pixels != nullptr)
                {
                    stbi_image_free(pixels);
                }
                return Core::Err<Assets::AssetTexture2DPayload>(Core::ErrorCode::AssetDecodeFailed);
            }

            const std::size_t byteCount = static_cast<std::size_t>(width)
                * static_cast<std::size_t>(height)
                * 4u;
            std::vector<std::byte> bytes(byteCount, std::byte{0});
            std::memcpy(bytes.data(), pixels, byteCount);
            stbi_image_free(pixels);
            return Assets::AssetTexture2DPayload{
                .Metadata = Assets::AssetTexture2DMetadata{
                    .Width = static_cast<std::uint32_t>(width),
                    .Height = static_cast<std::uint32_t>(height),
                    .Components = 4u,
                    .PixelFormat = Assets::AssetTexturePixelFormat::Rgba8Unorm,
                    .ColorSpace = Assets::AssetTextureColorSpace::SRGB,
                    .SourceKind = Assets::AssetTextureSourceKind::ExternalFile,
                    .SourceFormat = request.Route.Format,
                    .SourcePath = request.Path,
                },
                .PixelBytes = std::move(bytes),
            };
        }

        [[nodiscard]] Core::Expected<Assets::AssetModelScenePayload> DecodeGltfScene(
            const Assets::AssetModelTextureIORequest& request)
        {
            std::vector<Assets::AssetModelExternalResourceDiagnostic> diagnostics{};
            auto model = LoadGltfModel(request, diagnostics);
            if (!model.has_value())
            {
                return Core::Err<Assets::AssetModelScenePayload>(model.error());
            }
            return BuildScenePayload(std::move(*model), request, std::move(diagnostics));
        }
    }

    Core::Result RegisterPromotedModelTextureIOCallbacks(
        Assets::AssetModelTextureIOBridge& bridge)
    {
        auto result = bridge.RegisterModelSceneImporter(
            Assets::AssetFileFormat::GLTF,
            DecodeGltfScene);
        result = Combine(
            std::move(result),
            bridge.RegisterModelSceneImporter(Assets::AssetFileFormat::GLB, DecodeGltfScene));
        result = Combine(
            std::move(result),
            bridge.RegisterTextureImporter(Assets::AssetFileFormat::PNG, DecodeStbTexture));
        result = Combine(
            std::move(result),
            bridge.RegisterTextureImporter(Assets::AssetFileFormat::JPEG, DecodeStbTexture));
        result = Combine(
            std::move(result),
            bridge.RegisterTextureImporter(Assets::AssetFileFormat::TGA, DecodeStbTexture));
        result = Combine(
            std::move(result),
            bridge.RegisterTextureImporter(Assets::AssetFileFormat::BMP, DecodeStbTexture));
        result = Combine(
            std::move(result),
            bridge.RegisterTextureImporter(Assets::AssetFileFormat::HDR, DecodeStbTexture));
        return result;
    }
}
