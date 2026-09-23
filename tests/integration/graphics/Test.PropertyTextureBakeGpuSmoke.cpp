#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.PropertyTextureBake;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Geometry.MeshSoup;
import Geometry.UvAtlas;

// Records the property-texture bake pass on the promoted Vulkan device and
// compares both render targets texel by texel with the CPU reference raster.
namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    class ExitAfterFramesApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}

        void Frame(double, double) override
        {
            if (++m_Frames >= 4u)
                Kernel().RequestExit();
        }

        void Shutdown() override {}

    private:
        std::uint32_t m_Frames{0u};
    };

    class EngineShutdownGuard
    {
    public:
        explicit EngineShutdownGuard(Extrinsic::Runtime::Engine& engine) noexcept
            : m_Engine(engine)
        {
        }
        ~EngineShutdownGuard() { m_Engine.Shutdown(); }
        EngineShutdownGuard(const EngineShutdownGuard&) = delete;
        EngineShutdownGuard& operator=(const EngineShutdownGuard&) = delete;

    private:
        Extrinsic::Runtime::Engine& m_Engine;
    };

    // Atlas authored in texel coordinates. Corners sit on quarter texels and
    // quad diagonals satisfy x + y = k + 0.5, so no texel centre lies on a
    // triangle edge and GPU fill-rule ties cannot differ from the reference.
    struct TexelAtlas
    {
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::vector<glm::vec2> Texcoords{};
        std::vector<std::uint32_t> Indices{};

        [[nodiscard]] glm::vec2 Uv(const float x, const float y) const
        {
            return glm::vec2{x / static_cast<float>(Width), y / static_cast<float>(Height)};
        }

        // Quad split along its (x0, y1)-(x1, y0) diagonal; `seam` duplicates
        // the diagonal slots so the halves become separate UV charts.
        void AddQuad(const float x0, const float y0, const float x1, const float y1,
                     const bool seam = false)
        {
            const auto base = static_cast<std::uint32_t>(Texcoords.size());
            Texcoords.insert(Texcoords.end(),
                             {Uv(x0, y0), Uv(x1, y0), Uv(x1, y1), Uv(x0, y1)});
            if (!seam)
            {
                Indices.insert(Indices.end(), {base, base + 1u, base + 3u,
                                               base + 1u, base + 2u, base + 3u});
                return;
            }
            Texcoords.insert(Texcoords.end(), {Uv(x1, y0), Uv(x0, y1)});
            Indices.insert(Indices.end(), {base, base + 1u, base + 3u,
                                           base + 4u, base + 2u, base + 5u});
        }
    };

    struct BakeCase
    {
        TexelAtlas Atlas{};
        std::vector<glm::vec4> Values{};
        std::uint32_t Padding{0u};
        RHI::Format Format{RHI::Format::R32_FLOAT};
        Graphics::PropertyTextureBakeDomain Domain{Graphics::PropertyTextureBakeDomain::Vertex};
        Graphics::PropertyTextureBakeValueKind ValueKind{Graphics::PropertyTextureBakeValueKind::Scalar};
        Graphics::PropertyTextureBakeEncoding Encoding{Graphics::PropertyTextureBakeEncoding::Raw};
    };

    struct GpuImage
    {
        std::vector<std::byte> Values{};
        std::vector<float> Coverage{};
    };

    template <class T>
    [[nodiscard]] RHI::BufferHandle UploadBuffer(RHI::IDevice& device,
                                                 const std::span<const T> data,
                                                 const RHI::BufferUsage usage,
                                                 const char* name)
    {
        const RHI::BufferHandle buffer = device.CreateBuffer(RHI::BufferDesc{
            .SizeBytes = std::max<std::uint64_t>(data.size_bytes(), 16u),
            .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst | usage,
            .HostVisible = true,
            .DebugName = name,
        });
        if (buffer.IsValid() && !data.empty())
            device.WriteBuffer(buffer, data.data(), data.size_bytes(), 0u);
        return buffer;
    }

    [[nodiscard]] std::optional<GpuImage> RunGpuBake(RHI::IDevice& device, const BakeCase& bake)
    {
        const TexelAtlas& atlas = bake.Atlas;
        const auto charts = Graphics::BuildPropertyTextureBakeCharts(atlas.Indices);
        const auto edges = Graphics::BuildPropertyTextureBakeGutterEdges(atlas.Indices);
        const std::uint64_t texelCount =
            static_cast<std::uint64_t>(atlas.Width) * atlas.Height;

        std::vector<RHI::BufferHandle> buffers{};
        const auto upload = [&](const auto& values, const RHI::BufferUsage usage, const char* name)
        {
            buffers.push_back(UploadBuffer(device, std::span{values}, usage, name));
            return buffers.back();
        };
        const RHI::BufferHandle texcoords =
            upload(atlas.Texcoords, RHI::BufferUsage::None, "PropertyTextureBakeGpuSmoke.Texcoords");
        const RHI::BufferHandle values =
            upload(bake.Values, RHI::BufferUsage::None, "PropertyTextureBakeGpuSmoke.Values");
        const RHI::BufferHandle indices =
            upload(atlas.Indices, RHI::BufferUsage::Index, "PropertyTextureBakeGpuSmoke.Indices");
        const RHI::BufferHandle chartBuffer =
            upload(charts.TriangleChart, RHI::BufferUsage::None, "PropertyTextureBakeGpuSmoke.Charts");
        const RHI::BufferHandle edgeBuffer =
            upload(edges, RHI::BufferUsage::None, "PropertyTextureBakeGpuSmoke.GutterEdges");
        const RHI::BufferHandle valueReadback = device.CreateBuffer(RHI::BufferDesc{
            .SizeBytes = texelCount * 16u,
            .Usage = RHI::BufferUsage::TransferDst,
            .HostVisible = true,
            .DebugName = "PropertyTextureBakeGpuSmoke.ValueReadback",
        });
        const RHI::BufferHandle coverageReadback = device.CreateBuffer(RHI::BufferDesc{
            .SizeBytes = texelCount * sizeof(float),
            .Usage = RHI::BufferUsage::TransferDst,
            .HostVisible = true,
            .DebugName = "PropertyTextureBakeGpuSmoke.CoverageReadback",
        });
        buffers.push_back(valueReadback);
        buffers.push_back(coverageReadback);

        const RHI::TextureHandle output = device.CreateTexture(RHI::TextureDesc{
            .Width = atlas.Width,
            .Height = atlas.Height,
            .MipLevels = 1u,
            .Fmt = bake.Format,
            .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::ColorTarget |
                     RHI::TextureUsage::TransferSrc,
            .InitialLayout = RHI::TextureLayout::Undefined,
            .DebugName = "PropertyTextureBakeGpuSmoke.Output",
        });
        const RHI::TextureHandle coverage = device.CreateTexture(
            Graphics::MakePropertyTextureBakeCoverageTextureDesc(atlas.Width, atlas.Height));
        const RHI::TextureHandle depth = device.CreateTexture(
            Graphics::MakePropertyTextureBakeDepthTextureDesc(atlas.Width, atlas.Height));
        const RHI::PipelineHandle pipeline = device.CreatePipeline(
            Graphics::MakePropertyTextureBakePipelineDesc(
                Extrinsic::Core::Filesystem::GetShaderPath("shaders/property_texture_bake.vert.spv"),
                Extrinsic::Core::Filesystem::GetShaderPath("shaders/property_texture_bake.frag.spv"),
                bake.Format));

        const auto release = [&]
        {
            device.WaitIdle();
            if (pipeline.IsValid())
                device.DestroyPipeline(pipeline);
            for (const RHI::TextureHandle texture : {output, coverage, depth})
            {
                if (texture.IsValid())
                    device.DestroyTexture(texture);
            }
            for (const RHI::BufferHandle buffer : buffers)
            {
                if (buffer.IsValid())
                    device.DestroyBuffer(buffer);
            }
        };
        for (const RHI::BufferHandle buffer : buffers)
        {
            if (!buffer.IsValid())
            {
                ADD_FAILURE() << "bake smoke buffer allocation failed";
                release();
                return std::nullopt;
            }
        }
        if (!output.IsValid() || !coverage.IsValid() || !depth.IsValid() || !pipeline.IsValid())
        {
            ADD_FAILURE() << "bake smoke texture or pipeline creation failed";
            release();
            return std::nullopt;
        }

        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
        {
            ADD_FAILURE() << "BeginFrame failed";
            release();
            return std::nullopt;
        }
        RHI::ICommandContext& cmd = device.GetGraphicsContext(frame.FrameIndex);
        cmd.Begin();
        for (const RHI::BufferHandle buffer : {texcoords, values, chartBuffer, edgeBuffer})
            cmd.BufferBarrier(buffer, RHI::MemoryAccess::HostWrite, RHI::MemoryAccess::ShaderRead);
        cmd.BufferBarrier(indices, RHI::MemoryAccess::HostWrite, RHI::MemoryAccess::IndexRead);
        const Extrinsic::Core::Result recorded = Graphics::RecordPropertyTextureBake(
            cmd,
            Graphics::PropertyTextureBakeRecordDesc{
                .Pipeline = pipeline,
                .OutputTexture = output,
                .CoverageTexture = coverage,
                .DepthTexture = depth,
                .IndexBuffer = indices,
                .TexcoordBDA = device.GetBufferDeviceAddress(texcoords),
                .PropertyBDA = device.GetBufferDeviceAddress(values),
                .IndexBDA = device.GetBufferDeviceAddress(indices),
                .ChartBDA = device.GetBufferDeviceAddress(chartBuffer),
                .GutterEdgeBDA = device.GetBufferDeviceAddress(edgeBuffer),
                .IndexCount = static_cast<std::uint32_t>(atlas.Indices.size()),
                .GutterEdgeCount = static_cast<std::uint32_t>(edges.size()),
                .Width = atlas.Width,
                .Height = atlas.Height,
                .PaddingTexels = bake.Padding,
                .Domain = bake.Domain,
                .ValueKind = bake.ValueKind,
                .Encoding = bake.Encoding,
                .FinalLayout = RHI::TextureLayout::TransferSrc,
                .CoverageFinalLayout = RHI::TextureLayout::TransferSrc,
            });
        if (recorded.has_value())
        {
            cmd.CopyTextureToBuffer(output, RHI::TextureLayout::TransferSrc, 0u, 0u,
                                    valueReadback, 0u, 0u, 0u, atlas.Width, atlas.Height);
            cmd.CopyTextureToBuffer(coverage, RHI::TextureLayout::TransferSrc, 0u, 0u,
                                    coverageReadback, 0u, 0u, 0u, atlas.Width, atlas.Height);
            cmd.BufferBarrier(valueReadback, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::HostRead);
            cmd.BufferBarrier(coverageReadback, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::HostRead);
        }
        cmd.End();
        device.EndFrame(frame);
        device.Present(frame);
        device.WaitIdle();
        if (!recorded.has_value())
        {
            ADD_FAILURE() << "RecordPropertyTextureBake rejected the smoke inputs";
            release();
            return std::nullopt;
        }

        // Both smoke formats (R32F, RGBA8) are four bytes per texel.
        GpuImage image{};
        image.Values.resize(static_cast<std::size_t>(texelCount) * 4u);
        image.Coverage.resize(static_cast<std::size_t>(texelCount));
        device.ReadBuffer(valueReadback, image.Values.data(), image.Values.size(), 0u);
        device.ReadBuffer(coverageReadback, image.Coverage.data(),
                          image.Coverage.size() * sizeof(float), 0u);
        release();
        return image;
    }

    [[nodiscard]] Graphics::PropertyTextureBakeReferenceImage RunReference(const BakeCase& bake)
    {
        const auto charts = Graphics::BuildPropertyTextureBakeCharts(bake.Atlas.Indices);
        const auto edges = Graphics::BuildPropertyTextureBakeGutterEdges(bake.Atlas.Indices);
        auto image = Graphics::RasterizePropertyTextureBakeReference(
            Graphics::PropertyTextureBakeReferenceInput{
                .Texcoords = bake.Atlas.Texcoords,
                .Values = bake.Values,
                .Indices = bake.Atlas.Indices,
                .Charts = &charts,
                .GutterEdges = edges,
                .Width = bake.Atlas.Width,
                .Height = bake.Atlas.Height,
                .PaddingTexels = bake.Padding,
                .Domain = bake.Domain,
                .ValueKind = bake.ValueKind,
                .Encoding = bake.Encoding,
            });
        EXPECT_TRUE(image.has_value());
        return image.has_value() ? std::move(*image) : Graphics::PropertyTextureBakeReferenceImage{};
    }
}

TEST(PropertyTextureBakeGpuSmoke, RawAndLabelBakesMatchTheCpuReferenceTexelForTexel)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
        GTEST_SKIP() << "GLFW could not initialize; the gpu;vulkan bake smoke is opt-in.";

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic property texture bake gpu;vulkan smoke";
    config.Window.Width = 96u;
    config.Window.Height = 96u;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    auto engine = std::make_unique<Intrinsic::Tests::RuntimeTestKernel>(
        config, std::make_unique<ExitAfterFramesApp>());
    engine->Initialize();
    const auto inputs =
        Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs(&engine->GetDevice());
    if (!inputs.LogicalDeviceReady || !inputs.SwapchainReady || !inputs.CommandSyncReady)
    {
        engine->Shutdown();
        GTEST_SKIP() << "Promoted Vulkan did not reach logical-device/swapchain readiness.";
    }
    EngineShutdownGuard guard{*engine};
    engine->Run();
    RHI::IDevice& device = engine->GetDevice();
    ASSERT_TRUE(device.IsOperational()) << "Promoted Vulkan did not become operational";

    // Exercise the native packer's texel-centre anchor on an actual thin
    // chart. CPU coverage alone cannot establish device raster coverage.
    {
        namespace Atlas = Geometry::UvAtlas;
        const std::vector<glm::vec3> positions{{0, 0, 0}, {10, 0, 0}, {0, 10, 0},
                                              {20, 0, 0}, {40, 0, 0}, {20, .02f, 0}};
        const std::vector<Geometry::MeshSoup::PolygonFace> faces{{{0, 1, 2}}, {{3, 4, 5}}};
        Atlas::UvAtlasOptions options;
        options.PreserveValidAuthoredUvs = false;
        options.ForceRegenerate = true;
        options.AllowXAtlasFallback = false;
        options.Resolution = 128;
        options.Padding = 2;
        const auto atlas = Atlas::ResolveUvAtlas({.Positions = positions, .Faces = faces}, options);
        ASSERT_EQ(atlas.Status, Atlas::UvAtlasStatus::Success);
        ASSERT_EQ(atlas.Diagnostics.ChartCount, 2u);
        BakeCase thin{.Atlas = TexelAtlas{.Width = 128, .Height = 128}, .Padding = 2};
        thin.Atlas.Texcoords = atlas.SourceCornerUvs;
        thin.Atlas.Indices = {0, 1, 2, 3, 4, 5};
        thin.Values = {glm::vec4(-7), glm::vec4(-7), glm::vec4(-7),
                       glm::vec4(3), glm::vec4(3), glm::vec4(3)};
        const auto gpu = RunGpuBake(device, thin);
        ASSERT_TRUE(gpu.has_value());
        std::size_t firstInterior = 0, thinInterior = 0;
        for (std::size_t texel = 0; texel < gpu->Coverage.size(); ++texel)
        {
            const auto coverage = gpu->Coverage[texel];
            firstInterior += coverage == 1.0f;
            thinInterior += coverage == 2.0f;
            if (coverage == 0.0f)
                continue;
            float value = 0;
            std::memcpy(&value, gpu->Values.data() + texel * sizeof(float), sizeof(float));
            EXPECT_NEAR(value, std::abs(coverage) == 1.0f ? -7.0f : 3.0f, 1.0e-4f);
        }
        EXPECT_GT(firstInterior, 0u);
        EXPECT_GT(thinInterior, 0u) << "the thin packed chart must survive device rasterization";
    }

    // Raw R32F: an affine field with negative values, an all-zero chart and a
    // two-chart UV seam with opposite constants, all with two-texel gutters.
    {
        BakeCase raw{.Atlas = TexelAtlas{.Width = 32u, .Height = 32u}, .Padding = 2u};
        raw.Atlas.AddQuad(2.25f, 3.25f, 12.25f, 13.25f);
        for (int corner = 0; corner < 4; ++corner)
        {
            const glm::vec2 uv = raw.Atlas.Texcoords[static_cast<std::size_t>(corner)];
            raw.Values.push_back(glm::vec4{0.5f * uv.x * 32.0f - 3.0f, 0.0f, 0.0f, 0.0f});
        }
        raw.Atlas.AddQuad(17.25f, 3.25f, 29.25f, 15.25f);
        raw.Values.insert(raw.Values.end(), 4u, glm::vec4{0.0f});
        // Seam slots are corners 0,1,2,3 plus duplicates of 1 and 3: triangle
        // (0,1,3) carries +7 and triangle (1',2,3') carries -7.
        raw.Atlas.AddQuad(4.25f, 19.25f, 14.25f, 29.25f, true);
        raw.Values.insert(raw.Values.end(), {glm::vec4{7.0f}, glm::vec4{7.0f}, glm::vec4{-7.0f},
                                             glm::vec4{7.0f}, glm::vec4{-7.0f}, glm::vec4{-7.0f}});

        const auto gpu = RunGpuBake(device, raw);
        ASSERT_TRUE(gpu.has_value());
        const auto cpu = RunReference(raw);
        ASSERT_EQ(cpu.Values.size(), gpu->Coverage.size());

        std::size_t interior = 0u;
        std::size_t gutter = 0u;
        std::size_t zeroInterior = 0u;
        std::size_t negative = 0u;
        for (std::size_t texel = 0u; texel < cpu.Values.size(); ++texel)
        {
            float value = 0.0f;
            std::memcpy(&value, gpu->Values.data() + texel * 4u, sizeof(float));
            ASSERT_EQ(gpu->Coverage[texel], cpu.Coverage[texel])
                << "coverage/chart mismatch at texel " << texel % 32u << "," << texel / 32u;
            EXPECT_NEAR(value, cpu.Values[texel].x, 1.0e-4f)
                << "value mismatch at texel " << texel % 32u << "," << texel / 32u;
            interior += cpu.Coverage[texel] > 0.0f ? 1u : 0u;
            gutter += cpu.Coverage[texel] < 0.0f ? 1u : 0u;
            zeroInterior += cpu.Coverage[texel] == 2.0f && value == 0.0f ? 1u : 0u;
            negative += cpu.Coverage[texel] != 0.0f && value < 0.0f ? 1u : 0u;
        }
        EXPECT_GT(interior, 300u);
        EXPECT_GT(gutter, 150u);
        EXPECT_GT(zeroInterior, 100u) << "zero-valued raw data must stay covered, not masked";
        EXPECT_GT(negative, 50u);
    }

    // Encoded RGBA8 face labels stay discrete in the interior and gutter.
    {
        BakeCase labels{
            .Atlas = TexelAtlas{.Width = 16u, .Height = 16u},
            .Padding = 1u,
            .Format = RHI::Format::RGBA8_UNORM,
            .Domain = Graphics::PropertyTextureBakeDomain::Face,
            .ValueKind = Graphics::PropertyTextureBakeValueKind::Label,
            .Encoding = Graphics::PropertyTextureBakeEncoding::LabelPalette,
        };
        labels.Atlas.AddQuad(2.25f, 2.25f, 13.25f, 13.25f);
        labels.Values = {glm::vec4{std::bit_cast<float>(3u), 0.0f, 0.0f, 1.0f},
                         glm::vec4{std::bit_cast<float>(9u), 0.0f, 0.0f, 1.0f}};

        const auto gpu = RunGpuBake(device, labels);
        ASSERT_TRUE(gpu.has_value());
        const auto cpu = RunReference(labels);
        ASSERT_EQ(cpu.Values.size(), gpu->Coverage.size());
        std::size_t covered = 0u;
        for (std::size_t texel = 0u; texel < cpu.Values.size(); ++texel)
        {
            ASSERT_EQ(gpu->Coverage[texel], cpu.Coverage[texel]) << "texel " << texel;
            for (std::size_t channel = 0u; channel < 4u; ++channel)
            {
                const int expected = static_cast<int>(
                    std::lround(cpu.Values[texel][static_cast<glm::length_t>(channel)] * 255.0f));
                const int actual = static_cast<int>(
                    std::to_integer<unsigned>(gpu->Values[texel * 4u + channel]));
                EXPECT_LE(std::abs(actual - expected), 1)
                    << "texel " << texel << " channel " << channel;
            }
            covered += cpu.Coverage[texel] != 0.0f ? 1u : 0u;
        }
        EXPECT_GT(covered, 121u) << "interior plus a one-texel gutter";
    }
}
