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

        inline constexpr std::array<Payload, 0> NoPayloads{};
        inline constexpr std::array<Payload, 1> MeshOnly{Payload::Mesh};
        inline constexpr std::array<Payload, 1> PointCloudOnly{Payload::PointCloud};
        inline constexpr std::array<Payload, 1> GraphOnly{Payload::Graph};
        inline constexpr std::array<Payload, 1> ModelSceneOnly{Payload::ModelScene};
        inline constexpr std::array<Payload, 1> TextureOnly{Payload::Texture2D};
        inline constexpr std::array<Payload, 2> MeshAndPointCloud{
            Payload::Mesh,
            Payload::PointCloud};

        inline constexpr std::array<std::string_view, 1> ObjAliases{"obj"};
        inline constexpr std::array<std::string_view, 1> OffAliases{"off"};
        inline constexpr std::array<std::string_view, 1> StlAliases{"stl"};
        inline constexpr std::array<std::string_view, 1> PlyAliases{"ply"};
        inline constexpr std::array<std::string_view, 1> XyzAliases{"xyz"};
        inline constexpr std::array<std::string_view, 1> PtsAliases{"pts"};
        inline constexpr std::array<std::string_view, 1> XyzRgbAliases{"xyzrgb"};
        inline constexpr std::array<std::string_view, 1> PcdAliases{"pcd"};
        inline constexpr std::array<std::string_view, 1> TgfAliases{"tgf"};
        inline constexpr std::array<std::string_view, 2> EdgeAliases{"edges", "edgelist"};
        inline constexpr std::array<std::string_view, 1> GltfAliases{"gltf"};
        inline constexpr std::array<std::string_view, 1> GlbAliases{"glb"};
        inline constexpr std::array<std::string_view, 1> PngAliases{"png"};
        inline constexpr std::array<std::string_view, 2> JpegAliases{"jpg", "jpeg"};
        inline constexpr std::array<std::string_view, 1> TgaAliases{"tga"};
        inline constexpr std::array<std::string_view, 1> BmpAliases{"bmp"};
        inline constexpr std::array<std::string_view, 1> HdrAliases{"hdr"};
        inline constexpr std::array<std::string_view, 2> KtxAliases{"ktx", "ktx2"};

        inline constexpr std::array<AssetFileFormatInfo, 18> Formats{{
            {AssetFileFormat::OBJ, "obj", ObjAliases, MeshOnly, MeshOnly, false, false},
            {AssetFileFormat::OFF, "off", OffAliases, MeshOnly, NoPayloads, false, false},
            {AssetFileFormat::STL, "stl", StlAliases, MeshOnly, MeshOnly, true, true},
            {AssetFileFormat::PLY, "ply", PlyAliases, MeshAndPointCloud, MeshAndPointCloud, true, true},
            {AssetFileFormat::XYZ, "xyz", XyzAliases, PointCloudOnly, PointCloudOnly, false, false},
            {AssetFileFormat::PTS, "pts", PtsAliases, PointCloudOnly, NoPayloads, false, false},
            {AssetFileFormat::XYZRGB, "xyzrgb", XyzRgbAliases, PointCloudOnly, NoPayloads, false, false},
            {AssetFileFormat::PCD, "pcd", PcdAliases, PointCloudOnly, PointCloudOnly, true, true},
            {AssetFileFormat::TGF, "tgf", TgfAliases, GraphOnly, GraphOnly, false, false},
            {AssetFileFormat::EdgeList, "edges", EdgeAliases, GraphOnly, GraphOnly, false, false},
            {AssetFileFormat::GLTF, "gltf", GltfAliases, ModelSceneOnly, NoPayloads, false, false},
            {AssetFileFormat::GLB, "glb", GlbAliases, ModelSceneOnly, NoPayloads, true, false},
            {AssetFileFormat::PNG, "png", PngAliases, TextureOnly, NoPayloads, true, false},
            {AssetFileFormat::JPEG, "jpg", JpegAliases, TextureOnly, NoPayloads, true, false},
            {AssetFileFormat::TGA, "tga", TgaAliases, TextureOnly, NoPayloads, true, false},
            {AssetFileFormat::BMP, "bmp", BmpAliases, TextureOnly, NoPayloads, true, false},
            {AssetFileFormat::HDR, "hdr", HdrAliases, TextureOnly, NoPayloads, true, false},
            {AssetFileFormat::KTX, "ktx", KtxAliases, TextureOnly, NoPayloads, true, false},
        }};

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
        case AssetFileFormat::OBJ:
            return "OBJ";
        case AssetFileFormat::OFF:
            return "OFF";
        case AssetFileFormat::STL:
            return "STL";
        case AssetFileFormat::PLY:
            return "PLY";
        case AssetFileFormat::XYZ:
            return "XYZ";
        case AssetFileFormat::PTS:
            return "PTS";
        case AssetFileFormat::XYZRGB:
            return "XYZRGB";
        case AssetFileFormat::PCD:
            return "PCD";
        case AssetFileFormat::TGF:
            return "TGF";
        case AssetFileFormat::EdgeList:
            return "EdgeList";
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
