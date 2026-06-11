#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>

#include <gtest/gtest.h>

import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.VisualizationPropertyBufferResidency;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    template <typename T, std::size_t N>
    [[nodiscard]] std::span<const std::byte> BytesOf(
        const std::array<T, N>& values) noexcept
    {
        return std::as_bytes(std::span<const T>{values.data(), values.size()});
    }
}

TEST(VisualizationPropertyBufferResidencyContract, ValidatesDescriptorShapeAndFinitePayloads)
{
    const std::array<float, 3> values{{1.0f, 2.0f, 3.0f}};
    const std::array<float, 2> nonFinite{{1.0f, std::numeric_limits<float>::infinity()}};

    const std::array<Graphics::VisualizationPropertyBufferUploadDescriptor, 5> descriptors{{
        Graphics::VisualizationPropertyBufferUploadDescriptor{
            .SourceKey = "valid.scalar",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ValueType = Graphics::VisualizationValueType::ScalarFloat,
            .ElementCount = 3u,
            .StrideBytes = sizeof(float),
            .DirtyStamp = 1u,
            .Bytes = BytesOf(values),
        },
        Graphics::VisualizationPropertyBufferUploadDescriptor{
            .SourceKey = "missing.stride",
            .ElementCount = 3u,
            .StrideBytes = sizeof(double),
            .Bytes = BytesOf(values),
        },
        Graphics::VisualizationPropertyBufferUploadDescriptor{
            .SourceKey = "zero.elements",
            .ElementCount = 0u,
            .StrideBytes = sizeof(float),
        },
        Graphics::VisualizationPropertyBufferUploadDescriptor{
            .SourceKey = "bad.type",
            .ValueType = Graphics::VisualizationValueType::Count,
            .ElementCount = 1u,
            .StrideBytes = sizeof(float),
            .Bytes = BytesOf(values).first(sizeof(float)),
        },
        Graphics::VisualizationPropertyBufferUploadDescriptor{
            .SourceKey = "nonfinite",
            .ValueType = Graphics::VisualizationValueType::ScalarFloat,
            .ElementCount = 2u,
            .StrideBytes = sizeof(float),
            .Bytes = BytesOf(nonFinite),
        },
    }};

    const Graphics::VisualizationPropertyBufferDiagnostics diagnostics =
        Graphics::ValidateVisualizationPropertyBufferUploads(descriptors);

    EXPECT_EQ(diagnostics.InputBufferCount, 5u);
    EXPECT_EQ(diagnostics.AcceptedBufferCount, 1u);
    EXPECT_EQ(diagnostics.InvalidStrideCount, 1u);
    EXPECT_EQ(diagnostics.InvalidByteSizeCount, 1u);
    EXPECT_EQ(diagnostics.ZeroElementCount, 1u);
    EXPECT_EQ(diagnostics.UnsupportedTypeCount, 1u);
    EXPECT_EQ(diagnostics.NonFiniteValueCount, 1u);
    EXPECT_TRUE(diagnostics.HasErrors);
}

TEST(VisualizationPropertyBufferResidencyContract, UploadsReusesAndRejectsStaleDirtyStamp)
{
    MockDevice device;
    RHI::BufferManager bufferManager{device};
    Graphics::VisualizationPropertyBufferResidency residency{device, bufferManager};

    const std::array<float, 3> values{{1.0f, 2.0f, 3.0f}};
    const Graphics::VisualizationPropertyBufferUploadDescriptor descriptor{
        .SourceKey = "curvature",
        .Domain = Graphics::VisualizationAttributeDomain::Vertex,
        .ValueType = Graphics::VisualizationValueType::ScalarFloat,
        .ElementCount = 3u,
        .StrideBytes = sizeof(float),
        .DirtyStamp = 7u,
        .Bytes = BytesOf(values),
    };

    Graphics::VisualizationPropertyBufferDiagnostics diagnostics =
        residency.Update(std::span<const Graphics::VisualizationPropertyBufferUploadDescriptor>{
            &descriptor, 1u});

    EXPECT_EQ(diagnostics.AcceptedBufferCount, 1u);
    EXPECT_EQ(diagnostics.UploadedBufferCount, 1u);
    EXPECT_EQ(device.CreateBufferCount, 1);
    ASSERT_EQ(device.BufferWrites.size(), 1u);
    const Graphics::VisualizationPropertyBufferAddress* address =
        residency.Find("curvature");
    ASSERT_NE(address, nullptr);
    EXPECT_NE(address->BufferBDA, 0u);

    diagnostics = residency.Update(
        std::span<const Graphics::VisualizationPropertyBufferUploadDescriptor>{
            &descriptor, 1u});

    EXPECT_EQ(diagnostics.AcceptedBufferCount, 1u);
    EXPECT_EQ(diagnostics.UploadedBufferCount, 0u);
    EXPECT_EQ(diagnostics.ReusedBufferCount, 1u);
    EXPECT_EQ(device.CreateBufferCount, 1);
    EXPECT_EQ(device.BufferWrites.size(), 1u);

    Graphics::VisualizationPropertyBufferUploadDescriptor stale = descriptor;
    stale.DirtyStamp = 6u;
    diagnostics = residency.Update(
        std::span<const Graphics::VisualizationPropertyBufferUploadDescriptor>{
            &stale, 1u});

    EXPECT_EQ(diagnostics.StaleDirtyStampCount, 1u);
    EXPECT_TRUE(diagnostics.HasErrors);

    residency.Clear();
    EXPECT_EQ(device.DestroyBufferCount, 1);
}

TEST(VisualizationPropertyBufferResidencyContract, RendererPublishesAddressBeforePacketValidation)
{
    const std::array<float, 3> values{{-1.0f, 0.0f, 1.0f}};
    const std::array<Graphics::VisualizationPropertyBufferUploadDescriptor, 1> buffers{{
        Graphics::VisualizationPropertyBufferUploadDescriptor{
            .SourceKey = "curvature",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ValueType = Graphics::VisualizationValueType::ScalarFloat,
            .ElementCount = 3u,
            .StrideBytes = sizeof(float),
            .DirtyStamp = 11u,
            .Bytes = BytesOf(values),
        },
    }};
    const std::array<Graphics::ScalarAttributePacket, 1> scalars{{
        Graphics::ScalarAttributePacket{
            .Name = "curvature",
            .SourceBufferKey = "curvature",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 3u,
            .RangeMin = -1.0f,
            .RangeMax = 1.0f,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{841u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationPropertyBuffers = buffers,
        .VisualizationScalars = scalars,
    });

    Graphics::RenderWorld world = renderer->ExtractRenderWorld(
        Graphics::RenderFrameInput{.Viewport = {.Width = 64, .Height = 64}});

    ASSERT_EQ(world.Visualization.Scalars.size(), 1u);
    EXPECT_NE(world.Visualization.Scalars.front().ScalarBufferBDA, 0u);
    EXPECT_EQ(world.Visualization.Diagnostics.AcceptedPacketCount, 1u);
    EXPECT_FALSE(world.Visualization.Diagnostics.HasErrors);
    EXPECT_EQ(world.Visualization.PropertyBufferDiagnostics.UploadedBufferCount, 1u);

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_EQ(stats.VisualizationPropertyBuffers.UploadedBufferCount, 1u);
    EXPECT_EQ(stats.VisualizationPropertyBuffers.UploadDeferralCount, 0u);

    renderer->Shutdown();
}
