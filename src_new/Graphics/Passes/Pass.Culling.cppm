module;

export module Extrinsic.Graphics.Pass.Culling;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Types;
import Extrinsic.Graphics.CullingSystem;

// ============================================================
// Pass.Culling — compute prologue frame-graph node.
//
// Responsibility (Pass half of the System/Pass split):
//   Records the three GPU commands that drive GPU-driven culling:
//     1. Reset VisibilityCountBuffer atomic counter to 0.
//     2. Dispatch instance_cull.comp (reads CullDataBuffer,
//        writes DrawCommandBuffer + VisibilityCountBuffer).
//     3. Issue UAV barrier so subsequent indirect draw passes
//        see the populated DrawCommandBuffer.
//
// This node runs as the compute prologue — before Pass 01 (Picking)
// and before any render pass. In the GPU frame graph it declares:
//   virtual WRITE: DrawCommandBuffer
//   virtual WRITE: VisibilityCountBuffer
// All passes that consume indirect draw args declare
//   virtual READ:  DrawCommandBuffer
//   virtual READ:  VisibilityCountBuffer
// and the frame graph inserts barriers automatically.
//
// Injected dependency:
//   CullingSystem& — resource authority (buffers, pipeline, slot pool).
//   The pass does not own any GPU resources.
//
// Per-frame call sequence (inside Execute):
//   culling.SyncGpuBuffer();              // flush CPU-side dirty uploads
//   cmd.ResetBuffer(VisibilityCountBuf);  // counter → 0  +  barrier
//   cmd.BindPipeline(GetCullPipeline());
//   cmd.PushConstants(CullPushConstants{frustum, count, BDAs});
//   cmd.Dispatch(ceil(count / 64));
//   cmd.UAVBarrier({DrawCommandBuf, VisibilityCountBuf});
// ============================================================

namespace Extrinsic::Graphics
{
    export class CullingPass
    {
    public:
        explicit CullingPass(CullingSystem& culling) : m_Culling(culling) {}

        CullingPass(const CullingPass&)            = delete;
        CullingPass& operator=(const CullingPass&) = delete;

        /// Execute the compute prologue for this frame.
        /// @param cmd     open compute command context (graphics queue).
        /// @param camera  current frame's camera UBO (frustum planes extracted here).
        void Execute(RHI::ICommandContext& cmd, const RHI::CameraUBO& camera);

    private:
        CullingSystem& m_Culling;
    };
}

