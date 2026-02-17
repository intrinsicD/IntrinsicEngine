module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

export module Graphics:Importers.OFF;

import :IORegistry;
import :AssetErrors;

export namespace Graphics
{
    class OFFLoader final : public IAssetLoader
    {
    public:
        ~OFFLoader() override;

        [[nodiscard]] std::string_view FormatName() const override { return "Object File Format (OFF)"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) override;
    };
}
