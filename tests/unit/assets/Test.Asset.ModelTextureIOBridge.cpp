#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTextureIOBridge;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;

using namespace Extrinsic::Assets;
namespace Core = Extrinsic::Core;
namespace CoreIO = Extrinsic::Core::IO;
using Core::ErrorCode;
using Core::Expected;

namespace
{
    struct FakeGeometryPayload
    {
        std::uint32_t VertexCount{0};
    };

    [[nodiscard]] std::vector<std::byte> Bytes(std::string_view text)
    {
        std::vector<std::byte> out;
        out.reserve(text.size());
        for (const char c : text)
        {
            out.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(c)));
        }
        return out;
    }

    class FakeIOBackend final : public CoreIO::IIOBackend
    {
    public:
        void Add(std::string path, std::string_view data)
        {
            Files[std::move(path)] = Bytes(data);
        }

        [[nodiscard]] Expected<CoreIO::IOReadResult> Read(
            const CoreIO::IORequest& request) override
        {
            const auto it = Files.find(request.Path);
            if (it == Files.end())
            {
                return Core::Err<CoreIO::IOReadResult>(ErrorCode::FileNotFound);
            }

            const std::vector<std::byte>& file = it->second;
            if (request.Offset > file.size())
            {
                return Core::Err<CoreIO::IOReadResult>(ErrorCode::OutOfRange);
            }

            const std::size_t available = file.size() - request.Offset;
            const std::size_t size =
                request.Size == 0u ? available : request.Size;
            if (size > available)
            {
                return Core::Err<CoreIO::IOReadResult>(ErrorCode::OutOfRange);
            }

            CoreIO::IOReadResult result{};
            result.Data.assign(
                file.begin() + static_cast<std::ptrdiff_t>(request.Offset),
                file.begin() + static_cast<std::ptrdiff_t>(request.Offset + size));
            return result;
        }

        [[nodiscard]] Core::Result Write(
            const CoreIO::IORequest& request,
            std::span<const std::byte> data) override
        {
            Files[request.Path] = std::vector<std::byte>(data.begin(), data.end());
            return Core::Ok();
        }

    private:
        std::unordered_map<std::string, std::vector<std::byte>> Files{};
    };

    [[nodiscard]] AssetTexture2DPayload MakeTexturePayload(
        AssetFileFormat format,
        std::string sourcePath)
    {
        AssetTexture2DPayload payload{};
        payload.Metadata.Width = 1u;
        payload.Metadata.Height = 1u;
        payload.Metadata.Components = 4u;
        payload.Metadata.PixelFormat = AssetTexturePixelFormat::Rgba8Unorm;
        payload.Metadata.ColorSpace = AssetTextureColorSpace::SRGB;
        payload.Metadata.SourceKind = AssetTextureSourceKind::ExternalFile;
        payload.Metadata.SourceFormat = format;
        payload.Metadata.SourcePath = std::move(sourcePath);
        payload.PixelBytes = {
            std::byte{0x11},
            std::byte{0x22},
            std::byte{0x33},
            std::byte{0x44}};
        return payload;
    }

    [[nodiscard]] AssetModelScenePayload MakeModelPayload(
        std::string sourcePath,
        std::vector<AssetModelExternalResourceDiagnostic> diagnostics = {})
    {
        AssetModelScenePayload payload{};
        payload.SourcePath = std::move(sourcePath);
        payload.GeometryPayloads.push_back(AssetGeometryPayload::Make(
            AssetPayloadKind::Mesh,
            FakeGeometryPayload{.VertexCount = 3u},
            "FakeGeometryPayload"));
        payload.Primitives.push_back(AssetModelPrimitivePayload{
            .Name = "triangle",
            .GeometryKind = AssetPayloadKind::Mesh,
            .GeometryPayloadIndex = 0u,
            .MaterialIndex = kInvalidAssetModelIndex,
            .VertexCount = 3u,
            .IndexCount = 3u});
        payload.ExternalResourceDiagnostics = std::move(diagnostics);
        return payload;
    }
}

TEST(AssetModelTextureIOBridge, TextureImportReadsPrimaryBytesAndValidatesPayload)
{
    FakeIOBackend backend;
    backend.Add("/assets/albedo.png", "png-bytes");

    AssetModelTextureIOBridge bridge;
    ASSERT_TRUE(bridge.RegisterTextureImporter(
        AssetFileFormat::PNG,
        [](const AssetModelTextureIORequest& request)
            -> Expected<AssetTexture2DPayload>
        {
            EXPECT_EQ(request.Route.Format, AssetFileFormat::PNG);
            EXPECT_EQ(request.Route.PayloadKind, AssetPayloadKind::Texture2D);
            EXPECT_EQ(request.Path, "/assets/albedo.png");
            EXPECT_EQ(request.BasePath, "/assets");
            EXPECT_EQ(request.SourceBytes.size(), 9u);
            EXPECT_TRUE(static_cast<bool>(request.ReadExternalResource));
            return MakeTexturePayload(request.Route.Format, request.Path);
        }).has_value());

    auto texture = bridge.ImportTexture2D("/assets/albedo.png", backend);
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->Metadata.SourceFormat, AssetFileFormat::PNG);
    EXPECT_EQ(texture->Metadata.SourcePath, "/assets/albedo.png");
    EXPECT_EQ(texture->PixelBytes.size(), 4u);
}

TEST(AssetModelTextureIOBridge, ModelImportResolvesExternalResourcesRelativeToSource)
{
    FakeIOBackend backend;
    backend.Add("/scene/duck.gltf", "gltf-json");
    backend.Add("/scene/duck.bin", "buffer");

    AssetModelTextureIOBridge bridge;
    ASSERT_TRUE(bridge.RegisterModelSceneImporter(
        AssetFileFormat::GLTF,
        [](const AssetModelTextureIORequest& request)
            -> Expected<AssetModelScenePayload>
        {
            EXPECT_EQ(request.Route.Format, AssetFileFormat::GLTF);
            EXPECT_EQ(request.Route.PayloadKind, AssetPayloadKind::ModelScene);
            EXPECT_EQ(request.BasePath, "/scene");
            EXPECT_EQ(request.SourceBytes.size(), 9u);

            auto external = request.ReadExternalResource("duck.bin");
            EXPECT_TRUE(external.has_value());
            if (external.has_value())
            {
                EXPECT_EQ(external->Uri, "duck.bin");
                EXPECT_EQ(external->ResolvedPath, "/scene/duck.bin");
                EXPECT_EQ(external->Bytes.size(), 6u);
            }

            return MakeModelPayload(
                request.Path,
                {AssetModelExternalResourceDiagnostic{
                    .ResourceKind = AssetModelResourceKind::Buffer,
                    .Status = AssetModelResourceStatus::Ready,
                    .Error = ErrorCode::Success,
                    .Uri = "duck.bin"}});
        }).has_value());

    auto model = bridge.ImportModelScene("/scene/duck.gltf", backend);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->ExternalResourceDiagnostics.size(), 1u);
    EXPECT_EQ(
        model->ExternalResourceDiagnostics[0].Status,
        AssetModelResourceStatus::Ready);
}

TEST(AssetModelTextureIOBridge, ModelCallbacksCanRecordExternalReadDiagnostics)
{
    FakeIOBackend backend;
    backend.Add("/scene/missing-buffer.gltf", "gltf-json");

    AssetModelTextureIOBridge bridge;
    ASSERT_TRUE(bridge.RegisterModelSceneImporter(
        AssetFileFormat::GLTF,
        [](const AssetModelTextureIORequest& request)
            -> Expected<AssetModelScenePayload>
        {
            auto external = request.ReadExternalResource("missing.bin");
            EXPECT_FALSE(external.has_value());
            const ErrorCode readError = external.has_value()
                ? ErrorCode::Success
                : external.error();

            return MakeModelPayload(
                request.Path,
                {AssetModelExternalResourceDiagnostic{
                    .ResourceKind = AssetModelResourceKind::Buffer,
                    .Status = AssetModelResourceStatus::FileReadFailed,
                    .Error = readError,
                    .Uri = "missing.bin",
                    .Message = "external buffer could not be read"}});
        }).has_value());

    auto model = bridge.ImportModelScene("/scene/missing-buffer.gltf", backend);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->ExternalResourceDiagnostics.size(), 1u);
    EXPECT_EQ(
        model->ExternalResourceDiagnostics[0].Error,
        ErrorCode::FileNotFound);
}

TEST(AssetModelTextureIOBridge, RejectsMissingCallbacksAndInvalidRegistrations)
{
    FakeIOBackend backend;
    AssetModelTextureIOBridge bridge;

    auto missing = bridge.ImportTexture2D("/assets/albedo.png", backend);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(), ErrorCode::AssetLoaderMissing);

    auto emptyModelCallback = bridge.RegisterModelSceneImporter(
        AssetFileFormat::GLTF,
        AssetModelSceneImportCallback{});
    ASSERT_FALSE(emptyModelCallback.has_value());
    EXPECT_EQ(emptyModelCallback.error(), ErrorCode::InvalidArgument);

    auto unsupportedModelFormat = bridge.RegisterModelSceneImporter(
        AssetFileFormat::PNG,
        [](const AssetModelTextureIORequest&) -> Expected<AssetModelScenePayload>
        {
            return MakeModelPayload("unused");
        });
    ASSERT_FALSE(unsupportedModelFormat.has_value());
    EXPECT_EQ(
        unsupportedModelFormat.error(),
        ErrorCode::AssetUnsupportedFormat);

    auto unsupportedTextureFormat = bridge.RegisterTextureImporter(
        AssetFileFormat::GLTF,
        [](const AssetModelTextureIORequest&) -> Expected<AssetTexture2DPayload>
        {
            return MakeTexturePayload(AssetFileFormat::PNG, "unused");
        });
    ASSERT_FALSE(unsupportedTextureFormat.has_value());
    EXPECT_EQ(
        unsupportedTextureFormat.error(),
        ErrorCode::AssetUnsupportedFormat);
}

TEST(AssetModelTextureIOBridge, PropagatesReadAndDecodeErrors)
{
    FakeIOBackend backend;
    AssetModelTextureIOBridge bridge;

    ASSERT_TRUE(bridge.RegisterTextureImporter(
        AssetFileFormat::PNG,
        [](const AssetModelTextureIORequest&)
            -> Expected<AssetTexture2DPayload>
        {
            return Core::Err<AssetTexture2DPayload>(
                ErrorCode::AssetDecodeFailed);
        }).has_value());

    auto missingFile = bridge.ImportTexture2D("/assets/missing.png", backend);
    ASSERT_FALSE(missingFile.has_value());
    EXPECT_EQ(missingFile.error(), ErrorCode::FileNotFound);

    backend.Add("/assets/bad.png", "bad");
    auto decodeFailed = bridge.ImportTexture2D("/assets/bad.png", backend);
    ASSERT_FALSE(decodeFailed.has_value());
    EXPECT_EQ(decodeFailed.error(), ErrorCode::AssetDecodeFailed);
}

TEST(AssetModelTextureIOBridge, RejectsDecodedPayloadsThatFailValidation)
{
    FakeIOBackend backend;
    backend.Add("/assets/invalid.png", "png");

    AssetModelTextureIOBridge bridge;
    ASSERT_TRUE(bridge.RegisterTextureImporter(
        AssetFileFormat::PNG,
        [](const AssetModelTextureIORequest&)
            -> Expected<AssetTexture2DPayload>
        {
            return AssetTexture2DPayload{};
        }).has_value());

    auto invalid = bridge.ImportTexture2D("/assets/invalid.png", backend);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), ErrorCode::AssetInvalidData);
}
