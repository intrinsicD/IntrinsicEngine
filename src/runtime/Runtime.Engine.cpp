module;

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.Engine;

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
import Extrinsic.Backends.Vulkan;
#endif
import Extrinsic.Backends.Null;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.Core.IOBackend;
import Extrinsic.Core.Tasks;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.AssetGeometryIO;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.AssetModelTextureIO;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Runtime.EcsSystemBundle;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTextureIOBridge;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Platform.Input;
import Geometry.Graph.IO;
import Geometry.Graph;
import Geometry.AABB;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.Mesh.Conversion;
import Geometry.MeshSoup;
import Geometry.OBB;
import Geometry.PointCloud;
import Geometry.PointCloud.IO;
import Geometry.Properties;
import Geometry.Sphere;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr double kIdleSleepSeconds = 0.016; // ~60 Hz event wake
        constexpr int kGizmoMouseButton = 0;
        constexpr int kSelectionMouseButton = 0;

        struct RuntimeFrameContext
        {
            double FrameDeltaSeconds{0.0};
            double FixedStepAlpha{0.0};
            std::uint64_t FrameIndex{0};
            Graphics::RenderFrameInput RenderInput{};
            RuntimeRenderExtractionStats ExtractionStats{};
            std::uint32_t PooledFrontSlot{RenderWorldPool::kInvalidSlot};
        };

        [[nodiscard]] Graphics::Components::RenderPoints::RenderType ToRenderPointType(
            const MeshVertexViewRenderMode mode) noexcept
        {
            namespace G = Graphics::Components;
            switch (mode)
            {
            case MeshVertexViewRenderMode::FlatCircle:
                return G::RenderPoints::RenderType::Flat;
            case MeshVertexViewRenderMode::SurfaceAlignedCircle:
                return G::RenderPoints::RenderType::Surfel;
            case MeshVertexViewRenderMode::ImpostorSphere:
                return G::RenderPoints::RenderType::Sphere;
            }
            return G::RenderPoints::RenderType::Sphere;
        }

        [[nodiscard]] MeshVertexViewRenderMode ToMeshVertexViewRenderMode(
            const Graphics::Components::RenderPoints::RenderType type) noexcept
        {
            namespace G = Graphics::Components;
            switch (type)
            {
            case G::RenderPoints::RenderType::Flat:
                return MeshVertexViewRenderMode::FlatCircle;
            case G::RenderPoints::RenderType::Surfel:
                return MeshVertexViewRenderMode::SurfaceAlignedCircle;
            case G::RenderPoints::RenderType::Sphere:
                return MeshVertexViewRenderMode::ImpostorSphere;
            }
            return MeshVertexViewRenderMode::ImpostorSphere;
        }

        // RUNTIME-070: runtime-baked fallback texture bytes for GpuAssetCache.
        // A 4×4 RGBA8_UNORM magenta-and-black checkerboard repeated from a 2×2
        // base pattern. The cache never reads files; runtime owns the bytes.
        // Layout: row-major, top-left origin, RGBA8 with alpha 0xFF so the
        // sampled colour is visually unambiguous when material code observes
        // `UsedFallback = true`.
        consteval std::array<std::byte, 4 * 4 * 4> MakeFallbackTextureBytes() noexcept
        {
            std::array<std::byte, 4 * 4 * 4> bytes{};
            for (std::size_t y = 0; y < 4; ++y)
            {
                for (std::size_t x = 0; x < 4; ++x)
                {
                    const bool magenta = (((x / 2) ^ (y / 2)) & 1u) == 0u;
                    const std::size_t base = (y * 4 + x) * 4;
                    bytes[base + 0] = static_cast<std::byte>(magenta ? 0xFF : 0x00);
                    bytes[base + 1] = static_cast<std::byte>(0x00);
                    bytes[base + 2] = static_cast<std::byte>(magenta ? 0xFF : 0x00);
                    bytes[base + 3] = static_cast<std::byte>(0xFF);
                }
            }
            return bytes;
        }

        constexpr auto kFallbackTextureBytes = MakeFallbackTextureBytes();

        [[nodiscard]] Graphics::GpuTextureFallbackDesc BuildFallbackTextureDesc() noexcept
        {
            Graphics::GpuTextureFallbackDesc desc{};
            desc.Bytes = std::span<const std::byte>(kFallbackTextureBytes);
            desc.Desc.Width = 4;
            desc.Desc.Height = 4;
            desc.Desc.MipLevels = 1;
            desc.Desc.Fmt = RHI::Format::RGBA8_UNORM;
            desc.Desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
            desc.Desc.DebugName = "gpu-asset-fallback-texture";
            desc.SamplerDesc.MagFilter = RHI::FilterMode::Nearest;
            desc.SamplerDesc.MinFilter = RHI::FilterMode::Nearest;
            desc.SamplerDesc.MipFilter = RHI::MipmapMode::Nearest;
            desc.SamplerDesc.AddressU = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.AddressV = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.AddressW = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.DebugName = "gpu-asset-fallback-sampler";
            return desc;
        }

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
        constexpr bool kPromotedVulkanAvailable = true;
#else
        constexpr bool kPromotedVulkanAvailable = false;
#endif

        std::unique_ptr<RHI::IDevice> CreateDevice(
            const Core::Config::RenderConfig& config)
        {
            const RuntimeDeviceSelection selection = SelectRuntimeDeviceBackend(
                config,
                kPromotedVulkanAvailable);
            if (selection.UsePromotedVulkanDevice)
            {
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
                Core::Log::Warn("[Runtime] Promoted Vulkan device selected; backend remains fail-closed until the first clean default-recipe validation promotes it.");
                return Backends::Vulkan::CreateVulkanDevice();
#endif
            }

            if (config.EnablePromotedVulkanDevice && !kPromotedVulkanAvailable)
            {
                Core::Log::Warn("[Runtime] Promoted Vulkan device requested but not compiled into this build; using Null device fallback.");
            }

            // Vulkan execution is opt-in during GRAPHICS-018. The default path
            // routes through the Null stub so IDevice::IsOperational() remains
            // false and resource managers surface DeviceNotOperational rather
            // than faking GPU work.
            return Backends::Null::CreateNullDevice();
        }

        [[nodiscard]] std::uint64_t Delta(
            const std::uint64_t after,
            const std::uint64_t before) noexcept
        {
            return after >= before ? after - before : 0u;
        }

        void DrainAssetImportEvents(Assets::AssetService& service)
        {
            if (Core::Tasks::Scheduler::IsInitialized())
            {
                Core::Tasks::Scheduler::WaitForAll();
            }
            service.Tick();
        }

        [[nodiscard]] Core::ErrorCode NormalizeImportError(
            const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::Success
                ? Core::ErrorCode::Unknown
                : error;
        }

        [[nodiscard]] bool IsTextureUploadDeferral(
            const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::DeviceNotOperational
                || error == Core::ErrorCode::ResourceBusy;
        }

        [[nodiscard]] std::string FileNameFromPath(const std::string_view path)
        {
            if (path.empty())
            {
                return {};
            }

            const std::size_t slash = path.find_last_of("/\\");
            const std::size_t begin = slash == std::string_view::npos
                ? 0u
                : slash + 1u;
            if (begin >= path.size())
            {
                return {};
            }
            return std::string(path.substr(begin));
        }

        [[nodiscard]] std::string GeometryEntityName(
            const std::string_view path,
            const Assets::AssetPayloadKind kind)
        {
            std::string name = FileNameFromPath(path);
            if (!name.empty())
            {
                return name;
            }
            name = "Imported";
            name += Assets::DebugNameForAssetPayloadKind(kind);
            return name;
        }

        [[nodiscard]] Graphics::Components::VisualizationConfig
            ImportedGeometryVisualization() noexcept
        {
            Graphics::Components::VisualizationConfig visualization{};
            visualization.Source =
                Graphics::Components::VisualizationConfig::ColorSource::UniformColor;
            visualization.Color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
            return visualization;
        }

        [[nodiscard]] bool CanUseDisconnectedRenderableFallback(
            const Geometry::Mesh::Conversion::ToHalfedgeMeshResult& converted) noexcept
        {
            bool hasRenderableTopologyFailure = false;
            for (const Geometry::Mesh::Conversion::ConversionDiagnostic& diagnostic :
                 converted.Diagnostics)
            {
                if (diagnostic.Severity !=
                    Geometry::MeshSoup::ValidationSeverity::Error)
                {
                    continue;
                }

                if (diagnostic.Kind ==
                    Geometry::Mesh::Conversion::ConversionDiagnosticKind::AddFaceFailed)
                {
                    hasRenderableTopologyFailure = true;
                    continue;
                }

                if (diagnostic.Kind !=
                    Geometry::Mesh::Conversion::ConversionDiagnosticKind::ValidationDiagnostic)
                {
                    return false;
                }

                if (diagnostic.ValidationKind ==
                        Geometry::MeshSoup::ValidationDiagnosticKind::NonManifoldEdge ||
                    diagnostic.ValidationKind ==
                        Geometry::MeshSoup::ValidationDiagnosticKind::InconsistentWinding)
                {
                    hasRenderableTopologyFailure = true;
                    continue;
                }

                return false;
            }
            return hasRenderableTopologyFailure;
        }

        [[nodiscard]] std::optional<Geometry::HalfedgeMesh::Mesh>
            BuildDisconnectedRenderableMesh(
                const std::vector<glm::vec3>& positions,
                const std::vector<std::vector<std::uint32_t>>& faces)
        {
            if (positions.empty() || faces.empty())
            {
                return std::nullopt;
            }

            Geometry::HalfedgeMesh::Mesh mesh;
            std::vector<Geometry::VertexHandle> faceVertices;
            for (const std::vector<std::uint32_t>& face : faces)
            {
                if (face.size() < 3u)
                {
                    return std::nullopt;
                }

                faceVertices.clear();
                faceVertices.reserve(face.size());
                for (const std::uint32_t index : face)
                {
                    if (index >= positions.size())
                    {
                        return std::nullopt;
                    }
                    faceVertices.push_back(mesh.AddVertex(positions[index]));
                }

                if (!mesh.AddFace(faceVertices).has_value())
                {
                    return std::nullopt;
                }
            }

            return mesh;
        }

        [[nodiscard]] Core::Expected<Geometry::HalfedgeMesh::Mesh> BuildHalfedgeMesh(
            const Geometry::MeshIO::MeshIOResult& meshPayload)
        {
            const auto positions = meshPayload.Vertices.Get<glm::vec3>("v:point");
            if (!positions || positions.Vector().empty())
            {
                return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                    Core::ErrorCode::AssetInvalidData);
            }

            const auto faces =
                meshPayload.Faces.Get<std::vector<std::uint32_t>>("f:vertices");
            if (!faces || faces.Vector().empty())
            {
                return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                    Core::ErrorCode::AssetInvalidData);
            }

            Geometry::MeshSoup::IndexedMesh soup{};
            for (const glm::vec3& position : positions.Vector())
            {
                static_cast<void>(soup.AddVertex(position));
            }

            for (const std::vector<std::uint32_t>& face : faces.Vector())
            {
                if (face.size() < 3u)
                {
                    return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                        Core::ErrorCode::InvalidFormat);
                }
                for (const std::uint32_t index : face)
                {
                    if (index >= soup.VertexCount())
                    {
                        return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                            Core::ErrorCode::OutOfRange);
                    }
                }
                static_cast<void>(soup.AddFace(face));
            }

            auto converted = Geometry::Mesh::Conversion::ToHalfedgeMesh(soup);
            if (!converted.Succeeded())
            {
                if (CanUseDisconnectedRenderableFallback(converted))
                {
                    if (std::optional<Geometry::HalfedgeMesh::Mesh> fallback =
                            BuildDisconnectedRenderableMesh(
                                positions.Vector(),
                                faces.Vector()))
                    {
                        return std::move(*fallback);
                    }
                }
                return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                    Core::ErrorCode::InvalidFormat);
            }
            return std::move(converted.Mesh);
        }

        struct DecodedMeshImport
        {
            Geometry::MeshIO::MeshIOResult Payload{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
        };

        struct DecodedGraphImport
        {
            Geometry::GraphIO::GraphIOResult Payload{};
        };

        struct DecodedPointCloudImport
        {
            Geometry::PointCloudIO::PointCloudIOResult Payload{};
        };

        using DecodedGeometryImportPayload =
            std::variant<DecodedMeshImport, DecodedGraphImport, DecodedPointCloudImport>;

        struct DecodedGeometryImport
        {
            std::string Path{};
            Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
            DecodedGeometryImportPayload Payload{};
        };

        struct DroppedGeometryImportState
        {
            RuntimeAssetImportRequest Request{};
            std::optional<DecodedGeometryImport> Decoded{};
            Core::ErrorCode Error{Core::ErrorCode::Unknown};
        };

        struct GeometryImportBounds
        {
            glm::vec3 Min{0.0f};
            glm::vec3 Max{0.0f};
            bool Valid{false};
        };

        struct MaterializedGeometryImport
        {
            RuntimeAssetImportResult Result{};
            std::optional<GeometryImportBounds> Bounds{};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        };

        [[nodiscard]] bool IsFinitePosition(const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        void AccumulateBounds(GeometryImportBounds& bounds,
                              const glm::vec3& position) noexcept
        {
            if (!IsFinitePosition(position))
                return;

            if (!bounds.Valid)
            {
                bounds.Min = position;
                bounds.Max = position;
                bounds.Valid = true;
                return;
            }

            bounds.Min = glm::min(bounds.Min, position);
            bounds.Max = glm::max(bounds.Max, position);
        }

        [[nodiscard]] std::optional<GeometryImportBounds> BoundsFromHalfedgeMesh(
            const Geometry::HalfedgeMesh::Mesh& mesh) noexcept
        {
            GeometryImportBounds bounds{};
            for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vertex{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(vertex) || mesh.IsDeleted(vertex))
                    continue;
                AccumulateBounds(bounds, mesh.Position(vertex));
            }
            if (!bounds.Valid)
                return std::nullopt;
            return bounds;
        }

        [[nodiscard]] std::optional<GeometryImportBounds> BoundsFromGraph(
            const Geometry::Graph::Graph& graph) noexcept
        {
            GeometryImportBounds bounds{};
            for (std::size_t i = 0u; i < graph.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vertex{
                    static_cast<Geometry::PropertyIndex>(i)};
                if (!graph.IsValid(vertex) || graph.IsDeleted(vertex))
                    continue;
                AccumulateBounds(bounds, graph.VertexPosition(vertex));
            }
            if (!bounds.Valid)
                return std::nullopt;
            return bounds;
        }

        [[nodiscard]] std::optional<GeometryImportBounds> BoundsFromCloud(
            const Geometry::PointCloud::Cloud& cloud) noexcept
        {
            GeometryImportBounds bounds{};
            for (const glm::vec3& position : cloud.Positions())
            {
                AccumulateBounds(bounds, position);
            }
            if (!bounds.Valid)
                return std::nullopt;
            return bounds;
        }

        [[nodiscard]] float RadiusForBounds(
            const GeometryImportBounds& bounds) noexcept
        {
            constexpr float kMinimumVisibleRadius = 0.05f;
            const float radius = 0.5f * glm::length(bounds.Max - bounds.Min);
            if (!std::isfinite(radius) || radius <= 0.0f)
                return kMinimumVisibleRadius;
            return std::max(kMinimumVisibleRadius, radius);
        }

        [[nodiscard]] CameraFocusTarget ToCameraFocusTarget(
            const GeometryImportBounds& bounds) noexcept
        {
            return CameraFocusTarget{
                .Center = 0.5f * (bounds.Min + bounds.Max),
                .Radius = RadiusForBounds(bounds),
            };
        }

        void AttachGeometryBounds(entt::registry& raw,
                                  const ECS::EntityHandle entity,
                                  const GeometryImportBounds& bounds)
        {
            if (!bounds.Valid)
                return;

            const glm::vec3 center = 0.5f * (bounds.Min + bounds.Max);
            const glm::vec3 extents = 0.5f * (bounds.Max - bounds.Min);
            const float radius = RadiusForBounds(bounds);

            ECS::Components::Culling::Local::Bounds local{};
            local.LocalBoundingAABB.Min = bounds.Min;
            local.LocalBoundingAABB.Max = bounds.Max;
            local.LocalBoundingSphere.Center = center;
            local.LocalBoundingSphere.Radius = radius;
            raw.emplace_or_replace<ECS::Components::Culling::Local::Bounds>(
                entity,
                local);

            ECS::Components::Culling::World::Bounds world{};
            world.WorldBoundingOBB.Center = center;
            world.WorldBoundingOBB.Extents = extents;
            world.WorldBoundingOBB.Rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
            world.WorldBoundingSphere.Center = center;
            world.WorldBoundingSphere.Radius = radius;
            raw.emplace_or_replace<ECS::Components::Culling::World::Bounds>(
                entity,
                world);
        }

        void FocusMainCameraOnImportedGeometry(
            CameraControllerRegistry& cameraControllers,
            const Core::Config::CameraControllerKind controllerKind,
            const bool cameraEnabled,
            const std::optional<GeometryImportBounds>& bounds)
        {
            if (!cameraEnabled || !bounds.has_value() || !bounds->Valid)
                return;

            ICameraController* controller =
                cameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            if (controller == nullptr)
            {
                cameraControllers.Register(
                    CameraControllerSlot::Main,
                    CreateCameraController(controllerKind));
                controller = cameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            }
            if (controller == nullptr)
                return;

            controller->Focus(ToCameraFocusTarget(*bounds));
            cameraControllers.MarkCameraTransition(CameraControllerSlot::Main);
        }

        [[nodiscard]] Core::Expected<DecodedGeometryImport> DecodeGeometryImport(
            const RuntimeAssetImportRequest& request)
        {
            auto route = Assets::ResolveAssetImportRoute(
                request.Path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
            if (!route.has_value())
            {
                return Core::Err<DecodedGeometryImport>(route.error());
            }
            if (!Assets::IsGeometryPayloadKind(route->PayloadKind))
            {
                return Core::Err<DecodedGeometryImport>(
                    Core::ErrorCode::AssetUnsupportedFormat);
            }

            Assets::AssetGeometryIOBridge bridge;
            if (Core::Result registered = RegisterPromotedGeometryIOCallbacks(bridge);
                !registered.has_value())
            {
                return Core::Err<DecodedGeometryImport>(registered.error());
            }

            auto decoded = bridge.Import(
                request.Path,
                Assets::AssetImportHint{.PayloadKind = route->PayloadKind});
            if (!decoded.has_value())
            {
                return Core::Err<DecodedGeometryImport>(decoded.error());
            }

            switch (route->PayloadKind)
            {
            case Assets::AssetPayloadKind::Mesh:
            {
                auto meshPayload =
                    decoded->Read<Geometry::MeshIO::MeshIOResult>();
                if (!meshPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        meshPayload.error());
                }

                auto mesh = BuildHalfedgeMesh(**meshPayload);
                if (!mesh.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(mesh.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedMeshImport{
                        .Payload = **meshPayload,
                        .Mesh = std::move(*mesh),
                    },
                };
            }
            case Assets::AssetPayloadKind::Graph:
            {
                auto graphPayload =
                    decoded->Read<Geometry::GraphIO::GraphIOResult>();
                if (!graphPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        graphPayload.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedGraphImport{.Payload = **graphPayload},
                };
            }
            case Assets::AssetPayloadKind::PointCloud:
            {
                auto cloudPayload =
                    decoded->Read<Geometry::PointCloudIO::PointCloudIOResult>();
                if (!cloudPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        cloudPayload.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedPointCloudImport{.Payload = **cloudPayload},
                };
            }
            default:
                break;
            }

            return Core::Err<DecodedGeometryImport>(
                Core::ErrorCode::AssetUnsupportedFormat);
        }

        [[nodiscard]] Core::Expected<MaterializedGeometryImport>
        MaterializeDecodedGeometryImport(
            Assets::AssetService& assetService,
            ECS::Scene::Registry& scene,
            const DecodedGeometryImport& decoded)
        {
            return std::visit(
                [&](const auto& payload) -> Core::Expected<MaterializedGeometryImport>
                {
                    using PayloadT = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<PayloadT, DecodedMeshImport>)
                    {
                        auto asset =
                            assetService.Load<Geometry::MeshIO::MeshIOResult>(
                                decoded.Path,
                                [&payload](std::string_view,
                                           Assets::AssetId)
                                    -> Core::Expected<Geometry::MeshIO::MeshIOResult>
                                {
                                    return payload.Payload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        DrainAssetImportEvents(assetService);

                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        raw.emplace<ECS::Components::Selection::SelectableTag>(entity);
                        raw.emplace<Graphics::Components::RenderSurface>(
                            entity,
                            Graphics::Components::RenderSurface{
                                .Domain = Graphics::Components::RenderSurface::SourceDomain::Vertex,
                            });
                        raw.emplace<Graphics::Components::VisualizationConfig>(
                            entity,
                            ImportedGeometryVisualization());
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromHalfedgeMesh(payload.Mesh);
                        if (bounds.has_value())
                        {
                            AttachGeometryBounds(raw, entity, *bounds);
                        }
                        Geometry::HalfedgeMesh::Mesh mesh = payload.Mesh;
                        ECS::Components::GeometrySources::PopulateFromMesh(
                            raw,
                            entity,
                            mesh);

                        return MaterializedGeometryImport{
                            .Result = RuntimeAssetImportResult{
                                .Asset = *asset,
                                .PayloadKind = decoded.PayloadKind,
                                .PrimitiveEntitiesCreated = 1u,
                            },
                            .Bounds = bounds,
                            .Entity = entity,
                        };
                    }
                    else if constexpr (std::is_same_v<PayloadT, DecodedGraphImport>)
                    {
                        auto asset =
                            assetService.Load<Geometry::GraphIO::GraphIOResult>(
                                decoded.Path,
                                [&payload](std::string_view,
                                           Assets::AssetId)
                                    -> Core::Expected<Geometry::GraphIO::GraphIOResult>
                                {
                                    return payload.Payload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        DrainAssetImportEvents(assetService);

                        Geometry::Graph::Graph graph = payload.Payload.Graph;
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromGraph(graph);
                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        raw.emplace<ECS::Components::Selection::SelectableTag>(entity);
                        raw.emplace<Graphics::Components::RenderEdges>(
                            entity,
                            Graphics::Components::RenderEdges{
                                .Domain = Graphics::Components::RenderEdges::SourceDomain::Vertex,
                            });
                        raw.emplace<Graphics::Components::RenderPoints>(
                            entity,
                            Graphics::Components::RenderPoints{});
                        raw.emplace<Graphics::Components::VisualizationConfig>(
                            entity,
                            ImportedGeometryVisualization());
                        if (bounds.has_value())
                        {
                            AttachGeometryBounds(raw, entity, *bounds);
                        }
                        ECS::Components::GeometrySources::PopulateFromGraph(
                            raw,
                            entity,
                            graph);

                        return MaterializedGeometryImport{
                            .Result = RuntimeAssetImportResult{
                                .Asset = *asset,
                                .PayloadKind = decoded.PayloadKind,
                                .PrimitiveEntitiesCreated = 1u,
                            },
                            .Bounds = bounds,
                            .Entity = entity,
                        };
                    }
                    else
                    {
                        auto asset =
                            assetService.Load<Geometry::PointCloudIO::PointCloudIOResult>(
                                decoded.Path,
                                [&payload](std::string_view,
                                           Assets::AssetId)
                                    -> Core::Expected<Geometry::PointCloudIO::PointCloudIOResult>
                                {
                                    return payload.Payload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        DrainAssetImportEvents(assetService);

                        Geometry::PointCloud::Cloud cloud = payload.Payload.Cloud;
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromCloud(cloud);
                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        raw.emplace<ECS::Components::Selection::SelectableTag>(entity);
                        raw.emplace<Graphics::Components::RenderPoints>(
                            entity,
                            Graphics::Components::RenderPoints{});
                        raw.emplace<Graphics::Components::VisualizationConfig>(
                            entity,
                            ImportedGeometryVisualization());
                        if (bounds.has_value())
                        {
                            AttachGeometryBounds(raw, entity, *bounds);
                        }
                        ECS::Components::GeometrySources::PopulateFromCloud(
                            raw,
                            entity,
                            cloud);

                        return MaterializedGeometryImport{
                            .Result = RuntimeAssetImportResult{
                                .Asset = *asset,
                                .PayloadKind = decoded.PayloadKind,
                                .PrimitiveEntitiesCreated = 1u,
                            },
                            .Bounds = bounds,
                            .Entity = entity,
                        };
                    }
                },
                decoded.Payload);
        }

        [[nodiscard]] std::uint32_t ClampCursorPixel(const float value,
                                                     const std::uint32_t extent) noexcept
        {
            if (extent == 0u || !std::isfinite(value))
                return 0u;
            const float clamped = std::clamp(value, 0.0f, static_cast<float>(extent - 1u));
            return static_cast<std::uint32_t>(clamped);
        }

        [[nodiscard]] bool CursorInsideViewport(const Platform::Input::Context::XY cursor,
                                                const Core::Extent2D viewport) noexcept
        {
            return viewport.Width > 0 &&
                   viewport.Height > 0 &&
                   std::isfinite(cursor.x) &&
                   std::isfinite(cursor.y) &&
                   cursor.x >= 0.0f &&
                   cursor.y >= 0.0f &&
                   cursor.x < static_cast<float>(viewport.Width) &&
                   cursor.y < static_cast<float>(viewport.Height);
        }

        // BUG-026 — platform cursor positions are window (logical) coordinates
        // (GLFW's cursor callback), while picking/gizmo math addresses
        // framebuffer pixels. On HiDPI hosts (content scale != 1) the two
        // differ; scale by the extent ratio so the pick pixel matches what is
        // under the cursor. Degenerate extents pass the cursor through.
        [[nodiscard]] Platform::Input::Context::XY WindowToFramebufferCursor(
            const Platform::Input::Context::XY cursor,
            const Core::Extent2D windowExtent,
            const Core::Extent2D framebufferExtent) noexcept
        {
            if (windowExtent.Width <= 0 || windowExtent.Height <= 0 ||
                framebufferExtent.Width <= 0 || framebufferExtent.Height <= 0)
            {
                return cursor;
            }
            const float scaleX = static_cast<float>(framebufferExtent.Width) /
                                 static_cast<float>(windowExtent.Width);
            const float scaleY = static_cast<float>(framebufferExtent.Height) /
                                 static_cast<float>(windowExtent.Height);
            return Platform::Input::Context::XY{cursor.x * scaleX, cursor.y * scaleY};
        }

        void SubmitViewportSelectionClickForFrame(SelectionController& selection,
                                                  const Platform::Input::Context& input,
                                                  const Core::Extent2D windowExtent,
                                                  const Core::Extent2D viewport,
                                                  const bool imguiCapturesMouse,
                                                  const bool gizmoCapturesMouse) noexcept
        {
            if (imguiCapturesMouse || gizmoCapturesMouse || Core::IsEmpty(viewport) ||
                !input.IsMouseButtonJustPressed(kSelectionMouseButton))
            {
                return;
            }

            const Platform::Input::Context::XY cursor =
                WindowToFramebufferCursor(input.GetMousePosition(), windowExtent, viewport);
            if (!CursorInsideViewport(cursor, viewport))
                return;

            selection.RequestClickPick(ClampCursorPixel(cursor.x, viewport.Width),
                                       ClampCursorPixel(cursor.y, viewport.Height));
        }

        [[nodiscard]] std::uint32_t BuildGizmoModifierMask(
            const Platform::Input::Context& input) noexcept
        {
            std::uint32_t mask = 0u;
            if (input.IsKeyPressed(Platform::Input::Key::LeftShift))
                mask |= static_cast<std::uint32_t>(GizmoModifier::Snap);
            return mask;
        }

        void RebuildSelectedGizmoEntities(
            const SelectionController& selection,
            ECS::Scene::Registry& scene,
            std::vector<ECS::EntityHandle>& outSelected)
        {
            outSelected.clear();
            for (const std::uint32_t stableId : selection.SelectedStableIds())
            {
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableId);
                if (scene.IsValid(entity))
                    outSelected.push_back(entity);
            }
        }

        void DriveGizmoInteractionForFrame(
            GizmoInteraction& gizmo,
            GizmoUndoStack& undo,
            ECS::Scene::Registry& scene,
            const Platform::Input::Context& input,
            const Graphics::CameraViewInput& cameraInput,
            const Core::Extent2D windowExtent,
            const Core::Extent2D viewport,
            std::span<const ECS::EntityHandle> selected)
        {
            gizmo.SetModifierMask(BuildGizmoModifierMask(input));
            if (Core::IsEmpty(viewport))
            {
                if (gizmo.IsDragging())
                    gizmo.DragCancel(scene);
                return;
            }

            const Platform::Input::Context::XY cursor =
                WindowToFramebufferCursor(input.GetMousePosition(), windowExtent, viewport);
            const std::uint32_t pixelX = ClampCursorPixel(cursor.x, viewport.Width);
            const std::uint32_t pixelY = ClampCursorPixel(cursor.y, viewport.Height);
            const Graphics::CameraViewSnapshot camera =
                Graphics::BuildCameraViewSnapshot(
                    cameraInput,
                    viewport,
                    Graphics::PickPixelRequest{
                        .X = pixelX,
                        .Y = pixelY,
                        .Pending = true,
                    });
            if (!camera.Valid || !camera.HasPickRay)
            {
                if (!input.IsMouseButtonPressed(kGizmoMouseButton) && gizmo.IsDragging())
                    (void)gizmo.DragCommit(scene, undo);
                return;
            }

            const PickRay ray{
                .Origin = camera.PickRayOrigin,
                .Direction = camera.PickRayDirection,
            };

            if (input.IsMouseButtonJustPressed(kGizmoMouseButton))
            {
                const GizmoHitResult hit = gizmo.HitTest(
                    scene,
                    camera,
                    glm::vec2{cursor.x, cursor.y},
                    viewport,
                    selected);
                if (hit.Hit)
                    (void)gizmo.BeginDrag(scene, hit, ray, selected);
            }
            else if (input.IsMouseButtonPressed(kGizmoMouseButton) && gizmo.IsDragging())
            {
                (void)gizmo.DragTick(scene, ray);
            }
            else if (!input.IsMouseButtonPressed(kGizmoMouseButton) && gizmo.IsDragging())
            {
                (void)gizmo.DragCommit(scene, undo);
            }
        }

        // Converts frame-recorded streaming passes into persistent executor tasks.
        // Kept as compatibility bridge while call sites still populate GetStreamingGraph().
        void SubmitStreamingGraphToExecutor(Core::Dag::TaskGraph& graph, StreamingExecutor& executor)
        {
            if (graph.PassCount() == 0)
                return;

            if (auto r = graph.Compile(); !r.has_value())
            {
                Core::Log::Error("[Runtime] StreamingGraph Compile() failed: error={}",
                           static_cast<int>(r.error()));
                graph.Reset();
                return;
            }

            auto plan = graph.BuildPlan();
            if (!plan.has_value())
            {
                Core::Log::Error("[Runtime] StreamingGraph BuildPlan() failed: error={}",
                           static_cast<int>(plan.error()));
                graph.Reset();
                return;
            }

            // Convert layer order into coarse dependencies:
            // every task in batch N depends on all submitted tasks from batches < N.
            // This preserves correctness and determinism with possible over-serialization.
            std::vector<StreamingTaskHandle> priorBatches{};
            std::vector<StreamingTaskHandle> currentBatch{};
            std::uint32_t activeBatch = std::numeric_limits<std::uint32_t>::max();

            for (const auto& task : *plan)
            {
                if (task.batch != activeBatch)
                {
                    priorBatches.insert(priorBatches.end(), currentBatch.begin(), currentBatch.end());
                    currentBatch.clear();
                    activeBatch = task.batch;
                }

                auto fn = graph.TakePassExecute(task.id.Index);
                if (fn)
                {
                    auto handle = executor.Submit(StreamingTaskDesc{
                        .Name = "StreamingPass",
                        .DependsOn = priorBatches,
                        .Execute = [f = std::move(fn)]() mutable
                        {
                            f();
                            return StreamingResult{};
                        },
                    });

                    if (handle.IsValid())
                    {
                        currentBatch.push_back(handle);
                    }
                }
            }

            graph.Reset();
        }
    }

    RuntimeDeviceSelection SelectRuntimeDeviceBackend(
        const Core::Config::RenderConfig& config,
        const bool promotedVulkanAvailable) noexcept
    {
        switch (config.Backend)
        {
        case Core::Config::GraphicsBackend::Vulkan:
            if (config.EnablePromotedVulkanDevice && promotedVulkanAvailable)
            {
                return RuntimeDeviceSelection{
                    .UsePromotedVulkanDevice = true,
                    .FallsBackToNullDevice = false,
                };
            }
            return RuntimeDeviceSelection{};
        }
        return RuntimeDeviceSelection{};
    }

    bool ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
        const Core::Config::RenderConfig& config,
        const bool isDeviceOperational) noexcept
    {
        if (config.Backend != Core::Config::GraphicsBackend::Vulkan)
            return false;
        if (!config.EnablePromotedVulkanDevice)
            return false;
        return !isDeviceOperational;
    }

    Core::Config::EngineConfig CreateReferenceEngineConfig()
    {
        Core::Config::EngineConfig config{};
        config.Window.Title = "Modular Vulkan Engine";
        config.Window.Width = 1600;
        config.Window.Height = 900;
        config.Render.Backend = Core::Config::GraphicsBackend::Vulkan;
        config.Render.EnablePromotedVulkanDevice = true;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = true;
        config.Render.FramesInFlight = 2;
        config.ReferenceScene.Enabled = true;
        config.ReferenceScene.Selector = Core::Config::ReferenceSceneSelector::Triangle;
        return config;
    }

    // ── Construction / destruction ────────────────────────────────────────

    Engine::Engine(Core::Config::EngineConfig config,
                   std::unique_ptr<IApplication> application)
        : m_Config(std::move(config))
        , m_Application(std::move(application))
    {
        if (!m_Application)
            std::terminate();
    }

    Engine::~Engine()
    {
        if (m_Initialized)
            Shutdown();
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void Engine::Initialize()
    {
        // ── 1. CPU fiber scheduler ────────────────────────────────────────
        // Must be first — all three graphs dispatch through it.
        Core::Tasks::Scheduler::Initialize(m_Config.Simulation.WorkerThreadCount);

        // ── 2. Subsystems ─────────────────────────────────────────────────
        // ARCH-005 / WORKSHOP-002: runtime owns the cross-layer composition
        // between platform window and graphics backend. RHI is platform-
        // neutral, so we fill a backend-agnostic `RHI::DeviceCreateDesc`
        // from the live `IWindow` here.
        m_Window   = Platform::CreateWindow(m_Config.Window);
        m_Device   = CreateDevice(m_Config.Render);
        const Platform::Extent2D initialExtent = m_Window->GetFramebufferExtent();
        m_Device->Initialize(RHI::MakeDeviceCreateDesc(
            m_Config.Render,
            initialExtent,
            m_Window->GetNativeHandle()));

        // GRAPHICS-033B: emit the Vulkan-requested-but-not-operational
        // breadcrumb and bump the operational diagnostics counters exactly
        // once per startup when the runtime requested the promoted Vulkan
        // device but the resolved device is not operational. Runtime never
        // aborts on this fallback — see the truth table in
        // `src/graphics/vulkan/README.md`.
        if (ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
                m_Config.Render, m_Device->IsOperational()))
        {
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
            const Backends::Vulkan::VulkanOperationalStatus status =
                Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus(m_Device.get());
            Core::Log::Warn(
                "[Runtime] VulkanRequestedButNotOperational status={} reason={}",
                Backends::Vulkan::ToString(status.Code),
                Backends::Vulkan::ToString(status.Reason));
            Backends::Vulkan::RecordVulkanOperationalFallback(status);
#else
            // Vulkan backend was not compiled into this build; the truth
            // table row resolves to `NotCompiled` with no reason.
            Core::Log::Warn(
                "[Runtime] VulkanRequestedButNotOperational status={} reason={}",
                "NotCompiled",
                "None");
#endif
        }

        m_Renderer = Graphics::CreateRenderer();
        m_Renderer->Initialize(*m_Device);
        m_RendererOperational = m_Device->IsOperational();

        // ── 2c. Runtime-side Dear ImGui adapter (RUNTIME-090 / GRAPHICS-079) ─
        // Constructed after the Window and Renderer exist. The adapter owns the
        // ImGui context lifecycle and produces exactly one ImGuiOverlayFrame per
        // engine frame into the runtime-owned overlay system; RunFrame brackets
        // the variable tick with BeginFrame/EndFrame (the producer half per
        // GRAPHICS-013CQ). GRAPHICS-079 Slice B hands that same overlay instance
        // to the renderer consumer, so the producer and `ImGuiPass` route share
        // one system without graphics seeing live runtime/editor state.
        m_ImGuiAdapter = std::make_unique<ImGuiAdapter>(*m_Window, m_ImGuiOverlay);
        m_ImGuiAdapter->Initialize();
        if (m_ImGuiEditorCallback)
            m_ImGuiAdapter->SetEditorCallback(m_ImGuiEditorCallback);
        m_Renderer->SetImGuiOverlaySystem(&m_ImGuiOverlay);

        // ── 2d. Render-world pool (GRAPHICS-036C) ─────────────────────────
        // Size the runtime-owned slot pool from the render config: one logical
        // buffer in the default synchronous mode (serial extraction/render,
        // behavior-preserving), or the triple-buffered default when pipelined
        // extraction is requested. The production default remains synchronous;
        // GRAPHICS-036D proves the opt-in render-N-1 path by consuming the
        // previous front while extraction writes the newly acquired back slot.
        m_RenderWorldPool = std::make_unique<RenderWorldPool>(
            m_Config.Render.SynchronousExtraction
                ? 1u
                : RenderWorldPool::kDefaultBuffers);

        // ── 3. CPU task graph (ECS system scheduling) ─────────────────────
        m_FrameGraph = std::make_unique<Core::FrameGraph>();

        // ── 4. Streaming task graph (asset IO / geometry processing) ──────
        m_StreamingGraph = Core::Dag::CreateTaskGraph(Core::Dag::QueueDomain::Streaming);
        m_StreamingExecutor = std::make_unique<StreamingExecutor>();

        // ── 5. Asset service ──────────────────────────────────────────────
        m_AssetService = std::make_unique<Assets::AssetService>();

        // ── 5b. GPU asset cache ───────────────────────────────────────────
        // Bridges AssetId to refcounted Buffer/Texture leases.  Subscribes
        // to AssetEventBus for Failed / Reloaded / Destroyed transitions;
        // type-specific bridges drive RequestUpload separately. The cache
        // receives the renderer's `SamplerManager` so RUNTIME-070's fallback
        // texture (and future texture-asset bridges) can resolve sampler
        // descriptors through the deduplicated manager path.
        m_GpuAssetCache = std::make_unique<Graphics::GpuAssetCache>(
            m_Renderer->GetBufferManager(),
            m_Renderer->GetTextureManager(),
            m_Renderer->GetSamplerManager(),
            m_Device->GetTransferQueue());

        // RUNTIME-070: bootstrap the runtime-owned 4×4 magenta-and-black
        // checkerboard fallback texture exactly once. Skipped when the
        // device is non-operational (e.g. the Null backend) — material
        // resolution then returns `GpuAssetFallbackReason::Unavailable` and
        // shaders route to factor-only shading, matching the documented
        // contract in `src/graphics/assets/README.md`.
        if (m_Device->IsOperational())
        {
            const Graphics::GpuTextureFallbackDesc fallbackDesc =
                BuildFallbackTextureDesc();
            if (auto r = m_GpuAssetCache->InitializeFallbackTexture(fallbackDesc);
                !r.has_value())
            {
                Core::Log::Warn(
                    "[Runtime] GpuAssetCache fallback texture bootstrap failed: error={}; material code will use factor-only fallback.",
                    static_cast<int>(r.error()));
            }
        }

        m_GpuAssetCacheListener = m_AssetService->SubscribeAll(
            [cache = m_GpuAssetCache.get()](Assets::AssetId id, Assets::AssetEvent ev)
            {
                switch (ev)
                {
                case Assets::AssetEvent::Failed:    cache->NotifyFailed(id);    break;
                case Assets::AssetEvent::Reloaded:  cache->NotifyReloaded(id);  break;
                case Assets::AssetEvent::Destroyed: cache->NotifyDestroyed(id); break;
                case Assets::AssetEvent::Ready:     /* no-op: type-specific bridges
                                                       drive RequestUpload */    break;
                }
            });
        // ── 6. ECS scene ──────────────────────────────────────────────────
        m_Scene = std::make_unique<ECS::Scene::Registry>();

        m_AssetModelTextureHandoff = std::make_unique<AssetModelTextureHandoff>(
            *m_AssetService,
            *m_GpuAssetCache);
        m_AssetModelSceneHandoff = std::make_unique<AssetModelSceneHandoff>(
            *m_AssetService,
            *m_GpuAssetCache,
            *m_Scene,
            *m_Renderer);

        // RUNTIME-092 Slice B — attach the runtime-owned stable-entity lookup
        // to the selection authority so render-id resolution flows through the
        // single runtime sidecar (which decodes + validates against the
        // registry) rather than a bare cast. The lookup is rebuilt each frame
        // in RunFrame before the pick-readback drain.
        m_SelectionController.SetStableEntityLookup(&m_StableEntityLookup);

        // ── 6b. Reference scene bootstrap (GRAPHICS-029A/B) ───────────────
        // Opt-in: only fires when EngineConfig::ReferenceScene::Enabled is
        // true. The default-off path leaves m_ReferenceScenePopulation
        // empty so RenderExtraction observes zero candidates. Double-install
        // is rejected via std::terminate to match the registry's
        // GRAPHICS-029 Decision 7 invariant.
        //
        // GRAPHICS-029B: install the production default providers for any
        // selector that does not yet have an explicit registration so the
        // resolve path is always covered without colliding with the strict
        // double-install guard.
        if (m_Config.ReferenceScene.Enabled)
        {
            if (m_ReferenceSceneInstalled)
                std::terminate();

            RegisterDefaultReferenceProvidersIfAbsent(m_ReferenceSceneRegistry);

            IReferenceSceneProvider& provider =
                m_ReferenceSceneRegistry.Resolve(m_Config.ReferenceScene.Selector);
            m_ReferenceScenePopulation = provider.Populate(*m_Scene);
            m_ReferenceCamera = m_ReferenceScenePopulation.Camera;
            m_ReferenceSceneInstalled = true;
        }

        // ── 7. Application ────────────────────────────────────────────────
        m_Application->OnInitialize(*this);

        m_Initialized = true;
        m_Running     = true;

        m_Window->Listen(
            [this](const Platform::Event& event)
            {
                HandlePlatformEvent(event);
            });
    }

    void Engine::Shutdown()
    {
        if (m_Window)
            m_Window->Listen({});

        // GRAPHICS-079 Slice B — detach the renderer consumer before the adapter
        // shuts the shared overlay system down, so the renderer never observes a
        // borrowed but inactive overlay during the rest of teardown.
        if (m_Renderer)
            m_Renderer->SetImGuiOverlaySystem(nullptr);
        // RUNTIME-090 Slice B — tear the Dear ImGui adapter down while the
        // Window and overlay system it references are still alive. The adapter
        // destructor shuts the overlay system + ImGui context down; the overlay
        // system value member is reusable on a later re-Initialize().
        m_ImGuiAdapter.reset();

        struct ShutdownHooks final : Core::IShutdownHooks
        {
            Engine& Owner;
            bool& Running;
            bool& Initialized;
            std::unique_ptr<IApplication>& Application;
            std::unique_ptr<Platform::IWindow>& Window;
            std::unique_ptr<RHI::IDevice>& Device;
            std::unique_ptr<Graphics::IRenderer>& Renderer;
            std::unique_ptr<Core::FrameGraph>& FrameGraph;
            std::unique_ptr<Core::Dag::TaskGraph>& StreamingGraph;
            std::unique_ptr<StreamingExecutor>& StreamingExecutorPtr;
            std::unique_ptr<Assets::AssetService>& AssetService;
            std::unique_ptr<Graphics::GpuAssetCache>& GpuAssetCache;
            std::unique_ptr<AssetModelTextureHandoff>& AssetModelTextureHandoffPtr;
            std::unique_ptr<AssetModelSceneHandoff>& AssetModelSceneHandoffPtr;
            Assets::AssetEventBus::ListenerToken& GpuAssetCacheListener;
            std::unique_ptr<ECS::Scene::Registry>& Scene;
            ReferenceSceneRegistry& ReferenceRegistry;
            ReferenceScenePopulation& ReferencePopulation;
            std::optional<Graphics::CameraViewInput>& ReferenceCameraSeed;
            CameraControllerRegistry& CameraControllers;
            bool& ReferenceInstalled;
            Core::Config::ReferenceSceneSelector ReferenceSelector;
            bool ReferenceEnabled;

            ShutdownHooks(Engine& owner,
                          bool& running,
                          bool& initialized,
                          std::unique_ptr<IApplication>& application,
                          std::unique_ptr<Platform::IWindow>& window,
                          std::unique_ptr<RHI::IDevice>& device,
                          std::unique_ptr<Graphics::IRenderer>& renderer,
                          std::unique_ptr<Core::FrameGraph>& frameGraph,
                          std::unique_ptr<Core::Dag::TaskGraph>& streamingGraph,
                          std::unique_ptr<StreamingExecutor>& streamingExecutor,
                          std::unique_ptr<Assets::AssetService>& assetService,
                          std::unique_ptr<Graphics::GpuAssetCache>& gpuAssetCache,
                          std::unique_ptr<AssetModelTextureHandoff>& assetModelTextureHandoff,
                          std::unique_ptr<AssetModelSceneHandoff>& assetModelSceneHandoff,
                          Assets::AssetEventBus::ListenerToken& gpuAssetCacheListener,
                          std::unique_ptr<ECS::Scene::Registry>& scene,
                          ReferenceSceneRegistry& referenceRegistry,
                          ReferenceScenePopulation& referencePopulation,
                          std::optional<Graphics::CameraViewInput>& referenceCameraSeed,
                          CameraControllerRegistry& cameraControllers,
                          bool& referenceInstalled,
                          Core::Config::ReferenceSceneSelector referenceSelector,
                          bool referenceEnabled)
                : Owner(owner)
                , Running(running)
                , Initialized(initialized)
                , Application(application)
                , Window(window)
                , Device(device)
                , Renderer(renderer)
                , FrameGraph(frameGraph)
                , StreamingGraph(streamingGraph)
                , StreamingExecutorPtr(streamingExecutor)
                , AssetService(assetService)
                , GpuAssetCache(gpuAssetCache)
                , AssetModelTextureHandoffPtr(assetModelTextureHandoff)
                , AssetModelSceneHandoffPtr(assetModelSceneHandoff)
                , GpuAssetCacheListener(gpuAssetCacheListener)
                , Scene(scene)
                , ReferenceRegistry(referenceRegistry)
                , ReferencePopulation(referencePopulation)
                , ReferenceCameraSeed(referenceCameraSeed)
                , CameraControllers(cameraControllers)
                , ReferenceInstalled(referenceInstalled)
                , ReferenceSelector(referenceSelector)
                , ReferenceEnabled(referenceEnabled)
            {
            }

            void StopRunning() override { Running = false; }
            void WaitDeviceIdle() override
            {
                if (Device)
                    Device->WaitIdle();
            }
            void ShutdownApplication() override
            {
                if (Application)
                    Application->OnShutdown(Owner);
            }
            void ShutdownStreaming() override
            {
                if (StreamingExecutorPtr)
                    StreamingExecutorPtr->ShutdownAndDrain();
            }
            void DestroyScene() override
            {
                // The model-scene handoff borrows the scene and renderer, so
                // detach it before provider teardown or wholesale scene reset.
                AssetModelSceneHandoffPtr.reset();

                // Reference scene teardown (GRAPHICS-029A/B): route entity
                // destruction through the same provider that authored them
                // before the scene registry is wholesale destroyed, and
                // clear the cached camera seed so a re-Initialize loop does
                // not republish a stale reference camera.
                if (ReferenceEnabled && ReferenceInstalled && Scene)
                {
                    if (IReferenceSceneProvider* provider =
                            ReferenceRegistry.ResolveOrNull(ReferenceSelector))
                    {
                        provider->Teardown(*Scene, ReferencePopulation.Entities);
                    }
                    ReferencePopulation = ReferenceScenePopulation{};
                    ReferenceInstalled = false;
                }
                ReferenceCameraSeed.reset();
                CameraControllers = CameraControllerRegistry{};
                Scene.reset();
            }
            void DestroyAssets() override
            {
                // Unsubscribe before destroying the cache so a late event
                // flush cannot reach a freed cache.  The cache is destroyed
                // before the renderer (which owns Buffer/Texture managers)
                // so leases unwind through live managers.
                AssetModelTextureHandoffPtr.reset();
                if (AssetService &&
                    GpuAssetCacheListener != Assets::AssetEventBus::InvalidToken)
                {
                    AssetService->UnsubscribeAll(GpuAssetCacheListener);
                    GpuAssetCacheListener = Assets::AssetEventBus::InvalidToken;
                }
                GpuAssetCache.reset();
                AssetService.reset();
            }
            void DestroyStreamingState() override
            {
                StreamingExecutorPtr.reset();
                StreamingGraph.reset();
            }
            void DestroyFrameGraph() override { FrameGraph.reset(); }
            void ShutdownRenderer() override
            {
                if (Renderer)
                {
                    Owner.m_RenderExtraction.Shutdown(*Renderer);
                    Renderer->Shutdown();
                    Renderer.reset();
                }
            }
            void ShutdownDevice() override
            {
                if (Device)
                {
                    Device->Shutdown();
                    Device.reset();
                }
            }
            void DestroyWindow() override { Window.reset(); }
            void ShutdownScheduler() override
            {
                // Shut down the fiber scheduler last — worker threads must exit cleanly
                // before any other thread-local storage or allocators are destroyed.
                Core::Tasks::Scheduler::Shutdown();
            }
            void MarkUninitialized() override { Initialized = false; }
        };

        ShutdownHooks hooks(*this,
                            m_Running,
                            m_Initialized,
                            m_Application,
                            m_Window,
                            m_Device,
                            m_Renderer,
                            m_FrameGraph,
                            m_StreamingGraph,
                            m_StreamingExecutor,
                            m_AssetService,
                            m_GpuAssetCache,
                            m_AssetModelTextureHandoff,
                            m_AssetModelSceneHandoff,
                            m_GpuAssetCacheListener,
                            m_Scene,
                            m_ReferenceSceneRegistry,
                            m_ReferenceScenePopulation,
                            m_ReferenceCamera,
                            m_CameraControllers,
                            m_ReferenceSceneInstalled,
                            m_Config.ReferenceScene.Selector,
                            m_Config.ReferenceScene.Enabled);
        Core::ExecuteShutdownContract(hooks);
    }

    // ── Main loop ─────────────────────────────────────────────────────────

    void Engine::Run()
    {
        while (m_Running && !m_Window->ShouldClose())
            RunFrame();
    }

    void Engine::RunFrame()
    {
        RuntimeFrameContext frameContext{};

        // ── Phase 1: Platform ─────────────────────────────────────────────
        struct PlatformFrameHooks final : Core::IPlatformFrameHooks
        {
            Platform::IWindow& Window;

            explicit PlatformFrameHooks(Platform::IWindow& window)
                : Window(window)
            {
            }

            void PollEvents() override { Window.PollEvents(); }
            [[nodiscard]] bool ShouldClose() const override
            {
                return Window.ShouldClose();
            }
            [[nodiscard]] bool IsMinimized() const override
            {
                return Window.IsMinimized();
            }
            void WaitForEventsTimeout(double seconds) override
            {
                Window.WaitForEventsTimeout(seconds);
            }
        };

        PlatformFrameHooks platformHooks{*m_Window};
        const Core::PlatformFrameResult platformResult =
            Core::ExecutePlatformBeginFrameContract(platformHooks,
                                                    kIdleSleepSeconds);
        if (platformResult.ShouldClose || !m_Running)
        {
            RequestExit();
            return;
        }

        m_FrameClock.BeginFrame();

        if (!platformResult.ContinueFrame)
        {
            m_FrameClock.Resample();
            return;
        }

        // Swapchain resize: drain GPU, resize resources, then proceed normally.
        if (m_Window->WasResized())
        {
            const auto extent = m_Window->GetFramebufferExtent();
            if (extent.Width > 0 && extent.Height > 0)
            {
                m_Device->WaitIdle();
                m_Device->Resize(static_cast<unsigned>(extent.Width),
                                 static_cast<unsigned>(extent.Height));
                m_Renderer->Resize(static_cast<unsigned>(extent.Width),
                                   static_cast<unsigned>(extent.Height));
            }
            m_Window->AcknowledgeResize();
        }

        struct OperationalTransitionHooks final : Core::IOperationalTransitionHooks
        {
            RHI::IDevice& Device;
            Graphics::IRenderer& Renderer;
            bool& RendererOperational;

            OperationalTransitionHooks(RHI::IDevice& device,
                                       Graphics::IRenderer& renderer,
                                       bool& rendererOperational)
                : Device(device)
                , Renderer(renderer)
                , RendererOperational(rendererOperational)
            {
            }

            [[nodiscard]] bool IsDeviceOperational() const override { return Device.IsOperational(); }
            [[nodiscard]] bool IsRendererOperational() const override { return RendererOperational; }
            void WaitDeviceIdle() override { Device.WaitIdle(); }
            [[nodiscard]] bool RebuildRendererOperationalResources() override
            {
                return Renderer.RebuildOperationalResources(Device);
            }
            void MarkRendererOperational() override { RendererOperational = true; }
        };

        OperationalTransitionHooks operationalHooks(*m_Device, *m_Renderer, m_RendererOperational);
        (void)Core::ExecuteOperationalTransitionContract(operationalHooks);

        // ── Phase 2: Fixed-step simulation + CPU task graph ───────────────
        // Each tick: app adds FrameGraph passes → Engine compiles and executes
        // the ECS system DAG → reset for next tick.

        const double frameDt = m_FrameClock.FrameDeltaClamped(m_MaxFrameDelta);
        frameContext.FrameDeltaSeconds = frameDt;
        m_Accumulator += frameDt;

        int substeps = 0;
        while (m_Accumulator >= m_FixedDt && substeps < m_MaxSubSteps)
        {
            // App registers system passes via engine.GetFrameGraph().AddPass(...)
            m_Application->OnSimTick(*this, m_FixedDt);

            // RUNTIME-091: register the promoted baseline ECS systems
            // (TransformHierarchy, BoundsPropagation) after the app has
            // had a chance to add its own fixed-step passes. The FrameGraph
            // resolves the actual execution order through TypeToken reads/
            // writes and the named TransformUpdate / WorldBoundsUpdate
            // signals, so app passes that mutate transforms run before
            // TransformHierarchy and app passes that WaitFor either signal
            // run after the propagation seam.
            (void)RegisterPromotedEcsSystemBundle(*m_FrameGraph, *m_Scene);

            // CPU task graph: compile dependency order, execute in topo-layer
            // sequence (currently sequential execution), then reset.
            if (m_FrameGraph->PassCount() > 0)
            {
                if (auto r = m_FrameGraph->Compile(); r.has_value())
                {
                    if (auto exec = m_FrameGraph->Execute(); !exec.has_value())
                    {
                        Core::Log::Error("[Runtime] FrameGraph Execute() failed: error={}",
                                   static_cast<int>(exec.error()));
                    }
                }
                else
                {
                    Core::Log::Error("[Runtime] FrameGraph Compile() failed: error={}",
                               static_cast<int>(r.error()));
                }
                m_FrameGraph->Reset();
            }

            m_Accumulator -= m_FixedDt;
            ++substeps;
        }

        const double alpha = m_Accumulator / m_FixedDt;
        frameContext.FixedStepAlpha = alpha;

        // ── RUNTIME-090 Slice B: open the Dear ImGui frame ────────────────
        // BeginFrame runs after Window::PollEvents (Phase 1) and the
        // minimize/resize early returns, immediately before the variable tick,
        // so the editor hook and any ImGui draws issued during OnVariableTick
        // run inside the NewFrame()/Render() scope. Minimized frames return
        // before this point, so a NewFrame is never left without a matching
        // Render() in EndFrame.
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->BeginFrame(frameDt);

        // ── Phase 3: Variable tick ────────────────────────────────────────
        m_Application->OnVariableTick(*this, alpha, frameDt);

        // ── RUNTIME-090 Slice B: close the Dear ImGui frame ───────────────
        // EndFrame runs after the variable tick and before the render
        // contract's IRenderer::PrepareFrame(): it invokes the editor hook,
        // calls ImGui::Render(), walks ImDrawData, and submits one
        // ImGuiOverlayFrame to the overlay system (per GRAPHICS-013CQ). The
        // renderer consumer is attached in Initialize(); graphics-side
        // draw upload + recorded Pass.ImGui execution remain later GRAPHICS-079
        // slices.
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->EndFrame();

        // ── Phase 4: Build render snapshot ────────────────────────────────
        const Platform::Extent2D viewport = m_Window->GetFramebufferExtent();
        frameContext.RenderInput = Graphics::RenderFrameInput{
            .Alpha    = alpha,
            .Viewport = viewport,
        };
        Graphics::RenderFrameInput& renderInput = frameContext.RenderInput;

        if (m_Config.Camera.Enabled)
        {
            ICameraController* controller = m_CameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            if (controller == nullptr)
            {
                const Graphics::CameraViewInput seed = m_ReferenceCamera.has_value()
                    ? BuildReferenceCameraViewInput(*m_ReferenceCamera, viewport.Width, viewport.Height)
                    : Graphics::CameraViewInput{};
                m_CameraControllers.Register(
                    CameraControllerSlot::Main,
                    CreateCameraController(m_Config.Camera.Controller, seed));
                controller = m_CameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            }

            if (controller != nullptr)
            {
                const Platform::IWindow& window = *m_Window;
                controller->Update(window.GetInput(), frameDt);
                renderInput.Camera = controller->GetView(viewport);
                renderInput.Camera.ExplicitCameraTransition =
                    m_CameraControllers.ConsumeCameraTransition(CameraControllerSlot::Main);
            }
        }

        RebuildSelectedGizmoEntities(m_SelectionController, *m_Scene, m_GizmoSelectedEntities);
        const Platform::IWindow& inputWindow = *m_Window;
        const Platform::Extent2D windowExtent = inputWindow.GetWindowExtent();
        DriveGizmoInteractionForFrame(m_GizmoInteraction,
                                      m_GizmoUndoStack,
                                      *m_Scene,
                                      inputWindow.GetInput(),
                                      renderInput.Camera,
                                      windowExtent,
                                      viewport,
                                      m_GizmoSelectedEntities);
        SubmitViewportSelectionClickForFrame(m_SelectionController,
                                             inputWindow.GetInput(),
                                             windowExtent,
                                             viewport,
                                             m_ImGuiAdapter != nullptr && m_ImGuiAdapter->WantsMouseCapture(),
                                             m_GizmoInteraction.IsDragging());

        // ── BUG-024: pre-render transform flush ───────────────────────────
        // Local-transform mutations made after the fixed-step ECS bundle —
        // Sandbox Editor UI inspector edits (applied inside the ImGui editor
        // hook during EndFrame above), OnVariableTick app mutations, and the
        // GizmoInteraction drag just driven — would otherwise reach render
        // extraction with a stale Transform::WorldMatrix and only become
        // visible one frame late (or never, when no further fixed-step tick
        // runs). Flush TransformHierarchy → BoundsPropagation → RenderSync
        // here, before the transform-gizmo packets are built and before
        // ExtractRenderWorld observes the scene, so the rendered model
        // matrix and the gizmo packets agree with the authored transform in
        // the same frame.
        (void)FlushPreRenderTransformState(*m_Scene);

        const std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos =
            m_GizmoPacketBuilder.Build(*m_Scene,
                                       m_GizmoSelectedEntities,
                                       m_GizmoInteraction.Mode(),
                                       m_GizmoInteraction.Orientation(),
                                       m_GizmoInteraction.Config().AxisLength);

        // ── RUNTIME-089 Slice B: drain the coalesced selection pick ───────
        // Input ports / editor tools submit hover/click picks onto the
        // controller (GetSelectionController()); here we drain the single
        // coalesced survivor into the frame input and the renderer's
        // SelectionSystem so graphics issues the pick this frame. The
        // controller tracks the drained pick as in-flight; the matching
        // readback is consumed in the maintenance phase below. Graphics stays
        // reporting-only — it never reads live ECS or runtime selection state.
        if (const std::optional<PendingSelectionPick> pick =
                m_SelectionController.ConsumePendingPick())
        {
            renderInput.HasPendingPick = true;
            renderInput.Pick = Graphics::PickPixelRequest{
                .X        = pick->PixelX,
                .Y        = pick->PixelY,
                .Pending  = true,
                // Carry the controller's correlation token so the readback
                // resolves this exact request, not whichever pick is oldest.
                .Sequence = pick->Sequence,
            };
            m_Renderer->GetSelectionSystem().RequestPick(Graphics::PickRequest{
                .PixelX = pick->PixelX,
                .PixelY = pick->PixelY,
            });

            // BUG-026 — capture the issuing frame's camera context, keyed by
            // the pick's correlation Sequence. The readback completes frames
            // later (the camera may have moved), so the depth-to-world cursor
            // reconstruction and the CPU ray fallback must replay *this*
            // frame's inverse view-projection / pick ray, not the consume
            // frame's. Bounded mirror of the controller's in-flight FIFO.
            const Graphics::CameraViewSnapshot pickCamera =
                Graphics::BuildCameraViewSnapshot(renderInput.Camera,
                                                  viewport,
                                                  renderInput.Pick);
            if (pickCamera.Valid)
            {
                const std::uint32_t viewportWidth =
                    viewport.Width > 0 ? static_cast<std::uint32_t>(viewport.Width) : 0u;
                const std::uint32_t viewportHeight =
                    viewport.Height > 0 ? static_cast<std::uint32_t>(viewport.Height) : 0u;
                PickReadbackContext context{};
                context.InverseViewProjection = pickCamera.InverseViewProjection;
                context.ViewportWidth         = viewportWidth;
                context.ViewportHeight        = viewportHeight;
                context.HasWorldRay           = pickCamera.HasPickRay;
                context.WorldRayOrigin        = pickCamera.PickRayOrigin;
                context.WorldRayDirection     = pickCamera.PickRayDirection;
                // `2 / (|P[1][1]| * H)`: for perspective ([1][1] =
                // ±1/tan(fovY/2)) this is the world-units-per-pixel at view
                // depth 1; for orthographic ([1][1] = ±2/orthoHeight, e.g.
                // the promoted TopDownCameraController) the same expression
                // is the depth-invariant orthoHeight/H, and the flag tells
                // refinement not to scale it by the hit distance. The sign
                // carries the Vulkan Y flip in both cases.
                const float projectionScaleY =
                    std::abs(renderInput.Camera.Projection[1][1]);
                if (projectionScaleY > 0.000001f && viewportHeight > 0u)
                {
                    context.WorldUnitsPerPixelAtUnitDepth =
                        2.0f / (projectionScaleY *
                                static_cast<float>(viewportHeight));
                }
                context.OrthographicProjection =
                    IsOrthographicProjection(renderInput.Camera.Projection);
                constexpr std::size_t kMaxInFlightPickContexts = 32u;
                if (m_InFlightPickContexts.size() >= kMaxInFlightPickContexts)
                {
                    m_InFlightPickContexts.erase(m_InFlightPickContexts.begin());
                }
                m_InFlightPickContexts.push_back(InFlightPickContext{
                    .Sequence = pick->Sequence,
                    .Context  = context,
                });
            }
        }

        // ── Phases 5–9: promoted render-frame contract ───────────────────
        RHI::FrameHandle frame{};
        Graphics::RenderWorld renderWorld{};

        // GRAPHICS-036C — the render-world pool slot lifecycle is driven around
        // extraction inside the hook (producer: AcquireBack/PublishFront;
        // consumer: AcquireFront) and the front reference is released after the
        // frame retires below. `frameIndex` stamps the acquired slot so the
        // consumer's frame-age diagnostic reads 0 in the synchronous baseline.
        frameContext.FrameIndex = m_FrameIndex++;

        struct RenderFrameHooks final : Core::IRenderFrameHooks
        {
            Graphics::IRenderer& Renderer;
            ECS::Scene::Registry& Scene;
            RenderExtractionCache& Extraction;
            Graphics::GpuAssetCache* GpuAssetCache;
            const SelectionController& Selection;
            RenderWorldPool& Pool;
            bool SynchronousExtraction;
            RuntimeRenderExtractionStats& Stats;
            std::uint64_t FrameIndex;
            std::uint32_t& OutFrontSlot;
            RHI::FrameHandle& Frame;
            const Graphics::RenderFrameInput& Input;
            std::span<const Graphics::TransformGizmoRenderPacket> TransformGizmos;
            Graphics::RenderWorld& World;

            RenderFrameHooks(Graphics::IRenderer& renderer,
                             ECS::Scene::Registry& scene,
                             RenderExtractionCache& extraction,
                             Graphics::GpuAssetCache* gpuAssetCache,
                             const SelectionController& selection,
                             RenderWorldPool& pool,
                             const bool synchronousExtraction,
                             RuntimeRenderExtractionStats& stats,
                             std::uint64_t frameIndex,
                             std::uint32_t& outFrontSlot,
                             RHI::FrameHandle& frame,
                             const Graphics::RenderFrameInput& input,
                             std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos,
                             Graphics::RenderWorld& world)
                : Renderer(renderer)
                , Scene(scene)
                , Extraction(extraction)
                , GpuAssetCache(gpuAssetCache)
                , Selection(selection)
                , Pool(pool)
                , SynchronousExtraction(synchronousExtraction)
                , Stats(stats)
                , FrameIndex(frameIndex)
                , OutFrontSlot(outFrontSlot)
                , Frame(frame)
                , Input(input)
                , TransformGizmos(transformGizmos)
                , World(world)
            {
            }

            bool BeginFrame() override
            {
                return Renderer.BeginFrame(Frame);
            }
            void ExtractRenderWorld() override
            {
                // GRAPHICS-036C — producer half: acquire a back slot, write the
                // snapshot into it via ExtractAndSubmit, then publish it as the
                // front. AcquireBack only fails closed (kInvalidSlot) when the
                // pool is exhausted; in that case the previous front stays current
                // and we skip the publish so no in-flight slot is overwritten.
                const std::uint32_t backSlot = Pool.AcquireBack(FrameIndex);

                // RUNTIME-089 Slice B — mirror the runtime selection snapshot
                // into RenderWorld::Selection via the extraction batch. In
                // pipelined mode the renderer writes into the acquired back
                // slot while the consumer below reads the previous front slot.
                const std::uint32_t submitSlot =
                    backSlot != RenderWorldPool::kInvalidSlot ? backSlot : 0u;
                if (backSlot != RenderWorldPool::kInvalidSlot)
                {
                    Stats = Extraction.ExtractAndSubmit(Scene,
                                                         Renderer,
                                                         GpuAssetCache,
                                                         &Selection,
                                                         submitSlot,
                                                         TransformGizmos);
                    Pool.PublishFront(backSlot);
                }
                else
                {
                    Stats = Extraction.GetLastStats();
                }

                // Consumer half: synchronous mode preserves the existing
                // same-frame consume. Pipelined mode intentionally consumes the
                // previously published front (render-N-1) after extraction has
                // published N.
                OutFrontSlot = SynchronousExtraction
                    ? Pool.AcquireFront(FrameIndex)
                    : Pool.AcquirePreviousFront(FrameIndex);

                const std::uint32_t extractSlot =
                    OutFrontSlot != RenderWorldPool::kInvalidSlot ? OutFrontSlot : submitSlot;
                World = Renderer.ExtractRenderWorld(Input, extractSlot);

                // GRAPHICS-036B — surface the pool's three counters on the
                // extraction stats for editor overlays / tests.
                MirrorRenderWorldPoolDiagnostics(Pool, Stats);
            }
            void PrepareFrame() override { Renderer.PrepareFrame(World); }
            void ExecuteFrame() override { Renderer.ExecuteFrame(Frame, World); }
            std::uint64_t EndFrame() override { return Renderer.EndFrame(Frame); }
        };

        RenderFrameHooks renderHooks(*m_Renderer,
                                     *m_Scene,
                                     m_RenderExtraction,
                                     m_GpuAssetCache.get(),
                                     m_SelectionController,
                                     *m_RenderWorldPool,
                                     m_Config.Render.SynchronousExtraction,
                                     frameContext.ExtractionStats,
                                     frameContext.FrameIndex,
                                     frameContext.PooledFrontSlot,
                                     frame,
                                     renderInput,
                                     transformGizmos,
                                     renderWorld);

        const Core::RenderFrameResult renderResult = Core::ExecuteRenderFrameContract(renderHooks);
        m_LastExtractionStats = frameContext.ExtractionStats;
        if (!renderResult.BeganFrame)
        {
            // BeginFrame failed before extraction ran, so no slot was acquired
            // (PooledFrontSlot stays kInvalidSlot) — nothing to release.
            m_FrameClock.EndFrame();
            return;
        }

        const std::uint64_t completedGpuValue = renderResult.CompletedGpuValue;
        m_Device->Present(frame);

        // ── Phase 10: Maintenance ─────────────────────────────────────────
        struct TransferHooks final : Core::ITransferFrameHooks
        {
            RHI::IDevice& Device;

            explicit TransferHooks(RHI::IDevice& device)
                : Device(device)
            {
            }

            void CollectCompletedTransfers() override
            {
                // GPU-side resource retirement, staging GC, readback processing.
                Device.GetTransferQueue().CollectCompleted();
            }
        };

        struct StreamingHooks final : Core::IStreamingFrameHooks
        {
            Core::Dag::TaskGraph& Graph;
            StreamingExecutor& Executor;

            StreamingHooks(Core::Dag::TaskGraph& graph, StreamingExecutor& executor)
                : Graph(graph)
                , Executor(executor)
            {
            }

            void DrainCompletions() override { Executor.DrainCompletions(); }
            void ApplyMainThreadResults() override { Executor.ApplyMainThreadResults(); }
            void SubmitFrameWork() override { SubmitStreamingGraphToExecutor(Graph, Executor); }
            void PumpBackground(std::uint32_t maxLaunches) override { Executor.PumpBackground(maxLaunches); }
        };

        struct AssetHooks final : Core::IAssetFrameHooks
        {
            Assets::AssetService&     AssetService;
            Graphics::GpuAssetCache*  GpuAssetCache;
            AssetModelSceneHandoff*   ModelSceneHandoff;
            RHI::IDevice&             Device;
            RenderExtractionCache&    Extraction;
            Graphics::IRenderer&      Renderer;

            AssetHooks(Assets::AssetService& assetService,
                       Graphics::GpuAssetCache* gpuAssetCache,
                       AssetModelSceneHandoff* modelSceneHandoff,
                       RHI::IDevice& device,
                       RenderExtractionCache& extraction,
                       Graphics::IRenderer& renderer)
                : AssetService(assetService)
                , GpuAssetCache(gpuAssetCache)
                , ModelSceneHandoff(modelSceneHandoff)
                , Device(device)
                , Extraction(extraction)
                , Renderer(renderer)
            {
            }

            void TickAssets() override
            {
                // Asset service main-thread tick: advances state machines, fires
                // AssetEventBus::Ready / Reloaded / Destroyed callbacks.  The
                // cache subscribed in Engine::Initialize observes those events
                // synchronously during this Tick.
                AssetService.Tick();
                const std::uint64_t currentFrame = Device.GetGlobalFrameNumber();
                const std::uint32_t framesInFlight = Device.GetFramesInFlight();
                if (GpuAssetCache)
                {
                    GpuAssetCache->Tick(currentFrame, framesInFlight);
                }
                if (ModelSceneHandoff)
                {
                    static_cast<void>(
                        ModelSceneHandoff->ResolvePendingMaterialTextureBindings());
                }
                // GRAPHICS-030C: drive the procedural geometry cache's
                // deferred-retire window with the same CPU frame counter and
                // framesInFlight the asset cache uses.  Final FreeGeometry
                // calls fall through to GpuWorld here.
                Extraction.TickProceduralGeometry(currentFrame, framesInFlight, Renderer);
                // RUNTIME-085 Slice C — mirror the same window for the
                // runtime-owned mesh-residency retire queue.
                Extraction.TickMeshGeometry(currentFrame, framesInFlight, Renderer);
                // RUNTIME-086 Slice B — and for the graph-residency queue.
                Extraction.TickGraphGeometry(currentFrame, framesInFlight, Renderer);
                // RUNTIME-087 — and for the point-cloud-residency queue.
                Extraction.TickPointCloudGeometry(currentFrame, framesInFlight, Renderer);
                // RUNTIME-088 Slice B — and for the mesh edge/vertex primitive
                // view residency queue (one queue for both view lanes).
                Extraction.TickMeshPrimitiveViewGeometry(currentFrame, framesInFlight, Renderer);
            }
        };

        TransferHooks transferHooks(*m_Device);
        StreamingHooks streamingHooks(*m_StreamingGraph, *m_StreamingExecutor);
        AssetHooks assetHooks(*m_AssetService,
                              m_GpuAssetCache.get(),
                              m_AssetModelSceneHandoff.get(),
                              *m_Device,
                              m_RenderExtraction,
                              *m_Renderer);
        Core::ExecuteMaintenanceContract(transferHooks, streamingHooks, assetHooks, 8);

        // ── RUNTIME-092 Slice B: refresh the stable-entity lookup ──────────
        // Rebuild the runtime-owned StableId winner-map from the live registry
        // before consuming pick readbacks, so durable-id resolution and the
        // editor/serialization-facing ResolveByStableId/ResolveSelected APIs
        // observe this frame's entity set. Render-id resolution (the path the
        // controller takes for a pick hit) decodes + validates against the live
        // registry directly and does not depend on the map, so a recycled slot
        // is rejected regardless; the rebuild keeps the durable map coherent for
        // the other consumers and is the single per-frame maintenance point.
        m_StableEntityLookup.Rebuild(*m_Scene);

        // ── RUNTIME-089 Slice B: consume the completed pick readbacks ──────
        // DrainCompletedPickingSlots can publish several completed picking
        // slots into the SelectionSystem during the render/transfer phases, so
        // drain the whole FIFO — not just the newest — and resolve each result
        // by its correlation Sequence. ConsumeHit/ConsumeNoHit(reg, seq) replay
        // the exact in-flight request's kind/mode (hover vs click, Replace/Add/
        // Toggle) even when picks complete out of issue order or a slot is
        // recycled. A result with no Sequence (uncorrelated; e.g. a pick issued
        // outside the controller bridge) falls back to the oldest in-flight
        // pick. The controller resolves the stable id, rejects stale/
        // non-selectable hits, and mutates ECS Selected/Hovered tags.
        {
            Graphics::SelectionSystem& selectionSystem = m_Renderer->GetSelectionSystem();
            while (const std::optional<Graphics::PickReadbackResult> result =
                       selectionSystem.PopPickResult())
            {
                if (result->Sequence != 0u)
                {
                    if (result->Hit)
                        m_SelectionController.ConsumeHit(*m_Scene, result->StableEntityId, result->Sequence);
                    else
                        m_SelectionController.ConsumeNoHit(*m_Scene, result->Sequence);
                }
                else
                {
                    if (result->Hit)
                        m_SelectionController.ConsumeHit(*m_Scene, result->StableEntityId);
                    else
                        m_SelectionController.ConsumeNoHit(*m_Scene);
                }

                // ── RUNTIME-093 Slice B2: refine the pick into a sub-primitive ──
                // Bridge each readback's encoded primitive hint to the authoritative
                // CPU GeometrySources of the hit entity and cache the result for the
                // editor. The whole loop runs oldest→newest, so the last readback's
                // refinement wins, matching the controller's latest-pick-wins
                // coalescing; a background (no-hit) readback clears the cache. The
                // bridge mutates nothing and only ever reads the live registry.
                //
                // BUG-026 — replay the issuing frame's camera context (matched by
                // the correlation Sequence) so the readback's depth sample
                // reconstructs the world/local cursor position and anchors the
                // closest-vertex/edge/face refinement.
                const PickReadbackContext* pickContext = nullptr;
                auto contextIt = m_InFlightPickContexts.end();
                if (result->Sequence != 0u)
                {
                    contextIt = std::find_if(
                        m_InFlightPickContexts.begin(),
                        m_InFlightPickContexts.end(),
                        [seq = result->Sequence](const InFlightPickContext& entry)
                        { return entry.Sequence == seq; });
                    if (contextIt != m_InFlightPickContexts.end())
                        pickContext = &contextIt->Context;
                }
                m_LastRefinedPrimitive =
                    RefinePickReadbackResult(*m_Scene, *result, pickContext);
                if (contextIt != m_InFlightPickContexts.end())
                    m_InFlightPickContexts.erase(contextIt);
            }
        }

        // completedGpuValue is the renderer's per-frame timeline value.  The
        // GpuAssetCache currently retires on the CPU frame counter (which is
        // a conservative proxy for GPU completion); a follow-up may key
        // retirement directly on completedGpuValue for tighter recycling.
        (void)completedGpuValue;

        // ── GRAPHICS-036C: release the pooled front at frame retire ────────
        // The renderer consumed the acquired snapshot this frame (commands are
        // recorded by ExecuteFrame above). Synchronous mode releases the current
        // front; pipelined mode releases the previous front consumed by render-N
        // after extraction-N has already published the new front.
        if (frameContext.PooledFrontSlot != RenderWorldPool::kInvalidSlot)
            m_RenderWorldPool->ReleaseFront(frameContext.PooledFrontSlot);

        // ── Phase 11: Clock EndFrame ──────────────────────────────────────
        m_FrameClock.EndFrame();
    }

    // ── Query / control ───────────────────────────────────────────────────

    bool Engine::IsRunning() const noexcept { return m_Running; }
    void Engine::RequestExit()      noexcept { m_Running = false; }

    Platform::IWindow&    Engine::GetWindow()        noexcept { return *m_Window;        }
    RHI::IDevice&         Engine::GetDevice()        noexcept { return *m_Device;        }
    Graphics::IRenderer&  Engine::GetRenderer()      noexcept { return *m_Renderer;      }
    Assets::AssetService& Engine::GetAssetService()  noexcept { return *m_AssetService;  }
    Graphics::GpuAssetCache& Engine::GetGpuAssetCache() noexcept { return *m_GpuAssetCache; }
    ECS::Scene::Registry& Engine::GetScene()         noexcept { return *m_Scene;         }
    GizmoInteraction& Engine::GetGizmoInteraction() noexcept { return m_GizmoInteraction; }
    const GizmoInteraction& Engine::GetGizmoInteraction() const noexcept { return m_GizmoInteraction; }
    GizmoUndoStack& Engine::GetGizmoUndoStack() noexcept { return m_GizmoUndoStack; }
    const GizmoUndoStack& Engine::GetGizmoUndoStack() const noexcept { return m_GizmoUndoStack; }

    const RenderWorldPool& Engine::GetRenderWorldPool() const noexcept { return *m_RenderWorldPool; }
    const RuntimeRenderExtractionStats& Engine::GetLastRenderExtractionStats() const noexcept
    {
        return m_LastExtractionStats;
    }

    Core::Expected<RuntimeAssetImportResult> Engine::ImportAssetFromPath(
        RuntimeAssetImportRequest request)
    {
        auto result = ImportAssetFromPathImpl(request);
        RecordAssetImportEvent(request, result);
        if (result.has_value() &&
            (result->PrimitiveEntitiesCreated > 0u || result->MaterializedModelScene))
        {
            (void)m_EditorCommandHistory.MarkDirty("Import Asset");
        }
        return result;
    }

    const std::optional<RuntimeAssetImportEvent>& Engine::GetLastAssetImportEvent()
        const noexcept
    {
        return m_LastAssetImportEvent;
    }

    void Engine::HandlePlatformEvent(const Platform::Event& event)
    {
        if (std::holds_alternative<Platform::WindowCloseEvent>(event))
        {
            RequestExit();
            return;
        }

        if (const auto* dropped = std::get_if<Platform::WindowDropEvent>(&event))
        {
            HandleWindowDropEvent(*dropped);
        }
    }

    void Engine::DispatchPlatformEventForTest(const Platform::Event& event)
    {
        HandlePlatformEvent(event);
    }

    void Engine::HandleWindowDropEvent(const Platform::WindowDropEvent& event)
    {
        ImportDroppedFilePaths(event.Paths);
    }

    void Engine::ImportDroppedFilePaths(std::span<const std::string> paths)
    {
        for (const std::string& path : paths)
        {
            if (path.empty())
                continue;

            const Assets::AssetRouteDiagnostic diagnostic =
                Assets::DiagnoseAssetImportRoute(
                    path,
                    Assets::AssetRouteOperation::Import,
                    Assets::AssetImportHint{
                        .PayloadKind = Assets::AssetPayloadKind::Unknown,
                    });
            if (diagnostic.Status == Assets::AssetRouteStatus::AmbiguousPayloadKind)
            {
                const Assets::AssetFileFormatInfo* format =
                    Assets::FindAssetFileFormat(path);
                std::vector<Assets::AssetPayloadKind> geometryPayloads{};
                if (format != nullptr)
                {
                    for (const Assets::AssetPayloadKind payloadKind :
                         format->ImportPayloads)
                    {
                        if (!Assets::IsGeometryPayloadKind(payloadKind))
                            continue;
                        geometryPayloads.push_back(payloadKind);
                    }
                }
                if (!geometryPayloads.empty())
                {
                    QueueDroppedGeometryImport(path, std::move(geometryPayloads));
                    continue;
                }
            }

            auto route = Assets::ResolveAssetImportRoute(
                path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{
                    .PayloadKind = Assets::AssetPayloadKind::Unknown,
                });
            if (route.has_value() &&
                Assets::IsGeometryPayloadKind(route->PayloadKind))
            {
                QueueDroppedGeometryImport(path, {route->PayloadKind});
                continue;
            }

            (void)ImportAssetFromPath(
                RuntimeAssetImportRequest{
                    .Path = path,
                    .PayloadKind = Assets::AssetPayloadKind::Unknown,
                });
        }
    }

    void Engine::QueueDroppedGeometryImport(
        std::string path,
        std::vector<Assets::AssetPayloadKind> payloadKinds)
    {
        if (!m_Initialized ||
            !m_StreamingExecutor ||
            !m_AssetService ||
            !m_Scene ||
            path.empty() ||
            payloadKinds.empty())
        {
            RuntimeAssetImportRequest request{
                .Path = std::move(path),
                .PayloadKind = payloadKinds.empty()
                    ? Assets::AssetPayloadKind::Unknown
                    : payloadKinds.front(),
            };
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState));
            return;
        }

        auto state = std::make_shared<DroppedGeometryImportState>();
        state->Request = RuntimeAssetImportRequest{
            .Path = path,
            .PayloadKind = payloadKinds.front(),
        };

        const StreamingTaskHandle handle = m_StreamingExecutor->Submit(
            StreamingTaskDesc{
                .Name = "Runtime.ImportDroppedGeometry." +
                    FileNameFromPath(path),
                .Kind = Core::Dag::TaskKind::AssetDecode,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = 4u,
                .Execute = [
                    state,
                    path = std::move(path),
                    payloadKinds = std::move(payloadKinds)]() mutable -> StreamingResult
                {
                    Core::ErrorCode lastError = Core::ErrorCode::Unknown;
                    for (const Assets::AssetPayloadKind payloadKind : payloadKinds)
                    {
                        RuntimeAssetImportRequest request{
                            .Path = path,
                            .PayloadKind = payloadKind,
                        };
                        auto decoded = DecodeGeometryImport(request);
                        state->Request = request;
                        if (decoded.has_value())
                        {
                            state->Decoded = std::move(*decoded);
                            state->Error = Core::ErrorCode::Success;
                            return StreamingResult{
                                StreamingCpuPayloadReady{.PayloadToken = 0u}};
                        }
                        lastError = decoded.error();
                    }

                    state->Error = lastError;
                    return StreamingResult{
                        StreamingCpuPayloadReady{.PayloadToken = 0u}};
                },
                .ApplyOnMainThread = [this, state](StreamingResult&&) mutable
                {
                    Core::Expected<RuntimeAssetImportResult> result =
                        Core::Err<RuntimeAssetImportResult>(state->Error);
                    if (state->Decoded.has_value())
                    {
                        auto materialized = MaterializeDecodedGeometryImport(
                            *m_AssetService,
                            *m_Scene,
                            *state->Decoded);
                        if (materialized.has_value())
                        {
                            FocusMainCameraOnImportedGeometry(
                                m_CameraControllers,
                                m_Config.Camera.Controller,
                                m_Config.Camera.Enabled,
                                materialized->Bounds);
                            (void)m_SelectionController.SetSelectedEntity(
                                *m_Scene,
                                materialized->Entity);
                            result = materialized->Result;
                        }
                        else
                        {
                            result = Core::Err<RuntimeAssetImportResult>(
                                materialized.error());
                        }
                    }
                    RecordAssetImportEvent(state->Request, result);
                },
            });

        if (!handle.IsValid())
        {
            RuntimeAssetImportRequest request{
                .Path = std::move(state->Request.Path),
                .PayloadKind = state->Request.PayloadKind,
            };
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState));
        }
    }

    void Engine::RecordAssetImportEvent(
        const RuntimeAssetImportRequest& request,
        const Core::Expected<RuntimeAssetImportResult>& result)
    {
        RuntimeAssetImportEvent event{};
        event.Sequence = ++m_AssetImportEventSequence;
        event.Path = request.Path;
        event.RequestedPayloadKind = request.PayloadKind;
        event.Error = result.has_value()
            ? Core::ErrorCode::Success
            : result.error();
        if (result.has_value())
        {
            event.Result = *result;
        }
        m_LastAssetImportEvent = std::move(event);
    }

    Core::Expected<RuntimeAssetImportResult> Engine::ImportAssetFromPathImpl(
        RuntimeAssetImportRequest request)
    {
        if (!m_Initialized ||
            !m_AssetService ||
            !m_GpuAssetCache ||
            !m_AssetModelTextureHandoff ||
            !m_AssetModelSceneHandoff)
        {
            return Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState);
        }
        if (request.Path.empty())
        {
            return Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidPath);
        }

        auto route = Assets::ResolveAssetImportRoute(
            request.Path,
            Assets::AssetRouteOperation::Import,
            Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        if (!route.has_value())
        {
            return Core::Err<RuntimeAssetImportResult>(route.error());
        }
        if (Assets::IsGeometryPayloadKind(route->PayloadKind))
        {
            auto decoded = DecodeGeometryImport(
                RuntimeAssetImportRequest{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                });
            if (!decoded.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(decoded.error());
            }

            auto materialized = MaterializeDecodedGeometryImport(
                *m_AssetService,
                *m_Scene,
                *decoded);
            if (!materialized.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(
                    materialized.error());
            }
            FocusMainCameraOnImportedGeometry(
                m_CameraControllers,
                m_Config.Camera.Controller,
                m_Config.Camera.Enabled,
                materialized->Bounds);
            (void)m_SelectionController.SetSelectedEntity(*m_Scene,
                                                          materialized->Entity);
            return materialized->Result;
        }
        if (route->PayloadKind != Assets::AssetPayloadKind::ModelScene &&
            route->PayloadKind != Assets::AssetPayloadKind::Texture2D)
        {
            return Core::Err<RuntimeAssetImportResult>(
                Core::ErrorCode::AssetUnsupportedFormat);
        }

        Assets::AssetModelTextureIOBridge bridge;
        if (Core::Result registered =
                RegisterPromotedModelTextureIOCallbacks(bridge);
            !registered.has_value())
        {
            return Core::Err<RuntimeAssetImportResult>(registered.error());
        }

        Core::IO::FileIOBackend backend;
        if (route->PayloadKind == Assets::AssetPayloadKind::ModelScene)
        {
            auto decoded = bridge.ImportModelScene(request.Path, backend);
            if (!decoded.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(decoded.error());
            }

            auto payload =
                std::make_shared<Assets::AssetModelScenePayload>(
                    std::move(*decoded));
            const AssetModelSceneHandoffDiagnostics before =
                m_AssetModelSceneHandoff->GetDiagnostics();
            auto asset = m_AssetService->Load<Assets::AssetModelScenePayload>(
                request.Path,
                [payload](std::string_view,
                          Assets::AssetId) -> Core::Expected<Assets::AssetModelScenePayload>
                {
                    return *payload;
                });
            if (!asset.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(asset.error());
            }

            DrainAssetImportEvents(*m_AssetService);
            if (m_AssetModelSceneHandoff->FindRecord(*asset) == nullptr)
            {
                if (Core::Result materialized =
                        m_AssetModelSceneHandoff->MaterializeReadyModelScene(*asset);
                    !materialized.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        materialized.error());
                }
            }

            const AssetModelSceneHandoffDiagnostics after =
                m_AssetModelSceneHandoff->GetDiagnostics();
            if (after.ModelSceneMaterializeFailures >
                    before.ModelSceneMaterializeFailures &&
                after.LastFailedAsset == *asset)
            {
                return Core::Err<RuntimeAssetImportResult>(
                    NormalizeImportError(after.LastError));
            }

            return RuntimeAssetImportResult{
                .Asset = *asset,
                .PayloadKind = route->PayloadKind,
                .PrimitiveEntitiesCreated =
                    Delta(after.PrimitiveEntitiesCreated,
                          before.PrimitiveEntitiesCreated),
                .EmbeddedTextureAssetsCreated =
                    Delta(after.EmbeddedTextureAssetsCreated,
                          before.EmbeddedTextureAssetsCreated),
                .TextureUploadRequests =
                    Delta(after.EmbeddedTextureUploadRequests,
                          before.EmbeddedTextureUploadRequests),
                .MaterializedModelScene =
                    after.ModelSceneMaterializeSuccesses >
                        before.ModelSceneMaterializeSuccesses,
            };
        }

        auto decoded = bridge.ImportTexture2D(request.Path, backend);
        if (!decoded.has_value())
        {
            return Core::Err<RuntimeAssetImportResult>(decoded.error());
        }

        auto payload =
            std::make_shared<Assets::AssetTexture2DPayload>(std::move(*decoded));
        const AssetModelTextureHandoffDiagnostics before =
            m_AssetModelTextureHandoff->GetDiagnostics();
        auto asset = m_AssetService->Load<Assets::AssetTexture2DPayload>(
            request.Path,
            [payload](std::string_view,
                      Assets::AssetId) -> Core::Expected<Assets::AssetTexture2DPayload>
            {
                return *payload;
            });
        if (!asset.has_value())
        {
            return Core::Err<RuntimeAssetImportResult>(asset.error());
        }

        DrainAssetImportEvents(*m_AssetService);
        AssetModelTextureHandoffDiagnostics after =
            m_AssetModelTextureHandoff->GetDiagnostics();
        const bool uploadWasAlreadyHandled =
            after.TextureUploadRequests > before.TextureUploadRequests ||
            after.TextureUploadDeferrals > before.TextureUploadDeferrals ||
            after.TextureUploadFailures > before.TextureUploadFailures;

        if (!uploadWasAlreadyHandled &&
            m_GpuAssetCache->GetState(*asset) == Graphics::GpuAssetState::NotRequested)
        {
            if (Core::Result uploaded =
                    m_AssetModelTextureHandoff->UploadReadyTexture(*asset);
                !uploaded.has_value())
            {
                if (!IsTextureUploadDeferral(uploaded.error()))
                {
                    return Core::Err<RuntimeAssetImportResult>(uploaded.error());
                }
            }
        }

        after = m_AssetModelTextureHandoff->GetDiagnostics();
        if (after.TextureUploadFailures > before.TextureUploadFailures &&
            after.LastFailedAsset == *asset)
        {
            return Core::Err<RuntimeAssetImportResult>(
                NormalizeImportError(after.LastError));
        }

        return RuntimeAssetImportResult{
            .Asset = *asset,
            .PayloadKind = route->PayloadKind,
            .TextureUploadRequests =
                Delta(after.TextureUploadRequests, before.TextureUploadRequests),
            .RequestedTextureUpload =
                after.TextureUploadRequests > before.TextureUploadRequests,
        };
    }

    void Engine::ClearSceneRuntimeState()
    {
        if (m_Renderer)
            m_RenderExtraction.ClearSceneState(*m_Renderer);
        if (m_Scene)
            m_SelectionController.ClearSceneState(*m_Scene);
        m_LastRefinedPrimitive.reset();
    }

    Core::Expected<SceneSerializationResult> Engine::SaveSceneToPath(
        std::string path)
    {
        if (!m_Initialized || !m_Scene)
            return Core::Err<SceneSerializationResult>(Core::ErrorCode::InvalidState);
        if (path.empty())
            return Core::Err<SceneSerializationResult>(Core::ErrorCode::InvalidPath);

        Core::IO::FileIOBackend backend;
        auto saved = SaveSceneDocument(*m_Scene, path, backend);
        if (saved.has_value())
            m_EditorCommandHistory.MarkSaved(path);
        return saved;
    }

    Core::Expected<SceneDeserializationResult> Engine::LoadSceneFromPath(
        std::string path)
    {
        if (!m_Initialized || !m_Scene)
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidState);
        if (path.empty())
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidPath);

        Core::IO::FileIOBackend backend;
        ECS::Scene::Registry loadedScene;
        auto loaded = LoadSceneDocument(loadedScene, path, backend);
        if (!loaded.has_value())
            return Core::Err<SceneDeserializationResult>(loaded.error());

        ClearSceneRuntimeState();
        m_Scene->Clear();
        m_Scene->Raw() = std::move(loadedScene.Raw());
        m_StableEntityLookup.Rebuild(*m_Scene);
        m_EditorCommandHistory.ResetDocument(path);
        return loaded;
    }

    Core::Result Engine::NewSceneDocument()
    {
        if (!m_Initialized || !m_Scene)
            return Core::Err(Core::ErrorCode::InvalidState);

        ClearSceneRuntimeState();
        m_Scene->Clear();
        m_StableEntityLookup.Clear();
        m_EditorCommandHistory.ResetDocument();
        return Core::Ok();
    }

    Core::Result Engine::CloseSceneDocument()
    {
        return NewSceneDocument();
    }

    SelectionController&  Engine::GetSelectionController() noexcept { return m_SelectionController; }
    EditorCommandHistory& Engine::GetEditorCommandHistory() noexcept
    {
        return m_EditorCommandHistory;
    }
    const EditorCommandHistory& Engine::GetEditorCommandHistory() const noexcept
    {
        return m_EditorCommandHistory;
    }
    const std::optional<PrimitiveSelectionResult>&
    Engine::GetLastRefinedPrimitiveSelection() const noexcept { return m_LastRefinedPrimitive; }
    Core::FrameGraph&     Engine::GetFrameGraph()    noexcept { return *m_FrameGraph;    }
    Core::Dag::TaskGraph& Engine::GetStreamingGraph() noexcept { return *m_StreamingGraph; }

    ReferenceSceneRegistry& Engine::GetReferenceSceneRegistry() noexcept
    {
        return m_ReferenceSceneRegistry;
    }

    bool Engine::IsReferenceSceneInstalled() const noexcept
    {
        return m_ReferenceSceneInstalled;
    }

    const std::optional<Graphics::CameraViewInput>& Engine::GetReferenceCameraSeed() const noexcept
    {
        return m_ReferenceCamera;
    }

    CameraControllerRegistry& Engine::GetCameraControllerRegistry() noexcept
    {
        return m_CameraControllers;
    }

    void Engine::SetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId,
        const MeshPrimitiveViewSettings settings)
    {
        if (!m_Scene)
        {
            return;
        }

        namespace G = Graphics::Components;
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(stableEntityId);
        entt::registry& raw = m_Scene->Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
        {
            return;
        }

        if (settings.EnableEdgeView)
        {
            raw.emplace_or_replace<G::RenderEdges>(entity);
        }
        else if (raw.all_of<G::RenderEdges>(entity))
        {
            raw.remove<G::RenderEdges>(entity);
        }

        if (settings.EnableVertexView)
        {
            G::RenderPoints points =
                raw.all_of<G::RenderPoints>(entity)
                    ? raw.get<G::RenderPoints>(entity)
                    : G::RenderPoints{};
            points.Type = ToRenderPointType(settings.VertexRenderMode);
            points.SizeSource = settings.VertexPointRadiusPx;
            raw.emplace_or_replace<G::RenderPoints>(entity, points);
        }
        else if (raw.all_of<G::RenderPoints>(entity))
        {
            raw.remove<G::RenderPoints>(entity);
        }

        m_RenderExtraction.ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    void Engine::ClearMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) noexcept
    {
        if (m_Scene)
        {
            namespace G = Graphics::Components;
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            entt::registry& raw = m_Scene->Raw();
            if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
            {
                raw.remove<G::RenderEdges, G::RenderPoints>(entity);
            }
        }
        m_RenderExtraction.ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    MeshPrimitiveViewSettings Engine::GetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) const noexcept
    {
        MeshPrimitiveViewSettings settings{};
        if (!m_Scene)
        {
            return settings;
        }

        namespace G = Graphics::Components;
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(stableEntityId);
        const entt::registry& raw = m_Scene->Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
        {
            return settings;
        }

        settings.EnableEdgeView = raw.all_of<G::RenderEdges>(entity);
        if (const auto* points = raw.try_get<G::RenderPoints>(entity))
        {
            settings.EnableVertexView = true;
            settings.VertexRenderMode = ToMeshVertexViewRenderMode(points->Type);
            if (const auto* uniform = std::get_if<float>(&points->SizeSource))
            {
                settings.VertexPointRadiusPx = *uniform;
            }
        }
        return settings;
    }

    void Engine::SetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId,
        RenderExtractionCache::VisualizationAdapterBinding binding)
    {
        m_RenderExtraction.SetVisualizationAdapterBinding(
            stableEntityId,
            std::move(binding));
    }

    void Engine::ClearVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) noexcept
    {
        m_RenderExtraction.ClearVisualizationAdapterBinding(stableEntityId);
    }

    std::optional<RenderExtractionCache::VisualizationAdapterBinding>
    Engine::GetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_RenderExtraction.GetVisualizationAdapterBinding(stableEntityId);
    }

    void Engine::SetImGuiEditorCallback(std::function<void()> callback)
    {
        m_ImGuiEditorCallback = std::move(callback);
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->SetEditorCallback(m_ImGuiEditorCallback);
    }

    const ImGuiAdapter& Engine::GetImGuiAdapter() const noexcept
    {
        return *m_ImGuiAdapter;
    }
}
