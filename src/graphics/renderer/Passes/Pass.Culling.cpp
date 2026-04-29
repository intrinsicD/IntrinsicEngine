module;

module Extrinsic.Graphics.Pass.Culling;

namespace Extrinsic::Graphics
{
    void CullingPass::Execute(RHI::ICommandContext& cmd,
                              const RHI::CameraUBO& camera,
                              const GpuWorld&       gpuWorld)
    {
        m_Culling.SyncGpuBuffer();
        m_Culling.ResetCounters(cmd);
        m_Culling.DispatchCull(cmd, camera, gpuWorld);
    }
}
