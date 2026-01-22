module;
#include <string>
#include <memory>
#include <expected>

export module Graphics:ModelLoader;

import :AssetErrors;
import :Model;
import :Geometry;
import RHI;

export namespace Graphics
{
    struct ModelLoadResult
    {
        std::unique_ptr<Model> ModelData;
        RHI::TransferToken Token; // The fence/timeline value to wait for
    };

    class ModelLoader
    {
    public:
        [[nodiscard]]
        static std::expected<ModelLoadResult, AssetError> LoadAsync(
            std::shared_ptr<RHI::VulkanDevice> device,
            RHI::TransferManager& transferManager,
            GeometryPool& geometryStorage,
            const std::string& filepath);
    };
}
