#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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

TEST(ProceduralGeometryCacheTest, ReleaseAtZeroDoesNotFreeUntilRetireWindowElapses)
{
    ProceduralGeometryCache cache;
    Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
    desc.VertexCount = 3u;
    auto upload = [&](const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc&) {
        return MakeHandle(13u);
    };

    ProceduralGeometryParams params{};
    const auto key = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);

    const auto handle = cache.EnsureResident(key, desc, upload);
    ASSERT_TRUE(handle.IsValid());

    std::vector<Extrinsic::Graphics::GpuGeometryHandle> freed;
    auto freeFn = [&](Extrinsic::Graphics::GpuGeometryHandle h) { freed.push_back(h); };

    EXPECT_TRUE(cache.Release(key));
    EXPECT_EQ(cache.Stats().Releases, 1u);
    EXPECT_EQ(cache.PendingRetireCount(), 1u);

    constexpr std::uint32_t framesInFlight = 2u;
    constexpr std::uint64_t baseFrame = 100u;

    // Tick at the release frame anchors the deadline but does not free.
    cache.Tick(baseFrame, framesInFlight, freeFn);
    EXPECT_EQ(cache.Stats().FreeRetires, 0u);
    EXPECT_TRUE(freed.empty());
    EXPECT_NE(cache.Find(key), nullptr);

    // For framesInFlight - 1 subsequent ticks, the entry stays resident.
    for (std::uint32_t i = 1; i < framesInFlight; ++i)
    {
        cache.Tick(baseFrame + i, framesInFlight, freeFn);
        EXPECT_EQ(cache.Stats().FreeRetires, 0u);
        EXPECT_TRUE(freed.empty());
        EXPECT_NE(cache.Find(key), nullptr);
    }

    // On the framesInFlight-th tick after release, the free fires exactly once.
    cache.Tick(baseFrame + framesInFlight, framesInFlight, freeFn);
    EXPECT_EQ(cache.Stats().FreeRetires, 1u);
    ASSERT_EQ(freed.size(), 1u);
    EXPECT_EQ(freed[0].Index, 13u);
    EXPECT_EQ(cache.Find(key), nullptr);
    EXPECT_EQ(cache.PendingRetireCount(), 0u);

    // Subsequent ticks do not double-free.
    cache.Tick(baseFrame + framesInFlight + 1u, framesInFlight, freeFn);
    EXPECT_EQ(cache.Stats().FreeRetires, 1u);
    EXPECT_EQ(freed.size(), 1u);
}

TEST(ProceduralGeometryCacheTest, ResurrectInRetireWindowCancelsFreeAndReusesHandle)
{
    ProceduralGeometryCache cache;
    Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
    desc.VertexCount = 3u;
    int uploadCalls = 0;
    auto upload = [&](const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc&) {
        ++uploadCalls;
        return MakeHandle(21u);
    };

    ProceduralGeometryParams params{};
    const auto key = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);

    const auto first = cache.EnsureResident(key, desc, upload);
    ASSERT_TRUE(first.IsValid());
    EXPECT_TRUE(cache.Release(key));
    EXPECT_EQ(cache.PendingRetireCount(), 1u);

    // Anchor the deadline so we're observably inside the retire window.
    std::vector<Extrinsic::Graphics::GpuGeometryHandle> freed;
    auto freeFn = [&](Extrinsic::Graphics::GpuGeometryHandle h) { freed.push_back(h); };
    cache.Tick(50u, /*framesInFlight=*/3u, freeFn);
    EXPECT_EQ(cache.Stats().FreeRetires, 0u);
    EXPECT_EQ(cache.PendingRetireCount(), 1u);

    // Resurrect: same key, no upload should fire, RetireCancellations bumps.
    const auto resurrected = cache.EnsureResident(key, desc, upload);
    EXPECT_EQ(uploadCalls, 1);
    EXPECT_EQ(resurrected, first);
    EXPECT_EQ(cache.Stats().RetireCancellations, 1u);
    EXPECT_EQ(cache.PendingRetireCount(), 0u);

    const auto* entry = cache.Find(key);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->RefCount, 1u);
    EXPECT_EQ(entry->Handle, first);

    // Advance past what would have been the free deadline: no free fires.
    cache.Tick(60u, 3u, freeFn);
    EXPECT_EQ(cache.Stats().FreeRetires, 0u);
    EXPECT_TRUE(freed.empty());
    EXPECT_NE(cache.Find(key), nullptr);
}

TEST(ProceduralGeometryCacheTest, RefCountSaturationRejectsFurtherIncrements)
{
    ProceduralGeometryCache cache;
    Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
    desc.VertexCount = 3u;
    auto upload = [&](const Extrinsic::Graphics::GpuWorld::GeometryUploadDesc&) {
        return MakeHandle(31u);
    };

    ProceduralGeometryParams params{};
    const auto key = MakeProceduralGeometryKey(ProceduralGeometryKind::Triangle, params);

    const auto handle = cache.EnsureResident(key, desc, upload);
    ASSERT_TRUE(handle.IsValid());

    // Drive refcount to the cap without 2^32 calls.
    ASSERT_TRUE(cache.PrimeRefCountForTest(key, std::numeric_limits<std::uint32_t>::max()));

    EXPECT_EQ(cache.Stats().RefCountSaturated, 0u);
    const auto saturated = cache.EnsureResident(key, desc, upload);
    EXPECT_EQ(saturated, handle);
    EXPECT_EQ(cache.Stats().RefCountSaturated, 1u);
    EXPECT_EQ(cache.Find(key)->RefCount, std::numeric_limits<std::uint32_t>::max());

    // Repeat: counter keeps growing, refcount never overflows.
    (void)cache.EnsureResident(key, desc, upload);
    EXPECT_EQ(cache.Stats().RefCountSaturated, 2u);
    EXPECT_EQ(cache.Find(key)->RefCount, std::numeric_limits<std::uint32_t>::max());
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
