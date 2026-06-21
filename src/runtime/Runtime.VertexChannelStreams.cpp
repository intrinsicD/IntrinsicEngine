module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.VertexChannelStreams;

import Extrinsic.Runtime.VertexAttributeBinding;

namespace Extrinsic::Runtime
{
    namespace
    {
        // Copy `count` elements of `elemSize` bytes from `src` into a stream slot
        // on `streams`, replacing any existing stream for `channel`.
        void SetChannelRaw(
            VertexChannelStreams& streams,
            const VertexChannel channel,
            const void* src,
            const std::uint32_t elemSize,
            const std::size_t count)
        {
            if (count != streams.VertexCount)
            {
                return;
            }

            std::vector<std::byte> bytes(static_cast<std::size_t>(elemSize) * count);
            if (!bytes.empty())
            {
                std::memcpy(bytes.data(), src, bytes.size());
            }

            for (auto& stream : streams.Streams)
            {
                if (stream.Channel == channel)
                {
                    stream.ElemSizeBytes = elemSize;
                    stream.Bytes = std::move(bytes);
                    return;
                }
            }
            streams.Streams.push_back(
                VertexChannelStreams::Stream{channel, elemSize, std::move(bytes)});
        }
    } // namespace

    const VertexChannelLayout* VertexLayout::Find(const VertexChannel channel) const noexcept
    {
        for (const auto& c : Channels)
        {
            if (c.Channel == channel)
            {
                return &c;
            }
        }
        return nullptr;
    }

    VertexLayout MakeTightLayout(
        const std::span<const std::pair<VertexChannel, std::uint32_t>> channelSizes)
    {
        VertexLayout layout{};
        std::uint32_t offset = 0;
        layout.Channels.reserve(channelSizes.size());
        for (const auto& [channel, size] : channelSizes)
        {
            layout.Channels.push_back(VertexChannelLayout{channel, offset, size});
            offset += size;
        }
        layout.StrideBytes = offset;
        return layout;
    }

    const VertexChannelStreams::Stream* VertexChannelStreams::Find(
        const VertexChannel channel) const noexcept
    {
        for (const auto& stream : Streams)
        {
            if (stream.Channel == channel)
            {
                return &stream;
            }
        }
        return nullptr;
    }

    void SetChannelVec3(
        VertexChannelStreams& streams, const VertexChannel channel, const std::span<const glm::vec3> data)
    {
        SetChannelRaw(streams, channel, data.data(), sizeof(glm::vec3), data.size());
    }

    void SetChannelVec2(
        VertexChannelStreams& streams, const VertexChannel channel, const std::span<const glm::vec2> data)
    {
        SetChannelRaw(streams, channel, data.data(), sizeof(glm::vec2), data.size());
    }

    void SetChannelPackedUnorm8(
        VertexChannelStreams& streams, const VertexChannel channel, const std::span<const std::uint32_t> data)
    {
        SetChannelRaw(streams, channel, data.data(), sizeof(std::uint32_t), data.size());
    }

    std::vector<std::byte> InterleaveToAoS(
        const VertexLayout& layout, const VertexChannelStreams& streams)
    {
        const std::size_t vertexCount = streams.VertexCount;
        const std::size_t stride = layout.StrideBytes;
        std::vector<std::byte> out(vertexCount * stride, std::byte{0});

        for (const auto& channel : layout.Channels)
        {
            const VertexChannelStreams::Stream* stream = streams.Find(channel.Channel);
            if (stream == nullptr || stream->ElemSizeBytes != channel.SizeBytes)
            {
                continue;  // zero-filled.
            }
            if (stream->Bytes.size() != static_cast<std::size_t>(channel.SizeBytes) * vertexCount)
            {
                continue;  // defensive: malformed stream, leave zero.
            }
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                std::memcpy(
                    out.data() + v * stride + channel.OffsetBytes,
                    stream->Bytes.data() + v * channel.SizeBytes,
                    channel.SizeBytes);
            }
        }
        return out;
    }
} // namespace Extrinsic::Runtime
