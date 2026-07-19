#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <span>
#include <vector>

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    using BakeVertex = Graphics::ObjectSpaceNormalTextureBakeVertex;
    using BakeTriangle = Graphics::ObjectSpaceNormalTextureBakeTriangle;

    enum class RecordedEvent
    {
        TextureBarrier,
        BeginRenderPass,
        SetViewport,
        SetScissor,
        BindPipeline,
        BindFrameSampledTextureAt,
        BindIndexBuffer,
        PushConstants,
        Draw,
        DrawIndexed,
        EndRenderPass,
    };

    struct DrawRecord
    {
        std::uint32_t VertexCount = 0u;
        std::uint32_t InstanceCount = 0u;
        std::uint32_t FirstVertex = 0u;
        std::uint32_t FirstInstance = 0u;
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}

        void BeginRenderPass(const RHI::RenderPassDesc& desc) override
        {
            Events.push_back(RecordedEvent::BeginRenderPass);
            ColorTargetCount = static_cast<std::uint32_t>(desc.ColorTargets.size());
            if (!desc.ColorTargets.empty())
            {
                FirstColorTarget = desc.ColorTargets.front().Target;
                FirstColorLoad = desc.ColorTargets.front().Load;
                FirstColorStore = desc.ColorTargets.front().Store;
                FirstClearR = desc.ColorTargets.front().ClearR;
                FirstClearG = desc.ColorTargets.front().ClearG;
                FirstClearB = desc.ColorTargets.front().ClearB;
                FirstClearA = desc.ColorTargets.front().ClearA;
                ColorTargetHistory.push_back(desc.ColorTargets.front().Target);
                ColorLoadHistory.push_back(desc.ColorTargets.front().Load);
                ColorStoreHistory.push_back(desc.ColorTargets.front().Store);
            }
        }

        void EndRenderPass() override
        {
            Events.push_back(RecordedEvent::EndRenderPass);
        }

        void SetViewport(float,
                         float,
                         const float width,
                         const float height,
                         float,
                         float) override
        {
            Events.push_back(RecordedEvent::SetViewport);
            ViewportWidth = width;
            ViewportHeight = height;
        }

        void SetScissor(std::int32_t,
                        std::int32_t,
                        const std::uint32_t width,
                        const std::uint32_t height) override
        {
            Events.push_back(RecordedEvent::SetScissor);
            ScissorWidth = width;
            ScissorHeight = height;
        }

        void BindPipeline(const RHI::PipelineHandle pipeline) override
        {
            Events.push_back(RecordedEvent::BindPipeline);
            Pipeline = pipeline;
            BoundPipelines.push_back(pipeline);
        }

        void BindFrameSampledTextureAt(
            const RHI::TextureHandle texture,
            const std::uint32_t descriptorIndex) override
        {
            Events.push_back(RecordedEvent::BindFrameSampledTextureAt);
            SampledTextureBindings.push_back(texture);
            SampledTextureBindingSlots.push_back(descriptorIndex);
        }

        void BindIndexBuffer(const RHI::BufferHandle buffer,
                             const std::uint64_t offset,
                             const RHI::IndexType indexType) override
        {
            Events.push_back(RecordedEvent::BindIndexBuffer);
            IndexBuffer = buffer;
            IndexOffset = offset;
            IndexType = indexType;
        }

        void PushConstants(const void* data,
                           const std::uint32_t size,
                           const std::uint32_t offset = 0) override
        {
            Events.push_back(RecordedEvent::PushConstants);
            PushConstantSize = size;
            PushConstantOffset = offset;
            PushConstantBytes.resize(size);
            if (data != nullptr && size > 0u)
            {
                std::memcpy(PushConstantBytes.data(), data, size);
            }
            PushConstantPayloads.push_back(PushConstantBytes);
            PushConstantSizes.push_back(size);
        }

        void Draw(const std::uint32_t vertexCount,
                  const std::uint32_t instanceCount,
                  const std::uint32_t firstVertex,
                  const std::uint32_t firstInstance) override
        {
            Events.push_back(RecordedEvent::Draw);
            DrawRecords.push_back(DrawRecord{
                .VertexCount = vertexCount,
                .InstanceCount = instanceCount,
                .FirstVertex = firstVertex,
                .FirstInstance = firstInstance,
            });
        }

        void DrawIndexed(const std::uint32_t indexCount,
                         const std::uint32_t instanceCount,
                         const std::uint32_t firstIndex,
                         const std::int32_t vertexOffset,
                         const std::uint32_t firstInstance) override
        {
            Events.push_back(RecordedEvent::DrawIndexed);
            DrawIndexCount = indexCount;
            DrawInstanceCount = instanceCount;
            DrawFirstIndex = firstIndex;
            DrawVertexOffset = vertexOffset;
            DrawFirstInstance = firstInstance;
        }

        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle,
                                      std::uint64_t,
                                      RHI::BufferHandle,
                                      std::uint64_t,
                                      std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle,
                               std::uint64_t,
                               RHI::BufferHandle,
                               std::uint64_t,
                               std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}

        void TextureBarrier(const RHI::TextureHandle texture,
                            const RHI::TextureLayout before,
                            const RHI::TextureLayout after) override
        {
            Events.push_back(RecordedEvent::TextureBarrier);
            TextureBarrierTextures.push_back(texture);
            TextureBarrierBefore.push_back(before);
            TextureBarrierAfter.push_back(after);
        }

        void BufferBarrier(RHI::BufferHandle,
                           RHI::MemoryAccess,
                           RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle,
                        RHI::BufferHandle,
                        std::uint64_t,
                        std::uint64_t,
                        std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle,
                                 std::uint64_t,
                                 RHI::TextureHandle,
                                 std::uint32_t,
                                 std::uint32_t) override {}

        std::vector<RecordedEvent> Events{};
        std::vector<RHI::TextureHandle> TextureBarrierTextures{};
        std::vector<RHI::TextureLayout> TextureBarrierBefore{};
        std::vector<RHI::TextureLayout> TextureBarrierAfter{};
        std::vector<RHI::TextureHandle> ColorTargetHistory{};
        std::vector<RHI::LoadOp> ColorLoadHistory{};
        std::vector<RHI::StoreOp> ColorStoreHistory{};
        std::vector<RHI::TextureHandle> SampledTextureBindings{};
        std::vector<std::uint32_t> SampledTextureBindingSlots{};
        RHI::TextureHandle FirstColorTarget{};
        RHI::LoadOp FirstColorLoad = RHI::LoadOp::DontCare;
        RHI::StoreOp FirstColorStore = RHI::StoreOp::DontCare;
        float FirstClearR = 0.0f;
        float FirstClearG = 0.0f;
        float FirstClearB = 0.0f;
        float FirstClearA = 1.0f;
        std::uint32_t ColorTargetCount = 0u;
        float ViewportWidth = 0.0f;
        float ViewportHeight = 0.0f;
        std::uint32_t ScissorWidth = 0u;
        std::uint32_t ScissorHeight = 0u;
        RHI::PipelineHandle Pipeline{};
        std::vector<RHI::PipelineHandle> BoundPipelines{};
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t IndexOffset = 0u;
        RHI::IndexType IndexType = RHI::IndexType::Uint16;
        std::uint32_t PushConstantSize = 0u;
        std::uint32_t PushConstantOffset = 0u;
        std::vector<std::byte> PushConstantBytes{};
        std::vector<std::vector<std::byte>> PushConstantPayloads{};
        std::vector<std::uint32_t> PushConstantSizes{};
        std::vector<DrawRecord> DrawRecords{};
        std::uint32_t DrawIndexCount = 0u;
        std::uint32_t DrawInstanceCount = 0u;
        std::uint32_t DrawFirstIndex = 0u;
        std::int32_t DrawVertexOffset = 0;
        std::uint32_t DrawFirstInstance = 0u;
    };

    constexpr float kEpsilon = 1.0e-5f;

    [[nodiscard]] std::array<BakeVertex, 3> MakeAsymmetricTriangle()
    {
        return {
            BakeVertex{
                .Uv = {0.0f, 0.0f},
                .Normal = {1.0f, 0.0f, 0.0f},
            },
            BakeVertex{
                .Uv = {1.0f, 0.0f},
                .Normal = {0.0f, 1.0f, 0.0f},
            },
            BakeVertex{
                .Uv = {0.0f, 1.0f},
                .Normal = {0.0f, 0.0f, 1.0f},
            },
        };
    }

    [[nodiscard]] std::array<BakeTriangle, 1> OneTriangle()
    {
        return {BakeTriangle{.A = 0u, .B = 1u, .C = 2u}};
    }

    void ExpectNearVec3(const glm::vec3& actual, const glm::vec3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, kEpsilon);
        EXPECT_NEAR(actual.y, expected.y, kEpsilon);
        EXPECT_NEAR(actual.z, expected.z, kEpsilon);
    }

    void ExpectNearVec4(const glm::vec4& actual, const glm::vec4& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, kEpsilon);
        EXPECT_NEAR(actual.y, expected.y, kEpsilon);
        EXPECT_NEAR(actual.z, expected.z, kEpsilon);
        EXPECT_NEAR(actual.w, expected.w, kEpsilon);
    }

    [[nodiscard]] bool HasTextureUsage(const RHI::TextureUsage flags,
                                       const RHI::TextureUsage bit) noexcept
    {
        return (static_cast<std::uint32_t>(flags) &
                static_cast<std::uint32_t>(bit)) != 0u;
    }

    [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakePlanRequest
    MakeValidPlanRequest()
    {
        return Graphics::ObjectSpaceNormalTextureBakePlanRequest{
            .GeneratedTextureAsset = Assets::AssetId{42u, 7u},
            .Geometry = Graphics::ObjectSpaceNormalTextureBakeGeometryBuffers{
                .IndexBuffer = RHI::BufferHandle{3u, 1u},
                .TexcoordBDA = 0x1000u,
                .NormalBDA = 0x2000u,
                .VertexCount = 3u,
                .FirstIndex = 6u,
                .IndexCount = 3u,
            },
            .Options = Graphics::ObjectSpaceNormalTextureBakeOptions{
                .Width = 32u,
                .Height = 64u,
                .PaddingTexels = 0u,
            },
            .SourceKey = Graphics::ObjectSpaceNormalTextureBakeSourceKey{
                .EntityKey = 0xabcdu,
                .GeometryGeneration = 11u,
                .TexcoordGeneration = 12u,
                .NormalGeneration = 13u,
            },
            .Pipeline = RHI::PipelineHandle{5u, 1u},
            .SamplerDesc = RHI::SamplerDesc{
                .AddressU = RHI::AddressMode::ClampToEdge,
                .AddressV = RHI::AddressMode::ClampToEdge,
                .AddressW = RHI::AddressMode::ClampToEdge,
                .DebugName = "ObjectSpaceNormalBake.ContractSampler",
            },
            .AdditionalTextureUsage = RHI::TextureUsage::TransferSrc,
            .InitialLayout = RHI::TextureLayout::Undefined,
            .FinalLayout = RHI::TextureLayout::TransferSrc,
            .DebugName = "ObjectSpaceNormalBake.ContractOutput",
            .ReadyFrame = 17u,
            .HasReadyFrame = true,
        };
    }

    [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakeDilationResources
    MakeValidDilationResources()
    {
        return Graphics::ObjectSpaceNormalTextureBakeDilationResources{
            .Pipeline = RHI::PipelineHandle{8u, 1u},
            .ScratchTexture = RHI::TextureHandle{10u, 1u},
            .ScratchInitialLayout = RHI::TextureLayout::Undefined,
            .OutputDescriptorSlot =
                Graphics::kObjectSpaceNormalBakeDilationOutputDescriptorSlot,
            .ScratchDescriptorSlot =
                Graphics::kObjectSpaceNormalBakeDilationScratchDescriptorSlot,
        };
    }

    template <typename T>
    [[nodiscard]] T DecodePushConstant(const std::vector<std::byte>& payload)
    {
        T out{};
        EXPECT_EQ(payload.size(), sizeof(T));
        if (payload.size() == sizeof(T))
        {
            std::memcpy(&out, payload.data(), sizeof(T));
        }
        return out;
    }
}

TEST(ObjectSpaceNormalTextureBake, ResolvesDefaultExtentAndClampPolicy)
{
    const auto defaults = Graphics::ResolveObjectSpaceNormalTextureBakeOptions({});
    EXPECT_EQ(defaults.Width, Graphics::kObjectSpaceNormalBakeDefaultExtent);
    EXPECT_EQ(defaults.Height, Graphics::kObjectSpaceNormalBakeDefaultExtent);
    EXPECT_EQ(defaults.PaddingTexels,
              Graphics::kObjectSpaceNormalBakeDefaultPaddingTexels);

    const auto clamped = Graphics::ResolveObjectSpaceNormalTextureBakeOptions(
        Graphics::ObjectSpaceNormalTextureBakeOptions{
            .Width = 1u,
            .Height = 99999u,
            .PaddingTexels = 999u,
            .AtlasUvEpsilon = -1.0f,
            .DegenerateUvAreaEpsilon = 0.0f,
            .DegenerateNormalLengthEpsilon = 0.0f,
        });
    EXPECT_EQ(clamped.Width, Graphics::kObjectSpaceNormalBakeMinExtent);
    EXPECT_EQ(clamped.Height, Graphics::kObjectSpaceNormalBakeMaxExtent);
    EXPECT_EQ(clamped.PaddingTexels,
              Graphics::kObjectSpaceNormalBakeMaxPaddingTexels);
    EXPECT_FLOAT_EQ(clamped.AtlasUvEpsilon, 1.0e-4f);
    EXPECT_FLOAT_EQ(clamped.DegenerateUvAreaEpsilon, 1.0e-10f);
    EXPECT_FLOAT_EQ(clamped.DegenerateNormalLengthEpsilon, 1.0e-6f);
}

TEST(ObjectSpaceNormalTextureBake, RejectsReservedTangentSpaceMode)
{
    const auto vertices = MakeAsymmetricTriangle();
    const auto triangles = OneTriangle();

    const auto validation = Graphics::ValidateObjectSpaceNormalTextureBakeInput(
        std::span<const BakeVertex>{vertices},
        std::span<const BakeTriangle>{triangles},
        Graphics::ObjectSpaceNormalTextureBakeOptions{
            .Space = Graphics::NormalTextureSpace::TangentSpaceNormal,
        });

    EXPECT_FALSE(validation.Succeeded());
    EXPECT_EQ(validation.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::
                  UnsupportedNormalTextureSpace);
}

TEST(ObjectSpaceNormalTextureBake, FailsClosedForNonAtlasAndDegenerateInput)
{
    auto vertices = MakeAsymmetricTriangle();
    const auto triangles = OneTriangle();
    vertices[1].Uv = {1.25f, 0.0f};

    auto validation = Graphics::ValidateObjectSpaceNormalTextureBakeInput(
        std::span<const BakeVertex>{vertices},
        std::span<const BakeTriangle>{triangles});
    EXPECT_EQ(validation.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::NonAtlasTexcoord);
    EXPECT_EQ(validation.Diagnostics.FirstFailureIndex, 1u);

    vertices = MakeAsymmetricTriangle();
    vertices[2].Uv = {2.0e-6f, 0.0f};
    validation = Graphics::ValidateObjectSpaceNormalTextureBakeInput(
        std::span<const BakeVertex>{vertices},
        std::span<const BakeTriangle>{triangles},
        Graphics::ObjectSpaceNormalTextureBakeOptions{
            .DegenerateUvAreaEpsilon = 1.0e-5f,
        });
    EXPECT_EQ(validation.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::DegenerateUvTriangle);
    EXPECT_EQ(validation.Diagnostics.DegenerateUvTriangleCount, 1u);
}

TEST(ObjectSpaceNormalTextureBake, SamplesKnownObjectNormalsAtSelectedTexelCenters)
{
    const auto vertices = MakeAsymmetricTriangle();
    const auto triangles = OneTriangle();
    const Graphics::ObjectSpaceNormalTextureBakeOptions options{
        .Width = 16u,
        .Height = 16u,
    };
    const auto resolved =
        Graphics::ResolveObjectSpaceNormalTextureBakeOptions(options);

    const glm::vec2 firstUv =
        Graphics::UvForObjectSpaceNormalBakeTexelCenter(0u, 0u, resolved);
    const auto first = Graphics::SampleObjectSpaceNormalTextureBakeAtUv(
        std::span<const BakeVertex>{vertices},
        std::span<const BakeTriangle>{triangles},
        firstUv,
        options);
    ASSERT_TRUE(first.Succeeded())
        << Graphics::DebugNameForObjectSpaceNormalTextureBakeStatus(first.Status);
    ExpectNearVec3(first.Barycentric, glm::vec3{0.9375f, 0.03125f, 0.03125f});

    const glm::vec3 firstNormal =
        glm::normalize(glm::vec3{0.9375f, 0.03125f, 0.03125f});
    ExpectNearVec3(first.ObjectNormal, firstNormal);
    ExpectNearVec4(first.EncodedRgba,
                   glm::vec4{(firstNormal * 0.5f) + glm::vec3{0.5f}, 1.0f});

    const glm::vec2 secondUv =
        Graphics::UvForObjectSpaceNormalBakeTexelCenter(1u, 1u, resolved);
    const auto second = Graphics::SampleObjectSpaceNormalTextureBakeAtUv(
        std::span<const BakeVertex>{vertices},
        std::span<const BakeTriangle>{triangles},
        secondUv,
        options);
    ASSERT_TRUE(second.Succeeded())
        << Graphics::DebugNameForObjectSpaceNormalTextureBakeStatus(second.Status);
    ExpectNearVec3(second.Barycentric,
                   glm::vec3{0.8125f, 0.09375f, 0.09375f});

    const glm::vec3 secondNormal =
        glm::normalize(glm::vec3{0.8125f, 0.09375f, 0.09375f});
    ExpectNearVec3(second.ObjectNormal, secondNormal);
    ExpectNearVec4(second.EncodedRgba,
                   glm::vec4{(secondNormal * 0.5f) + glm::vec3{0.5f}, 1.0f});
}

TEST(ObjectSpaceNormalTextureBake, ReportsNoContainingTriangleInsideAtlasOutsideIsland)
{
    const auto vertices = MakeAsymmetricTriangle();
    const auto triangles = OneTriangle();

    const auto sample = Graphics::SampleObjectSpaceNormalTextureBakeAtUv(
        std::span<const BakeVertex>{vertices},
        std::span<const BakeTriangle>{triangles},
        glm::vec2{0.75f, 0.75f});

    EXPECT_FALSE(sample.Succeeded());
    EXPECT_EQ(sample.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::NoContainingTriangle);
}

TEST(ObjectSpaceNormalTextureBake, BuildsGpuPipelineDescForRgbaBakeTarget)
{
    const RHI::PipelineDesc desc =
        Graphics::MakeObjectSpaceNormalTextureBakePipelineDesc(
            "object_space_normal_bake.vert.spv",
            "object_space_normal_bake.frag.spv");

    EXPECT_EQ(desc.VertexShaderPath, "object_space_normal_bake.vert.spv");
    EXPECT_EQ(desc.FragmentShaderPath, "object_space_normal_bake.frag.spv");
    EXPECT_TRUE(desc.ComputeShaderPath.empty());
    EXPECT_EQ(desc.Rasterizer.Culling, RHI::CullMode::None);
    EXPECT_FALSE(desc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(desc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(desc.ColorTargetCount, 1u);
    EXPECT_EQ(desc.ColorTargetFormats[0], RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(desc.PushConstantSize,
              sizeof(Graphics::ObjectSpaceNormalTextureBakeGpuPushConstants));
}

TEST(ObjectSpaceNormalTextureBake, BuildsDilationResourceDescs)
{
    const RHI::PipelineDesc pipeline =
        Graphics::MakeObjectSpaceNormalTextureBakeDilationPipelineDesc(
            "post_fullscreen.vert.spv",
            "object_space_normal_dilate.frag.spv");

    EXPECT_EQ(pipeline.VertexShaderPath, "post_fullscreen.vert.spv");
    EXPECT_EQ(pipeline.FragmentShaderPath,
              "object_space_normal_dilate.frag.spv");
    EXPECT_TRUE(pipeline.ComputeShaderPath.empty());
    EXPECT_EQ(pipeline.Rasterizer.Culling, RHI::CullMode::None);
    EXPECT_FALSE(pipeline.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(pipeline.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(pipeline.ColorTargetCount, 1u);
    EXPECT_EQ(pipeline.ColorTargetFormats[0], RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(pipeline.PushConstantSize,
              sizeof(Graphics::ObjectSpaceNormalTextureBakeDilationPushConstants));

    const RHI::TextureDesc scratch =
        Graphics::MakeObjectSpaceNormalTextureBakeDilationScratchTextureDesc(
            Graphics::ObjectSpaceNormalTextureBakeOptions{
                .Width = 12u,
                .Height = 99999u,
            },
            "DilationScratch.Contract");
    EXPECT_EQ(scratch.Width, Graphics::kObjectSpaceNormalBakeMinExtent);
    EXPECT_EQ(scratch.Height, Graphics::kObjectSpaceNormalBakeMaxExtent);
    EXPECT_EQ(scratch.Fmt, RHI::Format::RGBA8_UNORM);
    EXPECT_TRUE(HasTextureUsage(scratch.Usage, RHI::TextureUsage::Sampled));
    EXPECT_TRUE(HasTextureUsage(scratch.Usage, RHI::TextureUsage::ColorTarget));
    EXPECT_FALSE(HasTextureUsage(scratch.Usage, RHI::TextureUsage::Storage));
    EXPECT_STREQ(scratch.DebugName, "DilationScratch.Contract");
}

TEST(ObjectSpaceNormalTextureBake, DilationResourceLeaseOwnsPipelineAndScratch)
{
    Tests::MockDevice device;
    const auto desc =
        Graphics::MakeObjectSpaceNormalTextureBakeDilationResourceDesc(
            Graphics::ObjectSpaceNormalTextureBakeOptions{
                .Width = 32u,
                .Height = 32u,
            },
            "post_fullscreen.vert.spv",
            "object_space_normal_dilate.frag.spv",
            "ObjectSpaceNormalBake.LeaseScratch");

    Graphics::ObjectSpaceNormalTextureBakeDilationResourceLease lease;
    const Core::Result initialized = lease.Initialize(device, desc);
    ASSERT_TRUE(initialized.has_value());
    ASSERT_TRUE(lease.IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 1);
    EXPECT_EQ(device.CreateTextureCount, 1);
    ASSERT_EQ(device.CreatedTextureDescs.size(), 1u);
    EXPECT_STREQ(device.CreatedTextureDescs.front().DebugName,
                 "ObjectSpaceNormalBake.LeaseScratch");

    const auto resources = lease.GetResources();
    EXPECT_TRUE(resources.Pipeline.IsValid());
    EXPECT_TRUE(resources.ScratchTexture.IsValid());
    EXPECT_EQ(resources.OutputDescriptorSlot,
              Graphics::kObjectSpaceNormalBakeDilationOutputDescriptorSlot);
    EXPECT_EQ(resources.ScratchDescriptorSlot,
              Graphics::kObjectSpaceNormalBakeDilationScratchDescriptorSlot);

    lease.Shutdown();
    EXPECT_FALSE(lease.IsValid());
    EXPECT_EQ(device.DestroyTextureCount, 1);
    EXPECT_EQ(device.DestroyPipelineCount, 1);
}

TEST(ObjectSpaceNormalTextureBake, DilationResourceLeaseFailsClosedWhenDeviceIsUnavailable)
{
    Tests::MockDevice device;
    device.Operational = false;

    Graphics::ObjectSpaceNormalTextureBakeDilationResourceLease lease;
    const Core::Result initialized = lease.Initialize(
        device,
        Graphics::MakeObjectSpaceNormalTextureBakeDilationResourceDesc(
            {},
            "post_fullscreen.vert.spv",
            "object_space_normal_dilate.frag.spv"));

    ASSERT_FALSE(initialized.has_value());
    EXPECT_EQ(initialized.error(), Core::ErrorCode::DeviceNotOperational);
    EXPECT_FALSE(lease.IsValid());
    EXPECT_EQ(device.CreatePipelineCount, 0);
    EXPECT_EQ(device.CreateTextureCount, 0);
}

TEST(ObjectSpaceNormalTextureBake, BuildsGpuProducedTexturePlanWithCompletionKey)
{
    const auto request = MakeValidPlanRequest();

    const auto plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(request);
    ASSERT_TRUE(plan.Succeeded())
        << Graphics::DebugNameForObjectSpaceNormalTextureBakeStatus(plan.Status);

    EXPECT_EQ(plan.TextureRequest.Id, request.GeneratedTextureAsset);
    EXPECT_EQ(plan.TextureRequest.Desc.Width, 32u);
    EXPECT_EQ(plan.TextureRequest.Desc.Height, 64u);
    EXPECT_EQ(plan.TextureRequest.Desc.Fmt, RHI::Format::RGBA8_UNORM);
    EXPECT_TRUE(HasTextureUsage(plan.TextureRequest.Desc.Usage,
                                RHI::TextureUsage::Sampled));
    EXPECT_TRUE(HasTextureUsage(plan.TextureRequest.Desc.Usage,
                                RHI::TextureUsage::ColorTarget));
    EXPECT_TRUE(HasTextureUsage(plan.TextureRequest.Desc.Usage,
                                RHI::TextureUsage::TransferSrc));
    EXPECT_FALSE(HasTextureUsage(plan.TextureRequest.Desc.Usage,
                                 RHI::TextureUsage::Storage))
        << "The zero-padding path does not require dilation storage-image usage.";
    EXPECT_EQ(plan.TextureRequest.Desc.InitialLayout,
              RHI::TextureLayout::Undefined);
    EXPECT_STREQ(plan.TextureRequest.Desc.DebugName,
                 "ObjectSpaceNormalBake.ContractOutput");
    EXPECT_EQ(plan.TextureRequest.ReadyFrame, 17u);
    EXPECT_TRUE(plan.TextureRequest.HasReadyFrame);

    EXPECT_EQ(plan.CompletionKey.GeneratedTextureAsset,
              request.GeneratedTextureAsset);
    EXPECT_EQ(plan.CompletionKey.Source.EntityKey, 0xabcdu);
    EXPECT_EQ(plan.CompletionKey.Source.GeometryGeneration, 11u);
    EXPECT_EQ(plan.CompletionKey.Source.TexcoordGeneration, 12u);
    EXPECT_EQ(plan.CompletionKey.Source.NormalGeneration, 13u);
    EXPECT_EQ(plan.CompletionKey.Width, 32u);
    EXPECT_EQ(plan.CompletionKey.Height, 64u);
    EXPECT_EQ(plan.CompletionKey.PaddingTexels, 0u);
    EXPECT_EQ(plan.CompletionKey.Space,
              Graphics::NormalTextureSpace::ObjectSpaceNormal);
    EXPECT_FALSE(plan.DilationRequested);
    EXPECT_FALSE(plan.DilationAvailable);
    EXPECT_EQ(plan.RecordTemplate.FirstIndex, request.Geometry.FirstIndex);

    const RHI::TextureHandle outputTexture{9u, 2u};
    const auto record =
        Graphics::MakeObjectSpaceNormalTextureBakeGpuRecordDesc(plan,
                                                                outputTexture);
    EXPECT_EQ(record.Pipeline, request.Pipeline);
    EXPECT_EQ(record.OutputTexture, outputTexture);
    EXPECT_EQ(record.IndexBuffer, request.Geometry.IndexBuffer);
    EXPECT_EQ(record.TexcoordBDA, request.Geometry.TexcoordBDA);
    EXPECT_EQ(record.NormalBDA, request.Geometry.NormalBDA);
    EXPECT_EQ(record.FirstIndex, request.Geometry.FirstIndex);
    EXPECT_EQ(record.IndexCount, request.Geometry.IndexCount);
    EXPECT_EQ(record.Width, 32u);
    EXPECT_EQ(record.Height, 64u);
    EXPECT_EQ(record.InitialLayout, RHI::TextureLayout::Undefined);
    EXPECT_EQ(record.FinalLayout, RHI::TextureLayout::TransferSrc);
}

TEST(ObjectSpaceNormalTextureBake, PaddedGpuProducedTexturePlanFailsClosed)
{
    auto request = MakeValidPlanRequest();
    request.Options.PaddingTexels = 4u;

    const auto plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(request);

    EXPECT_FALSE(plan.Succeeded());
    EXPECT_EQ(plan.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::DilationUnavailable);
    EXPECT_TRUE(plan.DilationRequested);
    EXPECT_FALSE(plan.DilationAvailable);
    EXPECT_EQ(plan.Diagnostics.Options.PaddingTexels, 4u);
    EXPECT_FALSE(plan.TextureRequest.Id.IsValid());
    EXPECT_FALSE(plan.RecordTemplate.Pipeline.IsValid());
}

TEST(ObjectSpaceNormalTextureBake, PaddedGpuProducedTexturePlanUsesDilationResources)
{
    auto request = MakeValidPlanRequest();
    request.Options.PaddingTexels = 999u;
    request.Dilation = MakeValidDilationResources();

    const auto plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(request);

    ASSERT_TRUE(plan.Succeeded())
        << Graphics::DebugNameForObjectSpaceNormalTextureBakeStatus(plan.Status);
    EXPECT_TRUE(plan.DilationRequested);
    EXPECT_TRUE(plan.DilationAvailable);
    EXPECT_EQ(plan.Diagnostics.Options.PaddingTexels,
              Graphics::kObjectSpaceNormalBakeMaxPaddingTexels);
    EXPECT_EQ(plan.CompletionKey.PaddingTexels,
              Graphics::kObjectSpaceNormalBakeMaxPaddingTexels);
    EXPECT_TRUE(HasTextureUsage(plan.TextureRequest.Desc.Usage,
                                RHI::TextureUsage::Sampled));
    EXPECT_TRUE(HasTextureUsage(plan.TextureRequest.Desc.Usage,
                                RHI::TextureUsage::ColorTarget));
    EXPECT_FALSE(HasTextureUsage(plan.TextureRequest.Desc.Usage,
                                 RHI::TextureUsage::Storage));
    EXPECT_EQ(plan.RecordTemplate.PaddingTexels,
              Graphics::kObjectSpaceNormalBakeMaxPaddingTexels);
    EXPECT_EQ(plan.RecordTemplate.Dilation.Pipeline,
              request.Dilation.Pipeline);
    EXPECT_EQ(plan.RecordTemplate.Dilation.ScratchTexture,
              request.Dilation.ScratchTexture);

    const RHI::TextureHandle outputTexture{9u, 2u};
    const auto record =
        Graphics::MakeObjectSpaceNormalTextureBakeGpuRecordDesc(plan,
                                                                outputTexture);
    EXPECT_EQ(record.PaddingTexels,
              Graphics::kObjectSpaceNormalBakeMaxPaddingTexels);
    EXPECT_EQ(record.Dilation.ScratchTexture,
              request.Dilation.ScratchTexture);
}

TEST(ObjectSpaceNormalTextureBake, CompletionKeyDetectsStaleBakeResults)
{
    const auto plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(
        MakeValidPlanRequest());
    ASSERT_TRUE(plan.Succeeded());

    auto current = plan.CompletionKey;
    EXPECT_TRUE(Graphics::ObjectSpaceNormalTextureBakeCompletionKeyMatches(
        plan.CompletionKey,
        current));

    current.Source.NormalGeneration += 1u;
    EXPECT_FALSE(Graphics::ObjectSpaceNormalTextureBakeCompletionKeyMatches(
        plan.CompletionKey,
        current));

    current = plan.CompletionKey;
    current.PaddingTexels += 1u;
    EXPECT_FALSE(Graphics::ObjectSpaceNormalTextureBakeCompletionKeyMatches(
        plan.CompletionKey,
        current));
}

TEST(ObjectSpaceNormalTextureBake, BuildGpuProducedTexturePlanFailsClosed)
{
    auto request = MakeValidPlanRequest();
    request.GeneratedTextureAsset = Assets::AssetId{};
    auto plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(request);
    EXPECT_FALSE(plan.Succeeded());
    EXPECT_EQ(plan.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::
                  InvalidGeneratedTextureAsset);

    request = MakeValidPlanRequest();
    request.Geometry.IndexCount = 4u;
    plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(request);
    EXPECT_FALSE(plan.Succeeded());
    EXPECT_EQ(plan.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::InvalidIndexCount);
    EXPECT_EQ(plan.Diagnostics.FirstFailureIndex, 4u);

    request = MakeValidPlanRequest();
    request.Geometry.TexcoordBDA = 0u;
    plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(request);
    EXPECT_FALSE(plan.Succeeded());
    EXPECT_EQ(plan.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::InvalidGpuResource);

    request = MakeValidPlanRequest();
    request.Geometry.TexcoordBDA += 4u;
    plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(request);
    EXPECT_FALSE(plan.Succeeded());
    EXPECT_EQ(plan.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::InvalidGpuResource);

    request = MakeValidPlanRequest();
    request.Geometry.NormalBDA += 2u;
    plan = Graphics::BuildObjectSpaceNormalTextureBakePlan(request);
    EXPECT_FALSE(plan.Succeeded());
    EXPECT_EQ(plan.Status,
              Graphics::ObjectSpaceNormalTextureBakeStatus::InvalidGpuResource);
}

TEST(ObjectSpaceNormalTextureBake, RecordGpuBakeCommandsPinsRasterExtentAndDraw)
{
    RecordingCommandContext cmd;
    const Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc desc{
        .Pipeline = RHI::PipelineHandle{5u, 1u},
        .OutputTexture = RHI::TextureHandle{6u, 1u},
        .IndexBuffer = RHI::BufferHandle{7u, 1u},
        .TexcoordBDA = 0x1000u,
        .NormalBDA = 0x2000u,
        .FirstIndex = 9u,
        .IndexCount = 3u,
        .Width = 16u,
        .Height = 8u,
        .InitialLayout = RHI::TextureLayout::Undefined,
        .FinalLayout = RHI::TextureLayout::TransferSrc,
    };

    const auto result = Graphics::RecordObjectSpaceNormalTextureBake(cmd, desc);
    ASSERT_TRUE(result.has_value());

    const std::vector<RecordedEvent> expected{
        RecordedEvent::TextureBarrier,
        RecordedEvent::BeginRenderPass,
        RecordedEvent::SetViewport,
        RecordedEvent::SetScissor,
        RecordedEvent::BindPipeline,
        RecordedEvent::BindIndexBuffer,
        RecordedEvent::PushConstants,
        RecordedEvent::DrawIndexed,
        RecordedEvent::EndRenderPass,
        RecordedEvent::TextureBarrier,
    };
    EXPECT_EQ(cmd.Events, expected);
    ASSERT_EQ(cmd.TextureBarrierTextures.size(), 2u);
    EXPECT_EQ(cmd.TextureBarrierTextures[0], desc.OutputTexture);
    EXPECT_EQ(cmd.TextureBarrierBefore[0], RHI::TextureLayout::Undefined);
    EXPECT_EQ(cmd.TextureBarrierAfter[0], RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(cmd.TextureBarrierTextures[1], desc.OutputTexture);
    EXPECT_EQ(cmd.TextureBarrierBefore[1], RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(cmd.TextureBarrierAfter[1], RHI::TextureLayout::TransferSrc);

    EXPECT_EQ(cmd.ColorTargetCount, 1u);
    EXPECT_EQ(cmd.FirstColorTarget, desc.OutputTexture);
    EXPECT_EQ(cmd.FirstColorLoad, RHI::LoadOp::Clear);
    EXPECT_EQ(cmd.FirstColorStore, RHI::StoreOp::Store);
    EXPECT_FLOAT_EQ(cmd.FirstClearR, 0.5f);
    EXPECT_FLOAT_EQ(cmd.FirstClearG, 0.5f);
    EXPECT_FLOAT_EQ(cmd.FirstClearB, 1.0f);
    EXPECT_FLOAT_EQ(cmd.FirstClearA, 0.0f);
    EXPECT_FLOAT_EQ(cmd.ViewportWidth, 16.0f);
    EXPECT_FLOAT_EQ(cmd.ViewportHeight, 8.0f);
    EXPECT_EQ(cmd.ScissorWidth, 16u);
    EXPECT_EQ(cmd.ScissorHeight, 8u);
    EXPECT_EQ(cmd.Pipeline, desc.Pipeline);
    EXPECT_EQ(cmd.IndexBuffer, desc.IndexBuffer);
    EXPECT_EQ(cmd.IndexOffset, 0u);
    EXPECT_EQ(cmd.IndexType, RHI::IndexType::Uint32);
    ASSERT_EQ(cmd.PushConstantBytes.size(),
              sizeof(Graphics::ObjectSpaceNormalTextureBakeGpuPushConstants));
    Graphics::ObjectSpaceNormalTextureBakeGpuPushConstants push{};
    std::memcpy(&push, cmd.PushConstantBytes.data(), sizeof(push));
    EXPECT_EQ(push.TexcoordBDA, desc.TexcoordBDA);
    EXPECT_EQ(push.NormalBDA, desc.NormalBDA);
    EXPECT_EQ(cmd.DrawIndexCount, 3u);
    EXPECT_EQ(cmd.DrawInstanceCount, 1u);
    EXPECT_EQ(cmd.DrawFirstIndex, desc.FirstIndex);
    EXPECT_EQ(cmd.DrawVertexOffset, 0);
    EXPECT_EQ(cmd.DrawFirstInstance, 0u);
}

TEST(ObjectSpaceNormalTextureBake, RecordGpuBakeCommandsPingPongDilationPasses)
{
    RecordingCommandContext cmd;
    const RHI::TextureHandle outputTexture{6u, 1u};
    const RHI::TextureHandle scratchTexture{9u, 1u};
    const RHI::PipelineHandle rasterPipeline{5u, 1u};
    const RHI::PipelineHandle dilationPipeline{8u, 1u};
    const Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc desc{
        .Pipeline = rasterPipeline,
        .OutputTexture = outputTexture,
        .Dilation = Graphics::ObjectSpaceNormalTextureBakeDilationResources{
            .Pipeline = dilationPipeline,
            .ScratchTexture = scratchTexture,
            .ScratchInitialLayout = RHI::TextureLayout::Undefined,
            .OutputDescriptorSlot =
                Graphics::kObjectSpaceNormalBakeDilationOutputDescriptorSlot,
            .ScratchDescriptorSlot =
                Graphics::kObjectSpaceNormalBakeDilationScratchDescriptorSlot,
        },
        .IndexBuffer = RHI::BufferHandle{7u, 1u},
        .TexcoordBDA = 0x1000u,
        .NormalBDA = 0x2000u,
        .IndexCount = 3u,
        .Width = 16u,
        .Height = 8u,
        .PaddingTexels = 2u,
        .InitialLayout = RHI::TextureLayout::Undefined,
        .FinalLayout = RHI::TextureLayout::TransferSrc,
    };

    const auto result = Graphics::RecordObjectSpaceNormalTextureBake(cmd, desc);
    ASSERT_TRUE(result.has_value());

    const std::vector<RecordedEvent> expected{
        RecordedEvent::BindFrameSampledTextureAt,
        RecordedEvent::BindFrameSampledTextureAt,
        RecordedEvent::TextureBarrier,
        RecordedEvent::BeginRenderPass,
        RecordedEvent::SetViewport,
        RecordedEvent::SetScissor,
        RecordedEvent::BindPipeline,
        RecordedEvent::BindIndexBuffer,
        RecordedEvent::PushConstants,
        RecordedEvent::DrawIndexed,
        RecordedEvent::EndRenderPass,
        RecordedEvent::TextureBarrier,
        RecordedEvent::TextureBarrier,
        RecordedEvent::BeginRenderPass,
        RecordedEvent::SetViewport,
        RecordedEvent::SetScissor,
        RecordedEvent::BindPipeline,
        RecordedEvent::PushConstants,
        RecordedEvent::Draw,
        RecordedEvent::EndRenderPass,
        RecordedEvent::TextureBarrier,
        RecordedEvent::TextureBarrier,
        RecordedEvent::BeginRenderPass,
        RecordedEvent::SetViewport,
        RecordedEvent::SetScissor,
        RecordedEvent::BindPipeline,
        RecordedEvent::PushConstants,
        RecordedEvent::Draw,
        RecordedEvent::EndRenderPass,
        RecordedEvent::TextureBarrier,
    };
    EXPECT_EQ(cmd.Events, expected);

    ASSERT_EQ(cmd.SampledTextureBindings.size(), 2u);
    EXPECT_EQ(cmd.SampledTextureBindings[0], outputTexture);
    EXPECT_EQ(cmd.SampledTextureBindingSlots[0],
              Graphics::kObjectSpaceNormalBakeDilationOutputDescriptorSlot);
    EXPECT_EQ(cmd.SampledTextureBindings[1], scratchTexture);
    EXPECT_EQ(cmd.SampledTextureBindingSlots[1],
              Graphics::kObjectSpaceNormalBakeDilationScratchDescriptorSlot);

    ASSERT_EQ(cmd.TextureBarrierTextures.size(), 6u);
    EXPECT_EQ(cmd.TextureBarrierTextures[0], outputTexture);
    EXPECT_EQ(cmd.TextureBarrierBefore[0], RHI::TextureLayout::Undefined);
    EXPECT_EQ(cmd.TextureBarrierAfter[0], RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(cmd.TextureBarrierTextures[1], outputTexture);
    EXPECT_EQ(cmd.TextureBarrierBefore[1], RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(cmd.TextureBarrierAfter[1], RHI::TextureLayout::ShaderReadOnly);
    EXPECT_EQ(cmd.TextureBarrierTextures[2], scratchTexture);
    EXPECT_EQ(cmd.TextureBarrierBefore[2], RHI::TextureLayout::Undefined);
    EXPECT_EQ(cmd.TextureBarrierAfter[2], RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(cmd.TextureBarrierTextures[3], scratchTexture);
    EXPECT_EQ(cmd.TextureBarrierBefore[3], RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(cmd.TextureBarrierAfter[3], RHI::TextureLayout::ShaderReadOnly);
    EXPECT_EQ(cmd.TextureBarrierTextures[4], outputTexture);
    EXPECT_EQ(cmd.TextureBarrierBefore[4], RHI::TextureLayout::ShaderReadOnly);
    EXPECT_EQ(cmd.TextureBarrierAfter[4], RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(cmd.TextureBarrierTextures[5], outputTexture);
    EXPECT_EQ(cmd.TextureBarrierBefore[5], RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(cmd.TextureBarrierAfter[5], RHI::TextureLayout::TransferSrc);

    const std::vector<RHI::TextureHandle> expectedTargets{
        outputTexture,
        scratchTexture,
        outputTexture,
    };
    EXPECT_EQ(cmd.ColorTargetHistory, expectedTargets);
    const std::vector<RHI::LoadOp> expectedLoads{
        RHI::LoadOp::Clear,
        RHI::LoadOp::DontCare,
        RHI::LoadOp::DontCare,
    };
    EXPECT_EQ(cmd.ColorLoadHistory, expectedLoads);
    const std::vector<RHI::PipelineHandle> expectedPipelines{
        rasterPipeline,
        dilationPipeline,
        dilationPipeline,
    };
    EXPECT_EQ(cmd.BoundPipelines, expectedPipelines);

    ASSERT_EQ(cmd.PushConstantPayloads.size(), 3u);
    const auto rasterPush =
        DecodePushConstant<Graphics::ObjectSpaceNormalTextureBakeGpuPushConstants>(
            cmd.PushConstantPayloads[0]);
    EXPECT_EQ(rasterPush.TexcoordBDA, desc.TexcoordBDA);
    EXPECT_EQ(rasterPush.NormalBDA, desc.NormalBDA);
    const auto firstDilationPush =
        DecodePushConstant<
            Graphics::ObjectSpaceNormalTextureBakeDilationPushConstants>(
            cmd.PushConstantPayloads[1]);
    const auto secondDilationPush =
        DecodePushConstant<
            Graphics::ObjectSpaceNormalTextureBakeDilationPushConstants>(
            cmd.PushConstantPayloads[2]);
    EXPECT_EQ(firstDilationPush.SourceTextureSlot,
              Graphics::kObjectSpaceNormalBakeDilationOutputDescriptorSlot);
    EXPECT_EQ(secondDilationPush.SourceTextureSlot,
              Graphics::kObjectSpaceNormalBakeDilationScratchDescriptorSlot);

    ASSERT_EQ(cmd.DrawRecords.size(), 2u);
    EXPECT_EQ(cmd.DrawRecords[0].VertexCount, 3u);
    EXPECT_EQ(cmd.DrawRecords[0].InstanceCount, 1u);
    EXPECT_EQ(cmd.DrawRecords[0].FirstVertex, 0u);
    EXPECT_EQ(cmd.DrawRecords[0].FirstInstance, 0u);
    EXPECT_EQ(cmd.DrawRecords[1].VertexCount, 3u);
    EXPECT_EQ(cmd.DrawRecords[1].InstanceCount, 1u);
    EXPECT_EQ(cmd.DrawIndexCount, 3u);
    EXPECT_EQ(cmd.DrawFirstIndex, 0u);
    EXPECT_EQ(cmd.DrawVertexOffset, 0);
}

TEST(ObjectSpaceNormalTextureBake, RecordGpuBakeRejectsInvalidResourcesBeforeCommands)
{
    RecordingCommandContext cmd;

    const auto result = Graphics::RecordObjectSpaceNormalTextureBake(
        cmd,
        Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc{
            .Pipeline = RHI::PipelineHandle{5u, 1u},
            .OutputTexture = RHI::TextureHandle{6u, 1u},
            .IndexBuffer = RHI::BufferHandle{7u, 1u},
            .TexcoordBDA = 0u,
            .NormalBDA = 0x2000u,
            .IndexCount = 3u,
            .Width = 16u,
            .Height = 16u,
        });

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(cmd.Events.empty());

    const auto misalignedTexcoord =
        Graphics::RecordObjectSpaceNormalTextureBake(
            cmd,
            Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc{
                .Pipeline = RHI::PipelineHandle{5u, 1u},
                .OutputTexture = RHI::TextureHandle{6u, 1u},
                .IndexBuffer = RHI::BufferHandle{7u, 1u},
                .TexcoordBDA = 0x1004u,
                .NormalBDA = 0x2000u,
                .IndexCount = 3u,
                .Width = 16u,
                .Height = 16u,
            });
    ASSERT_FALSE(misalignedTexcoord.has_value());
    EXPECT_EQ(misalignedTexcoord.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(cmd.Events.empty());

    const auto misalignedNormal =
        Graphics::RecordObjectSpaceNormalTextureBake(
            cmd,
            Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc{
                .Pipeline = RHI::PipelineHandle{5u, 1u},
                .OutputTexture = RHI::TextureHandle{6u, 1u},
                .IndexBuffer = RHI::BufferHandle{7u, 1u},
                .TexcoordBDA = 0x1000u,
                .NormalBDA = 0x2002u,
                .IndexCount = 3u,
                .Width = 16u,
                .Height = 16u,
            });
    ASSERT_FALSE(misalignedNormal.has_value());
    EXPECT_EQ(misalignedNormal.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(cmd.Events.empty());

    const auto missingDilation = Graphics::RecordObjectSpaceNormalTextureBake(
        cmd,
        Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc{
            .Pipeline = RHI::PipelineHandle{5u, 1u},
            .OutputTexture = RHI::TextureHandle{6u, 1u},
            .IndexBuffer = RHI::BufferHandle{7u, 1u},
            .TexcoordBDA = 0x1000u,
            .NormalBDA = 0x2000u,
            .IndexCount = 3u,
            .Width = 16u,
            .Height = 16u,
            .PaddingTexels = 1u,
        });

    ASSERT_FALSE(missingDilation.has_value());
    EXPECT_EQ(missingDilation.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(cmd.Events.empty());

    const auto unclampedPadding = Graphics::RecordObjectSpaceNormalTextureBake(
        cmd,
        Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc{
            .Pipeline = RHI::PipelineHandle{5u, 1u},
            .OutputTexture = RHI::TextureHandle{6u, 1u},
            .Dilation = MakeValidDilationResources(),
            .IndexBuffer = RHI::BufferHandle{7u, 1u},
            .TexcoordBDA = 0x1000u,
            .NormalBDA = 0x2000u,
            .IndexCount = 3u,
            .Width = 16u,
            .Height = 16u,
            .PaddingTexels = Graphics::kObjectSpaceNormalBakeMaxPaddingTexels + 1u,
        });

    ASSERT_FALSE(unclampedPadding.has_value());
    EXPECT_EQ(unclampedPadding.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(cmd.Events.empty());
}
