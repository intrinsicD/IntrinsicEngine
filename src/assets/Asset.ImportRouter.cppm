module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module Extrinsic.Asset.ImportRouter;

import Extrinsic.Core.Error;

export namespace Extrinsic::Assets
{
    enum class AssetPayloadKind : std::uint8_t
    {
        Unknown,
        Mesh,
        PointCloud,
        Graph,
        ModelScene,
        Texture2D,
    };

    enum class AssetFileFormat : std::uint8_t
    {
        OBJ = 0,
        OFF = 1,
        STL = 2,
        PLY = 3,
        XYZ = 4,
        PTS = 5,
        XYZRGB = 6,
        PCD = 7,
        TGF = 8,
        EdgeList = 9,
        GLTF = 10,
        GLB = 11,
        PNG = 12,
        JPEG = 13,
        TGA = 14,
        BMP = 15,
        HDR = 16,
        KTX = 17,
        Unknown = 0xff,
    };

    enum class AssetRouteOperation : std::uint8_t
    {
        Import,
        Export,
    };

    enum class AssetRouteStatus : std::uint8_t
    {
        Ready,
        MissingExtension,
        UnsupportedExtension,
        AmbiguousPayloadKind,
        PayloadKindNotSupported,
    };

    struct AssetFileFormatInfo
    {
        AssetFileFormat Format{};
        std::string_view CanonicalExtension{};
        std::span<const std::string_view> ExtensionAliases{};
        std::span<const AssetPayloadKind> ImportPayloads{};
        std::span<const AssetPayloadKind> ExportPayloads{};
        bool SupportsBinaryImport{false};
        bool SupportsBinaryExport{false};
    };

    struct AssetImportHint
    {
        AssetPayloadKind PayloadKind{AssetPayloadKind::Unknown};
    };

    struct AssetImportRoute
    {
        AssetFileFormat Format{};
        AssetRouteOperation Operation{AssetRouteOperation::Import};
        AssetPayloadKind PayloadKind{AssetPayloadKind::Unknown};
        std::string CanonicalExtension{};
        bool PayloadHintRequired{false};
    };

    struct AssetRouteDiagnostic
    {
        AssetRouteStatus Status{AssetRouteStatus::Ready};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Extension{};
        AssetPayloadKind RequestedPayloadKind{AssetPayloadKind::Unknown};
        std::string Message{};
    };

    [[nodiscard]] std::span<const AssetFileFormatInfo> SupportedAssetFileFormats() noexcept;

    [[nodiscard]] const AssetFileFormatInfo* FindAssetFileFormat(
        std::string_view pathOrExtension) noexcept;

    [[nodiscard]] Core::Expected<AssetImportRoute> ResolveAssetImportRoute(
        std::string_view pathOrExtension,
        AssetRouteOperation operation = AssetRouteOperation::Import,
        AssetImportHint hint = {});

    [[nodiscard]] AssetRouteDiagnostic DiagnoseAssetImportRoute(
        std::string_view pathOrExtension,
        AssetRouteOperation operation = AssetRouteOperation::Import,
        AssetImportHint hint = {});

    [[nodiscard]] const char* DebugNameForAssetPayloadKind(
        AssetPayloadKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForAssetFileFormat(
        AssetFileFormat format) noexcept;

    [[nodiscard]] const char* DebugNameForAssetRouteStatus(
        AssetRouteStatus status) noexcept;
}
