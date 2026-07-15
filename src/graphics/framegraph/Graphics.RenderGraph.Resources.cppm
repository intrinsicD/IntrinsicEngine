module;

#include <cstdint>
#include <string>

export module Extrinsic.Graphics.RenderGraph:Resources;

import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
    export struct FrameResourceId
    {
        std::uint32_t Value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return Value != 0u;
        }
    };

    export [[nodiscard]] constexpr bool operator==(const FrameResourceId lhs,
                                                   const FrameResourceId rhs) noexcept
    {
        return lhs.Value == rhs.Value;
    }

    export [[nodiscard]] constexpr bool operator!=(const FrameResourceId lhs,
                                                   const FrameResourceId rhs) noexcept
    {
        return !(lhs == rhs);
    }

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
        FrameResourceId Id{};
        bool Imported = false;
        // Imported textures are read-only by default. Retained render targets
        // may opt into graph-authored writes while still declaring read-only
        // frame-boundary states (for example ShaderRead -> ColorAttachment ->
        // ShaderRead). Recipe-aware validation remains responsible for
        // restricting the authorized writer pass.
        bool ImportedWriteAllowed = false;
        bool IsBackbuffer = false;
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
        FrameResourceId Id{};
        bool Imported = false;
        bool AliasEligible = true;
        BufferState InitialState = BufferState::Undefined;
        BufferState FinalState = BufferState::Undefined;
        RHI::BufferHandle ImportedHandle{};
        RHI::BufferDesc Desc{};
        std::uint32_t Generation = 0;
    };
}
