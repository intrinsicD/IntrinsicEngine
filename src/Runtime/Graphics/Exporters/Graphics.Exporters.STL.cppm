module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

export module Graphics:Exporters.STL;

import :IORegistry;
import :AssetErrors;
import :Geometry;

export namespace Graphics
{
    class STLExporter final : public IAssetExporter
    {
    public:
        ~STLExporter() override;

        [[nodiscard]] std::string_view FormatName() const override { return "STL (Stereolithography)"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<std::vector<std::byte>, AssetError> Export(
            const GeometryCpuData& data,
            const ExportOptions& options = {}) override;
    };
}
