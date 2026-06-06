module;

#include <cstdint>

module Extrinsic.RHI.CommandContext;

import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

// Out-of-line definitions for ICommandContext. Keeping these bodies in a single
// module implementation unit anchors the interface's vtable to one TU (the
// destructor is the key function) and keeps RHI.CommandContext.cppm limited to
// declarations per AGENTS.md §5. Semantics are unchanged: the destructor is the
// defaulted virtual destructor, and the three non-pure virtuals keep their
// original default behaviour (a no-op, except BindFrameSampledTexture which
// forwards to the slot-explicit sibling at slot 0).
namespace Extrinsic::RHI
{
    ICommandContext::~ICommandContext() = default;

    void ICommandContext::BindFrameSampledTexture(TextureHandle texture)
    {
        BindFrameSampledTextureAt(texture, 0u);
    }

    void ICommandContext::CopyTextureToBuffer(TextureHandle src,
                                              TextureLayout srcLayout,
                                              std::uint32_t mipLevel,
                                              std::uint32_t arrayLayer,
                                              BufferHandle  dst,
                                              std::uint64_t dstOffset,
                                              std::uint32_t srcOffsetX,
                                              std::uint32_t srcOffsetY,
                                              std::uint32_t srcWidth,
                                              std::uint32_t srcHeight)
    {
        (void)src;
        (void)srcLayout;
        (void)mipLevel;
        (void)arrayLayer;
        (void)dst;
        (void)dstOffset;
        (void)srcOffsetX;
        (void)srcOffsetY;
        (void)srcWidth;
        (void)srcHeight;
    }

    void ICommandContext::BindFrameSampledTextureAt(TextureHandle texture, std::uint32_t descriptorIndex)
    {
        (void)texture;
        (void)descriptorIndex;
    }
}
