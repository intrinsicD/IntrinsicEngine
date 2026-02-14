module;
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

export module Graphics:Importers.GLTF;

import :IORegistry;
import :AssetErrors;

export namespace Graphics
{
    class GLTFLoader final : public IAssetLoader
    {
    public:
        ~GLTFLoader() override;

        [[nodiscard]] std::string_view FormatName() const override { return "glTF/GLB"; }
        [[nodiscard]] std::span<const std::string_view> Extensions() const override;

        [[nodiscard]] std::expected<ImportResult, AssetError> Load(
            std::span<const std::byte> data,
            const LoadContext& ctx) override;
    };
}
