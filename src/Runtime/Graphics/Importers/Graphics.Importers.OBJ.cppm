module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

export module Graphics.Importers.OBJ;

import Graphics.IORegistry;
import Graphics.AssetErrors;

export namespace Graphics
{
    class OBJLoader final : public IAssetLoader
    {
    public:
        ~OBJLoader() override;

        [[nodiscard]] std::string_view FormatName() const override { return "Wavefront OBJ"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) override;
    };
}
