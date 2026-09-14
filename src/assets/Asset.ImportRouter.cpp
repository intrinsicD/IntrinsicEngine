module;

#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

module Extrinsic.Asset.ImportRouter;

namespace Extrinsic::Assets
{
    namespace
    {
        using Payload = AssetPayloadKind;

        inline constexpr std::array<Payload, 0> NoDomains{};
        inline constexpr auto MeshOnly = std::to_array({Payload::Mesh});
        inline constexpr auto PointCloudOnly = std::to_array({Payload::PointCloud});
        inline constexpr auto GraphOnly = std::to_array({Payload::Graph});
        inline constexpr auto ModelSceneOnly = std::to_array({Payload::ModelScene});
        inline constexpr auto TextureOnly = std::to_array({Payload::Texture2D});
        inline constexpr auto MeshAndPointCloud = std::to_array({
            Payload::Mesh,
            Payload::PointCloud});

#define INTRINSIC_GEOMETRY_FORMAT(Name, Canonical, ImportDomains, ExportDomains, BinaryImport, BinaryExport, ...) \
    inline constexpr auto Aliases_##Name = std::to_array<std::string_view>({__VA_ARGS__});
#include "Core.GeometryFormatCatalog.inc"
#undef INTRINSIC_GEOMETRY_FORMAT

        inline constexpr auto Aliases_GLTF = std::to_array<std::string_view>({"gltf"});
        inline constexpr auto Aliases_GLB = std::to_array<std::string_view>({"glb"});
        inline constexpr auto Aliases_PNG = std::to_array<std::string_view>({"png"});
        inline constexpr auto Aliases_JPEG = std::to_array<std::string_view>({"jpg", "jpeg"});
        inline constexpr auto Aliases_TGA = std::to_array<std::string_view>({"tga"});
        inline constexpr auto Aliases_BMP = std::to_array<std::string_view>({"bmp"});
        inline constexpr auto Aliases_HDR = std::to_array<std::string_view>({"hdr"});
        inline constexpr auto Aliases_KTX = std::to_array<std::string_view>({"ktx", "ktx2"});

#define INTRINSIC_GEOMETRY_FORMAT(Name, Canonical, ImportDomains, ExportDomains, BinaryImport, BinaryExport, ...) \
    AssetFileFormatInfo{AssetFileFormat::Name, Canonical, Aliases_##Name, ImportDomains, ExportDomains, BinaryImport, BinaryExport},
        inline constexpr auto Formats = std::to_array<AssetFileFormatInfo>({
#include "Core.GeometryFormatCatalog.inc"
            {AssetFileFormat::GLTF, "gltf", Aliases_GLTF, ModelSceneOnly, NoDomains, false, false},
            {AssetFileFormat::GLB, "glb", Aliases_GLB, ModelSceneOnly, NoDomains, true, false},
            {AssetFileFormat::PNG, "png", Aliases_PNG, TextureOnly, NoDomains, true, false},
            {AssetFileFormat::JPEG, "jpg", Aliases_JPEG, TextureOnly, NoDomains, true, false},
            {AssetFileFormat::TGA, "tga", Aliases_TGA, TextureOnly, NoDomains, true, false},
            {AssetFileFormat::BMP, "bmp", Aliases_BMP, TextureOnly, NoDomains, true, false},
            {AssetFileFormat::HDR, "hdr", Aliases_HDR, TextureOnly, NoDomains, true, false},
            {AssetFileFormat::KTX, "ktx", Aliases_KTX, TextureOnly, NoDomains, true, false},
        });
#undef INTRINSIC_GEOMETRY_FORMAT

        [[nodiscard]] constexpr char ToLowerAscii(const char c) noexcept
        {
            return c >= 'A' && c <= 'Z'
                ? static_cast<char>(c - 'A' + 'a')
                : c;
        }

        [[nodiscard]] bool ExtensionEquals(
            const std::string_view lhs,
            const std::string_view rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;

            for (std::size_t i = 0u; i < lhs.size(); ++i)
            {
                if (ToLowerAscii(lhs[i]) != ToLowerAscii(rhs[i]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::string ExtractExtension(
            const std::string_view pathOrExtension)
        {
            if (pathOrExtension.empty())
                return {};

            const std::size_t slash = pathOrExtension.find_last_of("/\\");
            const bool containsPathSeparator = slash != std::string_view::npos;
            const std::string_view filename = containsPathSeparator
                ? pathOrExtension.substr(slash + 1u)
                : pathOrExtension;
            if (filename.empty())
                return {};

            std::string_view extension = filename;
            const std::size_t dot = filename.find_last_of('.');
            if (dot != std::string_view::npos)
            {
                if (dot + 1u >= filename.size())
                    return {};
                extension = filename.substr(dot + 1u);
            }
            else if (containsPathSeparator)
            {
                return {};
            }

            while (!extension.empty() && extension.front() == '.')
                extension.remove_prefix(1u);

            std::string normalized{};
            normalized.reserve(extension.size());
            for (const char c : extension)
                normalized.push_back(ToLowerAscii(c));
            return normalized;
        }

        [[nodiscard]] std::span<const AssetPayloadKind> PayloadsFor(
            const AssetFileFormatInfo& info,
            const AssetRouteOperation operation) noexcept
        {
            return operation == AssetRouteOperation::Import
                ? info.ImportPayloads
                : info.ExportPayloads;
        }

        [[nodiscard]] bool HasPayloadKind(
            const std::span<const AssetPayloadKind> payloads,
            const AssetPayloadKind payload) noexcept
        {
            for (const AssetPayloadKind candidate : payloads)
            {
                if (candidate == payload)
                    return true;
            }
            return false;
        }

        [[nodiscard]] std::string BuildDiagnosticMessage(
            const AssetRouteStatus status,
            const std::string_view extension,
            const AssetPayloadKind requested)
        {
            std::string message{};
            switch (status)
            {
            case AssetRouteStatus::Ready:
                return "Asset route is ready.";
            case AssetRouteStatus::MissingExtension:
                return "Asset route requires a file extension.";
            case AssetRouteStatus::UnsupportedExtension:
                message = "Unsupported asset extension";
                break;
            case AssetRouteStatus::AmbiguousPayloadKind:
                message = "Asset extension requires an explicit payload/domain hint";
                break;
            case AssetRouteStatus::PayloadKindNotSupported:
                message = "Asset extension does not support requested payload/domain";
                break;
            }

            if (!extension.empty())
            {
                message += " '";
                message += extension;
                message += "'";
            }
            if (requested != AssetPayloadKind::Unknown)
            {
                message += " for ";
                message += DebugNameForAssetPayloadKind(requested);
            }
            message += ".";
            return message;
        }

        struct RouteEvaluation
        {
            AssetImportRoute Route{};
            AssetRouteDiagnostic Diagnostic{};
        };

        [[nodiscard]] RouteEvaluation EvaluateRoute(
            const std::string_view pathOrExtension,
            const AssetRouteOperation operation,
            const AssetImportHint hint)
        {
            const std::string extension = ExtractExtension(pathOrExtension);
            if (extension.empty())
            {
                return RouteEvaluation{
                    .Diagnostic = AssetRouteDiagnostic{
                        .Status = AssetRouteStatus::MissingExtension,
                        .Error = Core::ErrorCode::InvalidPath,
                        .Extension = {},
                        .RequestedPayloadKind = hint.PayloadKind,
                        .Message = BuildDiagnosticMessage(
                            AssetRouteStatus::MissingExtension,
                            {},
                            hint.PayloadKind),
                    },
                };
            }

            const AssetFileFormatInfo* info = FindAssetFileFormat(extension);
            if (info == nullptr)
            {
                return RouteEvaluation{
                    .Diagnostic = AssetRouteDiagnostic{
                        .Status = AssetRouteStatus::UnsupportedExtension,
                        .Error = Core::ErrorCode::AssetUnsupportedFormat,
                        .Extension = extension,
                        .RequestedPayloadKind = hint.PayloadKind,
                        .Message = BuildDiagnosticMessage(
                            AssetRouteStatus::UnsupportedExtension,
                            extension,
                            hint.PayloadKind),
                    },
                };
            }

            const std::span<const AssetPayloadKind> payloads =
                PayloadsFor(*info, operation);
            if (payloads.empty())
            {
                return RouteEvaluation{
                    .Diagnostic = AssetRouteDiagnostic{
                        .Status = AssetRouteStatus::PayloadKindNotSupported,
                        .Error = Core::ErrorCode::AssetUnsupportedFormat,
                        .Extension = extension,
                        .RequestedPayloadKind = hint.PayloadKind,
                        .Message = BuildDiagnosticMessage(
                            AssetRouteStatus::PayloadKindNotSupported,
                            extension,
                            hint.PayloadKind),
                    },
                };
            }

            AssetPayloadKind payload = hint.PayloadKind;
            const bool hintRequired = payloads.size() > 1u;
            if (payload == AssetPayloadKind::Unknown)
            {
                if (hintRequired)
                {
                    return RouteEvaluation{
                        .Diagnostic = AssetRouteDiagnostic{
                            .Status = AssetRouteStatus::AmbiguousPayloadKind,
                            .Error = Core::ErrorCode::InvalidArgument,
                            .Extension = extension,
                            .RequestedPayloadKind = hint.PayloadKind,
                            .Message = BuildDiagnosticMessage(
                                AssetRouteStatus::AmbiguousPayloadKind,
                                extension,
                                hint.PayloadKind),
                        },
                    };
                }
                payload = payloads.front();
            }
            else if (!HasPayloadKind(payloads, payload))
            {
                return RouteEvaluation{
                    .Diagnostic = AssetRouteDiagnostic{
                        .Status = AssetRouteStatus::PayloadKindNotSupported,
                        .Error = Core::ErrorCode::InvalidArgument,
                        .Extension = extension,
                        .RequestedPayloadKind = hint.PayloadKind,
                        .Message = BuildDiagnosticMessage(
                            AssetRouteStatus::PayloadKindNotSupported,
                            extension,
                            hint.PayloadKind),
                    },
                };
            }

            return RouteEvaluation{
                .Route = AssetImportRoute{
                    .Format = info->Format,
                    .Operation = operation,
                    .PayloadKind = payload,
                    .CanonicalExtension = std::string(info->CanonicalExtension),
                    .PayloadHintRequired = hintRequired,
                },
                .Diagnostic = AssetRouteDiagnostic{
                    .Status = AssetRouteStatus::Ready,
                    .Error = Core::ErrorCode::Success,
                    .Extension = extension,
                    .RequestedPayloadKind = hint.PayloadKind,
                    .Message = BuildDiagnosticMessage(
                        AssetRouteStatus::Ready,
                        extension,
                        hint.PayloadKind),
                },
            };
        }
    }

    std::span<const AssetFileFormatInfo> SupportedAssetFileFormats() noexcept
    {
        return Formats;
    }

    const AssetFileFormatInfo* FindAssetFileFormat(
        const std::string_view pathOrExtension) noexcept
    {
        const std::string extension = ExtractExtension(pathOrExtension);
        if (extension.empty())
            return nullptr;

        for (const AssetFileFormatInfo& info : Formats)
        {
            if (ExtensionEquals(extension, info.CanonicalExtension))
                return &info;

            for (const std::string_view alias : info.ExtensionAliases)
            {
                if (ExtensionEquals(extension, alias))
                    return &info;
            }
        }
        return nullptr;
    }

    Core::Expected<AssetImportRoute> ResolveAssetImportRoute(
        const std::string_view pathOrExtension,
        const AssetRouteOperation operation,
        const AssetImportHint hint)
    {
        const RouteEvaluation evaluation =
            EvaluateRoute(pathOrExtension, operation, hint);
        if (evaluation.Diagnostic.Status != AssetRouteStatus::Ready)
            return Core::Err<AssetImportRoute>(evaluation.Diagnostic.Error);
        return evaluation.Route;
    }

    AssetRouteDiagnostic DiagnoseAssetImportRoute(
        const std::string_view pathOrExtension,
        const AssetRouteOperation operation,
        const AssetImportHint hint)
    {
        return EvaluateRoute(pathOrExtension, operation, hint).Diagnostic;
    }

    const char* DebugNameForAssetPayloadKind(
        const AssetPayloadKind kind) noexcept
    {
        switch (kind)
        {
        case AssetPayloadKind::Unknown:
            return "Unknown";
        case AssetPayloadKind::Mesh:
            return "Mesh";
        case AssetPayloadKind::PointCloud:
            return "PointCloud";
        case AssetPayloadKind::Graph:
            return "Graph";
        case AssetPayloadKind::ModelScene:
            return "ModelScene";
        case AssetPayloadKind::Texture2D:
            return "Texture2D";
        }
        return "Unknown";
    }

    const char* DebugNameForAssetFileFormat(
        const AssetFileFormat format) noexcept
    {
        switch (format)
        {
        case AssetFileFormat::Unknown:
            return "Unknown";
#define INTRINSIC_GEOMETRY_FORMAT(Name, ...) \
    case AssetFileFormat::Name:              \
        return #Name;
#include "Core.GeometryFormatCatalog.inc"
#undef INTRINSIC_GEOMETRY_FORMAT
        case AssetFileFormat::GLTF:
            return "GLTF";
        case AssetFileFormat::GLB:
            return "GLB";
        case AssetFileFormat::PNG:
            return "PNG";
        case AssetFileFormat::JPEG:
            return "JPEG";
        case AssetFileFormat::TGA:
            return "TGA";
        case AssetFileFormat::BMP:
            return "BMP";
        case AssetFileFormat::HDR:
            return "HDR";
        case AssetFileFormat::KTX:
            return "KTX";
        }
        return "Unknown";
    }

    const char* DebugNameForAssetRouteStatus(
        const AssetRouteStatus status) noexcept
    {
        switch (status)
        {
        case AssetRouteStatus::Ready:
            return "Ready";
        case AssetRouteStatus::MissingExtension:
            return "MissingExtension";
        case AssetRouteStatus::UnsupportedExtension:
            return "UnsupportedExtension";
        case AssetRouteStatus::AmbiguousPayloadKind:
            return "AmbiguousPayloadKind";
        case AssetRouteStatus::PayloadKindNotSupported:
            return "PayloadKindNotSupported";
        }
        return "Unknown";
    }
}
