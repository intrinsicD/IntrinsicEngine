module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

export module Graphics:Importers.STL;

import :IORegistry;
import :AssetErrors;

export namespace Graphics
{
    class STLLoader final : public IAssetLoader
    {
    public:
        ~STLLoader() override;

        [[nodiscard]] std::string_view FormatName() const override { return "STL (Stereolithography)"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) override;
    };
}
