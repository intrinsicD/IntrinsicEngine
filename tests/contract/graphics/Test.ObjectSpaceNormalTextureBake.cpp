#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <span>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

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
        BindIndexBuffer,
        PushConstants,
        DrawIndexed,
        EndRenderPass,
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
        }

        void Draw(std::uint32_t,
                  std::uint32_t,
                  std::uint32_t,
                  std::uint32_t) override {}

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
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t IndexOffset = 0u;
        RHI::IndexType IndexType = RHI::IndexType::Uint16;
        std::uint32_t PushConstantSize = 0u;
        std::uint32_t PushConstantOffset = 0u;
        std::vector<std::byte> PushConstantBytes{};
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

TEST(ObjectSpaceNormalTextureBake, RecordGpuBakeCommandsPinsRasterExtentAndDraw)
{
    RecordingCommandContext cmd;
    const Graphics::ObjectSpaceNormalTextureBakeGpuRecordDesc desc{
        .Pipeline = RHI::PipelineHandle{5u, 1u},
        .OutputTexture = RHI::TextureHandle{6u, 1u},
        .IndexBuffer = RHI::BufferHandle{7u, 1u},
        .TexcoordBDA = 0x1000u,
        .NormalBDA = 0x2000u,
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
    EXPECT_EQ(cmd.DrawFirstIndex, 0u);
    EXPECT_EQ(cmd.DrawVertexOffset, 0);
    EXPECT_EQ(cmd.DrawFirstInstance, 0u);
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
}
