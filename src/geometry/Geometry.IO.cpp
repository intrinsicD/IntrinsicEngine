module;

#include <array>
#include <span>
#include <string_view>

module Geometry.IO;

namespace Geometry::IO
{
    namespace
    {
        using Domain = GeometryIODomain;
        using Kind = GeometryIOFormatKind;

        inline constexpr std::array<Domain, 0> NoDomains{};
        inline constexpr auto MeshOnly = std::to_array({Domain::Mesh});
        inline constexpr auto PointCloudOnly = std::to_array({Domain::PointCloud});
        inline constexpr auto GraphOnly = std::to_array({Domain::Graph});
        inline constexpr auto MeshAndPointCloud = std::to_array({Domain::Mesh, Domain::PointCloud});

        // One alias array per catalog row; the canonical extension is part of
        // the alias list so the row text stays self-describing.
#define INTRINSIC_GEOMETRY_FORMAT(Name, Canonical, ImportDomains, ExportDomains, BinaryImport, BinaryExport, ...) \
    inline constexpr auto Aliases_##Name = std::to_array<std::string_view>({__VA_ARGS__});
#include "Core.GeometryFormatCatalog.inc"
#undef INTRINSIC_GEOMETRY_FORMAT

#define INTRINSIC_GEOMETRY_FORMAT(Name, Canonical, ImportDomains, ExportDomains, BinaryImport, BinaryExport, ...) \
    GeometryIOFormatInfo{Kind::Name, Canonical, Aliases_##Name, ImportDomains, ExportDomains, BinaryImport, BinaryExport},
        inline constexpr auto Formats = std::to_array<GeometryIOFormatInfo>({
#include "Core.GeometryFormatCatalog.inc"
        });
#undef INTRINSIC_GEOMETRY_FORMAT

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

    std::span<const GeometryIOFormatInfo> SupportedGeometryIOFormats()
    {
        return Formats;
    }

    const GeometryIOFormatInfo* FindGeometryIOFormat(std::string_view extension)
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

    std::span<const GeometryIODomain> ImportDomainsForExtension(std::string_view extension)
    {
        const GeometryIOFormatInfo* format = FindGeometryIOFormat(extension);
        return format == nullptr ? std::span<const GeometryIODomain>{} : format->ImportDomains;
    }

    std::span<const GeometryIODomain> ExportDomainsForExtension(std::string_view extension)
    {
        const GeometryIOFormatInfo* format = FindGeometryIOFormat(extension);
        return format == nullptr ? std::span<const GeometryIODomain>{} : format->ExportDomains;
    }

    bool HasAmbiguousImportDomains(std::string_view extension)
    {
        return ImportDomainsForExtension(extension).size() > 1;
    }

    bool HasAmbiguousExportDomains(std::string_view extension)
    {
        return ExportDomainsForExtension(extension).size() > 1;
    }

    bool SupportsImportDomain(std::string_view extension, GeometryIODomain domain)
    {
        return HasDomain(ImportDomainsForExtension(extension), domain);
    }

    bool SupportsExportDomain(std::string_view extension, GeometryIODomain domain)
    {
        return HasDomain(ExportDomainsForExtension(extension), domain);
    }
}
