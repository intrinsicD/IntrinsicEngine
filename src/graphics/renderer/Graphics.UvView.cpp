module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Graphics.UvView;

import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint32_t kMaximumTargetExtent = 4096u;
        constexpr double kUvPadding = 1.05;

        [[nodiscard]] bool IsAvailableBackgroundTexture(
            const RHI::BindlessIndex index,
            const std::uint32_t capacity) noexcept
        {
            // Slot zero is the RHI invalid/default texture. Every other index
            // must remain inside the descriptor array before the shader uses
            // it for non-uniform dynamic indexing.
            return index != RHI::kInvalidBindlessIndex && index < capacity;
        }

        [[nodiscard]] bool FiniteBounds(const UvViewBounds& bounds) noexcept
        {
            return std::isfinite(bounds.MinU) && std::isfinite(bounds.MinV) &&
                   std::isfinite(bounds.MaxU) && std::isfinite(bounds.MaxV) &&
                   bounds.MaxU > bounds.MinU && bounds.MaxV > bounds.MinV;
        }

        [[nodiscard]] bool ToRepresentableFloat(const double value,
                                                float& result) noexcept
        {
            constexpr double maximum =
                static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(value) || value < -maximum || value > maximum)
                return false;
            result = static_cast<float>(value);
            return std::isfinite(result);
        }

        [[nodiscard]] bool SameFloatPayload(const std::vector<float>& lhs,
                                            const std::vector<float>& rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;
            for (std::size_t index = 0u; index < lhs.size(); ++index)
            {
                if (std::bit_cast<std::uint32_t>(lhs[index]) !=
                    std::bit_cast<std::uint32_t>(rhs[index]))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] RHI::PipelineDesc BuildBackgroundPipelineDesc()
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/uv_view/background.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/uv_view/background.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA8_UNORM;
            desc.PushConstantSize = sizeof(UvViewPushConstants);
            desc.DebugName = "Renderer.UvView.Background";
            return desc;
        }

        [[nodiscard]] RHI::PipelineDesc BuildFillPipelineDesc()
        {
            RHI::PipelineDesc desc{};
            desc.VertexShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/uv_view/mesh.vert.spv");
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/uv_view/fill.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::TriangleList;
            desc.Rasterizer.Culling = RHI::CullMode::None;
            desc.DepthStencil.DepthTestEnable = false;
            desc.DepthStencil.DepthWriteEnable = false;
            desc.ColorBlend[0].Enable = true;
            desc.ColorBlend[0].SrcColorFactor = RHI::BlendFactor::SrcAlpha;
            desc.ColorBlend[0].DstColorFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorBlend[0].SrcAlphaFactor = RHI::BlendFactor::One;
            desc.ColorBlend[0].DstAlphaFactor = RHI::BlendFactor::OneMinusSrcAlpha;
            desc.ColorTargetCount = 1u;
            desc.ColorTargetFormats[0] = RHI::Format::RGBA8_UNORM;
            desc.PushConstantSize = sizeof(UvViewPushConstants);
            desc.DebugName = "Renderer.UvView.Fill";
            return desc;
        }

        [[nodiscard]] RHI::PipelineDesc BuildLinePipelineDesc()
        {
            RHI::PipelineDesc desc = BuildFillPipelineDesc();
            desc.FragmentShaderPath = Core::Filesystem::GetShaderPath(
                "shaders/uv_view/line.frag.spv");
            desc.PrimitiveTopology = RHI::Topology::LineList;
            desc.ColorBlend[0].Enable = true;
            desc.DebugName = "Renderer.UvView.Line";
            return desc;
        }
    }

    struct UvView::Impl
    {
        struct RetiredTexture
        {
            std::uint64_t ReleaseFrame = 0u;
            RHI::TextureManager::TextureLease Lease{};
        };

        struct RetiredBuffer
        {
            std::uint64_t ReleaseFrame = 0u;
            RHI::BufferManager::BufferLease Lease{};
        };

        RHI::IDevice* Device = nullptr;
        RHI::BufferManager* Buffers = nullptr;
        RHI::TextureManager* Textures = nullptr;
        RHI::SamplerManager* Samplers = nullptr;
        RHI::PipelineManager* Pipelines = nullptr;

        RHI::SamplerManager::SamplerLease Sampler{};
        RHI::PipelineManager::PipelineLease BackgroundPipeline{};
        RHI::PipelineManager::PipelineLease FillPipeline{};
        RHI::PipelineManager::PipelineLease LinePipeline{};
        RHI::TextureManager::TextureLease Target{};
        RHI::BufferManager::BufferLease LineIndexBuffer{};
        RHI::BufferManager::BufferLease DistortionBuffer{};
        std::vector<RetiredTexture> RetiredTextures;
        std::vector<RetiredBuffer> RetiredBuffers;

        UvViewRequest Request{};
        UvViewOutput Output{};
        RHI::GpuGeometryRecord GeometryRecord{};
        RHI::BufferHandle ManagedIndexBuffer{};
        UvViewPushConstants PushConstants{};
        std::uint64_t SubmissionSerial = 0u;
        std::uint64_t PreparedSerial = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t CompletedSubmissionSerial = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t TargetGeneration = 0u;
        std::uint64_t RecordedPassCount = 0u;
        std::vector<std::uint32_t> UploadedLineIndices;
        std::vector<float> UploadedDistortion;
        std::uint32_t LineIndexCount = 0u;
        bool ReadyToRecord = false;
        bool TargetHasContents = false;
        bool RecordedThisFrame = false;
        bool SubmittedSincePrepare = false;

        void ResetOutput(const UvViewStatus status,
                         const UvViewActiveMode mode,
                         std::string diagnostic)
        {
            Output = UvViewOutput{
                .Status = status,
                .ActiveMode = mode,
                .RequestedBackground = Request.Background,
                .ActiveBackground = Request.Background,
                .HeatmapActive = false,
                .RequestToken = Request.RequestToken,
                .TargetGeneration = TargetGeneration,
                .RecordedPassCount = RecordedPassCount,
                .Diagnostic = std::move(diagnostic),
            };
            ReadyToRecord = false;
        }

        void RetireCompletedResources()
        {
            if (Device == nullptr)
                return;
            const std::uint64_t frame = Device->GetGlobalFrameNumber();
            std::erase_if(RetiredTextures, [frame](const RetiredTexture& retired) {
                return retired.ReleaseFrame <= frame;
            });
            std::erase_if(RetiredBuffers, [frame](const RetiredBuffer& retired) {
                return retired.ReleaseFrame <= frame;
            });
        }

        [[nodiscard]] std::uint64_t SafeReleaseFrame() const noexcept
        {
            if (Device == nullptr)
                return 0u;
            return Device->GetGlobalFrameNumber() +
                   std::max<std::uint32_t>(Device->GetFramesInFlight(), 1u);
        }

        void Retire(RHI::TextureManager::TextureLease lease)
        {
            if (lease.IsValid())
                RetiredTextures.push_back({SafeReleaseFrame(), std::move(lease)});
        }

        void Retire(RHI::BufferManager::BufferLease lease)
        {
            if (lease.IsValid())
                RetiredBuffers.push_back({SafeReleaseFrame(), std::move(lease)});
        }

        [[nodiscard]] bool EnsureSampler()
        {
            if (Sampler.IsValid())
                return true;
            if (Samplers == nullptr)
                return false;
            auto sampler = Samplers->GetOrCreate({
                .MagFilter = RHI::FilterMode::Linear,
                .MinFilter = RHI::FilterMode::Linear,
                .MipFilter = RHI::MipmapMode::Linear,
                .AddressU = RHI::AddressMode::Repeat,
                .AddressV = RHI::AddressMode::Repeat,
                .AddressW = RHI::AddressMode::Repeat,
                .DebugName = "Renderer.UvView.Sampler",
            });
            if (!sampler.has_value())
                return false;
            Sampler = std::move(*sampler);
            return true;
        }

        [[nodiscard]] bool EnsurePipelines()
        {
            if (BackgroundPipeline.IsValid() && FillPipeline.IsValid() &&
                LinePipeline.IsValid())
            {
                return true;
            }
            if (Pipelines == nullptr || Device == nullptr || !Device->IsOperational())
                return false;

            BackgroundPipeline.Reset();
            FillPipeline.Reset();
            LinePipeline.Reset();
            auto background = Pipelines->Create(BuildBackgroundPipelineDesc());
            auto fill = Pipelines->Create(BuildFillPipelineDesc());
            auto line = Pipelines->Create(BuildLinePipelineDesc());
            if (!background.has_value() || !fill.has_value() || !line.has_value())
                return false;
            BackgroundPipeline = std::move(*background);
            FillPipeline = std::move(*fill);
            LinePipeline = std::move(*line);
            return true;
        }

        [[nodiscard]] bool EnsureTarget(const std::uint32_t width,
                                        const std::uint32_t height)
        {
            if (Textures == nullptr || !EnsureSampler())
                return false;
            if (Target.IsValid())
            {
                const RHI::TextureDesc* desc = Textures->GetDesc(Target.GetHandle());
                if (desc != nullptr && desc->Width == width && desc->Height == height)
                    return true;
                Retire(std::move(Target));
            }

            auto target = Textures->Create({
                .Width = width,
                .Height = height,
                .Fmt = RHI::Format::RGBA8_UNORM,
                .Usage = RHI::TextureUsage::Sampled |
                         RHI::TextureUsage::ColorTarget |
                         RHI::TextureUsage::TransferSrc,
                .InitialLayout = RHI::TextureLayout::Undefined,
                .DebugName = "Renderer.UvView.Color",
            }, Sampler.GetHandle());
            if (!target.has_value())
                return false;
            Target = std::move(*target);
            ++TargetGeneration;
            TargetHasContents = false;
            CompletedSubmissionSerial = std::numeric_limits<std::uint64_t>::max();
            return true;
        }

        [[nodiscard]] bool EnsureLineBuffer(const std::vector<std::uint32_t>& indices)
        {
            const std::uint64_t bytes =
                static_cast<std::uint64_t>(indices.size()) * sizeof(std::uint32_t);
            if (bytes == 0u || Buffers == nullptr || Device == nullptr)
                return false;
            if (LineIndexBuffer.IsValid() && UploadedLineIndices == indices)
            {
                LineIndexCount = static_cast<std::uint32_t>(indices.size());
                return true;
            }
            Retire(std::move(LineIndexBuffer));
            auto buffer = Buffers->Create({
                .SizeBytes = bytes,
                .Usage = RHI::BufferUsage::Index,
                .HostVisible = true,
                .DebugName = "Renderer.UvView.LineIndices",
            });
            if (!buffer.has_value())
            {
                UploadedLineIndices.clear();
                return false;
            }
            LineIndexBuffer = std::move(*buffer);
            Device->WriteBuffer(LineIndexBuffer.GetHandle(), indices.data(), bytes, 0u);
            UploadedLineIndices = indices;
            LineIndexCount = static_cast<std::uint32_t>(indices.size());
            return true;
        }

        [[nodiscard]] bool EnsureDistortionBuffer(const std::vector<float>& values)
        {
            const std::uint64_t bytes =
                static_cast<std::uint64_t>(values.size()) * sizeof(float);
            if (bytes == 0u || Buffers == nullptr || Device == nullptr)
                return false;
            if (DistortionBuffer.IsValid() &&
                SameFloatPayload(UploadedDistortion, values))
                return true;
            Retire(std::move(DistortionBuffer));
            auto buffer = Buffers->Create({
                .SizeBytes = bytes,
                .Usage = RHI::BufferUsage::Storage,
                .HostVisible = true,
                .DebugName = "Renderer.UvView.Distortion",
            });
            if (!buffer.has_value())
            {
                UploadedDistortion.clear();
                return false;
            }
            DistortionBuffer = std::move(*buffer);
            Device->WriteBuffer(DistortionBuffer.GetHandle(), values.data(), bytes, 0u);
            UploadedDistortion = values;
            return true;
        }

        void ClearGpuResources()
        {
            ReadyToRecord = false;
            TargetHasContents = false;
            RecordedThisFrame = false;
            SubmittedSincePrepare = false;
            CompletedSubmissionSerial = std::numeric_limits<std::uint64_t>::max();
            BackgroundPipeline.Reset();
            FillPipeline.Reset();
            LinePipeline.Reset();
            Target.Reset();
            LineIndexBuffer.Reset();
            DistortionBuffer.Reset();
            Sampler.Reset();
            RetiredTextures.clear();
            RetiredBuffers.clear();
            UploadedLineIndices.clear();
            UploadedDistortion.clear();
            LineIndexCount = 0u;
            GeometryRecord = {};
            ManagedIndexBuffer = {};
            PushConstants = {};
        }
    };

    UvView::UvView() : m_Impl(std::make_unique<Impl>()) {}
    UvView::~UvView() = default;

    void UvView::Initialize(RHI::IDevice& device,
                            RHI::BufferManager& buffers,
                            RHI::TextureManager& textures,
                            RHI::SamplerManager& samplers,
                            RHI::PipelineManager& pipelines)
    {
        m_Impl->Device = &device;
        m_Impl->Buffers = &buffers;
        m_Impl->Textures = &textures;
        m_Impl->Samplers = &samplers;
        m_Impl->Pipelines = &pipelines;
    }

    bool UvView::RebuildOperationalResources(RHI::IDevice& device)
    {
        m_Impl->ClearGpuResources();
        m_Impl->Device = &device;
        m_Impl->Request = {};
        ++m_Impl->SubmissionSerial;
        m_Impl->PreparedSerial = std::numeric_limits<std::uint64_t>::max();
        if (!device.IsOperational())
        {
            m_Impl->ResetOutput(
                UvViewStatus::CpuFallbackNonOperational,
                UvViewActiveMode::CpuLayout,
                "GPU UV view unavailable because the rebuilt render device is not operational.");
            return false;
        }
        if (m_Impl->Buffers == nullptr || m_Impl->Textures == nullptr ||
            m_Impl->Samplers == nullptr || m_Impl->Pipelines == nullptr)
        {
            m_Impl->ResetOutput(
                UvViewStatus::ResourceCreationFailed,
                UvViewActiveMode::CpuLayout,
                "GPU UV view resource managers are unavailable after device rebuild.");
            return false;
        }
        m_Impl->ResetOutput(
            UvViewStatus::Disabled,
            UvViewActiveMode::CpuLayout,
            "UV view is disabled until a request is refreshed.");
        return true;
    }

    void UvView::Shutdown()
    {
        m_Impl->ClearGpuResources();
        m_Impl->Pipelines = nullptr;
        m_Impl->Samplers = nullptr;
        m_Impl->Textures = nullptr;
        m_Impl->Buffers = nullptr;
        m_Impl->Device = nullptr;
        m_Impl->Request = {};
        m_Impl->ResetOutput(UvViewStatus::Disabled,
                            UvViewActiveMode::CpuLayout,
                            "UV view is disabled.");
    }

    void UvView::Submit(UvViewRequest request)
    {
        m_Impl->SubmittedSincePrepare = true;
        if (request.Enabled && request.RequestToken != 0u &&
            m_Impl->Request.Enabled &&
            m_Impl->Request.RequestToken == request.RequestToken)
        {
            return;
        }
        m_Impl->Request = std::move(request);
        ++m_Impl->SubmissionSerial;
    }

    void UvView::Prepare(GpuWorld& gpuWorld)
    {
        m_Impl->RetireCompletedResources();
        if (!m_Impl->SubmittedSincePrepare && m_Impl->Request.Enabled)
        {
            m_Impl->Request.Enabled = false;
            ++m_Impl->SubmissionSerial;
        }
        m_Impl->SubmittedSincePrepare = false;
        if (!m_Impl->Request.Enabled)
        {
            m_Impl->ResetOutput(UvViewStatus::Disabled,
                                UvViewActiveMode::CpuLayout,
                                "UV view is disabled.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }
        if (m_Impl->Device == nullptr || !m_Impl->Device->IsOperational())
        {
            m_Impl->ResetOutput(UvViewStatus::CpuFallbackNonOperational,
                                UvViewActiveMode::CpuLayout,
                                "GPU UV view unavailable because the render device is not operational.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }
        if (m_Impl->Request.Width == 0u || m_Impl->Request.Height == 0u ||
            !FiniteBounds(m_Impl->Request.Bounds))
        {
            m_Impl->ResetOutput(UvViewStatus::InvalidRequest,
                                UvViewActiveMode::CpuLayout,
                                "GPU UV view request has an invalid extent or UV bounds.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }
        if (m_Impl->Request.Width > kMaximumTargetExtent ||
            m_Impl->Request.Height > kMaximumTargetExtent)
        {
            m_Impl->ResetOutput(
                UvViewStatus::InvalidRequest,
                UvViewActiveMode::CpuLayout,
                "GPU UV view request exceeds the maximum supported target extent of 4096 pixels.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }

        const double minU =
            std::min(static_cast<double>(m_Impl->Request.Bounds.MinU), 0.0);
        const double minV =
            std::min(static_cast<double>(m_Impl->Request.Bounds.MinV), 0.0);
        const double maxU =
            std::max(static_cast<double>(m_Impl->Request.Bounds.MaxU), 1.0);
        const double maxV =
            std::max(static_cast<double>(m_Impl->Request.Bounds.MaxV), 1.0);
        double halfU = 0.5 * (maxU - minU) * kUvPadding;
        double halfV = 0.5 * (maxV - minV) * kUvPadding;
        const double paneAspect =
            static_cast<double>(m_Impl->Request.Width) /
            static_cast<double>(m_Impl->Request.Height);
        if ((halfU / halfV) < paneAspect)
            halfU = halfV * paneAspect;
        else
            halfV = halfU / paneAspect;

        float centerU = 0.0f;
        float centerV = 0.0f;
        float fittedHalfU = 0.0f;
        float fittedHalfV = 0.0f;
        if (!ToRepresentableFloat(0.5 * (minU + maxU), centerU) ||
            !ToRepresentableFloat(0.5 * (minV + maxV), centerV) ||
            !ToRepresentableFloat(halfU, fittedHalfU) ||
            !ToRepresentableFloat(halfV, fittedHalfV) ||
            fittedHalfU <= 0.0f || fittedHalfV <= 0.0f)
        {
            m_Impl->ResetOutput(
                UvViewStatus::InvalidRequest,
                UvViewActiveMode::CpuLayout,
                "GPU UV view bounds cannot be represented by the shader fit.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }

        RHI::GpuGeometryRecord geometry{};
        if (!m_Impl->Request.Geometry.IsValid() ||
            !gpuWorld.TryGetGeometryRecord(m_Impl->Request.Geometry, geometry) ||
            geometry.TexcoordBufferBDA == 0u || geometry.SurfaceIndexCount < 3u)
        {
            m_Impl->ResetOutput(UvViewStatus::WaitingForGeometry,
                                UvViewActiveMode::CpuLayout,
                                "Selected surface has no resident GPU texcoord geometry.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }
        if ((geometry.SurfaceIndexCount % 3u) != 0u ||
            m_Impl->Request.LineIndices.size() !=
                static_cast<std::size_t>(geometry.SurfaceIndexCount) * 2u ||
            (m_Impl->Request.LineIndices.size() % 2u) != 0u ||
            std::ranges::any_of(m_Impl->Request.LineIndices,
                                [geometry](const std::uint32_t index) {
                                    return index >= geometry.VertexCount;
                                }))
        {
            m_Impl->ResetOutput(UvViewStatus::InvalidRequest,
                                UvViewActiveMode::CpuLayout,
                                "GPU UV view line topology does not match the resident triangle surface.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }

        const std::uint32_t width = m_Impl->Request.Width;
        const std::uint32_t height = m_Impl->Request.Height;
        if (!m_Impl->EnsurePipelines() || !m_Impl->EnsureTarget(width, height) ||
            !m_Impl->EnsureLineBuffer(m_Impl->Request.LineIndices))
        {
            m_Impl->ResetOutput(UvViewStatus::ResourceCreationFailed,
                                UvViewActiveMode::CpuLayout,
                                "GPU UV view resources could not be created.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }

        const std::uint32_t triangleCount = geometry.SurfaceIndexCount / 3u;
        bool heatmapActive = false;
        std::string diagnostic;
        if (m_Impl->Request.ShowDistortionHeatmap)
        {
            if (m_Impl->Request.TriangleConformalDistortion.size() == triangleCount &&
                m_Impl->EnsureDistortionBuffer(
                    m_Impl->Request.TriangleConformalDistortion))
            {
                heatmapActive = true;
            }
            else
            {
                diagnostic = "Distortion heatmap fell back to the plain fill because its face payload is unavailable.";
            }
        }

        UvViewBackgroundMode activeBackground = m_Impl->Request.Background;
        if (activeBackground == UvViewBackgroundMode::Texture &&
            !IsAvailableBackgroundTexture(
                m_Impl->Request.BackgroundTexture,
                m_Impl->Device->GetBindlessHeap().GetCapacity()))
        {
            activeBackground = UvViewBackgroundMode::Checker;
            if (!diagnostic.empty())
                diagnostic += ' ';
            diagnostic += "Texture background fell back to checker because the selected material texture is unavailable.";
        }

        m_Impl->GeometryRecord = geometry;
        m_Impl->ManagedIndexBuffer = gpuWorld.GetManagedIndexBuffer();
        m_Impl->PushConstants = UvViewPushConstants{
            .TexcoordBDA = geometry.TexcoordBufferBDA,
            .DistortionBDA = heatmapActive
                ? m_Impl->Device->GetBufferDeviceAddress(
                    m_Impl->DistortionBuffer.GetHandle())
                : 0u,
            .UvCenterX = centerU,
            .UvCenterY = centerV,
            .UvHalfExtentX = fittedHalfU,
            .UvHalfExtentY = fittedHalfV,
            .BackgroundMode = static_cast<std::uint32_t>(activeBackground),
            .BackgroundTextureBindlessIndex = m_Impl->Request.BackgroundTexture,
            .ShowHeatmap = heatmapActive ? 1u : 0u,
        };

        m_Impl->Output = UvViewOutput{
            .Status = UvViewStatus::Ready,
            .ActiveMode = UvViewActiveMode::GpuShaded,
            .RequestedBackground = m_Impl->Request.Background,
            .ActiveBackground = activeBackground,
            .HeatmapActive = heatmapActive,
            .HasCompletedContents = m_Impl->TargetHasContents &&
                m_Impl->CompletedSubmissionSerial == m_Impl->SubmissionSerial,
            .RequestToken = m_Impl->Request.RequestToken,
            .Texture = m_Impl->Target.GetHandle(),
            .BindlessIndex = m_Impl->Textures->GetBindlessIndex(
                m_Impl->Target.GetHandle()),
            .Width = width,
            .Height = height,
            .TargetGeneration = m_Impl->TargetGeneration,
            .RecordedPassCount = m_Impl->RecordedPassCount,
            .Diagnostic = std::move(diagnostic),
        };
        if (m_Impl->Output.BindlessIndex == RHI::kInvalidBindlessIndex)
        {
            m_Impl->ResetOutput(UvViewStatus::ResourceCreationFailed,
                                UvViewActiveMode::CpuLayout,
                                "GPU UV view target has no bindless sampling slot.");
            m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
            return;
        }
        m_Impl->ReadyToRecord = true;
        m_Impl->PreparedSerial = m_Impl->SubmissionSerial;
    }

    void UvView::Record(RHI::ICommandContext& commandContext)
    {
        if (!m_Impl->ReadyToRecord || m_Impl->Pipelines == nullptr ||
            !m_Impl->ManagedIndexBuffer.IsValid() ||
            !m_Impl->LineIndexBuffer.IsValid())
        {
            return;
        }
        const RHI::PipelineHandle background =
            m_Impl->Pipelines->GetDeviceHandle(m_Impl->BackgroundPipeline.GetHandle());
        const RHI::PipelineHandle fill =
            m_Impl->Pipelines->GetDeviceHandle(m_Impl->FillPipeline.GetHandle());
        const RHI::PipelineHandle line =
            m_Impl->Pipelines->GetDeviceHandle(m_Impl->LinePipeline.GetHandle());
        if (!background.IsValid() || !fill.IsValid() || !line.IsValid())
            return;

        commandContext.SetViewport(0.0f, 0.0f,
                                   static_cast<float>(m_Impl->Output.Width),
                                   static_cast<float>(m_Impl->Output.Height),
                                   0.0f, 1.0f);
        commandContext.SetScissor(0, 0, m_Impl->Output.Width, m_Impl->Output.Height);

        commandContext.BindPipeline(background);
        commandContext.PushConstants(&m_Impl->PushConstants,
                                     sizeof(m_Impl->PushConstants));
        commandContext.Draw(3u, 1u, 0u, 0u);

        commandContext.BindPipeline(fill);
        commandContext.BindIndexBuffer(m_Impl->ManagedIndexBuffer,
                                       0u,
                                       RHI::IndexType::Uint32);
        commandContext.PushConstants(&m_Impl->PushConstants,
                                     sizeof(m_Impl->PushConstants));
        commandContext.DrawIndexed(m_Impl->GeometryRecord.SurfaceIndexCount,
                                   1u,
                                   m_Impl->GeometryRecord.SurfaceFirstIndex,
                                   0,
                                   0u);

        commandContext.BindPipeline(line);
        commandContext.BindIndexBuffer(m_Impl->LineIndexBuffer.GetHandle(),
                                       0u,
                                       RHI::IndexType::Uint32);
        commandContext.PushConstants(&m_Impl->PushConstants,
                                     sizeof(m_Impl->PushConstants));
        commandContext.DrawIndexed(m_Impl->LineIndexCount, 1u, 0u, 0, 0u);

        m_Impl->RecordedThisFrame = true;
        ++m_Impl->RecordedPassCount;
        m_Impl->Output.RecordedPassCount = m_Impl->RecordedPassCount;
    }

    void UvView::CompleteFrame(const bool graphExecutionSucceeded) noexcept
    {
        if (graphExecutionSucceeded && m_Impl->RecordedThisFrame)
        {
            m_Impl->TargetHasContents = true;
            m_Impl->CompletedSubmissionSerial = m_Impl->PreparedSerial;
            m_Impl->Output.HasCompletedContents =
                m_Impl->CompletedSubmissionSerial == m_Impl->SubmissionSerial;
        }
        m_Impl->RecordedThisFrame = false;
    }

    const UvViewOutput& UvView::GetOutput() const noexcept
    {
        return m_Impl->Output;
    }

    RHI::TextureHandle UvView::GetTarget() const noexcept
    {
        return m_Impl->Target.GetHandle();
    }

    bool UvView::ShouldRecord() const noexcept
    {
        return m_Impl->ReadyToRecord;
    }

    bool UvView::TargetHasShaderReadContents() const noexcept
    {
        return m_Impl->TargetHasContents;
    }
}
