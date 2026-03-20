module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

export module Graphics.Importers.PCD;

import Graphics.IORegistry;
import Graphics.AssetErrors;

export namespace Graphics
{
    class PCDLoader final : public IAssetLoader
    {
    public:
        ~PCDLoader() override = default;

        [[nodiscard]] std::string_view FormatName() const override { return "PCD Point Cloud"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) override;
    };
}
