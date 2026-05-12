#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.ProceduralGeometry;
import Extrinsic.Runtime.ProceduralGeometryPacker;

using Extrinsic::ECS::Components::ProceduralGeometryKind;
using Extrinsic::ECS::Components::ProceduralGeometryParams;
using Extrinsic::ECS::Components::ProceduralGeometryRef;
using Extrinsic::Runtime::HashProceduralGeometryParams;
using Extrinsic::Runtime::MakeProceduralGeometryKey;
using Extrinsic::Runtime::Pack;
using Extrinsic::Runtime::ProceduralGeometryCache;
using Extrinsic::Runtime::ProceduralGeometryKey;
using Extrinsic::Runtime::ProceduralGeometryPackBuffer;
using Extrinsic::Runtime::ProceduralVertex;

namespace
{
    // Simple synthetic GpuGeometryHandle factory used to drive cache tests without
    // standing up a live GpuWorld. The runtime cache is contract-tested by Impl-A;
    // wiring the bind path against IRenderer is GRAPHICS-030-Impl-B.
    [[nodiscard]] Extrinsic::Graphics::GpuGeometryHandle MakeHandle(std::uint32_t index,
                                                                    std::uint32_t generation = 1u) noexcept
    {
        Extrinsic::Graphics::GpuGeometryHandle handle{};
        handle.Index = index;
        handle.Generation = generation;
        return handle;
    }
}

TEST(ProceduralGeometryDescriptor, IdenticalParamsProduceIdenticalKeys)
{
    ProceduralGeometryParams a{};
    ProceduralGeometryParams b{};

    const auto keyA = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, a);
    const auto keyB = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, b);

    EXPECT_EQ(keyA, keyB);
    EXPECT_EQ(HashProceduralGeometryParams(a), HashProceduralGeometryParams(b));
}

TEST(ProceduralGeometryDescriptor, DifferingParamsProduceDistinctKeys)
{
    ProceduralGeometryParams a{};
    ProceduralGeometryParams b{};
    b.Payload[0] = 1.0f;

    const auto keyA = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, a);
    const auto keyB = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, b);

    EXPECT_NE(keyA, keyB);
    EXPECT_NE(HashProceduralGeometryParams(a), HashProceduralGeometryParams(b));
}

TEST(ProceduralGeometryDescriptor, KeysAreStableAcrossInvocations)
{
    ProceduralGeometryParams params{};
    params.VertexCount = 3;
    params.IndexCount = 3;
    params.Payload[3] = -2.5f;

    const auto first = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);
    const auto second = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);

    EXPECT_EQ(first, second);
    EXPECT_EQ(first.ParamsHash, second.ParamsHash);
}

TEST(ProceduralGeometryCacheTest, FirstEnsureResidentUploadsOnce)
{
    ProceduralGeometryCache cache;
    Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
    desc.VertexCount = 3u;
    desc.DebugName = "test-upload";

    int uploadCalls = 0;
    auto upload = [&](const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc& d) {
        ++uploadCalls;
        EXPECT_EQ(d.VertexCount, 3u);
        EXPECT_STREQ(d.DebugName, "test-upload");
        return MakeHandle(7u);
    };

    ProceduralGeometryParams params{};
    const auto key = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);

    const auto handle = cache.EnsureResident(key, desc, upload);

    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.Index, 7u);
    EXPECT_EQ(uploadCalls, 1);
    EXPECT_EQ(cache.Stats().Uploads, 1u);
    EXPECT_EQ(cache.Stats().ReuseHits, 0u);
    EXPECT_EQ(cache.Size(), 1u);

    const auto* entry = cache.Find(key);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->RefCount, 1u);
    EXPECT_EQ(entry->Handle.Index, 7u);
}

TEST(ProceduralGeometryCacheTest, SecondEnsureResidentHitsReuseAndIncrementsRefcount)
{
    ProceduralGeometryCache cache;
    Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
    desc.VertexCount = 3u;

    int uploadCalls = 0;
    auto upload = [&](const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc&) {
        ++uploadCalls;
        return MakeHandle(11u);
    };

    ProceduralGeometryParams params{};
    const auto key = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);

    const auto first = cache.EnsureResident(key, desc, upload);
    const auto second = cache.EnsureResident(key, desc, upload);

    EXPECT_EQ(uploadCalls, 1);
    EXPECT_EQ(first, second);
    EXPECT_EQ(cache.Stats().Uploads, 1u);
    EXPECT_EQ(cache.Stats().ReuseHits, 1u);

    const auto* entry = cache.Find(key);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->RefCount, 2u);
}

TEST(ProceduralGeometryCacheTest, ReleaseDecrementsRefcount)
{
    ProceduralGeometryCache cache;
    Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
    desc.VertexCount = 3u;

    auto upload = [&](const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc&) {
        return MakeHandle(3u);
    };

    ProceduralGeometryParams params{};
    const auto key = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);

    (void)cache.EnsureResident(key, desc, upload);
    (void)cache.EnsureResident(key, desc, upload);

    EXPECT_FALSE(cache.Release(key));
    EXPECT_EQ(cache.Find(key)->RefCount, 1u);
    EXPECT_EQ(cache.Stats().Releases, 1u);

    EXPECT_TRUE(cache.Release(key));
    EXPECT_EQ(cache.Find(key)->RefCount, 0u);
    EXPECT_EQ(cache.Stats().Releases, 2u);
}

TEST(ProceduralGeometryCacheTest, FailedUploadIncrementsCounterAndYieldsNoEntry)
{
    ProceduralGeometryCache cache;
    Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
    auto upload = [](const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc&) {
        return Extrinsic::Graphics::GpuGeometryHandle{};
    };

    ProceduralGeometryParams params{};
    const auto key = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);

    const auto handle = cache.EnsureResident(key, desc, upload);

    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(cache.Stats().FailedUploads, 1u);
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(cache.Find(key), nullptr);
}

TEST(ProceduralGeometryPackerTest, TrianglePackEmitsThreeVerticesThreeSurfaceIndices)
{
    ProceduralGeometryPackBuffer scratch;
    ProceduralGeometryParams params{};

    const auto packed = Pack(ProceduralGeometryKind::Triangle, params, scratch);

    ASSERT_TRUE(packed.has_value());
    EXPECT_EQ(packed->VertexCount, 3u);
    EXPECT_EQ(packed->SurfaceIndices.size(), 3u);
    EXPECT_EQ(packed->LineIndices.size(), 0u);
    EXPECT_EQ(packed->PackedVertexBytes.size_bytes(), sizeof(ProceduralVertex) * 3u);
    ASSERT_NE(packed->DebugName, nullptr);
    EXPECT_STREQ(packed->DebugName, "Procedural.Triangle");
    EXPECT_EQ(packed->SurfaceIndices[0], 0u);
    EXPECT_EQ(packed->SurfaceIndices[1], 1u);
    EXPECT_EQ(packed->SurfaceIndices[2], 2u);
}

TEST(ProceduralGeometryPackerTest, TrianglePackIsDeterministic)
{
    ProceduralGeometryPackBuffer scratchA;
    ProceduralGeometryPackBuffer scratchB;
    ProceduralGeometryParams params{};

    const auto a = Pack(ProceduralGeometryKind::Triangle, params, scratchA);
    const auto b = Pack(ProceduralGeometryKind::Triangle, params, scratchB);

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_EQ(a->PackedVertexBytes.size_bytes(), b->PackedVertexBytes.size_bytes());
    EXPECT_EQ(std::memcmp(a->PackedVertexBytes.data(),
                          b->PackedVertexBytes.data(),
                          a->PackedVertexBytes.size_bytes()),
              0);
    EXPECT_EQ(a->SurfaceIndices.size(), b->SurfaceIndices.size());
    for (std::size_t i = 0; i < a->SurfaceIndices.size(); ++i)
    {
        EXPECT_EQ(a->SurfaceIndices[i], b->SurfaceIndices[i]);
    }
}

TEST(ProceduralGeometryPackerTest, InvalidTriangleParamsAreRejected)
{
    ProceduralGeometryPackBuffer scratch;
    ProceduralGeometryParams params{};
    params.VertexCount = 4;
    params.IndexCount = 6;

    const auto packed = Pack(ProceduralGeometryKind::Triangle, params, scratch);

    EXPECT_FALSE(packed.has_value());
}

TEST(ProceduralGeometryPackerTest, PackBufferReusedAcrossCallsClearsBetweenInvocations)
{
    ProceduralGeometryPackBuffer scratch;
    ProceduralGeometryParams params{};

    const auto first = Pack(ProceduralGeometryKind::Triangle, params, scratch);
    ASSERT_TRUE(first.has_value());
    const std::size_t firstVertexBytes = scratch.VertexBytes.size();

    // Repack into the same scratch — packer must Clear() before refilling so the
    // buffer never grows beyond a single Triangle payload.
    const auto second = Pack(ProceduralGeometryKind::Triangle, params, scratch);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(scratch.VertexBytes.size(), firstVertexBytes);
    EXPECT_EQ(scratch.SurfaceIndices.size(), 3u);
}

TEST(ProceduralGeometryRefComponent, DefaultIsTriangleWithZeroPayload)
{
    ProceduralGeometryRef ref{};
    EXPECT_EQ(ref.Kind, ProceduralGeometryKind::Triangle);
    EXPECT_EQ(ref.Params.VertexCount, 0u);
    EXPECT_EQ(ref.Params.IndexCount, 0u);
    for (float f : ref.Params.Payload)
    {
        EXPECT_EQ(f, 0.0f);
    }
}
