module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

export module Graphics:Importers.TGF;

import :IORegistry;
import :AssetErrors;

export namespace Graphics
{
    class TGFLoader final : public IAssetLoader
    {
    public:
        ~TGFLoader() override;

        [[nodiscard]] std::string_view FormatName() const override { return "Trivial Graph Format"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) override;
    };
}
