module;

#include <unordered_map>
#include <memory>
#include <array>
#include <cstdint>
#include <span>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

export module Graphics.RenderPipeline;

import Graphics.Camera;
import Graphics.RenderGraph;
import Graphics.GPUScene;
import Graphics.MaterialRegistry;
import Graphics.Geometry;
import Geometry.Handle;
import Geometry.Frustum;
import Geometry.HalfedgeMesh;
import Geometry.Overlap;
import Geometry.PointCloudUtils;
import Geometry.Sphere;
import Graphics.ShaderRegistry;
import Graphics.PipelineLibrary;
import Graphics.DebugDraw;
import Graphics.Passes.SelectionOutlineSettings;
import Graphics.Passes.PostProcessSettings;
import Core.Hash;
import Core.Assets;
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
        ShadowAtlas,
        Count,
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
        case ShadowAtlas:
            return {resource, "ShadowAtlas"_id, VK_FORMAT_UNDEFINED, RenderResourceFormatSource::Depth,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, RenderResourceLifetime::FrameTransient, true};
        case Count:
            break;
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
                 RenderResource::ShadowAtlas,
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
        bool DepthPrepass = false;
        bool EntityId = false;
        bool PrimitiveId = false;
        bool Normals = false;
        bool MaterialChannels = false;
        bool Selection = false;
        bool Post = false;
        bool DebugVisualization = false;
        bool SceneColorLDR = false;
        bool Shadows = false;
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
            case ShadowAtlas: return Shadows;
            case Count: return false;
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

    // -----------------------------------------------------------------------
    // Light / Environment Packet — immutable per-frame scene lighting state.
    // -----------------------------------------------------------------------
    // Extracted once per frame and consumed by all lit render passes (forward
    // surface, deferred composition, point, line).  Values match the previous
    // hardcoded shader constants as defaults so existing visuals are unchanged.
    struct ShadowParams
    {
        static constexpr uint32_t MaxCascades = 4;

        bool Enabled = false;
        uint32_t CascadeCount = MaxCascades;
        std::array<float, MaxCascades> CascadeSplits{0.10f, 0.25f, 0.55f, 1.00f};
        float DepthBias = 0.0015f;
        float NormalBias = 0.0025f;
        float PcfFilterRadius = 1.5f;
        float SplitLambda = 0.85f;
    };

    // Practical split scheme for CSM:
    // d_i = λ * d_log + (1-λ) * d_uniform, normalized into [0, 1]
    // where i in [1, cascadeCount].
    [[nodiscard]] std::array<float, ShadowParams::MaxCascades> ComputeCascadeSplitDistances(
        float nearPlane,
        float farPlane,
        uint32_t cascadeCount,
        float splitLambda);

    struct LightEnvironmentPacket
    {
        // Directional light (single scene light for now).
        glm::vec3 LightDirection = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
        float     LightIntensity = 1.0f;
        glm::vec3 LightColor     = glm::vec3(1.0f, 1.0f, 1.0f);
        float     _pad0          = 0.0f;

        // Ambient / environment.
        glm::vec3 AmbientColor   = glm::vec3(1.0f, 1.0f, 1.0f);
        float     AmbientIntensity = 0.1f;

        // Shadow defaults chosen for stable cascaded-shadow-map bootstrapping.
        ShadowParams Shadows{};
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

    struct SelectionOutlinePacket
    {
        std::vector<uint32_t> SelectedPickIds{};
        uint32_t HoveredPickId = 0;
    };

    struct SurfaceCentroidPacketEntry
    {
        glm::vec3 Position{0.0f};
        uint32_t PackedColor = 0;
    };

    struct SurfaceDrawPacket
    {
        Geometry::GeometryHandle Geometry{};
        std::vector<uint32_t> FaceColors{};
        std::vector<uint32_t> VertexColors{};
        std::vector<uint32_t> VertexLabels{};
        std::vector<SurfaceCentroidPacketEntry> Centroids{};
        bool UseNearestVertexColors = false;
    };

    struct LineDrawPacket
    {
        Geometry::GeometryHandle Geometry{};
        Geometry::GeometryHandle EdgeView{};
        glm::mat4 WorldMatrix{1.0f};
        glm::vec4 Color{0.85f, 0.85f, 0.85f, 1.0f};
        float Width = 1.5f;
        bool Overlay = false;
        uint32_t EdgeCount = 0;
        uint32_t EntityKey = 0;
        std::vector<uint32_t> EdgeColors{};

        // Local-space bounding sphere (center.xyz, radius w).
        // Resolved from GeometryPool during render preparation so the
        // draw packet is self-contained for CPU frustum culling.
        glm::vec4 LocalBoundingSphere{0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct PointDrawPacket
    {
        Geometry::GeometryHandle Geometry{};
        glm::mat4 WorldMatrix{1.0f};
        glm::vec4 Color{1.0f, 0.6f, 0.0f, 1.0f};
        float Size = 0.008f;
        float SizeMultiplier = 1.0f;
        Geometry::PointCloud::RenderMode Mode = Geometry::PointCloud::RenderMode::FlatDisc;
        bool HasPerPointNormals = false;
        uint32_t EntityKey = 0;
        std::vector<uint32_t> Colors{};
        std::vector<float> Radii{};

        // Local-space bounding sphere (center.xyz, radius w).
        // Resolved from GeometryPool during render preparation so the
        // draw packet is self-contained for CPU frustum culling.
        glm::vec4 LocalBoundingSphere{0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct HtexPatchPreviewPacket
    {
        uint32_t SourceEntityId = 0;
        std::optional<Geometry::Halfedge::Mesh> Mesh{};
        uint64_t KMeansResultRevision = 0;
        std::vector<glm::vec3> KMeansCentroids{};

        [[nodiscard]] bool IsValid() const
        {
            return Mesh.has_value();
        }
    };

    // -----------------------------------------------------------------------
    // Editor Overlay Packet — immutable per-frame UI/editor overlay state.
    // -----------------------------------------------------------------------
    // Extracted once per frame to decouple ImGui draw-data generation from
    // render-graph recording.  The ImGuiPass checks HasDrawData before
    // issuing ImGui_ImplVulkan_RenderDrawData.
    struct EditorOverlayPacket
    {
        // True when GUI::DrawGUI() ran this frame and ImGui draw data is ready.
        bool HasDrawData = false;
    };

    // ---------------------------------------------------------------------
    // Extraction-time snapshots for interaction state
    // ---------------------------------------------------------------------
    // Immutable snapshot of the pending pick request.
    struct PickRequestSnapshot
    {
        bool Pending = false;
        uint32_t X = 0;
        uint32_t Y = 0;
    };

    // Immutable snapshot of the debug-view state.
    struct DebugViewSnapshot
    {
        bool Enabled = false;
        bool ShowInViewport = false;
        bool DisableCulling = false;
        Core::Hash::StringID SelectedResource = GetRenderResourceName(RenderResource::EntityId);
        float DepthNear = 0.1f;
        float DepthFar = 1000.0f;
    };

    // Immutable snapshot of retained GPUScene state at extraction time.
    struct GpuSceneSnapshot
    {
        bool Available = false;
        uint32_t ActiveCountApprox = 0;
    };

    // -----------------------------------------------------------------------
    // CulledDrawList — CPU frustum-culled draw packet indices (B1).
    // -----------------------------------------------------------------------
    // Produced by CullDrawPackets() during render preparation, consumed by
    // Line/PointPass instead of per-pass inline frustum culling.  Surface
    // packets are excluded because SurfacePass uses GPU-driven culling via
    // GPUScene (compute shader).
    //
    // When Active is true, passes iterate only the visible indices rather
    // than the full draw packet spans.  The CulledDrawList is valid for the
    // lifetime of the BuildGraphInput / RenderWorld it was built from.
    struct CulledDrawList
    {
        std::vector<uint32_t> VisibleLineIndices{};
        std::vector<uint32_t> VisiblePointIndices{};

        // Pre-cull statistics for telemetry/debugging.
        uint32_t TotalLineCount = 0;
        uint32_t TotalPointCount = 0;
        uint32_t CulledLineCount = 0;
        uint32_t CulledPointCount = 0;

        // True once CullDrawPackets has run (even if culling was disabled —
        // in that case all indices are present).
        bool Active = false;
    };

    // ---------------------------------------------------------------------
    // Frame Context
    // ---------------------------------------------------------------------
    struct RenderPassContext
    {
        RenderGraph& Graph;
        RenderBlackboard& Blackboard;

        const Core::Assets::AssetManager& AssetManager;
        GeometryPool& GeometryStorage;
        MaterialRegistry& MaterialRegistry;

        // Retained-mode GPU scene (owned by RenderDriver).
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

        PickRequestSnapshot PickRequest;
        DebugViewSnapshot Debug;

        std::span<const RenderGraphDebugImage> PrevFrameDebugImages{};
        std::span<const RenderGraphDebugPass> PrevFrameDebugPasses{};

        glm::mat4 CameraView{1.0f};
        glm::mat4 CameraProj{1.0f};
        LightEnvironmentPacket Lighting{};

        RHI::VulkanBuffer* PickReadbackBuffer = nullptr;
        bool HasSelectionWork = false;
        SelectionOutlinePacket SelectionOutline{};

        std::span<const PickingSurfacePacket> PickingSurfacePackets{};
        std::span<const PickingLinePacket> PickingLinePackets{};
        std::span<const PickingPointPacket> PickingPointPackets{};
        std::span<const SurfaceDrawPacket> SurfaceDrawPackets{};
        std::span<const LineDrawPacket> LineDrawPackets{};
        std::span<const PointDrawPacket> PointDrawPackets{};
        CulledDrawList CulledDraws{};
        const HtexPatchPreviewPacket* HtexPatchPreview = nullptr;

        // Debug draw snapshot — immutable copies extracted from DebugDraw accumulator.
        std::span<const DebugDraw::LineSegment> DebugDrawLines{};
        std::span<const DebugDraw::LineSegment> DebugDrawOverlayLines{};
        std::span<const DebugDraw::PointMarker> DebugDrawPoints{};
        std::span<const DebugDraw::TriangleVertex> DebugDrawTriangles{};

        // Editor overlay state (ImGui draw-data readiness).
        EditorOverlayPacket EditorOverlay{};
    };

    [[nodiscard]] inline RGTextureDesc BuildRenderResourceTextureDesc(RenderResource resource,
                                                                      const RenderPassContext& ctx)
    {
        const auto def = GetRenderResourceDefinition(resource);
        RGTextureDesc desc{};
        desc.Width = ctx.Resolution.width;
        desc.Height = ctx.Resolution.height;
        if (resource == RenderResource::ShadowAtlas)
        {
            constexpr uint32_t kShadowCascadeResolution = 2048u;
            desc.Width = kShadowCascadeResolution * ShadowParams::MaxCascades;
            desc.Height = kShadowCascadeResolution;
        }
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
        RenderPipelineFeatureDebugState DepthPrepass{};
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

    // -----------------------------------------------------------------------
    // BuildGraphInput — structured render preparation data for BuildGraph
    // -----------------------------------------------------------------------
    // Consolidates all per-frame extracted render state into a single struct,
    // replacing the 18-parameter BuildGraph signature. This serves as the
    // structured intermediate between the extraction stage (RenderWorld in
    // Runtime) and the graph construction stage (BuildGraph in Graphics).
    //
    // All spans are non-owning views into the owning RenderWorld. The
    // BuildGraphInput must not outlive the RenderWorld it was built from.
    struct BuildGraphInput
    {
        // Camera / view
        CameraComponent Camera{};
        LightEnvironmentPacket Lighting{};

        // Selection
        bool HasSelectionWork = false;
        SelectionOutlinePacket SelectionOutline{};

        // Interaction state (extraction-time snapshots)
        PickRequestSnapshot PickRequest{};
        DebugViewSnapshot DebugView{};

        // Picking draw packets
        std::span<const PickingSurfacePacket> SurfacePicking{};
        std::span<const PickingLinePacket> LinePicking{};
        std::span<const PickingPointPacket> PointPicking{};

        // Scene draw packets
        std::span<const SurfaceDrawPacket> SurfaceDraws{};
        std::span<const LineDrawPacket> LineDraws{};
        std::span<const PointDrawPacket> PointDraws{};

        // CPU-culled draw list (B1).  When Active, Line/PointPass iterate
        // only the visible indices rather than the full spans above.
        CulledDrawList CulledDraws{};

        // Optional preview data
        const HtexPatchPreviewPacket* HtexPatchPreview = nullptr;

        // Debug draw snapshots (immutable copies from DebugDraw accumulator)
        std::span<const DebugDraw::LineSegment> DebugDrawLines{};
        std::span<const DebugDraw::LineSegment> DebugDrawOverlayLines{};
        std::span<const DebugDraw::PointMarker> DebugDrawPoints{};
        std::span<const DebugDraw::TriangleVertex> DebugDrawTriangles{};

        // Editor overlay state
        EditorOverlayPacket EditorOverlay{};
    };

    // -----------------------------------------------------------------------
    // Render Preparation Functions (B1)
    // -----------------------------------------------------------------------

    // Resolve bounding spheres from GeometryPool into draw packet fields.
    // Must be called before CullDrawPackets so packets are self-contained.
    void ResolveDrawPacketBounds(
        std::span<LineDrawPacket> lines,
        std::span<PointDrawPacket> points,
        const GeometryPool& geometryStorage);

    // CPU frustum cull Line/Point draw packets.
    // Returns a CulledDrawList with indices of visible packets.
    // Surface packets are excluded (SurfacePass uses GPU culling).
    [[nodiscard]] CulledDrawList CullDrawPackets(
        std::span<const LineDrawPacket> lines,
        std::span<const PointDrawPacket> points,
        const glm::mat4& cameraProj,
        const glm::mat4& cameraView,
        bool cullingEnabled);

}
