module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

export module Graphics:Importers.XYZ;

import :IORegistry;
import :AssetErrors;

export namespace Graphics
{
    class XYZLoader final : public IAssetLoader
    {
    public:
        ~XYZLoader() override;

        [[nodiscard]] std::string_view FormatName() const override { return "XYZ Point Cloud"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) override;
    };
}
