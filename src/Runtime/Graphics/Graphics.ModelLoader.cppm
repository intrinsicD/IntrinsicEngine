module;
#include <string>
#include <memory>
#include <optional>

export module Runtime.Graphics.ModelLoader;

import Runtime.Graphics.Model;
import Runtime.Graphics.Geometry;
import Runtime.RHI.Device;
import Runtime.RHI.Transfer;

export namespace Runtime::Graphics
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
