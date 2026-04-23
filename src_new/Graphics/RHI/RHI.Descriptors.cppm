module;

#include <cstdint>
#include <string>

export module Extrinsic.RHI.Descriptors;

// ============================================================
// RHI Descriptors — fully API-agnostic types.
//
// No Vulkan, no OpenGL, no DirectX type ever appears here.
// The backend translates these to API-native types internally.
// ============================================================

export namespace Extrinsic::RHI
{
    // ----------------------------------------------------------
    // Pixel / texture formats
    // ----------------------------------------------------------
    enum class Format : uint32_t
    {
        Undefined = 0,

        // 8-bit per channel
        R8_UNORM,
        RG8_UNORM,
        RGBA8_UNORM,
        RGBA8_SRGB,
        BGRA8_UNORM,
        BGRA8_SRGB,

        // 16-bit per channel
        R16_FLOAT,
        RG16_FLOAT,
        RGBA16_FLOAT,
        R16_UINT,
        R16_UNORM,

        // 32-bit per channel
        R32_FLOAT,
        RG32_FLOAT,
        RGB32_FLOAT,
        RGBA32_FLOAT,
        R32_UINT,
        R32_SINT,

        // Depth / stencil
        D16_UNORM,
        D32_FLOAT,
        D24_UNORM_S8_UINT,
        D32_FLOAT_S8_UINT,

        // Block-compressed
        BC1_UNORM,
        BC3_UNORM,
        BC5_UNORM,
        BC7_UNORM,
        BC7_SRGB,
    };

    // ----------------------------------------------------------
    // Buffer usage flags (combinable)
    // ----------------------------------------------------------
    enum class BufferUsage : uint32_t
    {
        None        = 0,
        Vertex      = 1 << 0,
        Index       = 1 << 1,
        Uniform     = 1 << 2,
        Storage     = 1 << 3,   // read/write shader storage
        Indirect    = 1 << 4,   // indirect draw/dispatch args
        TransferSrc = 1 << 5,
        TransferDst = 1 << 6,
    };

    [[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept
    {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    [[nodiscard]] constexpr bool HasUsage(BufferUsage flags, BufferUsage bit) noexcept
    {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(bit)) != 0;
    }

    // ----------------------------------------------------------
    // Texture usage flags (combinable)
    // ----------------------------------------------------------
    enum class TextureUsage : uint32_t
    {
        None           = 0,
        Sampled        = 1 << 0,   // read in shader
        Storage        = 1 << 1,   // write in compute shader
        ColorTarget    = 1 << 2,   // render target
        DepthTarget    = 1 << 3,   // depth/stencil attachment
        TransferSrc    = 1 << 4,
        TransferDst    = 1 << 5,
    };

    [[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    // ----------------------------------------------------------
    // Texture layout — used for resource barrier transitions
    // ----------------------------------------------------------
    enum class TextureLayout : uint32_t
    {
        Undefined = 0,
        General,
        ColorAttachment,
        DepthAttachment,
        DepthReadOnly,
        ShaderReadOnly,
        TransferSrc,
        TransferDst,
        Present,
    };

    // ----------------------------------------------------------
    // Texture dimensionality
    // ----------------------------------------------------------
    enum class TextureDimension : uint32_t
    {
        Tex1D,
        Tex2D,
        Tex3D,
        TexCube,
    };

    // ----------------------------------------------------------
    // Filter / address modes for samplers
    // ----------------------------------------------------------
    enum class FilterMode : uint32_t { Nearest, Linear };
    enum class MipmapMode : uint32_t { Nearest, Linear };
    enum class AddressMode : uint32_t { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };
    enum class CompareOp   : uint32_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

    // ----------------------------------------------------------
    // Blend / depth-stencil state
    // ----------------------------------------------------------
    enum class BlendFactor : uint32_t
    {
        Zero, One,
        SrcColor, OneMinusSrcColor,
        SrcAlpha, OneMinusSrcAlpha,
        DstAlpha, OneMinusDstAlpha,
    };

    enum class BlendOp : uint32_t { Add, Subtract, ReverseSubtract, Min, Max };

    enum class DepthOp : uint32_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

    enum class CullMode  : uint32_t { None, Front, Back };
    enum class FrontFace : uint32_t { CounterClockwise, Clockwise };
    enum class FillMode  : uint32_t { Solid, Wireframe };
    enum class Topology  : uint32_t { TriangleList, TriangleStrip, LineList, LineStrip, PointList };

    // ----------------------------------------------------------
    // Present / frame-pacing policy
    // ----------------------------------------------------------
    // Backend maps this to the best available API mode:
    //   VSync        → FIFO              (always supported, tear-free)
    //   LowLatency   → Mailbox → FIFO fallback
    //   Uncapped     → Immediate → FIFO fallback
    //   Throttled    → FIFO_RELAXED → FIFO fallback  (editor idle)
    enum class PresentMode : uint32_t
    {
        VSync      = 0,
        LowLatency = 1,
        Uncapped   = 2,
        Throttled  = 3,
    };


    // ----------------------------------------------------------
    // Resource descriptors
    // ----------------------------------------------------------

    struct BufferDesc
    {
        uint64_t    SizeBytes   = 0;
        BufferUsage Usage       = BufferUsage::None;
        bool        HostVisible = false;   // mappable / upload heap
        const char* DebugName   = nullptr;
    };

    struct TextureDesc
    {
        uint32_t         Width      = 1;
        uint32_t         Height     = 1;
        uint32_t         DepthOrArrayLayers = 1;
        uint32_t         MipLevels  = 1;
        Format           Fmt        = Format::RGBA8_UNORM;
        TextureDimension Dimension  = TextureDimension::Tex2D;
        TextureUsage     Usage      = TextureUsage::Sampled;
        TextureLayout    InitialLayout = TextureLayout::Undefined;
        uint32_t         SampleCount   = 1;
        const char*      DebugName     = nullptr;
    };

    struct SamplerDesc
    {
        FilterMode  MagFilter  = FilterMode::Linear;
        FilterMode  MinFilter  = FilterMode::Linear;
        MipmapMode  MipFilter  = MipmapMode::Linear;
        AddressMode AddressU   = AddressMode::Repeat;
        AddressMode AddressV   = AddressMode::Repeat;
        AddressMode AddressW   = AddressMode::Repeat;
        float       MipLodBias = 0.0f;
        float       MinLod     = 0.0f;
        float       MaxLod     = 1000.0f;
        float       MaxAnisotropy = 1.0f;   // 1 = disabled
        bool        CompareEnable = false;
        CompareOp   Compare    = CompareOp::Never;
        const char* DebugName  = nullptr;
    };

    // Blend state for a single render target
    struct ColorBlendDesc
    {
        bool        Enable          = false;
        BlendFactor SrcColorFactor  = BlendFactor::One;
        BlendFactor DstColorFactor  = BlendFactor::Zero;
        BlendOp     ColorOp         = BlendOp::Add;
        BlendFactor SrcAlphaFactor  = BlendFactor::One;
        BlendFactor DstAlphaFactor  = BlendFactor::Zero;
        BlendOp     AlphaOp         = BlendOp::Add;
    };

    struct DepthStencilDesc
    {
        bool    DepthTestEnable  = true;
        bool    DepthWriteEnable = true;
        DepthOp DepthFunc        = DepthOp::Less;
        bool    StencilEnable    = false;
    };

    struct RasterizerDesc
    {
        CullMode  Culling     = CullMode::Back;
        FrontFace Winding     = FrontFace::CounterClockwise;
        FillMode  Fill        = FillMode::Solid;
        float     DepthBiasConstant = 0.0f;
        float     DepthBiasSlope   = 0.0f;
    };

    // Maximum counts — kept small for now; increase as needed.
    constexpr uint32_t MaxColorTargets      = 8;
    constexpr uint32_t MaxPushConstantBytes = 128;

    struct PipelineDesc
    {
        // Shader sources — the backend interprets the path format
        // (e.g. Vulkan expects SPIR-V .spv, a future Metal backend .metal, etc.)
        std::string VertexShaderPath;
        std::string FragmentShaderPath;
        std::string ComputeShaderPath;   // non-empty → compute pipeline

        // BDA-ONLY: vertex input state is intentionally absent.
        // All geometry is read from device-local buffers via Buffer Device Address
        // pushed as push constants.  vkCmdBindVertexBuffers is never called.
        // The Vulkan backend creates all graphics pipelines with
        // VkPipelineVertexInputStateCreateInfo { .vertexBindingDescriptionCount = 0 }.

        // Per-stage state
        Topology         PrimitiveTopology = Topology::TriangleList;
        RasterizerDesc   Rasterizer{};
        DepthStencilDesc DepthStencil{};
        ColorBlendDesc   ColorBlend[MaxColorTargets]{};
        uint32_t         ColorTargetCount = 1;

        // Render target formats — required for dynamic rendering (VK_KHR_dynamic_rendering).
        // ColorTargetFormats[i] must be set for each of the ColorTargetCount slots.
        // DepthTargetFormat = Format::Undefined means no depth attachment.
        Format ColorTargetFormats[MaxColorTargets]{};
        Format DepthTargetFormat = Format::Undefined;

        // Push-constant size in bytes.  Must be <= MaxPushConstantBytes (128).
        uint32_t PushConstantSize = 0;

        const char* DebugName = nullptr;
    };
}

