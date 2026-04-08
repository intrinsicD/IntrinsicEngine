module;
#include <string>
#include <memory>
#include <expected>

export module Graphics.ModelLoader;

import Graphics.AssetErrors;
import Graphics.Model;
import Graphics.Geometry;
import Graphics.IORegistry;
import Core.IOBackend;
import RHI.Buffer;
import RHI.Device;
import RHI.Transfer;

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
        // New signature: delegates parsing to IORegistry loaders (I/O-agnostic pipeline).
        [[nodiscard]]
        static std::expected<ModelLoadResult, AssetError> LoadAsync(
            std::shared_ptr<RHI::VulkanDevice> device,
            RHI::TransferManager& transferManager,
            RHI::BufferManager& bufferManager,
            GeometryPool& geometryStorage,
            const std::string& filepath,
            const IORegistry& registry,
            Core::IO::IIOBackend& backend);
    };
}
