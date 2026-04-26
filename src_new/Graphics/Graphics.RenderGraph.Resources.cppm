module;

#include <cstdint>
#include <string>

export module Extrinsic.Graphics.RenderGraph:Resources;

import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
    export enum class TextureState : std::uint8_t
    {
        Undefined = 0,
        ShaderRead,
        ShaderWrite,
        ColorAttachmentWrite,
        DepthWrite,
        TransferSrc,
        TransferDst,
        Present,
    };

    export enum class BufferState : std::uint8_t
    {
        Undefined = 0,
        ShaderRead,
        ShaderWrite,
        VertexRead,
        IndexRead,
        IndirectRead,
        TransferSrc,
        TransferDst,
        HostReadback,
    };

    export struct TextureRef
    {
        std::uint32_t Index = 0;
        std::uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return Generation != 0;
        }
    };

    export struct BufferRef
    {
        std::uint32_t Index = 0;
        std::uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return Generation != 0;
        }
    };

    export struct TextureResourceDesc
    {
        std::string Name{};
        bool Imported = false;
        bool AliasEligible = true;
        TextureState InitialState = TextureState::Undefined;
        TextureState FinalState = TextureState::Undefined;
        RHI::TextureHandle ImportedHandle{};
        RHI::TextureDesc Desc{};
        std::uint32_t Generation = 0;
    };

    export struct BufferResourceDesc
    {
        std::string Name{};
        bool Imported = false;
        bool AliasEligible = true;
        BufferState InitialState = BufferState::Undefined;
        BufferState FinalState = BufferState::Undefined;
        RHI::BufferHandle ImportedHandle{};
        RHI::BufferDesc Desc{};
        std::uint32_t Generation = 0;
    };
}
