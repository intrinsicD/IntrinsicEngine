module;

export module Extrinsic.Graphics.Pass.Culling;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Graphics
{
    export class CullingPass
    {
    public:
        explicit CullingPass(CullingSystem& culling) : m_Culling(culling) {}

        CullingPass(const CullingPass&)            = delete;
        CullingPass& operator=(const CullingPass&) = delete;

        void Execute(RHI::ICommandContext& cmd,
                     const RHI::CameraUBO& camera,
                     const GpuWorld&       gpuWorld);

    private:
        CullingSystem& m_Culling;
    };
}
