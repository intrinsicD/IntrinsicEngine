module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

export module Graphics.Importers.PLY;

import Graphics.IORegistry;
import Graphics.AssetErrors;

export namespace Graphics
{
    class PLYLoader final : public IAssetLoader
    {
    public:
        ~PLYLoader() override = default;

        [[nodiscard]] std::string_view FormatName() const override { return "Stanford PLY"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) override;
    };
}
