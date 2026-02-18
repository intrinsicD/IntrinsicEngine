module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

export module Graphics:Exporters.OBJ;

import :IORegistry;
import :AssetErrors;
import :Geometry;

export namespace Graphics
{
    class OBJExporter final : public IAssetExporter
    {
    public:
        ~OBJExporter() override;

        [[nodiscard]] std::string_view FormatName() const override { return "Wavefront OBJ"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<std::vector<std::byte>, AssetError> Export(
            const GeometryCpuData& data,
            const ExportOptions& options = {}) override;
    };
}
