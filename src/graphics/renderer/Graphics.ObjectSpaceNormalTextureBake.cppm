module;

#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <string>

export module Extrinsic.Graphics.ObjectSpaceNormalTextureBake;

import Extrinsic.Core.Error;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    inline constexpr std::uint32_t kObjectSpaceNormalBakeDefaultExtent = 512u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeMinExtent = 16u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeMaxExtent = 4096u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeDefaultPaddingTexels = 4u;
    inline constexpr std::uint32_t kObjectSpaceNormalBakeMaxPaddingTexels = 32u;

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

    struct ObjectSpaceNormalTextureBakeGpuRecordDesc
    {
        RHI::PipelineHandle Pipeline{};
        RHI::TextureHandle OutputTexture{};
        RHI::BufferHandle IndexBuffer{};
        std::uint64_t TexcoordBDA = 0u;
        std::uint64_t NormalBDA = 0u;
        std::uint32_t IndexCount = 0u;
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        RHI::TextureLayout InitialLayout = RHI::TextureLayout::Undefined;
        RHI::TextureLayout FinalLayout = RHI::TextureLayout::ShaderReadOnly;
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

    [[nodiscard]] Core::Result RecordObjectSpaceNormalTextureBake(
        RHI::ICommandContext& cmd,
        const ObjectSpaceNormalTextureBakeGpuRecordDesc& desc);
}
