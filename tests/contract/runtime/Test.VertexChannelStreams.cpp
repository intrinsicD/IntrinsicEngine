// Contract tests for the CPU SoA vertex channel substrate (RUNTIME-122 Slice A,
// ADR-0022). Proves the per-channel streams interleave into the exact AoS vertex
// byte layout the current packers emit. CPU-only; no GPU/RHI dependency.

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelStreams;
import Extrinsic.Runtime.MeshGeometryPacker;

using Extrinsic::Runtime::InterleaveToAoS;
using Extrinsic::Runtime::MakeTightLayout;
using Extrinsic::Runtime::MeshVertex;
using Extrinsic::Runtime::SetChannelVec2;
using Extrinsic::Runtime::SetChannelVec3;
using Extrinsic::Runtime::VertexChannel;
using Extrinsic::Runtime::VertexChannelStreams;
using Extrinsic::Runtime::VertexLayout;

namespace
{
    // The interleaved MeshVertex layout: position (12B) @0, texcoord (8B) @12,
    // normal (12B) @20, stride 32 — matches Runtime.MeshGeometryPacker::MeshVertex.
    VertexLayout MeshLayout()
    {
        const std::pair<VertexChannel, std::uint32_t> channels[] = {
            {VertexChannel::Position, sizeof(glm::vec3)},
            {VertexChannel::Texcoord, sizeof(glm::vec2)},
            {VertexChannel::Normal, sizeof(glm::vec3)},
        };
        return MakeTightLayout(channels);
    }
}

TEST(VertexChannelStreams, TightLayoutAssignsOffsetsAndStride)
{
    const VertexLayout layout = MeshLayout();
    ASSERT_EQ(layout.StrideBytes, 32u);
    ASSERT_EQ(layout.Channels.size(), 3u);

    const auto* pos = layout.Find(VertexChannel::Position);
    const auto* uv = layout.Find(VertexChannel::Texcoord);
    const auto* nrm = layout.Find(VertexChannel::Normal);
    ASSERT_NE(pos, nullptr);
    ASSERT_NE(uv, nullptr);
    ASSERT_NE(nrm, nullptr);
    EXPECT_EQ(pos->OffsetBytes, 0u);
    EXPECT_EQ(uv->OffsetBytes, 12u);
    EXPECT_EQ(nrm->OffsetBytes, 20u);
    EXPECT_EQ(layout.Find(VertexChannel::Color), nullptr);
}

TEST(VertexChannelStreams, InterleaveReproducesMeshVertexBytes)
{
    const std::vector<glm::vec3> positions{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    const std::vector<glm::vec2> texcoords{{0.1f, 0.2f}, {0.3f, 0.4f}};
    const std::vector<glm::vec3> normals{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};

    VertexChannelStreams streams;
    streams.SetVertexCount(2);
    SetChannelVec3(streams, VertexChannel::Position, positions);
    SetChannelVec2(streams, VertexChannel::Texcoord, texcoords);
    SetChannelVec3(streams, VertexChannel::Normal, normals);

    const std::vector<std::byte> bytes = InterleaveToAoS(MeshLayout(), streams);
    ASSERT_EQ(bytes.size(), 2u * sizeof(MeshVertex));

    MeshVertex v[2];
    std::memcpy(v, bytes.data(), bytes.size());

    EXPECT_FLOAT_EQ(v[0].Px, 1.0f);
    EXPECT_FLOAT_EQ(v[0].Py, 2.0f);
    EXPECT_FLOAT_EQ(v[0].Pz, 3.0f);
    EXPECT_FLOAT_EQ(v[0].U, 0.1f);
    EXPECT_FLOAT_EQ(v[0].V, 0.2f);
    EXPECT_FLOAT_EQ(v[0].Nx, 0.0f);
    EXPECT_FLOAT_EQ(v[0].Ny, 0.0f);
    EXPECT_FLOAT_EQ(v[0].Nz, 1.0f);

    EXPECT_FLOAT_EQ(v[1].Px, 4.0f);
    EXPECT_FLOAT_EQ(v[1].V, 0.4f);
    EXPECT_FLOAT_EQ(v[1].Nx, 1.0f);
}

TEST(VertexChannelStreams, MissingChannelIsZeroFilled)
{
    const std::vector<glm::vec3> positions{{7.0f, 8.0f, 9.0f}};

    VertexChannelStreams streams;
    streams.SetVertexCount(1);
    SetChannelVec3(streams, VertexChannel::Position, positions);
    // No texcoord, no normal stream.

    const std::vector<std::byte> bytes = InterleaveToAoS(MeshLayout(), streams);
    ASSERT_EQ(bytes.size(), sizeof(MeshVertex));

    MeshVertex v{};
    std::memcpy(&v, bytes.data(), bytes.size());
    EXPECT_FLOAT_EQ(v.Px, 7.0f);
    EXPECT_FLOAT_EQ(v.U, 0.0f);   // zero-filled
    EXPECT_FLOAT_EQ(v.V, 0.0f);
    EXPECT_FLOAT_EQ(v.Nx, 0.0f);  // zero-filled
    EXPECT_FLOAT_EQ(v.Nz, 0.0f);
}

TEST(VertexChannelStreams, CountMismatchLeavesChannelAbsent)
{
    const std::vector<glm::vec3> tooMany{{1, 1, 1}, {2, 2, 2}, {3, 3, 3}};

    VertexChannelStreams streams;
    streams.SetVertexCount(2);
    SetChannelVec3(streams, VertexChannel::Position, tooMany);  // 3 != 2 -> ignored

    EXPECT_EQ(streams.Find(VertexChannel::Position), nullptr);

    const std::vector<std::byte> bytes = InterleaveToAoS(MeshLayout(), streams);
    ASSERT_EQ(bytes.size(), 2u * sizeof(MeshVertex));
    for (const std::byte b : bytes)
    {
        EXPECT_EQ(b, std::byte{0});
    }
}

TEST(VertexChannelStreams, PositionUvLayoutMatchesLineAndPointStride)
{
    // GraphVertex / PointCloudVertex: position (12B) @0, texcoord (8B) @12,
    // stride 20.
    const std::pair<VertexChannel, std::uint32_t> channels[] = {
        {VertexChannel::Position, sizeof(glm::vec3)},
        {VertexChannel::Texcoord, sizeof(glm::vec2)},
    };
    const VertexLayout layout = MakeTightLayout(channels);
    EXPECT_EQ(layout.StrideBytes, 20u);

    const std::vector<glm::vec3> positions{{1.0f, 2.0f, 3.0f}};
    VertexChannelStreams streams;
    streams.SetVertexCount(1);
    SetChannelVec3(streams, VertexChannel::Position, positions);

    const std::vector<std::byte> bytes = InterleaveToAoS(layout, streams);
    ASSERT_EQ(bytes.size(), 20u);
    float px = 0.0f;
    std::memcpy(&px, bytes.data(), sizeof(float));
    EXPECT_FLOAT_EQ(px, 1.0f);
}
