module;

#include <array>
#include <span>
#include <string_view>

export module Geometry.IO;

export namespace Geometry::IO
{
    enum class GeometryIODomain
    {
        Mesh,
        PointCloud,
        Graph,
    };

    enum class GeometryIOFormatKind
    {
        OBJ,
        OFF,
        STL,
        PLY,
        XYZ,
        PTS,
        PWN,
        CSV,
        ThreeD,
        TXT,
        XYZRGB,
        PCD,
        TGF,
        EdgeList,
    };

    struct GeometryIOFormatInfo
    {
        GeometryIOFormatKind Kind{};
        std::string_view CanonicalExtension{};
        std::span<const std::string_view> ExtensionAliases{};
        std::span<const GeometryIODomain> ImportDomains{};
        std::span<const GeometryIODomain> ExportDomains{};
        bool SupportsBinaryImport = false;
        bool SupportsBinaryExport = false;
    };
}

namespace Geometry::IO
{
    namespace
    {
        using Domain = GeometryIODomain;
        using Kind = GeometryIOFormatKind;

        inline constexpr std::array<Domain, 0> NoDomains{};
        inline constexpr std::array<Domain, 1> MeshOnly{Domain::Mesh};
        inline constexpr std::array<Domain, 1> PointCloudOnly{Domain::PointCloud};
        inline constexpr std::array<Domain, 1> GraphOnly{Domain::Graph};
        inline constexpr std::array<Domain, 2> MeshAndPointCloud{Domain::Mesh, Domain::PointCloud};

        inline constexpr std::array<std::string_view, 1> ObjAliases{"obj"};
        inline constexpr std::array<std::string_view, 1> OffAliases{"off"};
        inline constexpr std::array<std::string_view, 1> StlAliases{"stl"};
        inline constexpr std::array<std::string_view, 1> PlyAliases{"ply"};
        inline constexpr std::array<std::string_view, 1> XyzAliases{"xyz"};
        inline constexpr std::array<std::string_view, 1> PtsAliases{"pts"};
        inline constexpr std::array<std::string_view, 1> PwnAliases{"pwn"};
        inline constexpr std::array<std::string_view, 1> CsvAliases{"csv"};
        inline constexpr std::array<std::string_view, 1> ThreeDAliases{"3d"};
        inline constexpr std::array<std::string_view, 1> TxtAliases{"txt"};
        inline constexpr std::array<std::string_view, 1> XyzRgbAliases{"xyzrgb"};
        inline constexpr std::array<std::string_view, 1> PcdAliases{"pcd"};
        inline constexpr std::array<std::string_view, 1> TgfAliases{"tgf"};
        inline constexpr std::array<std::string_view, 1> EdgeAliases{"edges"};

        inline constexpr std::array<GeometryIOFormatInfo, 14> Formats{{
            {Kind::OBJ, "obj", ObjAliases, MeshOnly, MeshOnly, false, false},
            {Kind::OFF, "off", OffAliases, MeshOnly, MeshOnly, false, false},
            {Kind::STL, "stl", StlAliases, MeshOnly, MeshOnly, true, true},
            {Kind::PLY, "ply", PlyAliases, MeshAndPointCloud, MeshAndPointCloud, true, true},
            {Kind::XYZ, "xyz", XyzAliases, PointCloudOnly, PointCloudOnly, false, false},
            {Kind::PTS, "pts", PtsAliases, PointCloudOnly, NoDomains, false, false},
            {Kind::PWN, "pwn", PwnAliases, PointCloudOnly, NoDomains, false, false},
            {Kind::CSV, "csv", CsvAliases, PointCloudOnly, NoDomains, false, false},
            {Kind::ThreeD, "3d", ThreeDAliases, PointCloudOnly, NoDomains, false, false},
            {Kind::TXT, "txt", TxtAliases, PointCloudOnly, NoDomains, false, false},
            {Kind::XYZRGB, "xyzrgb", XyzRgbAliases, PointCloudOnly, NoDomains, false, false},
            {Kind::PCD, "pcd", PcdAliases, PointCloudOnly, PointCloudOnly, true, true},
            {Kind::TGF, "tgf", TgfAliases, GraphOnly, GraphOnly, false, false},
            {Kind::EdgeList, "edges", EdgeAliases, GraphOnly, GraphOnly, false, false},
        }};

        [[nodiscard]] constexpr char ToLowerAscii(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        [[nodiscard]] constexpr std::string_view NormalizeExtension(std::string_view extension)
        {
            while (!extension.empty() && extension.front() == '.')
            {
                extension.remove_prefix(1);
            }
            return extension;
        }

        [[nodiscard]] constexpr bool ExtensionEquals(std::string_view lhs, std::string_view rhs)
        {
            lhs = NormalizeExtension(lhs);
            rhs = NormalizeExtension(rhs);
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < lhs.size(); ++i)
            {
                if (ToLowerAscii(lhs[i]) != ToLowerAscii(rhs[i]))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool HasDomain(std::span<const Domain> domains, Domain domain)
        {
            for (const Domain candidate : domains)
            {
                if (candidate == domain)
                {
                    return true;
                }
            }
            return false;
        }
    }
}

export namespace Geometry::IO
{
    [[nodiscard]] constexpr std::span<const GeometryIOFormatInfo> SupportedGeometryIOFormats()
    {
        return Formats;
    }

    [[nodiscard]] constexpr const GeometryIOFormatInfo* FindGeometryIOFormat(std::string_view extension)
    {
        extension = NormalizeExtension(extension);
        if (extension.empty())
        {
            return nullptr;
        }

        for (const GeometryIOFormatInfo& format : Formats)
        {
            if (ExtensionEquals(extension, format.CanonicalExtension))
            {
                return &format;
            }
            for (const std::string_view alias : format.ExtensionAliases)
            {
                if (ExtensionEquals(extension, alias))
                {
                    return &format;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr std::span<const GeometryIODomain> ImportDomainsForExtension(std::string_view extension)
    {
        const GeometryIOFormatInfo* format = FindGeometryIOFormat(extension);
        return format == nullptr ? std::span<const GeometryIODomain>{} : format->ImportDomains;
    }

    [[nodiscard]] constexpr std::span<const GeometryIODomain> ExportDomainsForExtension(std::string_view extension)
    {
        const GeometryIOFormatInfo* format = FindGeometryIOFormat(extension);
        return format == nullptr ? std::span<const GeometryIODomain>{} : format->ExportDomains;
    }

    [[nodiscard]] constexpr bool HasAmbiguousImportDomains(std::string_view extension)
    {
        return ImportDomainsForExtension(extension).size() > 1;
    }

    [[nodiscard]] constexpr bool HasAmbiguousExportDomains(std::string_view extension)
    {
        return ExportDomainsForExtension(extension).size() > 1;
    }

    [[nodiscard]] constexpr bool SupportsImportDomain(std::string_view extension, GeometryIODomain domain)
    {
        return HasDomain(ImportDomainsForExtension(extension), domain);
    }

    [[nodiscard]] constexpr bool SupportsExportDomain(std::string_view extension, GeometryIODomain domain)
    {
        return HasDomain(ExportDomainsForExtension(extension), domain);
    }
}
