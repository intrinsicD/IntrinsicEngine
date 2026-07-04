module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <span>
#include <string>
#include <utility>

module Extrinsic.Graphics.ObjectSpaceNormalTextureBake;

import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] float Cross2(const glm::vec2 a, const glm::vec2 b) noexcept
        {
            return a.x * b.y - a.y * b.x;
        }

        [[nodiscard]] bool IsAtlasUv(const glm::vec2 uv, const float epsilon) noexcept
        {
            return uv.x >= -epsilon && uv.y >= -epsilon &&
                   uv.x <= 1.0f + epsilon && uv.y <= 1.0f + epsilon;
        }

        [[nodiscard]] glm::vec3 Barycentric(
            const glm::vec2 a,
            const glm::vec2 b,
            const glm::vec2 c,
            const glm::vec2 p) noexcept
        {
            const float denominator =
                ((b.y - c.y) * (a.x - c.x)) +
                ((c.x - b.x) * (a.y - c.y));
            if (std::abs(denominator) <= 0.0f)
            {
                return glm::vec3{-1.0f};
            }

            const float w0 =
                (((b.y - c.y) * (p.x - c.x)) +
                 ((c.x - b.x) * (p.y - c.y))) / denominator;
            const float w1 =
                (((c.y - a.y) * (p.x - c.x)) +
                 ((a.x - c.x) * (p.y - c.y))) / denominator;
            return glm::vec3{w0, w1, 1.0f - w0 - w1};
        }

        [[nodiscard]] bool ContainsBarycentric(
            const glm::vec3 barycentric,
            const float epsilon) noexcept
        {
            return barycentric.x >= -epsilon &&
                   barycentric.y >= -epsilon &&
                   barycentric.z >= -epsilon &&
                   barycentric.x <= 1.0f + epsilon &&
                   barycentric.y <= 1.0f + epsilon &&
                   barycentric.z <= 1.0f + epsilon;
        }

        [[nodiscard]] glm::vec3 NormalizeOrFallback(
            const glm::vec3 normal,
            const float epsilon) noexcept
        {
            const float length = glm::length(normal);
            if (!std::isfinite(length) || length <= epsilon)
            {
                return glm::vec3{0.0f, 0.0f, 1.0f};
            }
            return normal / length;
        }

        [[nodiscard]] bool AreDilationResourcesUsable(
            const ObjectSpaceNormalTextureBakeDilationResources& resources) noexcept
        {
            return resources.IsValid();
        }

        [[nodiscard]] RHI::TextureHandle SelectDilationTarget(
            const RHI::TextureHandle output,
            const RHI::TextureHandle scratch,
            const bool useOutput) noexcept
        {
            return useOutput ? output : scratch;
        }

        struct TextureRecordingState
        {
            RHI::TextureHandle Handle{};
            RHI::TextureLayout Layout = RHI::TextureLayout::Undefined;
        };

        void TransitionTexture(RHI::ICommandContext& cmd,
                               TextureRecordingState& state,
                               const RHI::TextureLayout next)
        {
            if (state.Layout == next)
            {
                return;
            }
            cmd.TextureBarrier(state.Handle, state.Layout, next);
            state.Layout = next;
        }

        void RecordSingleColorFullscreenPass(
            RHI::ICommandContext& cmd,
            const RHI::TextureHandle target,
            const std::uint32_t width,
            const std::uint32_t height,
            const RHI::LoadOp load,
            const RHI::PipelineHandle pipeline)
        {
            const std::array<RHI::ColorAttachment, 1u> colorAttachments{{
                RHI::ColorAttachment{
                    .Target = target,
                    .Load = load,
                    .Store = RHI::StoreOp::Store,
                    .ClearR = 0.5f,
                    .ClearG = 0.5f,
                    .ClearB = 1.0f,
                    .ClearA = 0.0f,
                },
            }};

            cmd.BeginRenderPass(RHI::RenderPassDesc{
                .ColorTargets = std::span<const RHI::ColorAttachment>{
                    colorAttachments},
            });
            cmd.SetViewport(0.0f,
                            0.0f,
                            static_cast<float>(width),
                            static_cast<float>(height),
                            0.0f,
                            1.0f);
            cmd.SetScissor(0, 0, width, height);
            cmd.BindPipeline(pipeline);
        }

        void RecordRasterBakePass(
            RHI::ICommandContext& cmd,
            const ObjectSpaceNormalTextureBakeGpuRecordDesc& desc,
            const RHI::TextureHandle target)
        {
            const ObjectSpaceNormalTextureBakeGpuPushConstants push{
                .TexcoordBDA = desc.TexcoordBDA,
                .NormalBDA = desc.NormalBDA,
            };

            RecordSingleColorFullscreenPass(cmd,
                                            target,
                                            desc.Width,
                                            desc.Height,
                                            RHI::LoadOp::Clear,
                                            desc.Pipeline);
            cmd.BindIndexBuffer(desc.IndexBuffer, 0u, RHI::IndexType::Uint32);
            cmd.PushConstants(
                &push,
                static_cast<std::uint32_t>(
                    sizeof(ObjectSpaceNormalTextureBakeGpuPushConstants)),
                0u);
            cmd.DrawIndexed(desc.IndexCount, 1u, 0u, 0, 0u);
            cmd.EndRenderPass();
        }

        void RecordDilationPass(
            RHI::ICommandContext& cmd,
            const ObjectSpaceNormalTextureBakeGpuRecordDesc& desc,
            const RHI::TextureHandle target,
            const std::uint32_t sourceDescriptorSlot)
        {
            const ObjectSpaceNormalTextureBakeDilationPushConstants push{
                .SourceTextureSlot = sourceDescriptorSlot,
            };

            RecordSingleColorFullscreenPass(cmd,
                                            target,
                                            desc.Width,
                                            desc.Height,
                                            RHI::LoadOp::DontCare,
                                            desc.Dilation.Pipeline);
            cmd.PushConstants(
                &push,
                static_cast<std::uint32_t>(
                    sizeof(ObjectSpaceNormalTextureBakeDilationPushConstants)),
                0u);
            cmd.Draw(3u, 1u, 0u, 0u);
            cmd.EndRenderPass();
        }
    }

    const char* DebugNameForObjectSpaceNormalTextureBakeStatus(
        const ObjectSpaceNormalTextureBakeStatus status) noexcept
    {
        switch (status)
        {
        case ObjectSpaceNormalTextureBakeStatus::Success:
            return "Success";
        case ObjectSpaceNormalTextureBakeStatus::UnsupportedNormalTextureSpace:
            return "UnsupportedNormalTextureSpace";
        case ObjectSpaceNormalTextureBakeStatus::EmptyInput:
            return "EmptyInput";
        case ObjectSpaceNormalTextureBakeStatus::InvalidTriangleIndex:
            return "InvalidTriangleIndex";
        case ObjectSpaceNormalTextureBakeStatus::NonFiniteTexcoord:
            return "NonFiniteTexcoord";
        case ObjectSpaceNormalTextureBakeStatus::NonAtlasTexcoord:
            return "NonAtlasTexcoord";
        case ObjectSpaceNormalTextureBakeStatus::NonFiniteNormal:
            return "NonFiniteNormal";
        case ObjectSpaceNormalTextureBakeStatus::DegenerateNormal:
            return "DegenerateNormal";
        case ObjectSpaceNormalTextureBakeStatus::DegenerateUvTriangle:
            return "DegenerateUvTriangle";
        case ObjectSpaceNormalTextureBakeStatus::NoContainingTriangle:
            return "NoContainingTriangle";
        case ObjectSpaceNormalTextureBakeStatus::InvalidGeneratedTextureAsset:
            return "InvalidGeneratedTextureAsset";
        case ObjectSpaceNormalTextureBakeStatus::InvalidGpuResource:
            return "InvalidGpuResource";
        case ObjectSpaceNormalTextureBakeStatus::InvalidIndexCount:
            return "InvalidIndexCount";
        case ObjectSpaceNormalTextureBakeStatus::DilationUnavailable:
            return "DilationUnavailable";
        }
        return "Unknown";
    }

    ObjectSpaceNormalTextureBakeResolvedOptions
    ResolveObjectSpaceNormalTextureBakeOptions(
        const ObjectSpaceNormalTextureBakeOptions& options) noexcept
    {
        ObjectSpaceNormalTextureBakeResolvedOptions resolved{};
        const auto resolveExtent = [](const std::uint32_t requested) noexcept
        {
            const std::uint32_t extent =
                requested == 0u ? kObjectSpaceNormalBakeDefaultExtent : requested;
            return std::clamp(extent,
                              kObjectSpaceNormalBakeMinExtent,
                              kObjectSpaceNormalBakeMaxExtent);
        };

        resolved.Width = resolveExtent(options.Width);
        resolved.Height = resolveExtent(options.Height);
        resolved.PaddingTexels =
            std::min(options.PaddingTexels, kObjectSpaceNormalBakeMaxPaddingTexels);
        resolved.AtlasUvEpsilon =
            std::isfinite(options.AtlasUvEpsilon) && options.AtlasUvEpsilon >= 0.0f
                ? options.AtlasUvEpsilon
                : 1.0e-4f;
        resolved.DegenerateUvAreaEpsilon =
            std::isfinite(options.DegenerateUvAreaEpsilon) &&
                    options.DegenerateUvAreaEpsilon > 0.0f
                ? options.DegenerateUvAreaEpsilon
                : 1.0e-10f;
        resolved.DegenerateNormalLengthEpsilon =
            std::isfinite(options.DegenerateNormalLengthEpsilon) &&
                    options.DegenerateNormalLengthEpsilon > 0.0f
                ? options.DegenerateNormalLengthEpsilon
                : 1.0e-6f;
        resolved.Space = options.Space;
        return resolved;
    }

    ObjectSpaceNormalTextureBakeValidation
    ValidateObjectSpaceNormalTextureBakeInput(
        const std::span<const ObjectSpaceNormalTextureBakeVertex> vertices,
        const std::span<const ObjectSpaceNormalTextureBakeTriangle> triangles,
        const ObjectSpaceNormalTextureBakeOptions& options)
    {
        ObjectSpaceNormalTextureBakeValidation out{};
        out.Diagnostics.Options = ResolveObjectSpaceNormalTextureBakeOptions(options);
        out.Diagnostics.VertexCount = static_cast<std::uint32_t>(vertices.size());
        out.Diagnostics.TriangleCount = static_cast<std::uint32_t>(triangles.size());

        if (out.Diagnostics.Options.Space != NormalTextureSpace::ObjectSpaceNormal)
        {
            out.Status =
                ObjectSpaceNormalTextureBakeStatus::UnsupportedNormalTextureSpace;
            return out;
        }

        if (vertices.empty() || triangles.empty())
        {
            out.Status = ObjectSpaceNormalTextureBakeStatus::EmptyInput;
            return out;
        }

        for (std::size_t index = 0; index < vertices.size(); ++index)
        {
            const ObjectSpaceNormalTextureBakeVertex& vertex = vertices[index];
            if (!IsFinite(vertex.Uv))
            {
                out.Status = ObjectSpaceNormalTextureBakeStatus::NonFiniteTexcoord;
                out.Diagnostics.FirstFailureIndex = static_cast<std::uint32_t>(index);
                return out;
            }
            if (!IsAtlasUv(vertex.Uv, out.Diagnostics.Options.AtlasUvEpsilon))
            {
                out.Status = ObjectSpaceNormalTextureBakeStatus::NonAtlasTexcoord;
                out.Diagnostics.FirstFailureIndex = static_cast<std::uint32_t>(index);
                return out;
            }
            if (!IsFinite(vertex.Normal))
            {
                out.Status = ObjectSpaceNormalTextureBakeStatus::NonFiniteNormal;
                out.Diagnostics.FirstFailureIndex = static_cast<std::uint32_t>(index);
                return out;
            }
            if (glm::length(vertex.Normal) <=
                out.Diagnostics.Options.DegenerateNormalLengthEpsilon)
            {
                out.Status = ObjectSpaceNormalTextureBakeStatus::DegenerateNormal;
                out.Diagnostics.FirstFailureIndex = static_cast<std::uint32_t>(index);
                return out;
            }
        }

        for (std::size_t index = 0; index < triangles.size(); ++index)
        {
            const ObjectSpaceNormalTextureBakeTriangle tri = triangles[index];
            if (tri.A >= vertices.size() ||
                tri.B >= vertices.size() ||
                tri.C >= vertices.size())
            {
                out.Status = ObjectSpaceNormalTextureBakeStatus::InvalidTriangleIndex;
                out.Diagnostics.FirstFailureIndex = static_cast<std::uint32_t>(index);
                return out;
            }

            const glm::vec2 a = vertices[tri.A].Uv;
            const glm::vec2 b = vertices[tri.B].Uv;
            const glm::vec2 c = vertices[tri.C].Uv;
            const float area2 = std::abs(Cross2(b - a, c - a));
            if (area2 <= out.Diagnostics.Options.DegenerateUvAreaEpsilon)
            {
                ++out.Diagnostics.DegenerateUvTriangleCount;
                out.Status = ObjectSpaceNormalTextureBakeStatus::DegenerateUvTriangle;
                out.Diagnostics.FirstFailureIndex = static_cast<std::uint32_t>(index);
                return out;
            }
        }

        out.Status = ObjectSpaceNormalTextureBakeStatus::Success;
        return out;
    }

    glm::vec4 EncodeObjectSpaceNormalToRgba(const glm::vec3& normal) noexcept
    {
        const glm::vec3 n = NormalizeOrFallback(normal, 1.0e-6f);
        return glm::vec4{(n * 0.5f) + glm::vec3{0.5f}, 1.0f};
    }

    glm::vec2 UvForObjectSpaceNormalBakeTexelCenter(
        const std::uint32_t x,
        const std::uint32_t y,
        const ObjectSpaceNormalTextureBakeResolvedOptions& options) noexcept
    {
        const float width = static_cast<float>(std::max(options.Width, 1u));
        const float height = static_cast<float>(std::max(options.Height, 1u));
        return glm::vec2{
            (static_cast<float>(x) + 0.5f) / width,
            (static_cast<float>(y) + 0.5f) / height,
        };
    }

    ObjectSpaceNormalTextureBakeSample
    SampleObjectSpaceNormalTextureBakeAtUv(
        const std::span<const ObjectSpaceNormalTextureBakeVertex> vertices,
        const std::span<const ObjectSpaceNormalTextureBakeTriangle> triangles,
        const glm::vec2& uv,
        const ObjectSpaceNormalTextureBakeOptions& options)
    {
        ObjectSpaceNormalTextureBakeSample sample{};
        sample.Uv = uv;

        const ObjectSpaceNormalTextureBakeValidation validation =
            ValidateObjectSpaceNormalTextureBakeInput(vertices, triangles, options);
        if (!validation.Succeeded())
        {
            sample.Status = validation.Status;
            return sample;
        }

        const ObjectSpaceNormalTextureBakeResolvedOptions resolved =
            validation.Diagnostics.Options;
        if (!IsFinite(uv))
        {
            sample.Status = ObjectSpaceNormalTextureBakeStatus::NonFiniteTexcoord;
            return sample;
        }
        if (!IsAtlasUv(uv, resolved.AtlasUvEpsilon))
        {
            sample.Status = ObjectSpaceNormalTextureBakeStatus::NonAtlasTexcoord;
            return sample;
        }

        for (std::size_t index = 0; index < triangles.size(); ++index)
        {
            const ObjectSpaceNormalTextureBakeTriangle tri = triangles[index];
            const glm::vec2 a = vertices[tri.A].Uv;
            const glm::vec2 b = vertices[tri.B].Uv;
            const glm::vec2 c = vertices[tri.C].Uv;
            const glm::vec3 bary = Barycentric(a, b, c, uv);
            if (!ContainsBarycentric(bary, resolved.AtlasUvEpsilon))
            {
                continue;
            }

            const glm::vec3 normal =
                (bary.x * vertices[tri.A].Normal) +
                (bary.y * vertices[tri.B].Normal) +
                (bary.z * vertices[tri.C].Normal);
            const float normalLength = glm::length(normal);
            if (!std::isfinite(normalLength) ||
                normalLength <= resolved.DegenerateNormalLengthEpsilon)
            {
                sample.Status =
                    ObjectSpaceNormalTextureBakeStatus::DegenerateNormal;
                sample.Barycentric = bary;
                sample.TriangleIndex = static_cast<std::uint32_t>(index);
                return sample;
            }

            sample.Status = ObjectSpaceNormalTextureBakeStatus::Success;
            sample.Barycentric = bary;
            sample.ObjectNormal = normal / normalLength;
            sample.EncodedRgba = EncodeObjectSpaceNormalToRgba(sample.ObjectNormal);
            sample.TriangleIndex = static_cast<std::uint32_t>(index);
            return sample;
        }

        sample.Status = ObjectSpaceNormalTextureBakeStatus::NoContainingTriangle;
        return sample;
    }

    RHI::PipelineDesc MakeObjectSpaceNormalTextureBakePipelineDesc(
        std::string vertexShaderPath,
        std::string fragmentShaderPath,
        const RHI::Format colorFormat)
    {
        RHI::PipelineDesc desc{};
        desc.VertexShaderPath = std::move(vertexShaderPath);
        desc.FragmentShaderPath = std::move(fragmentShaderPath);
        desc.Rasterizer.Culling = RHI::CullMode::None;
        desc.DepthStencil.DepthTestEnable = false;
        desc.DepthStencil.DepthWriteEnable = false;
        desc.ColorTargetCount = 1u;
        desc.ColorTargetFormats[0] = colorFormat;
        desc.PushConstantSize =
            static_cast<std::uint32_t>(
                sizeof(ObjectSpaceNormalTextureBakeGpuPushConstants));
        desc.DebugName = "ObjectSpaceNormalTextureBake";
        return desc;
    }

    RHI::PipelineDesc MakeObjectSpaceNormalTextureBakeDilationPipelineDesc(
        std::string vertexShaderPath,
        std::string fragmentShaderPath,
        const RHI::Format colorFormat)
    {
        RHI::PipelineDesc desc{};
        desc.VertexShaderPath = std::move(vertexShaderPath);
        desc.FragmentShaderPath = std::move(fragmentShaderPath);
        desc.Rasterizer.Culling = RHI::CullMode::None;
        desc.DepthStencil.DepthTestEnable = false;
        desc.DepthStencil.DepthWriteEnable = false;
        desc.ColorTargetCount = 1u;
        desc.ColorTargetFormats[0] = colorFormat;
        desc.PushConstantSize =
            static_cast<std::uint32_t>(
                sizeof(ObjectSpaceNormalTextureBakeDilationPushConstants));
        desc.DebugName = "ObjectSpaceNormalTextureBake.Dilation";
        return desc;
    }

    RHI::TextureDesc MakeObjectSpaceNormalTextureBakeDilationScratchTextureDesc(
        const ObjectSpaceNormalTextureBakeOptions& options,
        const char* debugName) noexcept
    {
        const ObjectSpaceNormalTextureBakeResolvedOptions resolved =
            ResolveObjectSpaceNormalTextureBakeOptions(options);
        return RHI::TextureDesc{
            .Width = resolved.Width,
            .Height = resolved.Height,
            .MipLevels = 1u,
            .Fmt = RHI::Format::RGBA8_UNORM,
            .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::ColorTarget,
            .InitialLayout = RHI::TextureLayout::Undefined,
            .DebugName = debugName != nullptr
                ? debugName
                : "ObjectSpaceNormalTextureBake.DilationScratch",
        };
    }

    ObjectSpaceNormalTextureBakeDilationResourceDesc
    MakeObjectSpaceNormalTextureBakeDilationResourceDesc(
        const ObjectSpaceNormalTextureBakeOptions& options,
        std::string vertexShaderPath,
        std::string fragmentShaderPath,
        const char* scratchDebugName)
    {
        return ObjectSpaceNormalTextureBakeDilationResourceDesc{
            .Pipeline = MakeObjectSpaceNormalTextureBakeDilationPipelineDesc(
                std::move(vertexShaderPath),
                std::move(fragmentShaderPath)),
            .ScratchTexture =
                MakeObjectSpaceNormalTextureBakeDilationScratchTextureDesc(
                    options,
                    scratchDebugName),
        };
    }

    ObjectSpaceNormalTextureBakeDilationResourceLease::
        ~ObjectSpaceNormalTextureBakeDilationResourceLease()
    {
        Shutdown();
    }

    ObjectSpaceNormalTextureBakeDilationResourceLease::
        ObjectSpaceNormalTextureBakeDilationResourceLease(
            ObjectSpaceNormalTextureBakeDilationResourceLease&& other) noexcept
        : m_Device(std::exchange(other.m_Device, nullptr))
        , m_Pipeline(std::exchange(other.m_Pipeline, {}))
        , m_ScratchTexture(std::exchange(other.m_ScratchTexture, {}))
        , m_ScratchInitialLayout(other.m_ScratchInitialLayout)
        , m_OutputDescriptorSlot(other.m_OutputDescriptorSlot)
        , m_ScratchDescriptorSlot(other.m_ScratchDescriptorSlot)
    {
    }

    ObjectSpaceNormalTextureBakeDilationResourceLease&
    ObjectSpaceNormalTextureBakeDilationResourceLease::operator=(
        ObjectSpaceNormalTextureBakeDilationResourceLease&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        Shutdown();
        m_Device = std::exchange(other.m_Device, nullptr);
        m_Pipeline = std::exchange(other.m_Pipeline, {});
        m_ScratchTexture = std::exchange(other.m_ScratchTexture, {});
        m_ScratchInitialLayout = other.m_ScratchInitialLayout;
        m_OutputDescriptorSlot = other.m_OutputDescriptorSlot;
        m_ScratchDescriptorSlot = other.m_ScratchDescriptorSlot;
        return *this;
    }

    Core::Result ObjectSpaceNormalTextureBakeDilationResourceLease::Initialize(
        RHI::IDevice& device,
        const ObjectSpaceNormalTextureBakeDilationResourceDesc& desc)
    {
        Shutdown();

        if (!device.IsOperational())
        {
            return Core::Err(Core::ErrorCode::DeviceNotOperational);
        }
        if (desc.OutputDescriptorSlot == desc.ScratchDescriptorSlot ||
            desc.ScratchTexture.Width == 0u ||
            desc.ScratchTexture.Height == 0u)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        RHI::PipelineHandle pipeline = device.CreatePipeline(desc.Pipeline);
        if (!pipeline.IsValid())
        {
            return Core::Err(Core::ErrorCode::PipelineCreationFailed);
        }

        RHI::TextureHandle scratch = device.CreateTexture(desc.ScratchTexture);
        if (!scratch.IsValid())
        {
            device.DestroyPipeline(pipeline);
            return Core::Err(Core::ErrorCode::OutOfDeviceMemory);
        }

        m_Device = &device;
        m_Pipeline = pipeline;
        m_ScratchTexture = scratch;
        m_ScratchInitialLayout = desc.ScratchTexture.InitialLayout;
        m_OutputDescriptorSlot = desc.OutputDescriptorSlot;
        m_ScratchDescriptorSlot = desc.ScratchDescriptorSlot;
        return Core::Ok();
    }

    void ObjectSpaceNormalTextureBakeDilationResourceLease::Shutdown() noexcept
    {
        if (m_Device != nullptr)
        {
            if (m_ScratchTexture.IsValid())
            {
                m_Device->DestroyTexture(m_ScratchTexture);
            }
            if (m_Pipeline.IsValid())
            {
                m_Device->DestroyPipeline(m_Pipeline);
            }
        }
        m_Device = nullptr;
        m_Pipeline = {};
        m_ScratchTexture = {};
        m_ScratchInitialLayout = RHI::TextureLayout::Undefined;
        m_OutputDescriptorSlot = kObjectSpaceNormalBakeDilationOutputDescriptorSlot;
        m_ScratchDescriptorSlot =
            kObjectSpaceNormalBakeDilationScratchDescriptorSlot;
    }

    bool ObjectSpaceNormalTextureBakeDilationResourceLease::IsValid() const noexcept
    {
        return GetResources().IsValid();
    }

    ObjectSpaceNormalTextureBakeDilationResources
    ObjectSpaceNormalTextureBakeDilationResourceLease::GetResources() const noexcept
    {
        return ObjectSpaceNormalTextureBakeDilationResources{
            .Pipeline = m_Pipeline,
            .ScratchTexture = m_ScratchTexture,
            .ScratchInitialLayout = m_ScratchInitialLayout,
            .OutputDescriptorSlot = m_OutputDescriptorSlot,
            .ScratchDescriptorSlot = m_ScratchDescriptorSlot,
        };
    }

    ObjectSpaceNormalTextureBakePlan BuildObjectSpaceNormalTextureBakePlan(
        const ObjectSpaceNormalTextureBakePlanRequest& request) noexcept
    {
        ObjectSpaceNormalTextureBakePlan plan{};
        plan.Diagnostics.Options =
            ResolveObjectSpaceNormalTextureBakeOptions(request.Options);
        plan.Diagnostics.VertexCount = request.Geometry.VertexCount;
        plan.Diagnostics.TriangleCount = request.Geometry.IndexCount / 3u;
        plan.DilationRequested = plan.Diagnostics.Options.PaddingTexels > 0u;
        plan.DilationAvailable =
            AreDilationResourcesUsable(request.Dilation);

        const auto fail = [&plan](const ObjectSpaceNormalTextureBakeStatus status) noexcept
        {
            plan.Status = status;
            return plan;
        };

        if (plan.Diagnostics.Options.Space != NormalTextureSpace::ObjectSpaceNormal)
        {
            return fail(
                ObjectSpaceNormalTextureBakeStatus::UnsupportedNormalTextureSpace);
        }

        if (!request.GeneratedTextureAsset.IsValid())
        {
            return fail(
                ObjectSpaceNormalTextureBakeStatus::InvalidGeneratedTextureAsset);
        }

        if (request.Geometry.VertexCount == 0u || request.Geometry.IndexCount == 0u)
        {
            return fail(ObjectSpaceNormalTextureBakeStatus::EmptyInput);
        }

        if ((request.Geometry.IndexCount % 3u) != 0u)
        {
            plan.Diagnostics.FirstFailureIndex = request.Geometry.IndexCount;
            return fail(ObjectSpaceNormalTextureBakeStatus::InvalidIndexCount);
        }

        if (!request.Pipeline.IsValid() ||
            !request.Geometry.IndexBuffer.IsValid() ||
            request.Geometry.TexcoordBDA == 0u ||
            request.Geometry.NormalBDA == 0u)
        {
            return fail(ObjectSpaceNormalTextureBakeStatus::InvalidGpuResource);
        }

        if (plan.DilationRequested && !plan.DilationAvailable)
        {
            return fail(ObjectSpaceNormalTextureBakeStatus::DilationUnavailable);
        }

        const RHI::TextureUsage usage =
            (RHI::TextureUsage::Sampled | RHI::TextureUsage::ColorTarget) |
            request.AdditionalTextureUsage;
        const char* debugName = request.DebugName != nullptr
            ? request.DebugName
            : "ObjectSpaceNormalTextureBake.Output";

        plan.CompletionKey = ObjectSpaceNormalTextureBakeCompletionKey{
            .GeneratedTextureAsset = request.GeneratedTextureAsset,
            .Source = request.SourceKey,
            .Width = plan.Diagnostics.Options.Width,
            .Height = plan.Diagnostics.Options.Height,
            .PaddingTexels = plan.Diagnostics.Options.PaddingTexels,
            .Space = plan.Diagnostics.Options.Space,
        };

        plan.TextureRequest = GpuProducedTextureRequest{
            .Id = request.GeneratedTextureAsset,
            .Desc = RHI::TextureDesc{
                .Width = plan.Diagnostics.Options.Width,
                .Height = plan.Diagnostics.Options.Height,
                .MipLevels = 1u,
                .Fmt = RHI::Format::RGBA8_UNORM,
                .Usage = usage,
                .InitialLayout = request.InitialLayout,
                .DebugName = debugName,
            },
            .SamplerDesc = request.SamplerDesc,
            .Sampler = request.Sampler,
            .ReadyFrame = request.ReadyFrame,
            .HasReadyFrame = request.HasReadyFrame,
        };

        plan.RecordTemplate = ObjectSpaceNormalTextureBakeGpuRecordTemplate{
            .Pipeline = request.Pipeline,
            .Dilation = request.Dilation,
            .IndexBuffer = request.Geometry.IndexBuffer,
            .TexcoordBDA = request.Geometry.TexcoordBDA,
            .NormalBDA = request.Geometry.NormalBDA,
            .IndexCount = request.Geometry.IndexCount,
            .Width = plan.Diagnostics.Options.Width,
            .Height = plan.Diagnostics.Options.Height,
            .PaddingTexels = plan.Diagnostics.Options.PaddingTexels,
            .InitialLayout = request.InitialLayout,
            .FinalLayout = request.FinalLayout,
        };

        plan.Status = ObjectSpaceNormalTextureBakeStatus::Success;
        return plan;
    }

    bool ObjectSpaceNormalTextureBakeCompletionKeyMatches(
        const ObjectSpaceNormalTextureBakeCompletionKey& expected,
        const ObjectSpaceNormalTextureBakeCompletionKey& actual) noexcept
    {
        return expected.GeneratedTextureAsset == actual.GeneratedTextureAsset &&
               expected.Source.EntityKey == actual.Source.EntityKey &&
               expected.Source.GeometryGeneration ==
                   actual.Source.GeometryGeneration &&
               expected.Source.TexcoordGeneration ==
                   actual.Source.TexcoordGeneration &&
               expected.Source.NormalGeneration == actual.Source.NormalGeneration &&
               expected.Width == actual.Width &&
               expected.Height == actual.Height &&
               expected.PaddingTexels == actual.PaddingTexels &&
               expected.Space == actual.Space;
    }

    ObjectSpaceNormalTextureBakeGpuRecordDesc
    MakeObjectSpaceNormalTextureBakeGpuRecordDesc(
        const ObjectSpaceNormalTextureBakePlan& plan,
        const RHI::TextureHandle outputTexture) noexcept
    {
        return ObjectSpaceNormalTextureBakeGpuRecordDesc{
            .Pipeline = plan.RecordTemplate.Pipeline,
            .OutputTexture = outputTexture,
            .Dilation = plan.RecordTemplate.Dilation,
            .IndexBuffer = plan.RecordTemplate.IndexBuffer,
            .TexcoordBDA = plan.RecordTemplate.TexcoordBDA,
            .NormalBDA = plan.RecordTemplate.NormalBDA,
            .IndexCount = plan.RecordTemplate.IndexCount,
            .Width = plan.RecordTemplate.Width,
            .Height = plan.RecordTemplate.Height,
            .PaddingTexels = plan.RecordTemplate.PaddingTexels,
            .InitialLayout = plan.RecordTemplate.InitialLayout,
            .FinalLayout = plan.RecordTemplate.FinalLayout,
        };
    }

    Core::Result RecordObjectSpaceNormalTextureBake(
        RHI::ICommandContext& cmd,
        const ObjectSpaceNormalTextureBakeGpuRecordDesc& desc)
    {
        if (!desc.Pipeline.IsValid() ||
            !desc.OutputTexture.IsValid() ||
            !desc.IndexBuffer.IsValid() ||
            desc.TexcoordBDA == 0u ||
            desc.NormalBDA == 0u ||
            desc.IndexCount == 0u ||
            desc.Width == 0u ||
            desc.Height == 0u ||
            desc.PaddingTexels > kObjectSpaceNormalBakeMaxPaddingTexels)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        if (desc.PaddingTexels == 0u)
        {
            TextureRecordingState output{
                .Handle = desc.OutputTexture,
                .Layout = desc.InitialLayout,
            };
            TransitionTexture(cmd, output, RHI::TextureLayout::ColorAttachment);
            RecordRasterBakePass(cmd, desc, desc.OutputTexture);
            TransitionTexture(cmd, output, desc.FinalLayout);
            return Core::Ok();
        }

        if (!AreDilationResourcesUsable(desc.Dilation) ||
            desc.Dilation.ScratchTexture == desc.OutputTexture)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        TextureRecordingState output{
            .Handle = desc.OutputTexture,
            .Layout = desc.InitialLayout,
        };
        TextureRecordingState scratch{
            .Handle = desc.Dilation.ScratchTexture,
            .Layout = desc.Dilation.ScratchInitialLayout,
        };

        cmd.BindFrameSampledTextureAt(desc.OutputTexture,
                                      desc.Dilation.OutputDescriptorSlot);
        cmd.BindFrameSampledTextureAt(desc.Dilation.ScratchTexture,
                                      desc.Dilation.ScratchDescriptorSlot);

        const bool rasterToOutput = (desc.PaddingTexels % 2u) == 0u;
        TextureRecordingState& rasterTarget = rasterToOutput ? output : scratch;
        TransitionTexture(cmd, rasterTarget, RHI::TextureLayout::ColorAttachment);
        RecordRasterBakePass(cmd, desc, rasterTarget.Handle);
        TransitionTexture(cmd, rasterTarget, RHI::TextureLayout::ShaderReadOnly);

        bool sourceIsOutput = rasterToOutput;
        for (std::uint32_t pass = 0u; pass < desc.PaddingTexels; ++pass)
        {
            const bool targetIsOutput = !sourceIsOutput;
            TextureRecordingState& target = targetIsOutput ? output : scratch;
            const std::uint32_t sourceSlot = sourceIsOutput
                ? desc.Dilation.OutputDescriptorSlot
                : desc.Dilation.ScratchDescriptorSlot;
            const bool lastPass = (pass + 1u) == desc.PaddingTexels;

            TransitionTexture(cmd, target, RHI::TextureLayout::ColorAttachment);
            RecordDilationPass(cmd,
                               desc,
                               SelectDilationTarget(desc.OutputTexture,
                                                    desc.Dilation.ScratchTexture,
                                                    targetIsOutput),
                               sourceSlot);
            TransitionTexture(cmd,
                              target,
                              lastPass && targetIsOutput
                                  ? desc.FinalLayout
                                  : RHI::TextureLayout::ShaderReadOnly);

            sourceIsOutput = targetIsOutput;
        }
        return Core::Ok();
    }
}
