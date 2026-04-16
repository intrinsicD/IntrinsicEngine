module;

#include <cstdint>

export module Extrinsic.Asset.LoadPipeline;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;

export namespace Extrinsic::Assets
{
    struct LoadRequest
    {
        AssetId id{};
        uint32_t typeId{};
        const char* path{};
        bool needsGpuUpload = false;
    };

    class AssetLoadPipeline
    {
    public:
        Core::Result EnqueueIO(const LoadRequest& req);
        Core::Result OnCpuDecoded(AssetId id);
        Core::Result OnGpuUploaded(AssetId id);
    };
}
