// Geometry format types and capability queries; both IO and asset routing use the core catalog.
module;

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
#define INTRINSIC_GEOMETRY_FORMAT(Name, ...) Name,
#include "Core.GeometryFormatCatalog.inc"
#undef INTRINSIC_GEOMETRY_FORMAT
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

    [[nodiscard]] std::span<const GeometryIOFormatInfo> SupportedGeometryIOFormats();

    [[nodiscard]] const GeometryIOFormatInfo* FindGeometryIOFormat(std::string_view extension);

    [[nodiscard]] std::span<const GeometryIODomain> ImportDomainsForExtension(std::string_view extension);

    [[nodiscard]] std::span<const GeometryIODomain> ExportDomainsForExtension(std::string_view extension);

    [[nodiscard]] bool HasAmbiguousImportDomains(std::string_view extension);

    [[nodiscard]] bool HasAmbiguousExportDomains(std::string_view extension);

    [[nodiscard]] bool SupportsImportDomain(std::string_view extension, GeometryIODomain domain);

    [[nodiscard]] bool SupportsExportDomain(std::string_view extension, GeometryIODomain domain);
}
