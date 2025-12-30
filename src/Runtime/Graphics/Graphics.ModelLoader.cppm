module;
#include <string>
#include <memory>
#include <optional>

export module Graphics:ModelLoader;

import :Model;
import :Geometry;
import RHI;

export namespace Graphics
{
    struct ModelLoadResult
    {
        std::shared_ptr<Model> ModelData;
        RHI::TransferToken Token; // The fence/timeline value to wait for
    };

    class ModelLoader
    {
    public:
        [[nodiscard]]
        static std::optional<ModelLoadResult> LoadAsync(
            std::shared_ptr<RHI::VulkanDevice> device,
            RHI::TransferManager& transferManager,
            GeometryStorage& geometryStorage,
            const std::string& filepath
        );
    };
}
