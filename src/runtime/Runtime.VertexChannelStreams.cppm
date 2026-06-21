module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.VertexChannelStreams;

import Extrinsic.Runtime.VertexAttributeBinding;

export namespace Extrinsic::Runtime
{
    // CPU Structure-of-Arrays vertex storage substrate (ADR-0022). Each channel
    // is held as its own contiguous byte buffer so that a single attribute can be
    // uploaded/streamed independently (RUNTIME-122 Slice B / RUNTIME-124). For
    // compatibility with the current interleaved GpuScene vertex shaders, the
    // streams can be interleaved into the existing AoS vertex byte layout via
    // `InterleaveToAoS`. This module is GPU-agnostic.

    // One channel's placement within an interleaved AoS vertex.
    struct VertexChannelLayout
    {
        VertexChannel Channel = VertexChannel::Custom;
        std::uint32_t OffsetBytes = 0;  // offset of this channel within the AoS stride.
        std::uint32_t SizeBytes = 0;    // element size in bytes (e.g. 12 for vec3).
    };

    // Declarative interleaved vertex layout: ordered channels + total stride.
    // Mirrors the existing MeshVertex / GraphVertex / PointCloudVertex structs.
    struct VertexLayout
    {
        std::vector<VertexChannelLayout> Channels{};
        std::uint32_t StrideBytes = 0;

        [[nodiscard]] const VertexChannelLayout* Find(VertexChannel channel) const noexcept;
    };

    // Build a tightly-packed (no padding) interleaved layout from an ordered list
    // of (channel, element-size-bytes). Offsets are assigned in order; stride is
    // the sum of sizes.
    [[nodiscard]] VertexLayout MakeTightLayout(
        std::span<const std::pair<VertexChannel, std::uint32_t>> channelSizes);

    // Per-channel SoA byte storage for `VertexCount` vertices.
    struct VertexChannelStreams
    {
        struct Stream
        {
            VertexChannel Channel = VertexChannel::Custom;
            std::uint32_t ElemSizeBytes = 0;
            std::vector<std::byte> Bytes{};  // size == VertexCount * ElemSizeBytes.
        };

        std::uint32_t VertexCount = 0;
        std::vector<Stream> Streams{};

        [[nodiscard]] const Stream* Find(VertexChannel channel) const noexcept;
        void SetVertexCount(std::uint32_t count) noexcept { VertexCount = count; }
    };

    // Append/replace a channel from typed source data. Sets the stream's element
    // size and copies the raw bytes. `data` must hold `VertexChannelStreams::
    // VertexCount` elements; otherwise the channel is left absent.
    void SetChannelVec3(
        VertexChannelStreams& streams, VertexChannel channel, std::span<const glm::vec3> data);
    void SetChannelVec2(
        VertexChannelStreams& streams, VertexChannel channel, std::span<const glm::vec2> data);
    void SetChannelPackedUnorm8(
        VertexChannelStreams& streams, VertexChannel channel, std::span<const std::uint32_t> data);

    // Interleave the SoA streams into the AoS byte layout described by `layout`.
    // For each layout channel, the matching stream's bytes are written at the
    // channel's offset for every vertex; a layout channel with no matching stream
    // (or a size mismatch) is left zero-filled. The result is
    // `streams.VertexCount * layout.StrideBytes` bytes.
    [[nodiscard]] std::vector<std::byte> InterleaveToAoS(
        const VertexLayout& layout, const VertexChannelStreams& streams);
}
