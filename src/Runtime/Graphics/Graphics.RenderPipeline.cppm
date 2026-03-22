module;

#include <unordered_map>
#include <memory>
#include <span>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

export module Graphics.RenderPipeline;

import Graphics.RenderGraph;
import Graphics.GPUScene;
import Graphics.MaterialSystem;
import Graphics.Geometry;
import Geometry.Handle;
import Graphics.ShaderRegistry;
import Graphics.PipelineLibrary;
import Graphics.DebugDraw;
import Graphics.Passes.SelectionOutlineSettings;
import Graphics.Passes.PostProcessSettings;
import Core.Hash;
import Core.Assets;
import ECS;
import RHI.Bindless;
import RHI.Buffer;
import RHI.Descriptors;
import RHI.Device;
import RHI.Renderer;

export namespace Graphics
{
    namespace Passes
    {
    }

    enum class RenderResource : uint8_t
    {
        SceneDepth,
        EntityId,
        PrimitiveId,
        SceneNormal,
        Albedo,
        Material0,
        SceneColorHDR,
        SceneColorLDR,
        SelectionMask,
        SelectionOutline,
    };

    enum class RenderResourceFormatSource : uint8_t
    {
        Fixed,
        Swapchain,
        Depth,
    };

    enum class RenderResourceLifetime : uint8_t
    {
        Imported,
        FrameTransient,
    };

    enum class FrameLightingPath : uint8_t
    {
        None,
        Forward,
        Deferred,
        Hybrid,
    };

    [[nodiscard]] constexpr bool UsesDeferredComposition(FrameLightingPath lightingPath)
    {
        return lightingPath == FrameLightingPath::Deferred || lightingPath == FrameLightingPath::Hybrid;
    }

    struct RenderResourceDefinition
    {
        RenderResource Id{};
        Core::Hash::StringID Name{};
        VkFormat FixedFormat = VK_FORMAT_UNDEFINED;
        RenderResourceFormatSource FormatSource = RenderResourceFormatSource::Fixed;
        VkImageUsageFlags Usage = 0;
        VkImageAspectFlags Aspect = 0;
        RenderResourceLifetime Lifetime = RenderResourceLifetime::FrameTransient;
        bool Optional = true;
    };

    [[nodiscard]] constexpr RenderResourceDefinition GetRenderResourceDefinition(RenderResource resource)
    {
        using enum RenderResource;
        switch (resource)
        {
        case SceneDepth:
            return {resource, "SceneDepth"_id, VK_FORMAT_UNDEFINED, RenderResourceFormatSource::Depth,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, RenderResourceLifetime::Imported, false};
        case EntityId:
            return {resource, "EntityId"_id, VK_FORMAT_R32_UINT, RenderResourceFormatSource::Fixed,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, true};
        case PrimitiveId:
            return {resource, "PrimitiveId"_id, VK_FORMAT_R32_UINT, RenderResourceFormatSource::Fixed,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, true};
        case SceneNormal:
            return {resource, "SceneNormal"_id, VK_FORMAT_R16G16B16A16_SFLOAT, RenderResourceFormatSource::Fixed,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, true};
        case Albedo:
            return {resource, "Albedo"_id, VK_FORMAT_R8G8B8A8_UNORM, RenderResourceFormatSource::Fixed,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, true};
        case Material0:
            return {resource, "Material0"_id, VK_FORMAT_R16G16B16A16_SFLOAT, RenderResourceFormatSource::Fixed,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, true};
        case SceneColorHDR:
            return {resource, "SceneColorHDR"_id, VK_FORMAT_R16G16B16A16_SFLOAT, RenderResourceFormatSource::Fixed,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, false};
        case SceneColorLDR:
            return {resource, "SceneColorLDR"_id, VK_FORMAT_UNDEFINED, RenderResourceFormatSource::Swapchain,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, true};
        case SelectionMask:
            return {resource, "SelectionMask"_id, VK_FORMAT_R8_UNORM, RenderResourceFormatSource::Fixed,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, true};
        case SelectionOutline:
            return {resource, "SelectionOutline"_id, VK_FORMAT_UNDEFINED, RenderResourceFormatSource::Swapchain,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, RenderResourceLifetime::FrameTransient, true};
        }
        return {};
    }

    [[nodiscard]] constexpr Core::Hash::StringID GetRenderResourceName(RenderResource resource)
    {
        return GetRenderResourceDefinition(resource).Name;
    }

    [[nodiscard]] constexpr VkFormat ResolveRenderResourceFormat(RenderResource resource,
                                                                 VkFormat swapchainFormat,
                                                                 VkFormat depthFormat)
    {
        const auto def = GetRenderResourceDefinition(resource);
        switch (def.FormatSource)
        {
        case RenderResourceFormatSource::Fixed: return def.FixedFormat;
        case RenderResourceFormatSource::Swapchain: return swapchainFormat;
        case RenderResourceFormatSource::Depth: return depthFormat;
        }
        return VK_FORMAT_UNDEFINED;
    }

    [[nodiscard]] inline std::optional<RenderResource> TryGetRenderResourceByName(Core::Hash::StringID name)
    {
        for (RenderResource resource : {
                 RenderResource::SceneDepth,
                 RenderResource::EntityId,
                 RenderResource::PrimitiveId,
                 RenderResource::SceneNormal,
                 RenderResource::Albedo,
                 RenderResource::Material0,
                 RenderResource::SceneColorHDR,
                 RenderResource::SceneColorLDR,
                 RenderResource::SelectionMask,
                 RenderResource::SelectionOutline,
             })
        {
            if (GetRenderResourceName(resource) == name)
                return resource;
        }
        return std::nullopt;
    }

    struct FrameRecipe
    {
        bool Depth = false;
        bool EntityId = false;
        bool PrimitiveId = false;
        bool Normals = false;
        bool MaterialChannels = false;
        bool Selection = false;
        bool Post = false;
        bool DebugVisualization = false;
        bool SceneColorLDR = false;
        FrameLightingPath LightingPath = FrameLightingPath::None;

        [[nodiscard]] bool Requires(RenderResource resource) const
        {
            using enum RenderResource;
            switch (resource)
            {
            case SceneDepth: return Depth;
            case EntityId: return this->EntityId;
            case PrimitiveId: return this->PrimitiveId;
            case SceneNormal: return Normals;
            case Albedo:
            case Material0: return MaterialChannels;
            case SceneColorHDR: return LightingPath != FrameLightingPath::None || Post;
            case SceneColorLDR: return this->SceneColorLDR;
            case SelectionMask:
            case SelectionOutline: return false;
            }
            return false;
        }
    };

    // ---------------------------------------------------------------------
    // RenderBlackboard
    // ---------------------------------------------------------------------
    struct RenderBlackboard
    {
        std::unordered_map<Core::Hash::StringID, RGResourceHandle> Resources;

        void Add(Core::Hash::StringID name, RGResourceHandle handle)
        {
            Resources[name] = handle;
        }

        void Add(RenderResource resource, RGResourceHandle handle)
        {
            Add(GetRenderResourceName(resource), handle);
        }

        [[nodiscard]] RGResourceHandle Get(Core::Hash::StringID name) const
        {
            if (auto it = Resources.find(name); it != Resources.end())
                return it->second;
            return {};
        }

        [[nodiscard]] RGResourceHandle Get(RenderResource resource) const
        {
            return Get(GetRenderResourceName(resource));
        }
    };

    struct PickingSurfacePacket
    {
        Geometry::GeometryHandle Geometry{};
        glm::mat4 WorldMatrix{1.0f};
        uint32_t EntityId = 0;
        std::vector<uint32_t> TriangleFaceIds{};
    };

    struct PickingLinePacket
    {
        Geometry::GeometryHandle Geometry{};
        Geometry::GeometryHandle EdgeView{};
        glm::mat4 WorldMatrix{1.0f};
        uint32_t EntityId = 0;
        float Width = 1.0f;
        uint32_t EdgeCount = 0;
    };

    struct PickingPointPacket
    {
        Geometry::GeometryHandle Geometry{};
        glm::mat4 WorldMatrix{1.0f};
        uint32_t EntityId = 0;
        float Size = 1.0f;
    };

    // ---------------------------------------------------------------------
    // Frame Context
    // ---------------------------------------------------------------------
    struct RenderPassContext
    {
        RenderGraph& Graph;
        RenderBlackboard& Blackboard;

        // Scene
        const ECS::Scene& Scene;
        const Core::Assets::AssetManager& AssetManager;
        GeometryPool& GeometryStorage;
        MaterialSystem& MaterialSystem;

        // Retained-mode GPU scene (owned by RenderSystem).
        GPUScene* GpuScene = nullptr;

        // Frame
        uint32_t FrameIndex = 0;
        VkExtent2D Resolution{};
        FrameRecipe Recipe{};

        // Swapchain/backbuffer import context
        uint32_t SwapchainImageIndex = 0;
        VkFormat SwapchainFormat = VK_FORMAT_UNDEFINED;

        // Depth import context (matches the imported "SceneDepth" image)
        VkFormat DepthFormat = VK_FORMAT_UNDEFINED;

        // HDR scene color format for scene passes (Surface, Line, Point).
        VkFormat SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

        // Command submission helpers
        RHI::SimpleRenderer& Renderer;

        // Global GPU state
        RHI::VulkanBuffer* GlobalCameraUBO = nullptr;
        VkDescriptorSet GlobalDescriptorSet = VK_NULL_HANDLE;
        size_t GlobalCameraDynamicOffset = 0;

        RHI::BindlessDescriptorSystem& Bindless;

        struct PickRequestState
        {
            bool Pending = false;
            uint32_t X = 0;
            uint32_t Y = 0;
        } PickRequest;

        struct DebugState
        {
            bool Enabled = false;
            bool ShowInViewport = false;
            bool DisableCulling = false;
            Core::Hash::StringID SelectedResource = GetRenderResourceName(RenderResource::EntityId);
            float DepthNear = 0.1f;
            float DepthFar = 1000.0f;
        } Debug;

        std::span<const RenderGraphDebugImage> PrevFrameDebugImages{};
        std::span<const RenderGraphDebugPass> PrevFrameDebugPasses{};

        glm::mat4 CameraView{1.0f};
        glm::mat4 CameraProj{1.0f};

        RHI::VulkanBuffer* PickReadbackBuffer = nullptr;
        DebugDraw* DebugDrawPtr = nullptr;

        std::span<const PickingSurfacePacket> PickingSurfacePackets{};
        std::span<const PickingLinePacket> PickingLinePackets{};
        std::span<const PickingPointPacket> PickingPointPackets{};
    };

    [[nodiscard]] inline RGTextureDesc BuildRenderResourceTextureDesc(RenderResource resource,
                                                                      const RenderPassContext& ctx)
    {
        const auto def = GetRenderResourceDefinition(resource);
        RGTextureDesc desc{};
        desc.Width = ctx.Resolution.width;
        desc.Height = ctx.Resolution.height;
        desc.Format = ResolveRenderResourceFormat(resource, ctx.SwapchainFormat, ctx.DepthFormat);
        desc.Usage = def.Usage;
        desc.Aspect = def.Aspect;
        return desc;
    }

    [[nodiscard]] inline RGResourceHandle GetPresentationTarget(const RenderPassContext& ctx)
    {
        if (const auto ldr = ctx.Blackboard.Get(RenderResource::SceneColorLDR); ldr.IsValid())
            return ldr;
        return ctx.Blackboard.Get("Backbuffer"_id);
    }

    // ---------------------------------------------------------------------
    // Feature interface
    // ---------------------------------------------------------------------
    class IRenderFeature
    {
    public:
        virtual ~IRenderFeature() = default;
        virtual void Initialize(RHI::VulkanDevice& device,
                                RHI::DescriptorAllocator& descriptorPool,
                                RHI::DescriptorLayout& globalLayout) = 0;
        virtual void AddPasses(RenderPassContext& ctx) = 0;
        virtual void Shutdown() {}
        virtual void OnResize(uint32_t width, uint32_t height) { (void)width; (void)height; }
    };

    struct RenderPipelineFeatureDebugState
    {
        bool Exists = false;
        bool Enabled = false;
    };

    struct RenderPipelineDebugState
    {
        bool HasFeatureRegistry = false;
        bool PathDirty = false;
        RenderPipelineFeatureDebugState PickingPass{};
        RenderPipelineFeatureDebugState SurfacePass{};
        RenderPipelineFeatureDebugState SelectionOutlinePass{};
        RenderPipelineFeatureDebugState LinePass{};
        RenderPipelineFeatureDebugState PointPass{};
        RenderPipelineFeatureDebugState PostProcessPass{};
        RenderPipelineFeatureDebugState DebugViewPass{};
        RenderPipelineFeatureDebugState ImGuiPass{};
    };

    class RenderPipeline
    {
    public:
        virtual ~RenderPipeline() = default;

        virtual void Initialize(RHI::VulkanDevice& device,
                                RHI::DescriptorAllocator& descriptorPool,
                                RHI::DescriptorLayout& globalLayout,
                                const ShaderRegistry& shaderRegistry,
                                PipelineLibrary& pipelineLibrary) = 0;

        virtual void Shutdown() {}
        [[nodiscard]] virtual FrameRecipe BuildFrameRecipe(const RenderPassContext&) const { return {}; }
        virtual void SetupFrame(RenderPassContext& ctx) = 0;
        virtual void OnResize(uint32_t width, uint32_t height) { (void)width; (void)height; }
        virtual void PostCompile(uint32_t frameIndex,
                                 std::span<const RenderGraphDebugImage> debugImages,
                                 std::span<const RenderGraphDebugPass> debugPasses)
        {
            (void)frameIndex;
            (void)debugImages;
            (void)debugPasses;
        }

        virtual Passes::SelectionOutlineSettings* GetSelectionOutlineSettings() { return nullptr; }
        virtual Passes::PostProcessSettings* GetPostProcessSettings() { return nullptr; }
        virtual const Passes::HistogramReadback* GetHistogramReadback() const { return nullptr; }
        [[nodiscard]] virtual RenderPipelineDebugState GetDebugState() const { return {}; }
        [[nodiscard]] virtual const Passes::SelectionOutlineDebugState* GetSelectionOutlineDebugState() const { return nullptr; }
        [[nodiscard]] virtual const Passes::PostProcessDebugState* GetPostProcessDebugState() const { return nullptr; }
    };

    // ---------------------------------------------------------------------
    // Render Graph Validation
    // ---------------------------------------------------------------------

    enum class RenderGraphValidationSeverity : uint8_t
    {
        Warning,
        Error,
    };

    struct RenderGraphValidationDiagnostic
    {
        RenderGraphValidationSeverity Severity = RenderGraphValidationSeverity::Warning;
        std::string Message{};
    };

    struct RenderGraphValidationResult
    {
        std::vector<RenderGraphValidationDiagnostic> Diagnostics{};

        [[nodiscard]] bool HasErrors() const
        {
            for (const auto& d : Diagnostics)
            {
                if (d.Severity == RenderGraphValidationSeverity::Error)
                    return true;
            }
            return false;
        }

        [[nodiscard]] uint32_t ErrorCount() const
        {
            uint32_t count = 0;
            for (const auto& d : Diagnostics)
            {
                if (d.Severity == RenderGraphValidationSeverity::Error)
                    ++count;
            }
            return count;
        }

        [[nodiscard]] uint32_t WarningCount() const
        {
            uint32_t count = 0;
            for (const auto& d : Diagnostics)
            {
                if (d.Severity == RenderGraphValidationSeverity::Warning)
                    ++count;
            }
            return count;
        }
    };

    // -------------------------------------------------------------------------
    // Imported-resource write policy
    // -------------------------------------------------------------------------
    // Defines which passes are authorized to write to an imported resource.
    // Used by ValidateCompiledGraph to enforce the producer/consumer contract.
    struct ImportedResourceWritePolicy
    {
        Core::Hash::StringID ResourceName{};
        // Pass name that is the sole authorized writer. Empty means any pass
        // may write (no restriction beyond the standard validation).
        std::string AuthorizedWriter{};
    };

    // Returns the default write policies for the engine's imported resources.
    [[nodiscard]] inline std::vector<ImportedResourceWritePolicy> GetDefaultImportedWritePolicies()
    {
        return {
            // Backbuffer: only Present.LDR may write.
            {Core::Hash::StringID{"Backbuffer"}, "Present.LDR"},
            // SceneDepth: imported but written by scene passes (Picking,
            // MeshPass, Lines, Points). No single-writer restriction — scene
            // depth is shared among geometry passes by design. Leave
            // AuthorizedWriter empty to allow multiple scene pass writers.
        };
    }

}
