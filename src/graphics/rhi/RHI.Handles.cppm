module;

export module Extrinsic.RHI.Handles;

import Extrinsic.Core.StrongHandle;

// ============================================================
// All RHI typed resource handles — one module, one import.
// One tag struct per resource type; StrongHandle<Tag> provides
// generational index + type safety with zero overhead.
// ============================================================

export namespace Extrinsic::RHI
{
    struct BufferTag;
    using BufferHandle = Core::StrongHandle<BufferTag>;

    struct TextureTag;
    using TextureHandle  = Core::StrongHandle<TextureTag>;

    struct PipelineTag;
    using PipelineHandle = Core::StrongHandle<PipelineTag>;

    struct SamplerTag;
    using SamplerHandle  = Core::StrongHandle<SamplerTag>;

    struct RenderPassTag;
    using RenderPassHandle = Core::StrongHandle<RenderPassTag>;
}
