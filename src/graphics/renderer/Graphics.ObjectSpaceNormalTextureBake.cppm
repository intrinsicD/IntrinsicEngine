module;

#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <string>

export module Extrinsic.Graphics.ObjectSpaceNormalTextureBake;

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    inline constexpr std::uint32_t kObjectSpaceNormalBakeDefaultExtent = 512u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeMinExtent = 16u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeMaxExtent = 4096u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeDefaultPaddingTexels = 4u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeMaxPaddingTexels = 32u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeDilationOutputDescriptorSlot = 4u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeDilationScratchDescriptorSlot = 5u;

    enum class NormalTextureSpace : std::uint8_t
    {
        ObjectSpaceNormal,
        TangentSpaceNormal,
    };

    enum class ObjectSpaceNormalTextureBakeStatus : std::uint8_t
    {
        Success,
        UnsupportedNormalTextureSpace,
        EmptyInput,
        InvalidTriangleIndex,
        NonFiniteTexcoord,
        NonAtlasTexcoord,
        NonFiniteNormal,
        DegenerateNormal,
        DegenerateUvTriangle,
        NoContainingTriangle,
        InvalidGeneratedTextureAsset,
        InvalidGpuResource,
        InvalidIndexCount,
        DilationUnavailable,
    };

    struct ObjectSpaceNormalTextureBakeOptions
    {
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        std::uint32_t PaddingTexels = kObjectSpaceNormalBakeDefaultPaddingTexels;
        float AtlasUvEpsilon = 1.0e-4f;
        float DegenerateUvAreaEpsilon = 1.0e-10f;
        float DegenerateNormalLengthEpsilon = 1.0e-6f;
        NormalTextureSpace Space = NormalTextureSpace::ObjectSpaceNormal;
    };

    struct ObjectSpaceNormalTextureBakeResolvedOptions
    {
        std::uint32_t Width = kObjectSpaceNormalBakeDefaultExtent;
        std::uint32_t Height = kObjectSpaceNormalBakeDefaultExtent;
        std::uint32_t PaddingTexels = kObjectSpaceNormalBakeDefaultPaddingTexels;
        float AtlasUvEpsilon = 1.0e-4f;
        float DegenerateUvAreaEpsilon = 1.0e-10f;
        float DegenerateNormalLengthEpsilon = 1.0e-6f;
        NormalTextureSpace Space = NormalTextureSpace::ObjectSpaceNormal;
    };

    struct ObjectSpaceNormalTextureBakeVertex
    {
        glm::vec2 Uv{0.0f};
        glm::vec3 Normal{0.0f, 0.0f, 1.0f};
    };

    struct ObjectSpaceNormalTextureBakeTriangle
    {
        std::uint32_t A = 0u;
        std::uint32_t B = 0u;
        std::uint32_t C = 0u;
    };

    struct ObjectSpaceNormalTextureBakeDiagnostics
    {
        ObjectSpaceNormalTextureBakeResolvedOptions Options{};
        std::uint32_t VertexCount = 0u;
        std::uint32_t TriangleCount = 0u;
        std::uint32_t DegenerateUvTriangleCount = 0u;
        std::uint32_t FirstFailureIndex = 0u;
    };

    struct ObjectSpaceNormalTextureBakeValidation
    {
        ObjectSpaceNormalTextureBakeStatus Status{
            ObjectSpaceNormalTextureBakeStatus::Success};
        ObjectSpaceNormalTextureBakeDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ObjectSpaceNormalTextureBakeStatus::Success;
        }
    };

    struct ObjectSpaceNormalTextureBakeSample
    {
        ObjectSpaceNormalTextureBakeStatus Status{
            ObjectSpaceNormalTextureBakeStatus::Success};
        glm::vec2 Uv{0.0f};
        glm::vec3 Barycentric{0.0f};
        glm::vec3 ObjectNormal{0.0f, 0.0f, 1.0f};
        glm::vec4 EncodedRgba{0.5f, 0.5f, 1.0f, 1.0f};
        std::uint32_t TriangleIndex = 0u;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ObjectSpaceNormalTextureBakeStatus::Success;
        }
    };

    struct alignas(8) ObjectSpaceNormalTextureBakeGpuPushConstants
    {
        std::uint64_t TexcoordBDA = 0u;
        std::uint64_t NormalBDA = 0u;
    };

    struct alignas(4) ObjectSpaceNormalTextureBakeDilationPushConstants
    {
        std::uint32_t SourceTextureSlot = kObjectSpaceNormalBakeDilationOutputDescriptorSlot;
    };

    struct ObjectSpaceNormalTextureBakeDilationResources
    {
        RHI::PipelineHandle Pipeline{};
        RHI::TextureHandle ScratchTexture{};
        RHI::TextureLayout ScratchInitialLayout = RHI::TextureLayout::Undefined;
        std::uint32_t OutputDescriptorSlot =
            kObjectSpaceNormalBakeDilationOutputDescriptorSlot;
        std::uint32_t ScratchDescriptorSlot =
            kObjectSpaceNormalBakeDilationScratchDescriptorSlot;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Pipeline.IsValid() &&
                   ScratchTexture.IsValid() &&
                   OutputDescriptorSlot != ScratchDescriptorSlot;
        }
    };

    struct ObjectSpaceNormalTextureBakeDilationResourceDesc
    {
        RHI::PipelineDesc Pipeline{};
        RHI::TextureDesc ScratchTexture{};
        std::uint32_t OutputDescriptorSlot =
            kObjectSpaceNormalBakeDilationOutputDescriptorSlot;
        std::uint32_t ScratchDescriptorSlot =
            kObjectSpaceNormalBakeDilationScratchDescriptorSlot;
    };

    class ObjectSpaceNormalTextureBakeDilationResourceLease
    {
    public:
        ObjectSpaceNormalTextureBakeDilationResourceLease() = default;
        ~ObjectSpaceNormalTextureBakeDilationResourceLease();

        ObjectSpaceNormalTextureBakeDilationResourceLease(
            const ObjectSpaceNormalTextureBakeDilationResourceLease&) = delete;
        ObjectSpaceNormalTextureBakeDilationResourceLease& operator=(
            const ObjectSpaceNormalTextureBakeDilationResourceLease&) = delete;

        ObjectSpaceNormalTextureBakeDilationResourceLease(
            ObjectSpaceNormalTextureBakeDilationResourceLease&& other) noexcept;
        ObjectSpaceNormalTextureBakeDilationResourceLease& operator=(
            ObjectSpaceNormalTextureBakeDilationResourceLease&& other) noexcept;

        [[nodiscard]] Core::Result Initialize(
            RHI::IDevice& device,
            const ObjectSpaceNormalTextureBakeDilationResourceDesc& desc);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] ObjectSpaceNormalTextureBakeDilationResources
            GetResources() const noexcept;

    private:
        RHI::IDevice* m_Device = nullptr;
        RHI::PipelineHandle m_Pipeline{};
        RHI::TextureHandle m_ScratchTexture{};
        RHI::TextureLayout m_ScratchInitialLayout = RHI::TextureLayout::Undefined;
        std::uint32_t m_OutputDescriptorSlot =
            kObjectSpaceNormalBakeDilationOutputDescriptorSlot;
        std::uint32_t m_ScratchDescriptorSlot =
            kObjectSpaceNormalBakeDilationScratchDescriptorSlot;
    };

    struct ObjectSpaceNormalTextureBakeGpuRecordDesc
    {
        RHI::PipelineHandle Pipeline{};
        RHI::TextureHandle OutputTexture{};
        ObjectSpaceNormalTextureBakeDilationResources Dilation{};
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t TexcoordBDA = 0u;
        std::uint64_t NormalBDA = 0u;
        std::uint32_t FirstIndex = 0u;
        std::uint32_t IndexCount = 0u;
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        std::uint32_t PaddingTexels = 0u;
        RHI::TextureLayout InitialLayout = RHI::TextureLayout::Undefined;
        RHI::TextureLayout FinalLayout = RHI::TextureLayout::ShaderReadOnly;
    };

    struct ObjectSpaceNormalTextureBakeGeometryBuffers
    {
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t TexcoordBDA = 0u;
        std::uint64_t NormalBDA = 0u;
        std::uint32_t VertexCount = 0u;
        std::uint32_t FirstIndex = 0u;
        std::uint32_t IndexCount = 0u;
    };

    struct ObjectSpaceNormalTextureBakeSourceKey
    {
        std::uint64_t EntityKey = 0u;
        std::uint64_t GeometryGeneration = 0u;
        std::uint64_t TexcoordGeneration = 0u;
        std::uint64_t NormalGeneration = 0u;
    };

    struct ObjectSpaceNormalTextureBakeCompletionKey
    {
        Assets::AssetId GeneratedTextureAsset{};
        ObjectSpaceNormalTextureBakeSourceKey Source{};
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        std::uint32_t PaddingTexels = 0u;
        NormalTextureSpace Space = NormalTextureSpace::ObjectSpaceNormal;
    };

    struct ObjectSpaceNormalTextureBakeGpuRecordTemplate
    {
        RHI::PipelineHandle Pipeline{};
        ObjectSpaceNormalTextureBakeDilationResources Dilation{};
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t TexcoordBDA = 0u;
        std::uint64_t NormalBDA = 0u;
        std::uint32_t FirstIndex = 0u;
        std::uint32_t IndexCount = 0u;
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        std::uint32_t PaddingTexels = 0u;
        RHI::TextureLayout InitialLayout = RHI::TextureLayout::Undefined;
        RHI::TextureLayout FinalLayout = RHI::TextureLayout::ShaderReadOnly;
    };

    struct ObjectSpaceNormalTextureBakePlanRequest
    {
        Assets::AssetId GeneratedTextureAsset{};
        ObjectSpaceNormalTextureBakeGeometryBuffers Geometry{};
        ObjectSpaceNormalTextureBakeOptions Options{};
        ObjectSpaceNormalTextureBakeSourceKey SourceKey{};
        RHI::PipelineHandle Pipeline{};
        ObjectSpaceNormalTextureBakeDilationResources Dilation{};
        RHI::SamplerDesc SamplerDesc{};
        RHI::SamplerHandle Sampler{};
        RHI::TextureUsage AdditionalTextureUsage = RHI::TextureUsage::None;
        RHI::TextureLayout InitialLayout = RHI::TextureLayout::Undefined;
        RHI::TextureLayout FinalLayout = RHI::TextureLayout::ShaderReadOnly;
        const char* DebugName = "ObjectSpaceNormalTextureBake.Output";
        std::uint64_t ReadyFrame = 0u;
        bool HasReadyFrame = false;
    };

    struct ObjectSpaceNormalTextureBakePlan
    {
        ObjectSpaceNormalTextureBakeStatus Status{
            ObjectSpaceNormalTextureBakeStatus::Success};
        ObjectSpaceNormalTextureBakeDiagnostics Diagnostics{};
        ObjectSpaceNormalTextureBakeCompletionKey CompletionKey{};
        GpuProducedTextureRequest TextureRequest{};
        ObjectSpaceNormalTextureBakeGpuRecordTemplate RecordTemplate{};
        bool DilationRequested = false;
        bool DilationAvailable = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ObjectSpaceNormalTextureBakeStatus::Success;
        }
    };

    [[nodiscard]] const char* DebugNameForObjectSpaceNormalTextureBakeStatus(
        ObjectSpaceNormalTextureBakeStatus status) noexcept;

    [[nodiscard]] ObjectSpaceNormalTextureBakeResolvedOptions
        ResolveObjectSpaceNormalTextureBakeOptions(
            const ObjectSpaceNormalTextureBakeOptions& options) noexcept;

    [[nodiscard]] ObjectSpaceNormalTextureBakeValidation
        ValidateObjectSpaceNormalTextureBakeInput(
            std::span<const ObjectSpaceNormalTextureBakeVertex> vertices,
            std::span<const ObjectSpaceNormalTextureBakeTriangle> triangles,
            const ObjectSpaceNormalTextureBakeOptions& options = {});

    [[nodiscard]] glm::vec4 EncodeObjectSpaceNormalToRgba(
        const glm::vec3& normal) noexcept;

    [[nodiscard]] glm::vec2 UvForObjectSpaceNormalBakeTexelCenter(
        std::uint32_t x,
        std::uint32_t y,
        const ObjectSpaceNormalTextureBakeResolvedOptions& options) noexcept;

    [[nodiscard]] ObjectSpaceNormalTextureBakeSample
        SampleObjectSpaceNormalTextureBakeAtUv(
            std::span<const ObjectSpaceNormalTextureBakeVertex> vertices,
            std::span<const ObjectSpaceNormalTextureBakeTriangle> triangles,
            const glm::vec2& uv,
            const ObjectSpaceNormalTextureBakeOptions& options = {});

    [[nodiscard]] RHI::PipelineDesc MakeObjectSpaceNormalTextureBakePipelineDesc(
        std::string vertexShaderPath,
        std::string fragmentShaderPath,
        RHI::Format colorFormat = RHI::Format::RGBA8_UNORM);

    [[nodiscard]] RHI::PipelineDesc
        MakeObjectSpaceNormalTextureBakeDilationPipelineDesc(
            std::string vertexShaderPath,
            std::string fragmentShaderPath,
            RHI::Format colorFormat = RHI::Format::RGBA8_UNORM);

    [[nodiscard]] RHI::TextureDesc
        MakeObjectSpaceNormalTextureBakeDilationScratchTextureDesc(
            const ObjectSpaceNormalTextureBakeOptions& options,
            const char* debugName =
                "ObjectSpaceNormalTextureBake.DilationScratch") noexcept;

    [[nodiscard]] ObjectSpaceNormalTextureBakeDilationResourceDesc
        MakeObjectSpaceNormalTextureBakeDilationResourceDesc(
            const ObjectSpaceNormalTextureBakeOptions& options,
            std::string vertexShaderPath,
            std::string fragmentShaderPath,
            const char* scratchDebugName =
                "ObjectSpaceNormalTextureBake.DilationScratch");

    [[nodiscard]] ObjectSpaceNormalTextureBakePlan
        BuildObjectSpaceNormalTextureBakePlan(
            const ObjectSpaceNormalTextureBakePlanRequest& request) noexcept;

    [[nodiscard]] bool ObjectSpaceNormalTextureBakeCompletionKeyMatches(
        const ObjectSpaceNormalTextureBakeCompletionKey& expected,
        const ObjectSpaceNormalTextureBakeCompletionKey& actual) noexcept;

    [[nodiscard]] ObjectSpaceNormalTextureBakeGpuRecordDesc
        MakeObjectSpaceNormalTextureBakeGpuRecordDesc(
            const ObjectSpaceNormalTextureBakePlan& plan,
            RHI::TextureHandle outputTexture) noexcept;

    [[nodiscard]] Core::Result RecordObjectSpaceNormalTextureBake(
        RHI::ICommandContext& cmd,
        const ObjectSpaceNormalTextureBakeGpuRecordDesc& desc);
}
