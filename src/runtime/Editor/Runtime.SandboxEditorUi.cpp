module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

module Extrinsic.Runtime.SandboxEditorUi;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Geometry.KMeans;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace A = Extrinsic::Assets;
        namespace GK = Geometry::KMeans;

        inline constexpr std::array<A::AssetPayloadKind, 6> kImportPayloadKinds{{
            A::AssetPayloadKind::Unknown,
            A::AssetPayloadKind::Mesh,
            A::AssetPayloadKind::PointCloud,
            A::AssetPayloadKind::Graph,
            A::AssetPayloadKind::ModelScene,
            A::AssetPayloadKind::Texture2D,
        }};

        struct KMeansUiState
        {
            std::optional<SandboxEditorKMeansResult>* LastResult{nullptr};
            SandboxEditorGeometryProcessingDomain* Domain{nullptr};
            std::int32_t* ClusterCount{nullptr};
            std::int32_t* MaxIterations{nullptr};
            std::int32_t* Seed{nullptr};
            bool* UseHierarchicalInitialization{nullptr};
        };

        enum class DomainWindowSection : std::uint8_t
        {
            Render = 0,
            Visualization = 1,
            Selection = 2,
            Processing = 3,
            Count = 4,
        };

        [[nodiscard]] GS::Domain ExpectedDomainForWindowKind(
            const SandboxEditorDomainWindowKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorDomainWindowKind::Mesh:
                return GS::Domain::Mesh;
            case SandboxEditorDomainWindowKind::Graph:
                return GS::Domain::Graph;
            case SandboxEditorDomainWindowKind::PointCloud:
                return GS::Domain::PointCloud;
            }
            return GS::Domain::None;
        }

        [[nodiscard]] const char* DomainWindowTitle(
            const SandboxEditorDomainWindowKind kind,
            const DomainWindowSection section) noexcept
        {
            switch (kind)
            {
            case SandboxEditorDomainWindowKind::Mesh:
                switch (section)
                {
                case DomainWindowSection::Render: return "Mesh / Render";
                case DomainWindowSection::Visualization: return "Mesh / Visualization";
                case DomainWindowSection::Selection: return "Mesh / Selection";
                case DomainWindowSection::Processing: return "Mesh / Processing";
                case DomainWindowSection::Count: break;
                }
                break;
            case SandboxEditorDomainWindowKind::Graph:
                switch (section)
                {
                case DomainWindowSection::Render: return "Graph / Render";
                case DomainWindowSection::Visualization: return "Graph / Visualization";
                case DomainWindowSection::Selection: return "Graph / Selection";
                case DomainWindowSection::Processing: return "Graph / Processing";
                case DomainWindowSection::Count: break;
                }
                break;
            case SandboxEditorDomainWindowKind::PointCloud:
                switch (section)
                {
                case DomainWindowSection::Render: return "PointCloud / Render";
                case DomainWindowSection::Visualization: return "PointCloud / Visualization";
                case DomainWindowSection::Selection: return "PointCloud / Selection";
                case DomainWindowSection::Processing: return "PointCloud / Processing";
                case DomainWindowSection::Count: break;
                }
                break;
            }
            return "Domain / Unknown";
        }

        [[nodiscard]] std::size_t DomainWindowSlotIndex(
            const SandboxEditorDomainWindowKind kind,
            const DomainWindowSection section) noexcept
        {
            const auto kindIndex = static_cast<std::size_t>(kind);
            const auto sectionIndex = static_cast<std::size_t>(section);
            return kindIndex * static_cast<std::size_t>(DomainWindowSection::Count) +
                   sectionIndex;
        }

        [[nodiscard]] std::string ErrorName(const Core::ErrorCode error)
        {
            return std::string(Core::Error::ToString(error));
        }

        [[nodiscard]] std::string BuildImportSuccessMessage(
            const SandboxEditorFileImportCommand& command,
            const SandboxEditorFileImportResult& result)
        {
            std::string message = "Imported ";
            message += A::DebugNameForAssetPayloadKind(result.PayloadKind);
            message += " asset";
            if (!command.Path.empty())
            {
                message += " from ";
                message += command.Path;
            }
            message += ".";
            return message;
        }

        [[nodiscard]] std::string BuildImportFailureMessage(
            const Core::ErrorCode error)
        {
            std::string message = "Asset import failed: ";
            message += ErrorName(error);
            message += ".";
            return message;
        }

        [[nodiscard]] SandboxEditorFileImportResult BuildFileImportResultFromRuntimeEvent(
            const RuntimeAssetImportEvent& event)
        {
            if (!event.Result.has_value())
            {
                return SandboxEditorFileImportResult{
                    .Status = SandboxEditorCommandStatus::AssetImportFailed,
                    .PayloadKind = event.RequestedPayloadKind,
                    .Error = event.Error,
                    .Message = BuildImportFailureMessage(event.Error),
                };
            }

            const RuntimeAssetImportResult& imported = *event.Result;
            SandboxEditorFileImportResult result{
                .Status = SandboxEditorCommandStatus::Applied,
                .Asset = imported.Asset,
                .PayloadKind = imported.PayloadKind,
                .Error = Core::ErrorCode::Success,
                .PrimitiveEntitiesCreated = imported.PrimitiveEntitiesCreated,
                .EmbeddedTextureAssetsCreated = imported.EmbeddedTextureAssetsCreated,
                .TextureUploadRequests = imported.TextureUploadRequests,
                .MaterializedModelScene = imported.MaterializedModelScene,
                .RequestedTextureUpload = imported.RequestedTextureUpload,
            };
            result.Message = BuildImportSuccessMessage(
                SandboxEditorFileImportCommand{
                    .Path = event.Path,
                    .PayloadKind = event.RequestedPayloadKind,
                },
                result);
            return result;
        }

        [[nodiscard]] std::string BuildSceneFileSuccessMessage(
            const SandboxEditorSceneFileCommand& command,
            const SandboxEditorSceneFileResult& result)
        {
            std::string message{};
            switch (result.Operation)
            {
            case SandboxEditorSceneFileOperation::New:
                message = "Created new scene";
                break;
            case SandboxEditorSceneFileOperation::Save:
                message = "Saved scene";
                break;
            case SandboxEditorSceneFileOperation::Load:
                message = "Opened scene";
                break;
            case SandboxEditorSceneFileOperation::Close:
                message = "Closed scene";
                break;
            }
            if (!command.Path.empty())
            {
                if (result.Operation == SandboxEditorSceneFileOperation::Save)
                    message += " to ";
                else if (result.Operation == SandboxEditorSceneFileOperation::Load)
                    message += " from ";
                else
                    message += " ";
                message += command.Path;
            }
            message += " (entities=";
            message += std::to_string(result.Stats.Entities);
            message += ", mesh=";
            message += std::to_string(result.Stats.MeshEntities);
            message += ", graph=";
            message += std::to_string(result.Stats.GraphEntities);
            message += ", pointCloud=";
            message += std::to_string(result.Stats.PointCloudEntities);
            message += ").";
            return message;
        }

        [[nodiscard]] std::string BuildSceneFileFailureMessage(
            const SandboxEditorSceneFileOperation operation,
            const Core::ErrorCode error)
        {
            std::string message{};
            switch (operation)
            {
            case SandboxEditorSceneFileOperation::New:
                message = "Scene new failed: ";
                break;
            case SandboxEditorSceneFileOperation::Save:
                message = "Scene save failed: ";
                break;
            case SandboxEditorSceneFileOperation::Load:
                message = "Scene open failed: ";
                break;
            case SandboxEditorSceneFileOperation::Close:
                message = "Scene close failed: ";
                break;
            }
            message += ErrorName(error);
            message += ".";
            return message;
        }


        [[nodiscard]] SandboxEditorSpatialDebugBindingModel FromSpatialDebugBinding(
            const ECSC::SpatialDebugBinding& binding) noexcept
        {
            return SandboxEditorSpatialDebugBindingModel{
                .HasBinding = true,
                .Kind = binding.Kind,
                .RegistryKey = binding.RegistryKey,
                .LeafOnly = binding.LeafOnly,
                .OccupancyOnly = binding.OccupancyOnly,
                .MaxDepth = binding.MaxDepth,
            };
        }

        [[nodiscard]] ECSC::SpatialDebugBinding ToSpatialDebugBinding(
            const SandboxEditorSpatialDebugBindingCommand& command) noexcept
        {
            return ECSC::SpatialDebugBinding{
                .Kind = command.Kind,
                .RegistryKey = command.RegistryKey,
                .LeafOnly = command.LeafOnly,
                .OccupancyOnly = command.OccupancyOnly,
                .MaxDepth = command.MaxDepth,
            };
        }

        [[nodiscard]] bool SameSpatialDebugBinding(
            const ECSC::SpatialDebugBinding& lhs,
            const ECSC::SpatialDebugBinding& rhs) noexcept
        {
            return lhs.Kind == rhs.Kind &&
                   lhs.RegistryKey == rhs.RegistryKey &&
                   lhs.LeafOnly == rhs.LeafOnly &&
                   lhs.OccupancyOnly == rhs.OccupancyOnly &&
                   lhs.MaxDepth == rhs.MaxDepth;
        }

        [[nodiscard]] SandboxEditorVisualizationConfigModel FromVisualizationConfig(
            const G::VisualizationConfig& config)
        {
            return SandboxEditorVisualizationConfigModel{
                .HasConfig = true,
                .Source = config.Source,
                .Color = config.Color,
                .ScalarFieldName = config.ScalarFieldName,
                .ScalarDomain = config.ScalarDomain,
                .ColorBufferName = config.ColorBufferName,
                .ScalarAutoRange = config.Scalar.AutoRange,
                .ScalarRangeMin = config.Scalar.RangeMin,
                .ScalarRangeMax = config.Scalar.RangeMax,
                .ScalarBinCount = config.Scalar.BinCount,
                .IsolineCount = config.Scalar.Isolines.Num,
            };
        }

        [[nodiscard]] G::VisualizationConfig ToVisualizationConfig(
            const SandboxEditorVisualizationConfigCommand& command)
        {
            G::VisualizationConfig config{};
            config.Source = command.Source;
            config.Color = command.Color;
            config.ScalarFieldName = command.ScalarFieldName;
            config.ScalarDomain = command.ScalarDomain;
            config.ColorBufferName = command.ColorBufferName;
            config.Scalar.AutoRange = command.ScalarAutoRange;
            config.Scalar.RangeMin = command.ScalarRangeMin;
            config.Scalar.RangeMax = command.ScalarRangeMax;
            config.Scalar.BinCount = command.ScalarBinCount;
            config.Scalar.Isolines.Num = command.IsolineCount;
            return config;
        }

        [[nodiscard]] bool SameVisualizationConfig(
            const G::VisualizationConfig& lhs,
            const G::VisualizationConfig& rhs) noexcept
        {
            return lhs.Source == rhs.Source &&
                   lhs.Color.x == rhs.Color.x &&
                   lhs.Color.y == rhs.Color.y &&
                   lhs.Color.z == rhs.Color.z &&
                   lhs.Color.w == rhs.Color.w &&
                   lhs.ScalarFieldName == rhs.ScalarFieldName &&
                   lhs.ScalarDomain == rhs.ScalarDomain &&
                   lhs.ColorBufferName == rhs.ColorBufferName &&
                   lhs.Scalar.AutoRange == rhs.Scalar.AutoRange &&
                   lhs.Scalar.RangeMin == rhs.Scalar.RangeMin &&
                   lhs.Scalar.RangeMax == rhs.Scalar.RangeMax &&
                   lhs.Scalar.BinCount == rhs.Scalar.BinCount &&
                   lhs.Scalar.Isolines.Num == rhs.Scalar.Isolines.Num;
        }

        [[nodiscard]] bool IsInternalVisualizationProperty(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == GS::PropertyNames::kNormal ||
                   name == GS::PropertyNames::kEdgeV0 ||
                   name == GS::PropertyNames::kEdgeV1 ||
                   name == GS::PropertyNames::kHalfedgeToVertex ||
                   name == GS::PropertyNames::kHalfedgeNext ||
                   name == GS::PropertyNames::kHalfedgeFace ||
                   name == GS::PropertyNames::kFaceHalfedge ||
                   name == "v:point" ||
                   name == "v:tex" ||
                   name == "v:texcoord" ||
                   name == "p:position" ||
                   name == "p:normal";
        }

        [[nodiscard]] bool IsConnectivityVisualizationProperty(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == GS::PropertyNames::kEdgeV0 ||
                   name == GS::PropertyNames::kEdgeV1 ||
                   name == GS::PropertyNames::kHalfedgeToVertex ||
                   name == GS::PropertyNames::kHalfedgeNext ||
                   name == GS::PropertyNames::kHalfedgeFace ||
                   name == GS::PropertyNames::kFaceHalfedge ||
                   name == "v:point" ||
                   name == "v:tex" ||
                   name == "v:texcoord" ||
                   name == "p:position";
        }

        [[nodiscard]] std::optional<SandboxEditorVisualizationPropertyValueKind>
        DetectVisualizationPropertyKind(
            const Geometry::PropertySet& properties,
            const std::string& name)
        {
            if (properties.Get<float>(name))
                return SandboxEditorVisualizationPropertyValueKind::ScalarFloat;
            if (properties.Get<double>(name))
                return SandboxEditorVisualizationPropertyValueKind::ScalarDouble;
            if (properties.Get<glm::vec3>(name))
                return SandboxEditorVisualizationPropertyValueKind::Vec3;
            if (properties.Get<glm::vec4>(name))
                return SandboxEditorVisualizationPropertyValueKind::Vec4;
            if (properties.Get<std::uint32_t>(name))
                return SandboxEditorVisualizationPropertyValueKind::UInt32;
            return std::nullopt;
        }

        [[nodiscard]] bool IsScalarVisualizationKind(
            const SandboxEditorVisualizationPropertyValueKind kind) noexcept
        {
            return kind ==
                       SandboxEditorVisualizationPropertyValueKind::ScalarFloat ||
                   kind ==
                       SandboxEditorVisualizationPropertyValueKind::ScalarDouble;
        }

        [[nodiscard]] bool DomainSupportsVisualizationConfig(
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
            case Domain::MeshEdges:
            case Domain::MeshFaces:
            case Domain::GraphVertices:
            case Domain::GraphEdges:
            case Domain::PointCloudPoints:
                return true;
            }
            return false;
        }

        [[nodiscard]] G::VisualizationConfig::Domain ToVisualizationConfigDomain(
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshEdges:
            case Domain::GraphEdges:
                return G::VisualizationConfig::Domain::Edge;
            case Domain::MeshFaces:
                return G::VisualizationConfig::Domain::Face;
            case Domain::MeshVertices:
            case Domain::GraphVertices:
            case Domain::PointCloudPoints:
                return G::VisualizationConfig::Domain::Vertex;
            }
            return G::VisualizationConfig::Domain::Vertex;
        }

        [[nodiscard]] G::VisualizationConfig::ColorSource ToColorBufferSource(
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshEdges:
            case Domain::GraphEdges:
                return G::VisualizationConfig::ColorSource::PerEdgeBuffer;
            case Domain::MeshFaces:
                return G::VisualizationConfig::ColorSource::PerFaceBuffer;
            case Domain::MeshVertices:
            case Domain::GraphVertices:
            case Domain::PointCloudPoints:
                return G::VisualizationConfig::ColorSource::PerVertexBuffer;
            }
            return G::VisualizationConfig::ColorSource::PerVertexBuffer;
        }

        [[nodiscard]] const Geometry::PropertySet* PropertySetForVisualizationDomain(
            const GS::ConstSourceView& view,
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return view.ActiveDomain == GS::Domain::Mesh &&
                               view.VertexSource != nullptr
                           ? &view.VertexSource->Properties
                           : nullptr;
            case Domain::MeshEdges:
                return view.ActiveDomain == GS::Domain::Mesh &&
                               view.EdgeSource != nullptr
                           ? &view.EdgeSource->Properties
                           : nullptr;
            case Domain::MeshFaces:
                return view.ActiveDomain == GS::Domain::Mesh &&
                               view.FaceSource != nullptr
                           ? &view.FaceSource->Properties
                           : nullptr;
            case Domain::GraphVertices:
                return view.ActiveDomain == GS::Domain::Graph &&
                               view.NodeSource != nullptr
                           ? &view.NodeSource->Properties
                           : nullptr;
            case Domain::GraphEdges:
                return view.ActiveDomain == GS::Domain::Graph &&
                               view.EdgeSource != nullptr
                           ? &view.EdgeSource->Properties
                           : nullptr;
            case Domain::PointCloudPoints:
                return view.ActiveDomain == GS::Domain::PointCloud &&
                               view.VertexSource != nullptr
                           ? &view.VertexSource->Properties
                           : nullptr;
            }
            return nullptr;
        }

        void AppendVisualizationPropertiesForDomain(
            std::vector<SandboxEditorVisualizationPropertyInfo>& out,
            const Geometry::PropertySet& properties,
            const SandboxEditorVisualizationPropertyDomain domain)
        {
            if (!DomainSupportsVisualizationConfig(domain))
                return;

            for (const std::string& name : properties.Properties())
            {
                const std::optional<SandboxEditorVisualizationPropertyValueKind>
                    kind = DetectVisualizationPropertyKind(properties, name);
                if (!kind.has_value())
                    continue;

                const bool internal = IsInternalVisualizationProperty(name);
                const bool connectivity =
                    IsConnectivityVisualizationProperty(name);
                const bool scalar =
                    !internal && IsScalarVisualizationKind(*kind);
                const bool color =
                    !internal &&
                    *kind == SandboxEditorVisualizationPropertyValueKind::Vec4;
                const bool vector =
                    !connectivity &&
                    *kind == SandboxEditorVisualizationPropertyValueKind::Vec3;
                const bool integer =
                    !internal && !connectivity &&
                    *kind == SandboxEditorVisualizationPropertyValueKind::UInt32;
                if (!scalar && !color && !vector && !integer)
                {
                    continue;
                }

                out.push_back(SandboxEditorVisualizationPropertyInfo{
                    .Name = name,
                    .Domain = domain,
                    .ValueKind = *kind,
                    .ElementCount = properties.Size(),
                    .ScalarPresetAvailable = scalar,
                    .IsolinePresetAvailable = scalar,
                    .ColorBufferPresetAvailable = color,
                    .VectorFieldCandidate = vector,
                });
            }
        }

        [[nodiscard]] std::vector<SandboxEditorVisualizationPropertyInfo>
        BuildVisualizationProperties(const GS::ConstSourceView& view)
        {
            std::vector<SandboxEditorVisualizationPropertyInfo> out{};
            const auto append =
                [&](const SandboxEditorVisualizationPropertyDomain domain)
                {
                    if (const Geometry::PropertySet* properties =
                            PropertySetForVisualizationDomain(view, domain))
                    {
                        AppendVisualizationPropertiesForDomain(
                            out,
                            *properties,
                            domain);
                    }
                };

            switch (view.ActiveDomain)
            {
            case GS::Domain::Mesh:
                append(SandboxEditorVisualizationPropertyDomain::MeshVertices);
                append(SandboxEditorVisualizationPropertyDomain::MeshEdges);
                append(SandboxEditorVisualizationPropertyDomain::MeshFaces);
                break;
            case GS::Domain::Graph:
                append(SandboxEditorVisualizationPropertyDomain::GraphVertices);
                append(SandboxEditorVisualizationPropertyDomain::GraphEdges);
                break;
            case GS::Domain::PointCloud:
                append(SandboxEditorVisualizationPropertyDomain::PointCloudPoints);
                break;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return out;
        }

        [[nodiscard]] bool PropertySupportsPreset(
            const SandboxEditorVisualizationPropertyInfo& property,
            const SandboxEditorVisualizationPropertyPreset preset) noexcept
        {
            switch (preset)
            {
            case SandboxEditorVisualizationPropertyPreset::Scalar:
                return property.ScalarPresetAvailable;
            case SandboxEditorVisualizationPropertyPreset::Isoline:
                return property.IsolinePresetAvailable;
            case SandboxEditorVisualizationPropertyPreset::ColorBuffer:
                return property.ColorBufferPresetAvailable;
            }
            return false;
        }

        [[nodiscard]] SandboxEditorVisualizationAdapterBindingModel
        FromVisualizationAdapterBinding(
            const RenderExtractionCache::VisualizationAdapterBinding& binding)
        {
            return SandboxEditorVisualizationAdapterBindingModel{
                .HasBinding = true,
                .AdapterKey = binding.AdapterKey,
                .BufferBDA = binding.BufferBDA,
                .Kind = binding.Kind,
                .Options = binding.Options,
            };
        }

        [[nodiscard]] RenderExtractionCache::VisualizationAdapterBinding
        ToVisualizationAdapterBinding(
            const SandboxEditorVisualizationAdapterBindingCommand& command)
        {
            return RenderExtractionCache::VisualizationAdapterBinding{
                .AdapterKey = command.AdapterKey,
                .BufferBDA = command.BufferBDA,
                .Kind = command.Kind,
                .Options = command.Options,
            };
        }

        [[nodiscard]] const char* RenderSurfaceDomainName(
            const G::RenderSurface::SourceDomain domain) noexcept
        {
            switch (domain)
            {
            case G::RenderSurface::SourceDomain::Vertex: return "Vertex";
            case G::RenderSurface::SourceDomain::Face: return "Face";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* RenderEdgeDomainName(
            const G::RenderEdges::SourceDomain domain) noexcept
        {
            switch (domain)
            {
            case G::RenderEdges::SourceDomain::Vertex: return "Vertex";
            case G::RenderEdges::SourceDomain::Edge: return "Edge";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* RenderPointTypeName(
            const G::RenderPoints::RenderType type) noexcept
        {
            switch (type)
            {
            case G::RenderPoints::RenderType::Flat: return "Flat";
            case G::RenderPoints::RenderType::Sphere: return "Sphere";
            case G::RenderPoints::RenderType::Surfel: return "Surfel";
            }
            return "Unknown";
        }

        struct SandboxEditorRenderHintState
        {
            std::optional<G::RenderSurface> Surface{};
            std::optional<G::RenderEdges> Edges{};
            std::optional<G::RenderPoints> Points{};
        };

        [[nodiscard]] SandboxEditorRenderHintState ReadRenderHintState(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorRenderHintState state{};
            if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
                state.Surface = *surface;
            if (const auto* lines = raw.try_get<G::RenderEdges>(entity))
                state.Edges = *lines;
            if (const auto* points = raw.try_get<G::RenderPoints>(entity))
                state.Points = *points;
            return state;
        }

        [[nodiscard]] bool SameRenderSurface(
            const G::RenderSurface& lhs,
            const G::RenderSurface& rhs) noexcept
        {
            return lhs.Domain == rhs.Domain;
        }

        [[nodiscard]] bool SameRenderEdges(
            const G::RenderEdges& lhs,
            const G::RenderEdges& rhs)
        {
            return lhs.Domain == rhs.Domain &&
                   lhs.WidthSource == rhs.WidthSource;
        }

        [[nodiscard]] bool SameRenderPoints(
            const G::RenderPoints& lhs,
            const G::RenderPoints& rhs)
        {
            return lhs.Type == rhs.Type &&
                   lhs.SizeSource == rhs.SizeSource;
        }

        template <typename T, typename SameFn>
        [[nodiscard]] bool SameOptionalRenderComponent(
            const std::optional<T>& lhs,
            const std::optional<T>& rhs,
            SameFn same)
        {
            if (lhs.has_value() != rhs.has_value())
                return false;
            if (!lhs.has_value())
                return true;
            return same(*lhs, *rhs);
        }

        [[nodiscard]] bool SameRenderHintState(
            const SandboxEditorRenderHintState& lhs,
            const SandboxEditorRenderHintState& rhs)
        {
            return SameOptionalRenderComponent(
                       lhs.Surface, rhs.Surface, SameRenderSurface) &&
                   SameOptionalRenderComponent(
                       lhs.Edges, rhs.Edges, SameRenderEdges) &&
                   SameOptionalRenderComponent(
                       lhs.Points, rhs.Points, SameRenderPoints);
        }

        [[nodiscard]] bool IsFinitePositive(const float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] bool AnyRenderHintEdit(
            const SandboxEditorRenderHintCommand& command) noexcept
        {
            return command.SetSurface ||
                   command.SetEdges ||
                   command.SetUniformEdgeWidth ||
                   command.SetPoints ||
                   command.SetPointRenderType ||
                   command.SetUniformPointSize;
        }

        [[nodiscard]] bool RenderHintCommandMatchesDomain(
            const SandboxEditorRenderHintCommand& command,
            const GS::Domain domain) noexcept
        {
            const bool editsSurface = command.SetSurface;
            const bool editsEdges =
                command.SetEdges || command.SetUniformEdgeWidth;
            const bool editsPoints =
                command.SetPoints ||
                command.SetPointRenderType ||
                command.SetUniformPointSize;

            switch (domain)
            {
            case GS::Domain::Mesh:
                return editsSurface || editsEdges || editsPoints;
            case GS::Domain::Graph:
                return !editsSurface && (editsEdges || editsPoints);
            case GS::Domain::PointCloud:
                return !editsSurface && !editsEdges && editsPoints;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                return false;
            }
            return false;
        }

        [[nodiscard]] SandboxEditorRenderHintState ApplyRenderHintCommandToState(
            SandboxEditorRenderHintState state,
            const SandboxEditorRenderHintCommand& command)
        {
            if (command.SetSurface)
            {
                if (command.EnableSurface)
                {
                    G::RenderSurface surface =
                        state.Surface.value_or(G::RenderSurface{});
                    surface.Domain = command.SurfaceDomain;
                    state.Surface = surface;
                }
                else
                {
                    state.Surface.reset();
                }
            }

            if (command.SetEdges)
            {
                if (command.EnableEdges)
                {
                    G::RenderEdges lines =
                        state.Edges.value_or(G::RenderEdges{});
                    lines.Domain = command.EdgeDomain;
                    if (command.SetUniformEdgeWidth)
                        lines.WidthSource = command.UniformEdgeWidth;
                    state.Edges = lines;
                }
                else
                {
                    state.Edges.reset();
                }
            }
            else if (command.SetUniformEdgeWidth && state.Edges.has_value())
            {
                state.Edges->WidthSource = command.UniformEdgeWidth;
            }

            if (command.SetPoints)
            {
                if (command.EnablePoints)
                {
                    G::RenderPoints points =
                        state.Points.value_or(G::RenderPoints{});
                    points.Type = command.PointType;
                    if (command.SetUniformPointSize)
                        points.SizeSource = command.UniformPointSize;
                    state.Points = points;
                }
                else
                {
                    state.Points.reset();
                }
            }
            else if (state.Points.has_value())
            {
                if (command.SetPointRenderType)
                    state.Points->Type = command.PointType;
                if (command.SetUniformPointSize)
                    state.Points->SizeSource = command.UniformPointSize;
            }

            return state;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyRenderHintState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const SandboxEditorRenderHintState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            if (state.Surface.has_value())
                raw.emplace_or_replace<G::RenderSurface>(entity, *state.Surface);
            else if (raw.all_of<G::RenderSurface>(entity))
                raw.remove<G::RenderSurface>(entity);

            if (state.Edges.has_value())
                raw.emplace_or_replace<G::RenderEdges>(entity, *state.Edges);
            else if (raw.all_of<G::RenderEdges>(entity))
                raw.remove<G::RenderEdges>(entity);

            if (state.Points.has_value())
                raw.emplace_or_replace<G::RenderPoints>(entity, *state.Points);
            else if (raw.all_of<G::RenderPoints>(entity))
                raw.remove<G::RenderPoints>(entity);

            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] bool SameVec4(
            const glm::vec4 lhs,
            const glm::vec4 rhs) noexcept
        {
            return lhs.x == rhs.x &&
                   lhs.y == rhs.y &&
                   lhs.z == rhs.z &&
                   lhs.w == rhs.w;
        }

        [[nodiscard]] bool SameVisualizationAdapterOptions(
            const VisualizationAdapterOptions& lhs,
            const VisualizationAdapterOptions& rhs) noexcept
        {
            return lhs.SourceName == rhs.SourceName &&
                   lhs.OutputName == rhs.OutputName &&
                   lhs.Domain == rhs.Domain &&
                   lhs.BufferBDA == rhs.BufferBDA &&
                   lhs.ColorBufferBDA == rhs.ColorBufferBDA &&
                   lhs.PositionBufferBDA == rhs.PositionBufferBDA &&
                   lhs.VectorBufferBDA == rhs.VectorBufferBDA &&
                   lhs.AutoRange == rhs.AutoRange &&
                   lhs.RangeMin == rhs.RangeMin &&
                   lhs.RangeMax == rhs.RangeMax &&
                   lhs.Colormap == rhs.Colormap &&
                   lhs.IsoValueCount == rhs.IsoValueCount &&
                   lhs.LineWidth == rhs.LineWidth &&
                   SameVec4(lhs.OverlayColor, rhs.OverlayColor) &&
                   lhs.VectorScale == rhs.VectorScale &&
                   SameVec4(lhs.VectorColor, rhs.VectorColor) &&
                   lhs.DepthTested == rhs.DepthTested &&
                   lhs.EmitHtexPreview == rhs.EmitHtexPreview &&
                   lhs.EmitFragmentBake == rhs.EmitFragmentBake &&
                   lhs.SourceAttributeName == rhs.SourceAttributeName &&
                   lhs.FragmentBakeMapping == rhs.FragmentBakeMapping &&
                   lhs.MeshHasTexcoords == rhs.MeshHasTexcoords &&
                   lhs.PatchCount == rhs.PatchCount &&
                   lhs.FaceCount == rhs.FaceCount &&
                   lhs.AtlasWidth == rhs.AtlasWidth &&
                   lhs.AtlasHeight == rhs.AtlasHeight &&
                   lhs.TexcoordBufferBDA == rhs.TexcoordBufferBDA &&
                   lhs.HtexRecreatePayloadToken == rhs.HtexRecreatePayloadToken;
        }

        [[nodiscard]] bool SameVisualizationAdapterBinding(
            const RenderExtractionCache::VisualizationAdapterBinding& lhs,
            const RenderExtractionCache::VisualizationAdapterBinding& rhs) noexcept
        {
            return lhs.AdapterKey == rhs.AdapterKey &&
                   lhs.BufferBDA == rhs.BufferBDA &&
                   lhs.Kind == rhs.Kind &&
                   SameVisualizationAdapterOptions(lhs.Options, rhs.Options);
        }

        [[nodiscard]] G::RenderPoints::RenderType ToRenderPointType(
            const MeshVertexViewRenderMode mode) noexcept
        {
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

        [[nodiscard]] SandboxEditorCommandStatus ToSandboxEditorCommandStatus(
            const EditorCommandHistoryStatus status) noexcept
        {
            switch (status)
            {
            case EditorCommandHistoryStatus::Applied:
            case EditorCommandHistoryStatus::Recorded:
            case EditorCommandHistoryStatus::Undone:
            case EditorCommandHistoryStatus::Redone:
                return SandboxEditorCommandStatus::Applied;
            case EditorCommandHistoryStatus::NoChange:
                return SandboxEditorCommandStatus::NoChange;
            case EditorCommandHistoryStatus::MissingScene:
                return SandboxEditorCommandStatus::MissingScene;
            case EditorCommandHistoryStatus::MissingSelectionController:
                return SandboxEditorCommandStatus::MissingSelectionController;
            case EditorCommandHistoryStatus::StaleEntity:
                return SandboxEditorCommandStatus::StaleEntity;
            case EditorCommandHistoryStatus::MissingTransform:
                return SandboxEditorCommandStatus::MissingTransform;
            case EditorCommandHistoryStatus::EmptyUndoStack:
            case EditorCommandHistoryStatus::EmptyRedoStack:
            case EditorCommandHistoryStatus::InvalidCommand:
            case EditorCommandHistoryStatus::CommandFailed:
            case EditorCommandHistoryStatus::UndoFailed:
            case EditorCommandHistoryStatus::RedoFailed:
            case EditorCommandHistoryStatus::UnsupportedOperation:
                return SandboxEditorCommandStatus::NoChange;
            }
            return SandboxEditorCommandStatus::NoChange;
        }

        [[nodiscard]] Core::Extent2D SafeViewport(
            const Core::Extent2D commandViewport,
            const Core::Extent2D contextViewport) noexcept
        {
            if (!Core::IsEmpty(commandViewport))
                return commandViewport;
            if (!Core::IsEmpty(contextViewport))
                return contextViewport;
            return Core::Extent2D{1, 1};
        }

        constexpr SandboxEditorGeometryProcessingDomain kMeshTopologyDomains =
            SandboxEditorGeometryProcessingDomain::MeshVertices |
            SandboxEditorGeometryProcessingDomain::MeshEdges |
            SandboxEditorGeometryProcessingDomain::MeshHalfedges |
            SandboxEditorGeometryProcessingDomain::MeshFaces;

        constexpr SandboxEditorGeometryProcessingDomain kGraphTopologyDomains =
            SandboxEditorGeometryProcessingDomain::GraphVertices |
            SandboxEditorGeometryProcessingDomain::GraphEdges |
            SandboxEditorGeometryProcessingDomain::GraphHalfedges;

        constexpr SandboxEditorGeometryProcessingDomain kPointCloudDomains =
            SandboxEditorGeometryProcessingDomain::PointCloudPoints;

        [[nodiscard]] constexpr bool IsSurfaceTopologyAlgorithm(
            const SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
            case SandboxEditorGeometryProcessingAlgorithm::Remeshing:
            case SandboxEditorGeometryProcessingAlgorithm::Simplification:
            case SandboxEditorGeometryProcessingAlgorithm::Smoothing:
            case SandboxEditorGeometryProcessingAlgorithm::Subdivision:
            case SandboxEditorGeometryProcessingAlgorithm::Repair:
                return true;
            case SandboxEditorGeometryProcessingAlgorithm::KMeans:
            case SandboxEditorGeometryProcessingAlgorithm::NormalEstimation:
            case SandboxEditorGeometryProcessingAlgorithm::ShortestPath:
            case SandboxEditorGeometryProcessingAlgorithm::ConvexHull:
            case SandboxEditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            case SandboxEditorGeometryProcessingAlgorithm::VectorHeat:
            case SandboxEditorGeometryProcessingAlgorithm::Parameterization:
            case SandboxEditorGeometryProcessingAlgorithm::BooleanCSG:
            case SandboxEditorGeometryProcessingAlgorithm::Registration:
            case SandboxEditorGeometryProcessingAlgorithm::BilateralFilter:
            case SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation:
            case SandboxEditorGeometryProcessingAlgorithm::KernelDensity:
                return false;
            }
            return false;
        }

        [[nodiscard]] SandboxEditorGeometryProcessingDomain
        DomainsForSourceView(const GS::ConstSourceView& view) noexcept
        {
            switch (view.ActiveDomain)
            {
            case GS::Domain::Mesh:
                return kMeshTopologyDomains;
            case GS::Domain::Graph:
                return kGraphTopologyDomains;
            case GS::Domain::PointCloud:
                return kPointCloudDomains;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                return SandboxEditorGeometryProcessingDomain::None;
            }
            return SandboxEditorGeometryProcessingDomain::None;
        }

        [[nodiscard]] SandboxEditorDiagnostic MakeDiagnostic(
            const SandboxEditorDiagnosticCode code,
            std::string message)
        {
            return SandboxEditorDiagnostic{
                .Code = code,
                .Message = std::move(message),
            };
        }

        void AddDiagnostic(std::vector<SandboxEditorDiagnostic>& diagnostics,
                           const SandboxEditorDiagnosticCode code,
                           std::string message)
        {
            diagnostics.push_back(MakeDiagnostic(code, std::move(message)));
        }

        void AppendDiagnostics(std::vector<SandboxEditorDiagnostic>& destination,
                               const std::vector<SandboxEditorDiagnostic>& source)
        {
            destination.insert(destination.end(), source.begin(), source.end());
        }

        [[nodiscard]] std::string FallbackEntityName(const ECS::EntityHandle entity)
        {
            return "Entity " + std::to_string(
                static_cast<std::uint32_t>(entity));
        }

        [[nodiscard]] SandboxEditorEntityRow BuildEntityRow(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorEntityRow row{};
            row.Entity = entity;
            row.StableEntityId = SelectionController::ToStableEntityId(entity);
            row.Name = FallbackEntityName(entity);

            if (const auto* meta = raw.try_get<ECSC::MetaData>(entity);
                meta != nullptr && !meta->EntityName.empty())
            {
                row.Name = meta->EntityName;
            }

            if (const auto* stableId = raw.try_get<ECSC::StableId>(entity))
            {
                row.DurableStableId = *stableId;
                row.HasDurableStableId = ECSC::IsValid(*stableId);
            }

            row.Selectable = raw.all_of<Sel::SelectableTag>(entity);
            row.Selected = raw.all_of<Sel::SelectedTag>(entity);
            row.Hovered = raw.all_of<Sel::HoveredTag>(entity);
            return row;
        }

        [[nodiscard]] SandboxEditorTransformModel BuildTransformModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorTransformModel model{};
            if (const auto* local = raw.try_get<ECSC::Transform::Component>(entity))
            {
                model.HasLocalTransform = true;
                model.LocalPosition = local->Position;
                model.LocalRotation = local->Rotation;
                model.LocalScale = local->Scale;
            }

            if (const auto* world = raw.try_get<ECSC::Transform::WorldMatrix>(entity))
            {
                model.HasWorldTransform = true;
                model.WorldPosition = glm::vec3(world->Matrix[3]);
            }

            return model;
        }

        [[nodiscard]] SandboxEditorRenderHintModel BuildRenderHintModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorRenderHintModel model{};
            if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
            {
                model.HasRenderSurface = true;
                model.SurfaceDomainValue = surface->Domain;
                model.SurfaceDomain = RenderSurfaceDomainName(surface->Domain);
            }

            if (const auto* lines = raw.try_get<G::RenderEdges>(entity))
            {
                model.HasRenderEdges = true;
                model.EdgeDomainValue = lines->Domain;
                model.EdgeDomain = RenderEdgeDomainName(lines->Domain);
                if (const auto* width = std::get_if<float>(&lines->WidthSource))
                {
                    model.HasUniformEdgeWidth = true;
                    model.UniformEdgeWidth = *width;
                }
                else if (const auto* name = std::get_if<std::string>(&lines->WidthSource))
                {
                    model.HasNamedEdgeWidth = true;
                    model.EdgeWidthName = *name;
                }
            }

            if (const auto* points = raw.try_get<G::RenderPoints>(entity))
            {
                model.HasRenderPoints = true;
                model.PointRenderTypeValue = points->Type;
                model.PointRenderType = RenderPointTypeName(points->Type);

                if (const auto* size = std::get_if<float>(&points->SizeSource))
                {
                    model.HasUniformPointSize = true;
                    model.UniformPointSize = *size;
                }
                else if (const auto* name = std::get_if<std::string>(&points->SizeSource))
                {
                    model.HasNamedPointSize = true;
                    model.PointSizeName = *name;
                }
            }

            return model;
        }

        [[nodiscard]] SandboxEditorGeometryDomainModel BuildGeometryDomainModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            return SandboxEditorGeometryDomainModel{
                .Domain = view.ActiveDomain,
                .Valid = view.Valid(),
                .VertexCount = view.VerticesAlive(),
                .EdgeCount = view.EdgesAlive(),
                .HalfedgeCount = view.HalfedgesTotal(),
                .FaceCount = view.FacesAlive(),
                .NodeCount = view.NodesAlive(),
            };
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveFirstSelectedEntity(
            const SandboxEditorContext& context)
        {
            if (context.Scene == nullptr || context.Selection == nullptr)
                return std::nullopt;

            const entt::registry& raw = context.Scene->Raw();
            for (const std::uint32_t stableId : context.Selection->SelectedStableIds())
            {
                const ECS::EntityHandle entity =
                    SelectionController::ToEntityHandle(stableId);
                if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                    return entity;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
            const entt::registry& raw,
            const std::uint32_t stableId)
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableId);
            if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                return entity;
            return std::nullopt;
        }

        [[nodiscard]] SandboxEditorKMeansResult MakeKMeansResult(
            const SandboxEditorCommandStatus status,
            const SandboxEditorGeometryProcessingDomain domain,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorKMeansResult{
                .Status = status,
                .Domain = domain,
                .Error = error,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] bool IsKMeansExecutionDomain(
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            using Domain = SandboxEditorGeometryProcessingDomain;
            return domain == Domain::MeshVertices ||
                   domain == Domain::GraphVertices ||
                   domain == Domain::PointCloudPoints;
        }

        [[nodiscard]] bool SourceViewSupportsKMeansDomain(
            const GS::MutableSourceView& view,
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            using Domain = SandboxEditorGeometryProcessingDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return view.ActiveDomain == GS::Domain::Mesh &&
                       view.VertexSource != nullptr;
            case Domain::GraphVertices:
                return view.ActiveDomain == GS::Domain::Graph &&
                       view.NodeSource != nullptr;
            case Domain::PointCloudPoints:
                return view.ActiveDomain == GS::Domain::PointCloud &&
                       view.VertexSource != nullptr;
            case Domain::None:
            case Domain::MeshEdges:
            case Domain::MeshHalfedges:
            case Domain::MeshFaces:
            case Domain::GraphEdges:
            case Domain::GraphHalfedges:
                return false;
            }
            return false;
        }

        [[nodiscard]] Geometry::PropertySet* KMeansTargetProperties(
            GS::MutableSourceView& view,
            const SandboxEditorGeometryProcessingDomain domain) noexcept
        {
            using Domain = SandboxEditorGeometryProcessingDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
            case Domain::PointCloudPoints:
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case Domain::GraphVertices:
                return view.NodeSource != nullptr
                    ? &view.NodeSource->Properties
                    : nullptr;
            case Domain::None:
            case Domain::MeshEdges:
            case Domain::MeshHalfedges:
            case Domain::MeshFaces:
            case Domain::GraphEdges:
            case Domain::GraphHalfedges:
                return nullptr;
            }
            return nullptr;
        }

        [[nodiscard]] bool IsFinitePosition(const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec3>> CollectKMeansPositions(
            const Geometry::PropertySet& properties)
        {
            const auto positions =
                properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
                return std::nullopt;
            if (positions.Vector().size() != properties.Size())
                return std::nullopt;

            std::vector<glm::vec3> points{};
            points.reserve(positions.Vector().size());
            for (const glm::vec3& position : positions.Vector())
            {
                if (!IsFinitePosition(position))
                    return std::nullopt;
                points.push_back(position);
            }
            return points;
        }

        [[nodiscard]] glm::vec4 KMeansLabelColor(const std::uint32_t label)
        {
            const float h =
                std::fmod(0.61803398875f * static_cast<float>(label), 1.0f);
            constexpr float s = 0.65f;
            constexpr float v = 0.95f;

            const float hh = h * 6.0f;
            const float c = v * s;
            const float x =
                c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
            const float m = v - c;

            glm::vec3 rgb{0.0f};
            if (hh < 1.0f)
                rgb = {c, x, 0.0f};
            else if (hh < 2.0f)
                rgb = {x, c, 0.0f};
            else if (hh < 3.0f)
                rgb = {0.0f, c, x};
            else if (hh < 4.0f)
                rgb = {0.0f, x, c};
            else if (hh < 5.0f)
                rgb = {x, 0.0f, c};
            else
                rgb = {c, 0.0f, x};
            return glm::vec4(rgb + glm::vec3(m), 1.0f);
        }

        [[nodiscard]] bool PublishKMeansProperties(
            Geometry::PropertySet& properties,
            const SandboxEditorGeometryProcessingDomain domain,
            const GK::KMeansResult& result)
        {
            if (result.Labels.empty() ||
                result.Labels.size() != properties.Size())
            {
                return false;
            }

            const bool pointCloud =
                domain == SandboxEditorGeometryProcessingDomain::PointCloudPoints;
            const std::string labelName =
                pointCloud ? "p:kmeans_label" : "v:kmeans_label";
            const std::string colorName =
                pointCloud ? "p:kmeans_color" : "v:kmeans_color";

            auto labels = properties.GetOrAdd<std::uint32_t>(labelName, 0u);
            auto colors =
                properties.GetOrAdd<glm::vec4>(colorName, glm::vec4{1.0f});
            if (!labels || !colors)
                return false;
            if (labels.Vector().size() != result.Labels.size() ||
                colors.Vector().size() != result.Labels.size())
            {
                return false;
            }

            for (std::size_t i = 0u; i < result.Labels.size(); ++i)
            {
                labels.Vector()[i] = result.Labels[i];
                colors.Vector()[i] = KMeansLabelColor(result.Labels[i]);
            }

            if (!pointCloud)
            {
                auto labelFloats =
                    properties.GetOrAdd<float>("v:kmeans_label_f", 0.0f);
                if (!labelFloats ||
                    labelFloats.Vector().size() != result.Labels.size())
                {
                    return false;
                }
                for (std::size_t i = 0u; i < result.Labels.size(); ++i)
                    labelFloats.Vector()[i] =
                        static_cast<float>(result.Labels[i]);
            }
            return true;
        }

        [[nodiscard]] std::string BuildKMeansSuccessMessage(
            const SandboxEditorGeometryProcessingDomain domain,
            const SandboxEditorKMeansResult& result)
        {
            std::string message = "K-Means completed for ";
            message += DebugNameForSandboxEditorGeometryProcessingDomain(domain);
            message += " (labels=";
            message += std::to_string(result.LabelCount);
            message += ", clusters=";
            message += std::to_string(result.ClusterCount);
            message += ", iterations=";
            message += std::to_string(result.Iterations);
            message += ").";
            return message;
        }

        [[nodiscard]] SandboxEditorGeometryProcessingModel BuildGeometryProcessingModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorGeometryProcessingModel model{};
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable for processing controls.");
                return model;
            }
            if (context.Selection == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingSelectionController,
                              "Selection controller is unavailable for processing controls.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity is available for processing controls.");
                return model;
            }

            model.HasSelectedEntity = true;
            model.Capabilities =
                GetSandboxEditorGeometryProcessingCapabilities(
                    *context.Scene,
                    *selected);
            model.Entries =
                ResolveSandboxEditorGeometryProcessingEntries(model.Capabilities);
            model.KMeansDomains =
                GetAvailableSandboxEditorKMeansDomains(*context.Scene, *selected);
            if (context.LastKMeansResult != nullptr)
            {
                model.LastKMeansResult = *context.LastKMeansResult;
                if (!context.LastKMeansResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastKMeansResult->Message.empty()
                            ? "Last K-Means command failed."
                            : context.LastKMeansResult->Message);
                }
            }
            if (!model.Capabilities.HasAny())
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                              "Selected entity has no supported GeometrySources processing domain.");
            }
            return model;
        }

        [[nodiscard]] SandboxEditorPrimitiveDetailModel BuildPrimitiveDetailModel(
            const PrimitiveSelectionResult& primitive)
        {
            return SandboxEditorPrimitiveDetailModel{
                .HasPrimitive = true,
                .Primitive = primitive,
                .HasFaceId = primitive.FaceId != kInvalidPrimitiveIndex,
                .HasEdgeId = primitive.EdgeId != kInvalidPrimitiveIndex,
                .HasVertexId = primitive.VertexId != kInvalidPrimitiveIndex,
                .HasPointId = primitive.PointId != kInvalidPrimitiveIndex,
            };
        }

        [[nodiscard]] SandboxEditorInspectorModel BuildInspectorModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorInspectorModel model{};
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity is available for inspection.");
                return model;
            }

            const entt::registry& raw = context.Scene->Raw();
            model.HasEntity = true;
            model.Entity = BuildEntityRow(raw, *selected);
            model.Transform = BuildTransformModel(raw, *selected);
            model.RenderHints = BuildRenderHintModel(raw, *selected);
            model.Geometry = BuildGeometryDomainModel(raw, *selected);
            model.Processing =
                GetSandboxEditorGeometryProcessingCapabilities(
                    *context.Scene,
                    *selected);

            if (model.Geometry.Domain == GS::Domain::Unknown)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                              "Selected entity has mixed GeometrySources topology.");
            }

            return model;
        }

        [[nodiscard]] SandboxEditorSelectionModel BuildSelectionModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorSelectionModel model{};
            if (context.Selection == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingSelectionController,
                              "Selection controller is unavailable.");
                return model;
            }

            const auto selected = context.Selection->SelectedStableIds();
            model.SelectedStableIds.assign(selected.begin(), selected.end());
            model.HasHovered = context.Selection->HasHovered();
            model.HoveredStableId = context.Selection->HoveredStableId();

            if (context.Scene != nullptr)
            {
                const entt::registry& raw = context.Scene->Raw();
                for (const std::uint32_t stableId : model.SelectedStableIds)
                {
                    if (const std::optional<ECS::EntityHandle> entity =
                            ResolveStableEntity(raw, stableId);
                        entity.has_value())
                    {
                        model.SelectedEntities.push_back(BuildEntityRow(raw, *entity));
                    }
                }

                if (model.HasHovered)
                {
                    if (const std::optional<ECS::EntityHandle> hovered =
                            ResolveStableEntity(raw, model.HoveredStableId);
                        hovered.has_value())
                    {
                        model.HasHoveredEntity = true;
                        model.HoveredEntity = BuildEntityRow(raw, *hovered);
                    }
                }
            }
            else
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable for selection details.");
            }

            if (context.LastRefinedPrimitive != nullptr &&
                context.LastRefinedPrimitive->has_value())
            {
                model.Primitive = BuildPrimitiveDetailModel(**context.LastRefinedPrimitive);
            }

            if (model.SelectedStableIds.empty() && !model.Primitive.HasPrimitive)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity or refined primitive is available.");
            }

            return model;
        }

        [[nodiscard]] SandboxEditorDocumentModel BuildDocumentModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorDocumentModel model{};
            if (context.CommandHistory == nullptr)
            {
                model.StatusText =
                    "Document history is disabled: runtime command history is unavailable.";
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::EditorCommandHistoryUnavailable,
                              model.StatusText);
                return model;
            }

            const EditorCommandHistorySnapshot snapshot =
                context.CommandHistory->Snapshot();
            model.HistoryAvailable = true;
            model.Dirty = snapshot.Dirty;
            model.CanUndo = snapshot.CanUndo;
            model.CanRedo = snapshot.CanRedo;
            model.HasActivePath = snapshot.HasActivePath;
            model.ActivePath = snapshot.ActivePath;
            model.UndoLabel = snapshot.UndoLabel;
            model.RedoLabel = snapshot.RedoLabel;
            model.Revision = snapshot.Revision;
            model.SavedRevision = snapshot.SavedRevision;

            if (snapshot.Dirty)
                model.StatusText = "Scene document has unsaved changes.";
            else if (snapshot.HasActivePath)
                model.StatusText = "Scene document is saved.";
            else
                model.StatusText = "Scene document has no active file path.";
            return model;
        }

        [[nodiscard]] SandboxEditorSceneFileModel BuildSceneFileModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorSceneFileModel model{};
            model.CanNew = static_cast<bool>(context.SceneFileCommands.New);
            model.CanClose = static_cast<bool>(context.SceneFileCommands.Close);
            model.CanSave = static_cast<bool>(context.SceneFileCommands.Save);
            model.CanOpen = static_cast<bool>(context.SceneFileCommands.Load);
            model.LifecycleEnabled =
                context.SceneFileCommands.LifecycleAvailable();
            model.Enabled =
                context.SceneFileCommandsAvailable ||
                context.SceneFileCommands.Available() ||
                model.LifecycleEnabled;
            model.PendingPath = context.PendingSceneFilePath;
            if (model.Enabled)
            {
                model.StatusText =
                    "Scene path-entry commands available; native dialogs are deferred.";
            }
            else
            {
                model.StatusText =
                    "Scene workflows are disabled: runtime scene commands are unavailable.";
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::SceneFileUnavailable,
                              model.StatusText);
            }
            if (context.LastSceneFileResult != nullptr)
            {
                model.LastResult = *context.LastSceneFileResult;
                if (!context.LastSceneFileResult->Message.empty())
                    model.StatusText = context.LastSceneFileResult->Message;
                if (!context.LastSceneFileResult->Succeeded())
                {
                    AddDiagnostic(model.Diagnostics,
                                  SandboxEditorDiagnosticCode::SceneFileFailed,
                                  model.StatusText);
                }
            }
            return model;
        }

        [[nodiscard]] SandboxEditorFileImportModel BuildFileImportModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorFileImportModel model{};
            model.Enabled =
                context.AssetImportCommandsAvailable ||
                context.AssetImportCommands.Available();
            model.PendingPath = context.PendingAssetImportPath;
            model.PayloadKind = context.PendingAssetImportPayloadKind;
            if (model.Enabled)
            {
                model.StatusText = "Import commands available.";
            }
            else
            {
                model.StatusText =
                    "Asset import is disabled: runtime import commands are unavailable.";
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::AssetImportUnavailable,
                              model.StatusText);
            }
            if (context.LastAssetImportResult != nullptr)
            {
                model.LastResult = *context.LastAssetImportResult;
                if (!context.LastAssetImportResult->Message.empty())
                {
                    model.StatusText = context.LastAssetImportResult->Message;
                }
                if (!context.LastAssetImportResult->Succeeded())
                {
                    AddDiagnostic(model.Diagnostics,
                                  SandboxEditorDiagnosticCode::AssetImportFailed,
                                  model.StatusText);
                }
            }
            return model;
        }

        [[nodiscard]] const char* RenderCommandStatusName(
            const Graphics::RenderCommandPassStatus status) noexcept
        {
            switch (status)
            {
            case Graphics::RenderCommandPassStatus::Recorded:
                return "Recorded";
            case Graphics::RenderCommandPassStatus::SkippedNonOperational:
                return "SkippedNonOperational";
            case Graphics::RenderCommandPassStatus::SkippedUnavailable:
                return "SkippedUnavailable";
            }
            return "Unknown";
        }

        [[nodiscard]] SandboxEditorRenderGraphModel BuildRenderGraphModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorRenderGraphModel model{};
            if (context.RenderGraphStats == nullptr)
            {
                model.StatusText =
                    "Frame graph diagnostics are disabled: renderer stats are unavailable.";
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::RenderGraphStatsUnavailable,
                              model.StatusText);
                return model;
            }

            const Graphics::RenderGraphFrameStats& stats =
                *context.RenderGraphStats;
            model.Enabled = true;
            model.CompileSucceeded = stats.Compile.Succeeded;
            model.ExecuteSucceeded = stats.Execute.Succeeded;
            model.DeviceOperational = stats.Execute.DeviceOperational;
            model.PassCount = stats.Compile.PassCount;
            model.CulledPassCount = stats.Compile.CulledPassCount;
            model.ResourceCount = stats.Compile.ResourceCount;
            model.BarrierCount = stats.Compile.BarrierCount;
            model.QueueHandoffEdgeCount = stats.Compile.QueueHandoffEdgeCount;
            model.CrossQueueTimelineEdgeCount =
                stats.Compile.CrossQueueTimelineEdgeCount;
            model.CrossQueueTimelineSignalCount =
                stats.Compile.CrossQueueTimelineSignalCount;
            model.CrossQueueTimelineWaitCount =
                stats.Compile.CrossQueueTimelineWaitCount;
            model.CrossQueueOwnershipTransferCount =
                stats.Compile.CrossQueueOwnershipTransferCount;
            model.TransientMemoryEstimateBytes =
                stats.Compile.TransientMemoryEstimateBytes;
            model.CompileTimeMicros = stats.Compile.TimeMicros;
            model.ExecuteTimeMicros = stats.Execute.TimeMicros;
            model.CommandPassesRecorded = stats.CommandRecords.Recorded;
            model.CommandPassesSkipped = stats.CommandRecords.Skipped;
            model.CommandPassesSkippedNonOperational =
                stats.CommandRecords.SkippedNonOperational;
            model.CommandPassesSkippedUnavailable =
                stats.CommandRecords.SkippedUnavailable;
            model.AsyncComputeUtilizedFrames =
                stats.AsyncComputeUtilizedFrames;
            model.Diagnostic = stats.Diagnostic;
            model.LifecycleDiagnostic = stats.LifecycleDiagnostic;
            model.DebugDump = stats.DebugDump;
            model.StatusText = model.CompileSucceeded
                ? "Frame graph compile succeeded."
                : "Frame graph compile has not succeeded yet.";
            if (!model.Diagnostic.empty())
            {
                model.StatusText = model.Diagnostic;
            }

            model.CommandPasses.reserve(stats.CommandRecords.Passes.size());
            for (const Graphics::RenderGraphCommandPassStats& pass :
                 stats.CommandRecords.Passes)
            {
                model.CommandPasses.push_back(
                    SandboxEditorRenderGraphPassModel{
                        .Name = pass.Name,
                        .HasTypedId = pass.Id.IsValid(),
                        .TypedId = pass.Id.Value,
                        .Status = RenderCommandStatusName(pass.Status),
                    });
            }
            return model;
        }

        [[nodiscard]] SandboxEditorCameraRenderModel BuildCameraRenderModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorCameraRenderModel model{};
            model.CameraControlsAvailable = context.CameraControllers != nullptr;
            model.RenderSettingsAvailable = context.CameraControllers != nullptr;

            if (context.CameraControllers != nullptr)
            {
                if (const ICameraController* controller =
                        context.CameraControllers->ResolveOrNull(CameraControllerSlot::Main);
                    controller != nullptr)
                {
                    model.HasMainCameraController = true;
                    model.MainCameraControllerKind = controller->Kind();
                }
            }

            if (!model.CameraControlsAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::CameraRenderCommandsUnavailable,
                              "Camera/render setting command seams are unavailable.");
            }
            return model;
        }

        [[nodiscard]] SandboxEditorVisualizationModel BuildVisualizationModel(
            const SandboxEditorContext& context)
        {
            SandboxEditorVisualizationModel model{};
            model.GeometryDomainControlsAvailable = context.VisualizationCommandsAvailable;
            model.AdapterBindingControlsAvailable =
                context.VisualizationCommandsAvailable &&
                context.VisualizationAdapterBindings.Available();
            if (!context.VisualizationCommandsAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::VisualizationCommandsUnavailable,
                              "Visualization command seams are unavailable.");
                return model;
            }
            if (context.Scene == nullptr)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::MissingScene,
                              "Scene registry is unavailable for visualization controls.");
                return model;
            }

            const std::optional<ECS::EntityHandle> selected =
                ResolveFirstSelectedEntity(context);
            if (!selected.has_value())
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::NoSelectedEntity,
                              "No selected entity is available for visualization controls.");
                return model;
            }

            const entt::registry& raw = context.Scene->Raw();
            model.HasSelectedEntity = true;
            model.SelectedStableId =
                SelectionController::ToStableEntityId(*selected);
            const GS::ConstSourceView sourceView =
                GS::BuildConstView(raw, *selected);
            model.SelectedDomain = sourceView.ActiveDomain;
            model.Properties = BuildVisualizationProperties(sourceView);

            if (const auto* binding =
                    raw.try_get<ECSC::SpatialDebugBinding>(*selected);
                binding != nullptr)
            {
                model.SpatialDebug = FromSpatialDebugBinding(*binding);
            }

            if (const auto* visualization =
                    raw.try_get<G::VisualizationConfig>(*selected);
                visualization != nullptr)
            {
                model.Visualization =
                    FromVisualizationConfig(*visualization);
            }
            if (model.AdapterBindingControlsAvailable)
            {
                const std::optional<RenderExtractionCache::VisualizationAdapterBinding>
                    binding =
                    context.VisualizationAdapterBindings.GetBinding(model.SelectedStableId);
                if (binding.has_value())
                {
                    model.AdapterBinding =
                        FromVisualizationAdapterBinding(*binding);
                }
            }
            return model;
        }

        [[nodiscard]] SandboxEditorContext BuildContextFromEngine(Engine& engine)
        {
            return SandboxEditorContext{
                .Scene = &engine.GetScene(),
                .Selection = &engine.GetSelectionController(),
                .CommandHistory = &engine.GetEditorCommandHistory(),
                .LastRefinedPrimitive = &engine.GetLastRefinedPrimitiveSelection(),
                .CameraControllers = &engine.GetCameraControllerRegistry(),
                .CameraViewport = Core::Extent2D{
                    engine.GetWindow().GetFramebufferExtent().Width,
                    engine.GetWindow().GetFramebufferExtent().Height},
                .AssetImportCommands = SandboxEditorAssetImportCommandSurface{
                    .Import =
                        [&engine](const SandboxEditorFileImportCommand& command)
                        {
                            auto imported = engine.ImportAssetFromPath(
                                RuntimeAssetImportRequest{
                                    .Path = command.Path,
                                    .PayloadKind = command.PayloadKind,
                                });
                            if (!imported.has_value())
                            {
                                return SandboxEditorFileImportResult{
                                    .Status = SandboxEditorCommandStatus::AssetImportFailed,
                                    .PayloadKind = command.PayloadKind,
                                    .Error = imported.error(),
                                    .Message = BuildImportFailureMessage(imported.error()),
                                };
                            }

                            SandboxEditorFileImportResult result{
                                .Status = SandboxEditorCommandStatus::Applied,
                                .Asset = imported->Asset,
                                .PayloadKind = imported->PayloadKind,
                                .PrimitiveEntitiesCreated =
                                    imported->PrimitiveEntitiesCreated,
                                .EmbeddedTextureAssetsCreated =
                                    imported->EmbeddedTextureAssetsCreated,
                                .TextureUploadRequests =
                                    imported->TextureUploadRequests,
                                .MaterializedModelScene =
                                    imported->MaterializedModelScene,
                                .RequestedTextureUpload =
                                    imported->RequestedTextureUpload,
                            };
                            result.Message =
                                BuildImportSuccessMessage(command, result);
                            return result;
                        },
                },
                .SceneFileCommands = SandboxEditorSceneFileCommandSurface{
                    .New =
                        [&engine]()
                        {
                            Core::Result created = engine.NewSceneDocument();
                            if (!created.has_value())
                            {
                                return SandboxEditorSceneFileResult{
                                    .Status = SandboxEditorCommandStatus::SceneNewFailed,
                                    .Operation = SandboxEditorSceneFileOperation::New,
                                    .Error = created.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        SandboxEditorSceneFileOperation::New,
                                        created.error()),
                                };
                            }
                            SandboxEditorSceneFileResult result{
                                .Status = SandboxEditorCommandStatus::Applied,
                                .Operation = SandboxEditorSceneFileOperation::New,
                            };
                            result.Message = BuildSceneFileSuccessMessage({}, result);
                            return result;
                        },
                    .Save =
                        [&engine](const SandboxEditorSceneFileCommand& command)
                        {
                            auto saved = engine.SaveSceneToPath(command.Path);
                            if (!saved.has_value())
                            {
                                return SandboxEditorSceneFileResult{
                                    .Status = SandboxEditorCommandStatus::SceneSaveFailed,
                                    .Operation = SandboxEditorSceneFileOperation::Save,
                                    .Error = saved.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        SandboxEditorSceneFileOperation::Save,
                                        saved.error()),
                                };
                            }
                            SandboxEditorSceneFileResult result{
                                .Status = SandboxEditorCommandStatus::Applied,
                                .Operation = SandboxEditorSceneFileOperation::Save,
                                .Stats = saved->Stats,
                            };
                            result.Message = BuildSceneFileSuccessMessage(command, result);
                            return result;
                        },
                    .Load =
                        [&engine](const SandboxEditorSceneFileCommand& command)
                        {
                            auto loaded = engine.LoadSceneFromPath(command.Path);
                            if (!loaded.has_value())
                            {
                                return SandboxEditorSceneFileResult{
                                    .Status = SandboxEditorCommandStatus::SceneLoadFailed,
                                    .Operation = SandboxEditorSceneFileOperation::Load,
                                    .Error = loaded.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        SandboxEditorSceneFileOperation::Load,
                                        loaded.error()),
                                };
                            }
                            SandboxEditorSceneFileResult result{
                                .Status = SandboxEditorCommandStatus::Applied,
                                .Operation = SandboxEditorSceneFileOperation::Load,
                                .Stats = loaded->Stats,
                            };
                            result.Message = BuildSceneFileSuccessMessage(command, result);
                            return result;
                        },
                    .Close =
                        [&engine]()
                        {
                            Core::Result closed = engine.CloseSceneDocument();
                            if (!closed.has_value())
                            {
                                return SandboxEditorSceneFileResult{
                                    .Status = SandboxEditorCommandStatus::SceneCloseFailed,
                                    .Operation = SandboxEditorSceneFileOperation::Close,
                                    .Error = closed.error(),
                                    .Message = BuildSceneFileFailureMessage(
                                        SandboxEditorSceneFileOperation::Close,
                                        closed.error()),
                                };
                            }
                            SandboxEditorSceneFileResult result{
                                .Status = SandboxEditorCommandStatus::Applied,
                                .Operation = SandboxEditorSceneFileOperation::Close,
                            };
                            result.Message = BuildSceneFileSuccessMessage({}, result);
                            return result;
                        },
                },
                .VisualizationAdapterBindings = SandboxEditorVisualizationAdapterBindingCommandSurface{
                    .GetBinding =
                        [&engine](const std::uint32_t stableEntityId)
                        {
                            return engine.GetVisualizationAdapterBinding(stableEntityId);
                        },
                    .SetBinding =
                        [&engine](
                            const std::uint32_t stableEntityId,
                            RenderExtractionCache::VisualizationAdapterBinding binding)
                        {
                            engine.SetVisualizationAdapterBinding(
                                stableEntityId,
                                std::move(binding));
                        },
                    .ClearBinding =
                        [&engine](const std::uint32_t stableEntityId)
                        {
                            engine.ClearVisualizationAdapterBinding(stableEntityId);
                        },
                },
                .RenderGraphStats = &engine.GetRenderer().GetLastRenderGraphStats(),
                .ImGuiAdapterAvailable = engine.GetImGuiAdapter().IsInitialized(),
                .AssetImportCommandsAvailable = true,
                .SceneFileCommandsAvailable = true,
                .CameraRenderCommandsAvailable = true,
                .VisualizationCommandsAvailable = true,
            };
        }

        void DrawDiagnostics(const std::vector<SandboxEditorDiagnostic>& diagnostics)
        {
            for (const SandboxEditorDiagnostic& diagnostic : diagnostics)
            {
                ImGui::TextDisabled("%s: %s",
                                    DebugNameForSandboxEditorDiagnosticCode(diagnostic.Code),
                                    diagnostic.Message.c_str());
            }
        }

        void DrawVec3(const char* label, const glm::vec3 value)
        {
            ImGui::Text("%s: %.3f, %.3f, %.3f", label, value.x, value.y, value.z);
        }

        void DrawQuat(const char* label, const glm::quat value)
        {
            ImGui::Text("%s: %.3f, %.3f, %.3f, %.3f",
                        label,
                        value.w,
                        value.x,
                        value.y,
                        value.z);
        }

        void DrawDomainMenu(
            const SandboxEditorDomainWindowKind kind,
            std::array<bool, 12>* domainWindowOpen)
        {
            if (!ImGui::BeginMenu(DebugNameForSandboxEditorDomainWindowKind(kind)))
                return;

            const bool menuEnabled = domainWindowOpen != nullptr;
            if (!menuEnabled)
                ImGui::BeginDisabled();

            if (domainWindowOpen != nullptr)
            {
                ImGui::MenuItem(
                    "Render hints",
                    nullptr,
                    &(*domainWindowOpen)[DomainWindowSlotIndex(kind, DomainWindowSection::Render)]);
                ImGui::MenuItem(
                    "Visualization",
                    nullptr,
                    &(*domainWindowOpen)[DomainWindowSlotIndex(kind, DomainWindowSection::Visualization)]);
                ImGui::MenuItem(
                    "Selection details",
                    nullptr,
                    &(*domainWindowOpen)[DomainWindowSlotIndex(kind, DomainWindowSection::Selection)]);
                ImGui::MenuItem(
                    "Processing",
                    nullptr,
                    &(*domainWindowOpen)[DomainWindowSlotIndex(kind, DomainWindowSection::Processing)]);
            }
            else
            {
                (void)ImGui::MenuItem("Render hints", nullptr, false, false);
                (void)ImGui::MenuItem("Visualization", nullptr, false, false);
                (void)ImGui::MenuItem("Selection details", nullptr, false, false);
                (void)ImGui::MenuItem("Processing", nullptr, false, false);
            }

            if (!menuEnabled)
                ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        void DrawDomainMenus(std::array<bool, 12>* domainWindowOpen)
        {
            if (!ImGui::BeginMainMenuBar())
                return;
            DrawDomainMenu(SandboxEditorDomainWindowKind::PointCloud, domainWindowOpen);
            DrawDomainMenu(SandboxEditorDomainWindowKind::Graph, domainWindowOpen);
            DrawDomainMenu(SandboxEditorDomainWindowKind::Mesh, domainWindowOpen);
            ImGui::EndMainMenuBar();
        }

        [[nodiscard]] bool DomainWindowReady(
            const SandboxEditorDomainWindowModel& model) noexcept
        {
            return model.HasSelectedEntity && model.DomainMatches;
        }

        void DrawDomainWindowHeader(const SandboxEditorDomainWindowModel& model)
        {
            ImGui::Text("Expected domain: %s",
                        DebugNameForSandboxEditorGeometryDomain(model.ExpectedDomain));
            if (model.HasSelectedEntity)
            {
                ImGui::Text("Selected: %s (%u)",
                            model.SelectedEntity.Name.c_str(),
                            model.SelectedStableId);
                ImGui::Text("Selected domain: %s",
                            DebugNameForSandboxEditorGeometryDomain(model.SelectedDomain));
            }
            else
            {
                ImGui::TextDisabled("Selected: none");
            }
            DrawDiagnostics(model.Diagnostics);
        }

        void DrawRenderHintStatus(const SandboxEditorRenderHintModel& hints)
        {
            ImGui::Text("Surface: %s",
                        hints.HasRenderSurface ? hints.SurfaceDomain.c_str() : "none");
            if (hints.HasRenderEdges)
            {
                ImGui::Text("Edges: %s", hints.EdgeDomain.c_str());
                if (hints.HasUniformEdgeWidth)
                    ImGui::Text("Edge width: %.3f", hints.UniformEdgeWidth);
                if (hints.HasNamedEdgeWidth)
                    ImGui::Text("Edge width source: %s", hints.EdgeWidthName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Edges: none");
            }

            if (hints.HasRenderPoints)
            {
                ImGui::Text("Points: %s", hints.PointRenderType.c_str());
                if (hints.HasUniformPointSize)
                    ImGui::Text("Point size: %.3f", hints.UniformPointSize);
                if (hints.HasNamedPointSize)
                    ImGui::Text("Point size source: %s", hints.PointSizeName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Points: none");
            }
        }

        [[nodiscard]] bool DrawSurfaceDomainCombo(
            G::RenderSurface::SourceDomain* domain)
        {
            constexpr const char* kItems[]{"Vertex", "Face"};
            int current =
                *domain == G::RenderSurface::SourceDomain::Face ? 1 : 0;
            if (!ImGui::Combo("Surface domain", &current, kItems, 2))
                return false;
            *domain = current == 1
                ? G::RenderSurface::SourceDomain::Face
                : G::RenderSurface::SourceDomain::Vertex;
            return true;
        }

        [[nodiscard]] bool DrawEdgeDomainCombo(
            G::RenderEdges::SourceDomain* domain)
        {
            constexpr const char* kItems[]{"Vertex", "Edge"};
            int current =
                *domain == G::RenderEdges::SourceDomain::Edge ? 1 : 0;
            if (!ImGui::Combo("Edge domain", &current, kItems, 2))
                return false;
            *domain = current == 1
                ? G::RenderEdges::SourceDomain::Edge
                : G::RenderEdges::SourceDomain::Vertex;
            return true;
        }

        [[nodiscard]] bool DrawPointTypeCombo(
            G::RenderPoints::RenderType* type)
        {
            constexpr const char* kItems[]{"Flat", "Sphere", "Surfel"};
            int current = 1;
            switch (*type)
            {
            case G::RenderPoints::RenderType::Flat:
                current = 0;
                break;
            case G::RenderPoints::RenderType::Sphere:
                current = 1;
                break;
            case G::RenderPoints::RenderType::Surfel:
                current = 2;
                break;
            }
            if (!ImGui::Combo("Point type", &current, kItems, 3))
                return false;
            switch (current)
            {
            case 0:
                *type = G::RenderPoints::RenderType::Flat;
                break;
            case 2:
                *type = G::RenderPoints::RenderType::Surfel;
                break;
            case 1:
            default:
                *type = G::RenderPoints::RenderType::Sphere;
                break;
            }
            return true;
        }

        void DrawPointRenderHintControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            bool canEditRenderHints);

        void DrawEdgeRenderHintControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const bool canEditRenderHints)
        {
            bool edges = model.RenderHints.HasRenderEdges;
            if (ImGui::Checkbox("Edges", &edges) && canEditRenderHints)
            {
                (void)ApplySandboxEditorRenderHintCommand(
                    context,
                    SandboxEditorRenderHintCommand{
                        .StableEntityId = model.SelectedStableId,
                        .SetEdges = true,
                        .EnableEdges = edges,
                        .EdgeDomain = model.RenderHints.EdgeDomainValue,
                    });
            }

            if (!model.RenderHints.HasRenderEdges)
                return;

            G::RenderEdges::SourceDomain edgeDomain =
                model.RenderHints.EdgeDomainValue;
            if (DrawEdgeDomainCombo(&edgeDomain) && canEditRenderHints)
            {
                (void)ApplySandboxEditorRenderHintCommand(
                    context,
                    SandboxEditorRenderHintCommand{
                        .StableEntityId = model.SelectedStableId,
                        .SetEdges = true,
                        .EnableEdges = true,
                        .EdgeDomain = edgeDomain,
                    });
            }

            if (model.RenderHints.HasUniformEdgeWidth)
            {
                float edgeWidth = model.RenderHints.UniformEdgeWidth;
                if (ImGui::DragFloat(
                        "Edge width", &edgeWidth, 0.05f, 0.1f, 32.0f) &&
                    canEditRenderHints)
                {
                    (void)ApplySandboxEditorRenderHintCommand(
                        context,
                        SandboxEditorRenderHintCommand{
                            .StableEntityId = model.SelectedStableId,
                            .SetUniformEdgeWidth = true,
                            .UniformEdgeWidth = edgeWidth,
                        });
                }
            }
        }

        void DrawMeshRenderHintControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const bool canEditRenderHints)
        {
            bool surface = model.RenderHints.HasRenderSurface;
            if (ImGui::Checkbox("Surface", &surface) && canEditRenderHints)
            {
                (void)ApplySandboxEditorRenderHintCommand(
                    context,
                    SandboxEditorRenderHintCommand{
                        .StableEntityId = model.SelectedStableId,
                        .SetSurface = true,
                        .EnableSurface = surface,
                        .SurfaceDomain = model.RenderHints.SurfaceDomainValue,
                    });
            }

            if (model.RenderHints.HasRenderSurface)
            {
                G::RenderSurface::SourceDomain domain =
                    model.RenderHints.SurfaceDomainValue;
                if (DrawSurfaceDomainCombo(&domain) && canEditRenderHints)
                {
                    (void)ApplySandboxEditorRenderHintCommand(
                        context,
                        SandboxEditorRenderHintCommand{
                            .StableEntityId = model.SelectedStableId,
                            .SetSurface = true,
                            .EnableSurface = true,
                            .SurfaceDomain = domain,
                        });
                }
            }

            DrawEdgeRenderHintControls(model, context, canEditRenderHints);
            DrawPointRenderHintControls(model, context, canEditRenderHints);
        }

        void DrawPointRenderHintControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const bool canEditRenderHints)
        {
            bool points = model.RenderHints.HasRenderPoints;
            if (ImGui::Checkbox("Points", &points) && canEditRenderHints)
            {
                (void)ApplySandboxEditorRenderHintCommand(
                    context,
                    SandboxEditorRenderHintCommand{
                        .StableEntityId = model.SelectedStableId,
                        .SetPoints = true,
                        .EnablePoints = points,
                        .PointType = model.RenderHints.PointRenderTypeValue,
                    });
            }

            if (!model.RenderHints.HasRenderPoints)
                return;

            G::RenderPoints::RenderType pointType =
                model.RenderHints.PointRenderTypeValue;
            if (DrawPointTypeCombo(&pointType) && canEditRenderHints)
            {
                (void)ApplySandboxEditorRenderHintCommand(
                    context,
                        SandboxEditorRenderHintCommand{
                            .StableEntityId = model.SelectedStableId,
                            .PointType = pointType,
                            .SetPointRenderType = true,
                        });
            }

            if (model.RenderHints.HasUniformPointSize)
            {
                float pointSize = model.RenderHints.UniformPointSize;
                if (ImGui::DragFloat(
                        "Point radius", &pointSize, 0.001f, 0.0001f, 1.0f) &&
                    canEditRenderHints)
                {
                    (void)ApplySandboxEditorRenderHintCommand(
                        context,
                        SandboxEditorRenderHintCommand{
                            .StableEntityId = model.SelectedStableId,
                            .SetUniformPointSize = true,
                            .UniformPointSize = pointSize,
                        });
                }
            }
        }

        void DrawGraphRenderHintControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const bool canEditRenderHints)
        {
            DrawEdgeRenderHintControls(model, context, canEditRenderHints);
            DrawPointRenderHintControls(model, context, canEditRenderHints);
        }

        void DrawVisualizationPropertyPresets(
            const std::vector<SandboxEditorVisualizationPropertyInfo>& properties,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const bool canEditVisualization)
        {
            ImGui::SeparatorText("Properties");
            if (properties.empty())
            {
                ImGui::TextDisabled("No visualization-eligible properties.");
                return;
            }

            if (!canEditVisualization)
                ImGui::BeginDisabled();

            for (std::size_t i = 0u; i < properties.size(); ++i)
            {
                const SandboxEditorVisualizationPropertyInfo& property =
                    properties[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s  [%s, %s, %llu]",
                            property.Name.c_str(),
                            DebugNameForSandboxEditorVisualizationPropertyDomain(
                                property.Domain),
                            DebugNameForSandboxEditorVisualizationPropertyValueKind(
                                property.ValueKind),
                            static_cast<unsigned long long>(
                                property.ElementCount));

                bool wroteButton = false;
                if (property.ScalarPresetAvailable)
                {
                    if (ImGui::SmallButton("Scalar") && canEditVisualization)
                    {
                        (void)ApplySandboxEditorVisualizationPropertyCommand(
                            context,
                            SandboxEditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Domain = property.Domain,
                                .Preset =
                                    SandboxEditorVisualizationPropertyPreset::Scalar,
                                .PropertyName = property.Name,
                            });
                    }
                    wroteButton = true;
                }
                if (property.IsolinePresetAvailable)
                {
                    if (wroteButton)
                        ImGui::SameLine();
                    if (ImGui::SmallButton("Isolines") && canEditVisualization)
                    {
                        (void)ApplySandboxEditorVisualizationPropertyCommand(
                            context,
                            SandboxEditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Domain = property.Domain,
                                .Preset =
                                    SandboxEditorVisualizationPropertyPreset::Isoline,
                                .PropertyName = property.Name,
                                .IsolineCount = 12u,
                            });
                    }
                    wroteButton = true;
                }
                if (property.ColorBufferPresetAvailable)
                {
                    if (wroteButton)
                        ImGui::SameLine();
                    if (ImGui::SmallButton("Color buffer") &&
                        canEditVisualization)
                    {
                        (void)ApplySandboxEditorVisualizationPropertyCommand(
                            context,
                            SandboxEditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Domain = property.Domain,
                                .Preset =
                                    SandboxEditorVisualizationPropertyPreset::ColorBuffer,
                                .PropertyName = property.Name,
                            });
                    }
                    wroteButton = true;
                }
                if (property.VectorFieldCandidate && !wroteButton)
                {
                    ImGui::TextDisabled("Vector-field candidate; adapter residency is not owned by this UI slice.");
                }
                ImGui::PopID();
            }

            if (!canEditVisualization)
                ImGui::EndDisabled();
        }

        void DrawDomainRenderWindow(const SandboxEditorDomainWindowModel& model,
                                    const SandboxEditorContext& context)
        {
            DrawDomainWindowHeader(model);
            ImGui::SeparatorText("Render hint status");
            DrawRenderHintStatus(model.RenderHints);

            ImGui::SeparatorText("Render controls");
            const bool canEditRenderHints = DomainWindowReady(model);
            if (!canEditRenderHints)
                ImGui::BeginDisabled();
            switch (model.Kind)
            {
            case SandboxEditorDomainWindowKind::Mesh:
                DrawMeshRenderHintControls(model, context, canEditRenderHints);
                break;
            case SandboxEditorDomainWindowKind::Graph:
                DrawGraphRenderHintControls(model, context, canEditRenderHints);
                break;
            case SandboxEditorDomainWindowKind::PointCloud:
                DrawPointRenderHintControls(model, context, canEditRenderHints);
                break;
            }
            if (!canEditRenderHints)
                ImGui::EndDisabled();
        }

        void DrawDomainVisualizationWindow(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context)
        {
            DrawDomainWindowHeader(model);
            const SandboxEditorVisualizationModel& visualization = model.Visualization;

            if (visualization.SpatialDebug.HasBinding)
            {
                ImGui::Text("Spatial debug: %s key=%llu",
                            DebugNameForSandboxEditorSpatialDebugKind(
                                visualization.SpatialDebug.Kind),
                            static_cast<unsigned long long>(
                                visualization.SpatialDebug.RegistryKey));
            }
            else
            {
                ImGui::TextDisabled("Spatial debug: disabled");
            }

            if (visualization.Visualization.HasConfig)
            {
                ImGui::Text("Visualization: %s",
                            DebugNameForSandboxEditorVisualizationColorSource(
                                visualization.Visualization.Source));
            }
            else
            {
                ImGui::TextDisabled("Visualization: material/default");
            }

            const bool canEditVisualization =
                DomainWindowReady(model) &&
                model.VisualizationControlsAvailable;
            if (!canEditVisualization)
                ImGui::BeginDisabled();

            if (ImGui::Button("Enable BVH debug") && canEditVisualization)
            {
                (void)ApplySandboxEditorSpatialDebugBindingCommand(
                    context,
                    SandboxEditorSpatialDebugBindingCommand{
                        .StableEntityId = model.SelectedStableId,
                        .EnableBinding = true,
                    });
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear debug") && canEditVisualization)
            {
                (void)ApplySandboxEditorSpatialDebugBindingCommand(
                    context,
                    SandboxEditorSpatialDebugBindingCommand{
                        .StableEntityId = model.SelectedStableId,
                        .EnableBinding = false,
                    });
            }

            if (ImGui::Button("Uniform color") && canEditVisualization)
            {
                (void)ApplySandboxEditorVisualizationConfigCommand(
                    context,
                    SandboxEditorVisualizationConfigCommand{
                        .StableEntityId = model.SelectedStableId,
                        .EnableConfig = true,
                        .Source = G::VisualizationConfig::ColorSource::UniformColor,
                    });
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear vis") && canEditVisualization)
            {
                (void)ApplySandboxEditorVisualizationConfigCommand(
                    context,
                    SandboxEditorVisualizationConfigCommand{
                        .StableEntityId = model.SelectedStableId,
                        .EnableConfig = false,
                    });
            }

            if (!canEditVisualization)
                ImGui::EndDisabled();

            DrawVisualizationPropertyPresets(
                visualization.Properties,
                context,
                model.SelectedStableId,
                canEditVisualization);
        }

        void DrawPrimitiveDetails(const SandboxEditorPrimitiveDetailModel& primitive)
        {
            if (!primitive.HasPrimitive)
            {
                ImGui::TextDisabled("No refined primitive selection for this domain.");
                return;
            }

            const PrimitiveSelectionResult& result = primitive.Primitive;
            ImGui::Text("Primitive status: %s",
                        DebugNameForPrimitiveRefineStatus(result.Status));
            ImGui::Text("Primitive domain/kind: %s / %s",
                        DebugNameForSandboxEditorGeometryDomain(result.Domain),
                        DebugNameForSandboxEditorPrimitiveKind(result.Kind));
            if (primitive.HasFaceId)
                ImGui::Text("Face id: %u", result.FaceId);
            if (primitive.HasEdgeId)
                ImGui::Text("Edge id: %u", result.EdgeId);
            if (primitive.HasVertexId)
                ImGui::Text("Vertex id: %u", result.VertexId);
            if (primitive.HasPointId)
                ImGui::Text("Point id: %u", result.PointId);
            if (result.HasHitPosition)
            {
                DrawVec3("Local hit", result.LocalHit);
                DrawVec3("World hit", result.WorldHit);
            }
        }

        void DrawDomainSelectionWindow(
            const SandboxEditorDomainWindowModel& model)
        {
            DrawDomainWindowHeader(model);
            ImGui::SeparatorText("Primitive selection");
            DrawPrimitiveDetails(model.Primitive);
        }

        void DrawProcessingDomains(
            const SandboxEditorGeometryProcessingDomain domains)
        {
            constexpr std::array<SandboxEditorGeometryProcessingDomain, 8>
                kDisplayDomains{
                    SandboxEditorGeometryProcessingDomain::MeshVertices,
                    SandboxEditorGeometryProcessingDomain::MeshEdges,
                    SandboxEditorGeometryProcessingDomain::MeshHalfedges,
                    SandboxEditorGeometryProcessingDomain::MeshFaces,
                    SandboxEditorGeometryProcessingDomain::GraphVertices,
                    SandboxEditorGeometryProcessingDomain::GraphEdges,
                    SandboxEditorGeometryProcessingDomain::GraphHalfedges,
                    SandboxEditorGeometryProcessingDomain::PointCloudPoints,
                };

            bool any = false;
            for (const SandboxEditorGeometryProcessingDomain domain :
                 kDisplayDomains)
            {
                if (!HasAnySandboxEditorGeometryProcessingDomain(domains, domain))
                    continue;
                any = true;
                ImGui::BulletText("%s",
                                  DebugNameForSandboxEditorGeometryProcessingDomain(
                                      domain));
            }
            if (!any)
                ImGui::TextDisabled("No supported processing domains.");
        }

        [[nodiscard]] bool ContainsKMeansDomain(
            const std::vector<SandboxEditorGeometryProcessingDomain>& domains,
            const SandboxEditorGeometryProcessingDomain domain)
        {
            return std::find(domains.begin(), domains.end(), domain) != domains.end();
        }

        void DrawKMeansResultStatus(
            const std::optional<SandboxEditorKMeansResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last K-Means run: none");
                return;
            }

            const SandboxEditorKMeansResult& result = *lastResult;
            ImGui::Text("Last K-Means run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Domain: %s",
                        DebugNameForSandboxEditorGeometryProcessingDomain(
                            result.Domain));
            if (result.Succeeded())
            {
                ImGui::Text("Labels: %u  clusters: %u  iterations: %u",
                            result.LabelCount,
                            result.ClusterCount,
                            result.Iterations);
                ImGui::Text("Converged: %s  inertia: %.6f",
                            result.Converged ? "yes" : "no",
                            result.Inertia);
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawKMeansExecutionControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            KMeansUiState* kmeansState)
        {
            ImGui::SeparatorText("K-Means execution");
            if (processing.KMeansDomains.empty())
            {
                ImGui::TextDisabled("K-Means is unavailable for this selection.");
                return;
            }
            if (kmeansState == nullptr ||
                kmeansState->LastResult == nullptr ||
                kmeansState->Domain == nullptr ||
                kmeansState->ClusterCount == nullptr ||
                kmeansState->MaxIterations == nullptr ||
                kmeansState->Seed == nullptr ||
                kmeansState->UseHierarchicalInitialization == nullptr)
            {
                ImGui::TextDisabled("K-Means execution controls are not bound.");
                return;
            }

            if (!ContainsKMeansDomain(processing.KMeansDomains,
                                      *kmeansState->Domain))
            {
                *kmeansState->Domain = processing.KMeansDomains.front();
            }

            const char* currentDomain =
                DebugNameForSandboxEditorGeometryProcessingDomain(
                    *kmeansState->Domain);
            if (ImGui::BeginCombo("Domain", currentDomain))
            {
                for (const SandboxEditorGeometryProcessingDomain domain :
                     processing.KMeansDomains)
                {
                    const bool selected = *kmeansState->Domain == domain;
                    if (ImGui::Selectable(
                            DebugNameForSandboxEditorGeometryProcessingDomain(domain),
                            selected))
                    {
                        *kmeansState->Domain = domain;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::DragInt("Clusters", kmeansState->ClusterCount, 1.0f, 1, 1024);
            ImGui::DragInt("Max iterations", kmeansState->MaxIterations, 1.0f, 1, 4096);
            ImGui::DragInt("Seed", kmeansState->Seed, 1.0f, 0, 1'000'000);
            *kmeansState->ClusterCount =
                std::clamp(*kmeansState->ClusterCount, 1, 1024);
            *kmeansState->MaxIterations =
                std::clamp(*kmeansState->MaxIterations, 1, 4096);
            *kmeansState->Seed =
                std::clamp(*kmeansState->Seed, 0, 1'000'000);
            ImGui::Checkbox("Hierarchical initialization",
                            kmeansState->UseHierarchicalInitialization);

            if (ImGui::Button("Run K-Means"))
            {
                *kmeansState->LastResult = ApplySandboxEditorKMeansCommand(
                    context,
                    SandboxEditorKMeansCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Domain = *kmeansState->Domain,
                        .ClusterCount = static_cast<std::uint32_t>(
                            *kmeansState->ClusterCount),
                        .MaxIterations = static_cast<std::uint32_t>(
                            *kmeansState->MaxIterations),
                        .Seed = static_cast<std::uint32_t>(*kmeansState->Seed),
                        .UseHierarchicalInitialization =
                            *kmeansState->UseHierarchicalInitialization,
                    });
            }

            const std::optional<SandboxEditorKMeansResult>& result =
                kmeansState->LastResult->has_value()
                    ? *kmeansState->LastResult
                    : processing.LastKMeansResult;
            DrawKMeansResultStatus(result);
        }

        void DrawDomainProcessingWindow(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            KMeansUiState* kmeansState)
        {
            DrawDomainWindowHeader(model);
            ImGui::SeparatorText("Processing capabilities");

            const SandboxEditorGeometryProcessingModel& processing =
                model.Processing;
            DrawDiagnostics(processing.Diagnostics);
            if (!DomainWindowReady(model) || !processing.HasSelectedEntity)
            {
                ImGui::TextDisabled("Select a matching domain entity to inspect processing affordances.");
                return;
            }

            ImGui::Text("Editable surface mesh: %s",
                        processing.Capabilities.HasEditableSurfaceMesh ? "yes" : "no");
            ImGui::SeparatorText("Source domains");
            DrawProcessingDomains(processing.Capabilities.Domains);

            ImGui::SeparatorText("K-Means sources");
            if (processing.KMeansDomains.empty())
            {
                ImGui::TextDisabled("K-Means is unavailable for this selection.");
            }
            else
            {
                for (const SandboxEditorGeometryProcessingDomain domain :
                     processing.KMeansDomains)
                {
                    ImGui::BulletText(
                        "%s",
                        DebugNameForSandboxEditorGeometryProcessingDomain(domain));
                }
            }

            ImGui::SeparatorText("Available operations");
            if (processing.Entries.empty())
            {
                ImGui::TextDisabled("No processing operations match this selection.");
            }
            else
            {
                for (const SandboxEditorGeometryProcessingEntry& entry :
                     processing.Entries)
                {
                    ImGui::BulletText(
                        "%s",
                        DebugNameForSandboxEditorGeometryProcessingAlgorithm(
                            entry.Algorithm));
                }
            }
            DrawKMeansExecutionControls(model, context, processing, kmeansState);
        }

        void DrawOneDomainWindow(
            const SandboxEditorContext& context,
            const SandboxEditorDomainWindowKind kind,
            const DomainWindowSection section,
            std::array<bool, 12>& domainWindowOpen,
            KMeansUiState* kmeansState)
        {
            const std::size_t slot = DomainWindowSlotIndex(kind, section);
            if (!domainWindowOpen[slot])
                return;

            const SandboxEditorDomainWindowModel model =
                BuildSandboxEditorDomainWindowModel(context, kind);
            ImGui::SetNextWindowSize(ImVec2(340.0f, 300.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(DomainWindowTitle(kind, section), &domainWindowOpen[slot]))
            {
                switch (section)
                {
                case DomainWindowSection::Render:
                    DrawDomainRenderWindow(model, context);
                    break;
                case DomainWindowSection::Visualization:
                    DrawDomainVisualizationWindow(model, context);
                    break;
                case DomainWindowSection::Selection:
                    DrawDomainSelectionWindow(model);
                    break;
                case DomainWindowSection::Processing:
                    DrawDomainProcessingWindow(model, context, kmeansState);
                    break;
                case DomainWindowSection::Count:
                    break;
                }
            }
            ImGui::End();
        }

        void DrawDomainWindows(
            const SandboxEditorContext* context,
            std::array<bool, 12>* domainWindowOpen,
            KMeansUiState* kmeansState)
        {
            if (context == nullptr || domainWindowOpen == nullptr)
                return;

            constexpr std::array<SandboxEditorDomainWindowKind, 3> kKinds{
                SandboxEditorDomainWindowKind::PointCloud,
                SandboxEditorDomainWindowKind::Graph,
                SandboxEditorDomainWindowKind::Mesh,
            };
            constexpr std::array<DomainWindowSection, 4> kSections{
                DomainWindowSection::Render,
                DomainWindowSection::Visualization,
                DomainWindowSection::Selection,
                DomainWindowSection::Processing,
            };
            for (const SandboxEditorDomainWindowKind kind : kKinds)
            {
                for (const DomainWindowSection section : kSections)
                {
                    DrawOneDomainWindow(
                        *context,
                        kind,
                        section,
                        *domainWindowOpen,
                        kmeansState);
                }
            }
        }

        void DrawPanelFrame(
            const SandboxEditorPanelFrame& frame,
            const SandboxEditorContext* context,
            std::array<char, 1024>* importPathBuffer,
            std::array<char, 1024>* scenePathBuffer,
            A::AssetPayloadKind* importPayloadKind,
            std::optional<SandboxEditorFileImportResult>* lastImportResult,
            std::optional<SandboxEditorSceneFileResult>* lastSceneFileResult,
            std::array<bool, 12>* domainWindowOpen,
            KMeansUiState* kmeansState)
        {
            DrawDomainMenus(domainWindowOpen);
            DrawDomainWindows(context, domainWindowOpen, kmeansState);

            ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Sandbox Editor"))
            {
                ImGui::TextUnformatted("Promoted runtime editor shell");
                DrawDiagnostics(frame.Diagnostics);
            }
            ImGui::End();

            ImGui::SetNextWindowSize(ImVec2(280.0f, 420.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Scene Hierarchy"))
            {
                if (frame.Hierarchy.empty())
                {
                    ImGui::TextDisabled("No live scene entities.");
                }
                for (const SandboxEditorEntityRow& row : frame.Hierarchy)
                {
                    ImGui::PushID(static_cast<int>(row.StableEntityId));
                    const bool clicked =
                        ImGui::Selectable(row.Name.c_str(), row.Selected);
                    ImGui::PopID();
                    if (clicked && context != nullptr)
                        (void)SelectSandboxEditorEntity(*context, row.StableEntityId);
                    if (row.Hovered)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(hover)");
                    }
                }
            }
            ImGui::End();

            ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Inspector"))
            {
                if (!frame.Inspector.HasEntity)
                {
                    DrawDiagnostics(frame.Inspector.Diagnostics);
                }
                else
                {
                    const SandboxEditorInspectorModel& inspector = frame.Inspector;
                    ImGui::Text("Entity: %s", inspector.Entity.Name.c_str());
                    ImGui::Text("Render id: %u", inspector.Entity.StableEntityId);
                    ImGui::Text("Durable StableId: %s",
                                inspector.Entity.HasDurableStableId ? "valid" : "none");
                    if (inspector.Transform.HasLocalTransform)
                    {
                        if (context != nullptr)
                        {
                            glm::vec3 localPosition = inspector.Transform.LocalPosition;
                            if (ImGui::DragFloat3("Local position",
                                                  &localPosition.x,
                                                  0.01f))
                            {
                                (void)ApplySandboxEditorTransformEdit(
                                    *context,
                                    SandboxEditorTransformEditCommand{
                                        .StableEntityId = inspector.Entity.StableEntityId,
                                        .SetPosition = true,
                                        .Position = localPosition,
                                    });
                            }

                            glm::vec3 localScale = inspector.Transform.LocalScale;
                            if (ImGui::DragFloat3("Local scale",
                                                  &localScale.x,
                                                  0.01f))
                            {
                                (void)ApplySandboxEditorTransformEdit(
                                    *context,
                                    SandboxEditorTransformEditCommand{
                                        .StableEntityId = inspector.Entity.StableEntityId,
                                        .SetScale = true,
                                        .Scale = localScale,
                                    });
                            }
                        }
                        else
                        {
                            DrawVec3("Local position", inspector.Transform.LocalPosition);
                            DrawVec3("Local scale", inspector.Transform.LocalScale);
                        }
                        DrawQuat("Local rotation (wxyz)", inspector.Transform.LocalRotation);
                    }
                    if (inspector.Transform.HasWorldTransform)
                        DrawVec3("World position", inspector.Transform.WorldPosition);
                    ImGui::Text("Render hints: surface=%s edges=%s points=%s",
                                inspector.RenderHints.HasRenderSurface ? "yes" : "no",
                                inspector.RenderHints.HasRenderEdges ? "yes" : "no",
                                inspector.RenderHints.HasRenderPoints ? "yes" : "no");
                    if (inspector.RenderHints.HasRenderSurface)
                        ImGui::Text("Surface domain: %s",
                                    inspector.RenderHints.SurfaceDomain.c_str());
                    if (inspector.RenderHints.HasRenderEdges)
                    {
                        ImGui::Text("Edge domain: %s",
                                    inspector.RenderHints.EdgeDomain.c_str());
                        if (inspector.RenderHints.HasUniformEdgeWidth)
                            ImGui::Text("Edge width: %.3f",
                                        inspector.RenderHints.UniformEdgeWidth);
                        if (inspector.RenderHints.HasNamedEdgeWidth)
                            ImGui::Text("Edge width source: %s",
                                        inspector.RenderHints.EdgeWidthName.c_str());
                    }
                    if (inspector.RenderHints.HasRenderPoints)
                    {
                        ImGui::Text("Point type: %s",
                                    inspector.RenderHints.PointRenderType.c_str());
                        if (inspector.RenderHints.HasUniformPointSize)
                            ImGui::Text("Point size: %.3f",
                                        inspector.RenderHints.UniformPointSize);
                        if (inspector.RenderHints.HasNamedPointSize)
                            ImGui::Text("Point size source: %s",
                                        inspector.RenderHints.PointSizeName.c_str());
                    }
                    ImGui::Text("Geometry domain: %s",
                                DebugNameForSandboxEditorGeometryDomain(
                                    inspector.Geometry.Domain));
                    ImGui::Text("Counts: v=%zu e=%zu h=%zu f=%zu n=%zu",
                                inspector.Geometry.VertexCount,
                                inspector.Geometry.EdgeCount,
                                inspector.Geometry.HalfedgeCount,
                                inspector.Geometry.FaceCount,
                                inspector.Geometry.NodeCount);
                    DrawDiagnostics(inspector.Diagnostics);
                }
            }
            ImGui::End();

            if (ImGui::Begin("Selection Details"))
            {
                ImGui::Text("Selected entities: %zu", frame.Selection.SelectedStableIds.size());
                for (const SandboxEditorEntityRow& row : frame.Selection.SelectedEntities)
                    ImGui::BulletText("%s (%u)", row.Name.c_str(), row.StableEntityId);
                if (frame.Selection.HasHovered)
                {
                    ImGui::Text("Hovered render id: %u", frame.Selection.HoveredStableId);
                    if (frame.Selection.HasHoveredEntity)
                        ImGui::Text("Hovered entity: %s",
                                    frame.Selection.HoveredEntity.Name.c_str());
                }
                if (frame.Selection.Primitive.HasPrimitive)
                {
                    const PrimitiveSelectionResult& primitive =
                        frame.Selection.Primitive.Primitive;
                    ImGui::Text("Primitive status: %s",
                                DebugNameForPrimitiveRefineStatus(primitive.Status));
                    ImGui::Text("Primitive domain/kind: %s / %s",
                                DebugNameForSandboxEditorGeometryDomain(primitive.Domain),
                                DebugNameForSandboxEditorPrimitiveKind(primitive.Kind));
                    if (frame.Selection.Primitive.HasFaceId)
                        ImGui::Text("Face id: %u", primitive.FaceId);
                    if (frame.Selection.Primitive.HasEdgeId)
                        ImGui::Text("Edge id: %u", primitive.EdgeId);
                    if (frame.Selection.Primitive.HasVertexId)
                        ImGui::Text("Vertex id: %u", primitive.VertexId);
                    if (frame.Selection.Primitive.HasPointId)
                        ImGui::Text("Point id: %u", primitive.PointId);
                    if (primitive.HasHitPosition)
                    {
                        DrawVec3("Local hit", primitive.LocalHit);
                        DrawVec3("World hit", primitive.WorldHit);
                    }
                }
                DrawDiagnostics(frame.Selection.Diagnostics);
            }
            ImGui::End();

            if (ImGui::Begin("File / Scene"))
            {
                ImGui::TextWrapped("%s", frame.Document.StatusText.c_str());
                ImGui::Text("Active path: %s",
                            frame.Document.HasActivePath
                                ? frame.Document.ActivePath.c_str()
                                : "(none)");
                ImGui::Text("Dirty: %s", frame.Document.Dirty ? "yes" : "no");
                ImGui::Text("Revision: %llu saved: %llu",
                            static_cast<unsigned long long>(frame.Document.Revision),
                            static_cast<unsigned long long>(frame.Document.SavedRevision));
                const bool historyControlsAvailable =
                    context != nullptr && context->CommandHistory != nullptr;
                if (!historyControlsAvailable || !frame.Document.CanUndo)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Undo") && historyControlsAvailable)
                    (void)context->CommandHistory->Undo();
                if (!historyControlsAvailable || !frame.Document.CanUndo)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                if (!historyControlsAvailable || !frame.Document.CanRedo)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Redo") && historyControlsAvailable)
                    (void)context->CommandHistory->Redo();
                if (!historyControlsAvailable || !frame.Document.CanRedo)
                    ImGui::EndDisabled();
                if (!frame.Document.UndoLabel.empty())
                    ImGui::Text("Undo next: %s", frame.Document.UndoLabel.c_str());
                if (!frame.Document.RedoLabel.empty())
                    ImGui::Text("Redo next: %s", frame.Document.RedoLabel.c_str());
                DrawDiagnostics(frame.Document.Diagnostics);
                ImGui::Separator();
                ImGui::TextWrapped("%s",
                                    frame.SceneFile.FileDialogBoundaryText.c_str());
                if (!frame.SceneFile.LifecycleEnabled ||
                    context == nullptr ||
                    lastSceneFileResult == nullptr)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("New scene") &&
                    frame.SceneFile.LifecycleEnabled &&
                    context != nullptr &&
                    lastSceneFileResult != nullptr)
                {
                    *lastSceneFileResult =
                        ApplySandboxEditorNewSceneCommand(*context);
                }
                ImGui::SameLine();
                if (ImGui::Button("Close scene") &&
                    frame.SceneFile.LifecycleEnabled &&
                    context != nullptr &&
                    lastSceneFileResult != nullptr)
                {
                    *lastSceneFileResult =
                        ApplySandboxEditorCloseSceneCommand(*context);
                }
                if (!frame.SceneFile.LifecycleEnabled ||
                    context == nullptr ||
                    lastSceneFileResult == nullptr)
                {
                    ImGui::EndDisabled();
                }

                const bool sceneControlsAvailable =
                    frame.SceneFile.CanSave &&
                    frame.SceneFile.CanOpen &&
                    context != nullptr &&
                    scenePathBuffer != nullptr &&
                    lastSceneFileResult != nullptr;
                if (!sceneControlsAvailable)
                    ImGui::BeginDisabled();
                if (scenePathBuffer != nullptr)
                {
                    ImGui::InputText("Scene path",
                                     scenePathBuffer->data(),
                                     scenePathBuffer->size());
                }
                else
                {
                    ImGui::TextDisabled("Scene path input is not bound.");
                }
                if (ImGui::Button("Save / Save As") && sceneControlsAvailable)
                {
                    *lastSceneFileResult = ApplySandboxEditorSceneSaveCommand(
                        *context,
                        SandboxEditorSceneFileCommand{
                            .Path = std::string(scenePathBuffer->data()),
                        });
                }
                ImGui::SameLine();
                if (ImGui::Button("Open path") && sceneControlsAvailable)
                {
                    *lastSceneFileResult = ApplySandboxEditorSceneLoadCommand(
                        *context,
                        SandboxEditorSceneFileCommand{
                            .Path = std::string(scenePathBuffer->data()),
                        });
                }
                if (!sceneControlsAvailable)
                    ImGui::EndDisabled();
                ImGui::TextWrapped("%s", frame.SceneFile.StatusText.c_str());
                const SandboxEditorSceneFileResult* result =
                    lastSceneFileResult != nullptr && lastSceneFileResult->has_value()
                        ? &**lastSceneFileResult
                        : frame.SceneFile.LastResult.has_value()
                            ? &*frame.SceneFile.LastResult
                            : nullptr;
                if (result != nullptr)
                {
                    ImGui::Text("Last scene command: %s",
                                DebugNameForSandboxEditorCommandStatus(
                                    result->Status));
                    ImGui::Text("Stats: entities=%u mesh=%u graph=%u pointCloud=%u",
                                result->Stats.Entities,
                                result->Stats.MeshEntities,
                                result->Stats.GraphEntities,
                                result->Stats.PointCloudEntities);
                }
                DrawDiagnostics(frame.SceneFile.Diagnostics);
            }
            ImGui::End();

            if (ImGui::Begin("File / Import"))
            {
                const bool importControlsAvailable =
                    frame.FileImport.Enabled &&
                    context != nullptr &&
                    importPathBuffer != nullptr &&
                    importPayloadKind != nullptr &&
                    lastImportResult != nullptr;
                if (!importControlsAvailable)
                    ImGui::BeginDisabled();
                if (importPathBuffer != nullptr)
                {
                    ImGui::InputText("Path",
                                     importPathBuffer->data(),
                                     importPathBuffer->size());
                }
                else
                {
                    ImGui::TextDisabled("Path input is not bound.");
                }
                if (importPayloadKind != nullptr)
                {
                    if (ImGui::BeginCombo(
                            "Payload hint",
                            A::DebugNameForAssetPayloadKind(*importPayloadKind)))
                    {
                        for (const A::AssetPayloadKind kind : kImportPayloadKinds)
                        {
                            const bool selected = *importPayloadKind == kind;
                            if (ImGui::Selectable(
                                    A::DebugNameForAssetPayloadKind(kind),
                                    selected))
                            {
                                *importPayloadKind = kind;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextDisabled("Payload hint is not bound.");
                }
                if (ImGui::Button("Import asset") && importControlsAvailable)
                {
                    *lastImportResult = ApplySandboxEditorFileImportCommand(
                        *context,
                        SandboxEditorFileImportCommand{
                            .Path = std::string(importPathBuffer->data()),
                            .PayloadKind = *importPayloadKind,
                        });
                }
                if (!importControlsAvailable)
                    ImGui::EndDisabled();
                ImGui::TextWrapped("%s", frame.FileImport.StatusText.c_str());
                const SandboxEditorFileImportResult* result =
                    lastImportResult != nullptr && lastImportResult->has_value()
                        ? &**lastImportResult
                        : frame.FileImport.LastResult.has_value()
                            ? &*frame.FileImport.LastResult
                            : nullptr;
                if (result != nullptr)
                {
                    ImGui::Text("Last import: %s",
                                DebugNameForSandboxEditorCommandStatus(
                                    result->Status));
                    ImGui::Text("Payload: %s",
                                A::DebugNameForAssetPayloadKind(
                                    result->PayloadKind));
                    if (result->Asset.IsValid())
                    {
                        ImGui::Text("Asset: %u:%u",
                                    result->Asset.Index,
                                    result->Asset.Generation);
                    }
                    if (result->PrimitiveEntitiesCreated > 0u)
                    {
                        ImGui::Text("Primitive entities: %llu",
                                    static_cast<unsigned long long>(
                                        result->PrimitiveEntitiesCreated));
                    }
                    if (result->EmbeddedTextureAssetsCreated > 0u)
                    {
                        ImGui::Text("Embedded textures: %llu",
                                    static_cast<unsigned long long>(
                                        result->EmbeddedTextureAssetsCreated));
                    }
                    if (result->TextureUploadRequests > 0u)
                    {
                        ImGui::Text("Texture upload requests: %llu",
                                    static_cast<unsigned long long>(
                                        result->TextureUploadRequests));
                    }
                }
                DrawDiagnostics(frame.FileImport.Diagnostics);
            }
            ImGui::End();

            if (ImGui::Begin("Frame Graph"))
            {
                if (!frame.RenderGraph.Enabled)
                {
                    ImGui::TextDisabled("Renderer frame graph diagnostics are unavailable.");
                    DrawDiagnostics(frame.RenderGraph.Diagnostics);
                }
                else
                {
                    ImGui::TextWrapped("%s",
                                       frame.RenderGraph.StatusText.c_str());
                    ImGui::Text("Compile: %s (%llu us)",
                                frame.RenderGraph.CompileSucceeded ? "yes" : "no",
                                static_cast<unsigned long long>(
                                    frame.RenderGraph.CompileTimeMicros));
                    ImGui::Text("Execute: %s (%llu us), device=%s",
                                frame.RenderGraph.ExecuteSucceeded ? "yes" : "no",
                                static_cast<unsigned long long>(
                                    frame.RenderGraph.ExecuteTimeMicros),
                                frame.RenderGraph.DeviceOperational ? "operational" : "not operational");
                    ImGui::Text("Passes: %u live, %u culled",
                                frame.RenderGraph.PassCount,
                                frame.RenderGraph.CulledPassCount);
                    ImGui::Text("Resources: %u, barriers=%u, transient=%llu bytes",
                                frame.RenderGraph.ResourceCount,
                                frame.RenderGraph.BarrierCount,
                                static_cast<unsigned long long>(
                                    frame.RenderGraph.TransientMemoryEstimateBytes));
                    ImGui::Text("Queue handoffs: %u, timeline edges=%u signals=%u waits=%u ownership=%u",
                                frame.RenderGraph.QueueHandoffEdgeCount,
                                frame.RenderGraph.CrossQueueTimelineEdgeCount,
                                frame.RenderGraph.CrossQueueTimelineSignalCount,
                                frame.RenderGraph.CrossQueueTimelineWaitCount,
                                frame.RenderGraph.CrossQueueOwnershipTransferCount);
                    ImGui::Text("Command passes: recorded=%u skipped=%u nonOperational=%u unavailable=%u",
                                frame.RenderGraph.CommandPassesRecorded,
                                frame.RenderGraph.CommandPassesSkipped,
                                frame.RenderGraph.CommandPassesSkippedNonOperational,
                                frame.RenderGraph.CommandPassesSkippedUnavailable);
                    ImGui::Text("Async compute frames: %u",
                                frame.RenderGraph.AsyncComputeUtilizedFrames);
                    if (!frame.RenderGraph.LifecycleDiagnostic.empty())
                    {
                        ImGui::TextWrapped("Lifecycle: %s",
                                           frame.RenderGraph.LifecycleDiagnostic.c_str());
                    }
                    if (!frame.RenderGraph.Diagnostic.empty() &&
                        frame.RenderGraph.Diagnostic != frame.RenderGraph.StatusText)
                    {
                        ImGui::TextWrapped("Diagnostic: %s",
                                           frame.RenderGraph.Diagnostic.c_str());
                    }

                    if (ImGui::CollapsingHeader("Command Passes",
                                                ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        if (frame.RenderGraph.CommandPasses.empty())
                        {
                            ImGui::TextDisabled("No command pass records.");
                        }
                        for (const SandboxEditorRenderGraphPassModel& pass :
                             frame.RenderGraph.CommandPasses)
                        {
                            if (pass.HasTypedId)
                            {
                                ImGui::BulletText(
                                    "%s [%u] - %s",
                                    pass.Name.c_str(),
                                    pass.TypedId,
                                    pass.Status.c_str());
                            }
                            else
                            {
                                ImGui::BulletText("%s - %s",
                                                  pass.Name.c_str(),
                                                  pass.Status.c_str());
                            }
                        }
                    }

                    if (ImGui::CollapsingHeader("Compiler Debug Dump"))
                    {
                        if (frame.RenderGraph.DebugDump.empty())
                        {
                            ImGui::TextDisabled("No debug dump available.");
                        }
                        else
                        {
                            ImGui::BeginChild("##FrameGraphDebugDump",
                                              ImVec2(0.0f, 240.0f),
                                              true,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            ImGui::TextUnformatted(
                                frame.RenderGraph.DebugDump.c_str());
                            ImGui::EndChild();
                        }
                    }
                }
            }
            ImGui::End();

            if (ImGui::Begin("Camera / Render"))
            {
                if (frame.CameraRender.HasMainCameraController)
                {
                    ImGui::Text("Main camera: %s",
                                DebugNameForSandboxEditorCameraControllerKind(
                                    frame.CameraRender.MainCameraControllerKind));
                }
                else
                {
                    ImGui::TextDisabled("Main camera: not registered");
                }

                if (context != nullptr &&
                    frame.CameraRender.CameraControlsAvailable)
                {
                    ImGui::TextDisabled("Viewport controls: RMB/MMB drag rotates; WASD pans/moves; Shift accelerates; scroll zooms.");
                    if (ImGui::Button("Orbit"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = Core::Config::CameraControllerKind::Orbit,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Fly"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = Core::Config::CameraControllerKind::Fly,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Free look"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = Core::Config::CameraControllerKind::FreeLook,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Top down"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = Core::Config::CameraControllerKind::TopDown,
                            });
                    }
                }

                DrawDiagnostics(frame.CameraRender.Diagnostics);
            }
            ImGui::End();

            if (ImGui::Begin("Geometry Visualization"))
            {
                if (frame.Visualization.HasSelectedEntity)
                {
                    ImGui::Text("Selected render id: %u",
                                frame.Visualization.SelectedStableId);
                    ImGui::Text("Geometry domain: %s",
                                DebugNameForSandboxEditorGeometryDomain(
                                    frame.Visualization.SelectedDomain));

                    if (frame.Visualization.SpatialDebug.HasBinding)
                    {
                        ImGui::Text("Spatial debug: %s key=%llu",
                                    DebugNameForSandboxEditorSpatialDebugKind(
                                        frame.Visualization.SpatialDebug.Kind),
                                    static_cast<unsigned long long>(
                                        frame.Visualization.SpatialDebug.RegistryKey));
                    }
                    else
                    {
                        ImGui::TextDisabled("Spatial debug: disabled");
                    }

                    if (context != nullptr &&
                        frame.Visualization.GeometryDomainControlsAvailable)
                    {
                        if (ImGui::Button("Enable BVH debug"))
                        {
                            (void)ApplySandboxEditorSpatialDebugBindingCommand(
                                *context,
                                SandboxEditorSpatialDebugBindingCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .EnableBinding = true,
                                });
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear debug"))
                        {
                            (void)ApplySandboxEditorSpatialDebugBindingCommand(
                                *context,
                                SandboxEditorSpatialDebugBindingCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .EnableBinding = false,
                                });
                        }

                        if (frame.Visualization.Visualization.HasConfig)
                        {
                            ImGui::Text("Visualization: %s",
                                        DebugNameForSandboxEditorVisualizationColorSource(
                                            frame.Visualization.Visualization.Source));
                        }
                        else
                        {
                            ImGui::TextDisabled("Visualization: material/default");
                        }
                        if (frame.Visualization.AdapterBindingControlsAvailable)
                        {
                            if (frame.Visualization.AdapterBinding.HasBinding)
                            {
                                ImGui::Text(
                                    "Adapter: %s key=%llu buffer=%llu",
                                    DebugNameForSandboxEditorVisualizationAdapterBindingKind(
                                        frame.Visualization.AdapterBinding.Kind),
                                    static_cast<unsigned long long>(
                                        frame.Visualization.AdapterBinding.AdapterKey),
                                    static_cast<unsigned long long>(
                                        frame.Visualization.AdapterBinding.BufferBDA));
                            }
                            else
                            {
                                ImGui::TextDisabled("Adapter: no runtime binding");
                            }
                        }

                        if (ImGui::Button("Uniform color"))
                        {
                            (void)ApplySandboxEditorVisualizationConfigCommand(
                                *context,
                                SandboxEditorVisualizationConfigCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .EnableConfig = true,
                                        .Source = G::VisualizationConfig::ColorSource::UniformColor,
                                    });
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear vis"))
                        {
                            (void)ApplySandboxEditorVisualizationConfigCommand(
                                *context,
                                SandboxEditorVisualizationConfigCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .EnableConfig = false,
                                });
                        }
                        DrawVisualizationPropertyPresets(
                            frame.Visualization.Properties,
                            *context,
                            frame.Visualization.SelectedStableId,
                            true);
                    }
                }
                DrawDiagnostics(frame.Visualization.Diagnostics);
            }
            ImGui::End();
        }
    }

    const char* DebugNameForSandboxEditorDiagnosticCode(
        const SandboxEditorDiagnosticCode code) noexcept
    {
        switch (code)
        {
        case SandboxEditorDiagnosticCode::MissingScene:
            return "MissingScene";
        case SandboxEditorDiagnosticCode::MissingSelectionController:
            return "MissingSelectionController";
        case SandboxEditorDiagnosticCode::MissingImGuiAdapter:
            return "MissingImGuiAdapter";
        case SandboxEditorDiagnosticCode::AssetImportUnavailable:
            return "AssetImportUnavailable";
        case SandboxEditorDiagnosticCode::AssetImportFailed:
            return "AssetImportFailed";
        case SandboxEditorDiagnosticCode::SceneFileUnavailable:
            return "SceneFileUnavailable";
        case SandboxEditorDiagnosticCode::SceneFileFailed:
            return "SceneFileFailed";
        case SandboxEditorDiagnosticCode::NoSelectedEntity:
            return "NoSelectedEntity";
        case SandboxEditorDiagnosticCode::UnsupportedGeometryDomain:
            return "UnsupportedGeometryDomain";
        case SandboxEditorDiagnosticCode::CameraRenderCommandsUnavailable:
            return "CameraRenderCommandsUnavailable";
        case SandboxEditorDiagnosticCode::VisualizationCommandsUnavailable:
            return "VisualizationCommandsUnavailable";
        case SandboxEditorDiagnosticCode::InvalidVisualizationProperty:
            return "InvalidVisualizationProperty";
        case SandboxEditorDiagnosticCode::GeometryProcessingFailed:
            return "GeometryProcessingFailed";
        case SandboxEditorDiagnosticCode::RenderGraphStatsUnavailable:
            return "RenderGraphStatsUnavailable";
        case SandboxEditorDiagnosticCode::EditorCommandHistoryUnavailable:
            return "EditorCommandHistoryUnavailable";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorCommandStatus(
        const SandboxEditorCommandStatus status) noexcept
    {
        switch (status)
        {
        case SandboxEditorCommandStatus::Applied:
            return "Applied";
        case SandboxEditorCommandStatus::NoChange:
            return "NoChange";
        case SandboxEditorCommandStatus::MissingScene:
            return "MissingScene";
        case SandboxEditorCommandStatus::MissingSelectionController:
            return "MissingSelectionController";
        case SandboxEditorCommandStatus::MissingCameraControllerRegistry:
            return "MissingCameraControllerRegistry";
        case SandboxEditorCommandStatus::MissingAssetImportCommands:
            return "MissingAssetImportCommands";
        case SandboxEditorCommandStatus::MissingSceneFileCommands:
            return "MissingSceneFileCommands";
        case SandboxEditorCommandStatus::MissingPrimitiveViewCommands:
            return "MissingPrimitiveViewCommands";
        case SandboxEditorCommandStatus::MissingVisualizationCommands:
            return "MissingVisualizationCommands";
        case SandboxEditorCommandStatus::AssetImportFailed:
            return "AssetImportFailed";
        case SandboxEditorCommandStatus::SceneNewFailed:
            return "SceneNewFailed";
        case SandboxEditorCommandStatus::SceneSaveFailed:
            return "SceneSaveFailed";
        case SandboxEditorCommandStatus::SceneLoadFailed:
            return "SceneLoadFailed";
        case SandboxEditorCommandStatus::SceneCloseFailed:
            return "SceneCloseFailed";
        case SandboxEditorCommandStatus::StaleEntity:
            return "StaleEntity";
        case SandboxEditorCommandStatus::MissingTransform:
            return "MissingTransform";
        case SandboxEditorCommandStatus::UnsupportedGeometryDomain:
            return "UnsupportedGeometryDomain";
        case SandboxEditorCommandStatus::InvalidVisualizationProperty:
            return "InvalidVisualizationProperty";
        case SandboxEditorCommandStatus::InvalidProcessingParameters:
            return "InvalidProcessingParameters";
        case SandboxEditorCommandStatus::GeometryProcessingFailed:
            return "GeometryProcessingFailed";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorGeometryDomain(
        const GS::Domain domain) noexcept
    {
        switch (domain)
        {
        case GS::Domain::None:
            return "None";
        case GS::Domain::Mesh:
            return "Mesh";
        case GS::Domain::Graph:
            return "Graph";
        case GS::Domain::PointCloud:
            return "PointCloud";
        case GS::Domain::Unknown:
            return "Unknown";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorDomainWindowKind(
        const SandboxEditorDomainWindowKind kind) noexcept
    {
        switch (kind)
        {
        case SandboxEditorDomainWindowKind::Mesh:
            return "Mesh";
        case SandboxEditorDomainWindowKind::Graph:
            return "Graph";
        case SandboxEditorDomainWindowKind::PointCloud:
            return "PointCloud";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorPrimitiveKind(
        const RefinedPrimitiveKind kind) noexcept
    {
        switch (kind)
        {
        case RefinedPrimitiveKind::None:
            return "None";
        case RefinedPrimitiveKind::Entity:
            return "Entity";
        case RefinedPrimitiveKind::Face:
            return "Face";
        case RefinedPrimitiveKind::Edge:
            return "Edge";
        case RefinedPrimitiveKind::Vertex:
            return "Vertex";
        case RefinedPrimitiveKind::Point:
            return "Point";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorCameraControllerKind(
        const Core::Config::CameraControllerKind kind) noexcept
    {
        switch (kind)
        {
        case Core::Config::CameraControllerKind::Orbit:
            return "Orbit";
        case Core::Config::CameraControllerKind::Fly:
            return "Fly";
        case Core::Config::CameraControllerKind::FreeLook:
            return "FreeLook";
        case Core::Config::CameraControllerKind::TopDown:
            return "TopDown";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorSpatialDebugKind(
        const ECSC::SpatialDebugGeometryKind kind) noexcept
    {
        switch (kind)
        {
        case ECSC::SpatialDebugGeometryKind::Bvh:
            return "Bvh";
        case ECSC::SpatialDebugGeometryKind::KdTree:
            return "KdTree";
        case ECSC::SpatialDebugGeometryKind::Octree:
            return "Octree";
        case ECSC::SpatialDebugGeometryKind::ConvexHull:
            return "ConvexHull";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationColorSource(
        const G::VisualizationConfig::ColorSource source) noexcept
    {
        switch (source)
        {
        case G::VisualizationConfig::ColorSource::Material:
            return "Material";
        case G::VisualizationConfig::ColorSource::UniformColor:
            return "UniformColor";
        case G::VisualizationConfig::ColorSource::ScalarField:
            return "ScalarField";
        case G::VisualizationConfig::ColorSource::PerVertexBuffer:
            return "PerVertexBuffer";
        case G::VisualizationConfig::ColorSource::PerEdgeBuffer:
            return "PerEdgeBuffer";
        case G::VisualizationConfig::ColorSource::PerFaceBuffer:
            return "PerFaceBuffer";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationDomain(
        const G::VisualizationConfig::Domain domain) noexcept
    {
        switch (domain)
        {
        case G::VisualizationConfig::Domain::Vertex:
            return "Vertex";
        case G::VisualizationConfig::Domain::Edge:
            return "Edge";
        case G::VisualizationConfig::Domain::Face:
            return "Face";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationAdapterBindingKind(
        const RenderExtractionCache::VisualizationAdapterBindingKind kind) noexcept
    {
        using Kind = RenderExtractionCache::VisualizationAdapterBindingKind;
        switch (kind)
        {
        case Kind::Scalar:
            return "Scalar";
        case Kind::Color:
            return "Color";
        case Kind::VectorField:
            return "VectorField";
        case Kind::Isoline:
            return "Isoline";
        case Kind::HtexMetadata:
            return "HtexMetadata";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationPropertyDomain(
        const SandboxEditorVisualizationPropertyDomain domain) noexcept
    {
        using Domain = SandboxEditorVisualizationPropertyDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return "MeshVertices";
        case Domain::MeshEdges:
            return "MeshEdges";
        case Domain::MeshFaces:
            return "MeshFaces";
        case Domain::GraphVertices:
            return "GraphVertices";
        case Domain::GraphEdges:
            return "GraphEdges";
        case Domain::PointCloudPoints:
            return "PointCloudPoints";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationPropertyValueKind(
        const SandboxEditorVisualizationPropertyValueKind kind) noexcept
    {
        using Kind = SandboxEditorVisualizationPropertyValueKind;
        switch (kind)
        {
        case Kind::ScalarFloat:
            return "ScalarFloat";
        case Kind::ScalarDouble:
            return "ScalarDouble";
        case Kind::Vec3:
            return "Vec3";
        case Kind::Vec4:
            return "Vec4";
        case Kind::UInt32:
            return "UInt32";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorVisualizationPropertyPreset(
        const SandboxEditorVisualizationPropertyPreset preset) noexcept
    {
        using Preset = SandboxEditorVisualizationPropertyPreset;
        switch (preset)
        {
        case Preset::Scalar:
            return "Scalar";
        case Preset::Isoline:
            return "Isoline";
        case Preset::ColorBuffer:
            return "ColorBuffer";
        }
        return "Unknown";
    }

    SandboxEditorGeometryProcessingDomain
    GetSandboxEditorSupportedGeometryProcessingDomains(
        const SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept
    {
        using Domain = SandboxEditorGeometryProcessingDomain;
        switch (algorithm)
        {
        case SandboxEditorGeometryProcessingAlgorithm::KMeans:
            return Domain::MeshVertices |
                   Domain::GraphVertices |
                   Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::Remeshing:
        case SandboxEditorGeometryProcessingAlgorithm::Simplification:
        case SandboxEditorGeometryProcessingAlgorithm::Smoothing:
        case SandboxEditorGeometryProcessingAlgorithm::Subdivision:
        case SandboxEditorGeometryProcessingAlgorithm::Repair:
            return kMeshTopologyDomains;
        case SandboxEditorGeometryProcessingAlgorithm::NormalEstimation:
            return Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::ShortestPath:
            return Domain::MeshVertices | Domain::GraphVertices;
        case SandboxEditorGeometryProcessingAlgorithm::ConvexHull:
            return Domain::MeshVertices | Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            return Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::VectorHeat:
            return Domain::MeshVertices;
        case SandboxEditorGeometryProcessingAlgorithm::Parameterization:
            return Domain::MeshVertices | Domain::MeshFaces;
        case SandboxEditorGeometryProcessingAlgorithm::BooleanCSG:
            return Domain::MeshVertices | Domain::MeshFaces;
        case SandboxEditorGeometryProcessingAlgorithm::Registration:
        case SandboxEditorGeometryProcessingAlgorithm::BilateralFilter:
        case SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation:
        case SandboxEditorGeometryProcessingAlgorithm::KernelDensity:
            return Domain::PointCloudPoints;
        }
        return Domain::None;
    }

    bool SupportsSandboxEditorGeometryProcessingDomain(
        const SandboxEditorGeometryProcessingAlgorithm algorithm,
        const SandboxEditorGeometryProcessingDomain domain) noexcept
    {
        return HasAnySandboxEditorGeometryProcessingDomain(
            GetSandboxEditorSupportedGeometryProcessingDomains(algorithm),
            domain);
    }

    SandboxEditorGeometryProcessingCapabilities
    GetSandboxEditorGeometryProcessingCapabilities(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        SandboxEditorGeometryProcessingCapabilities capabilities{};
        const entt::registry& raw = registry.Raw();
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return capabilities;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        capabilities.Domains = DomainsForSourceView(view);
        capabilities.HasEditableSurfaceMesh =
            view.ActiveDomain == GS::Domain::Mesh && view.Valid();
        return capabilities;
    }

    std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        const SandboxEditorGeometryProcessingCapabilities capabilities)
    {
        static constexpr std::array<SandboxEditorGeometryProcessingAlgorithm, 17>
            kAlgorithmOrder{
                SandboxEditorGeometryProcessingAlgorithm::KMeans,
                SandboxEditorGeometryProcessingAlgorithm::NormalEstimation,
                SandboxEditorGeometryProcessingAlgorithm::Registration,
                SandboxEditorGeometryProcessingAlgorithm::BilateralFilter,
                SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation,
                SandboxEditorGeometryProcessingAlgorithm::KernelDensity,
                SandboxEditorGeometryProcessingAlgorithm::ShortestPath,
                SandboxEditorGeometryProcessingAlgorithm::VectorHeat,
                SandboxEditorGeometryProcessingAlgorithm::Parameterization,
                SandboxEditorGeometryProcessingAlgorithm::ConvexHull,
                SandboxEditorGeometryProcessingAlgorithm::SurfaceReconstruction,
                SandboxEditorGeometryProcessingAlgorithm::BooleanCSG,
                SandboxEditorGeometryProcessingAlgorithm::Remeshing,
                SandboxEditorGeometryProcessingAlgorithm::Simplification,
                SandboxEditorGeometryProcessingAlgorithm::Smoothing,
                SandboxEditorGeometryProcessingAlgorithm::Subdivision,
                SandboxEditorGeometryProcessingAlgorithm::Repair,
            };

        std::vector<SandboxEditorGeometryProcessingEntry> entries{};
        entries.reserve(kAlgorithmOrder.size());
        for (const SandboxEditorGeometryProcessingAlgorithm algorithm :
             kAlgorithmOrder)
        {
            if (IsSurfaceTopologyAlgorithm(algorithm) &&
                !capabilities.HasEditableSurfaceMesh)
            {
                continue;
            }

            const SandboxEditorGeometryProcessingDomain domains =
                capabilities.Domains &
                GetSandboxEditorSupportedGeometryProcessingDomains(algorithm);
            if (domains == SandboxEditorGeometryProcessingDomain::None)
                continue;

            entries.push_back(SandboxEditorGeometryProcessingEntry{
                .Algorithm = algorithm,
                .Domains = domains,
            });
        }
        return entries;
    }

    std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        return ResolveSandboxEditorGeometryProcessingEntries(
            GetSandboxEditorGeometryProcessingCapabilities(registry, entity));
    }

    std::vector<SandboxEditorGeometryProcessingDomain>
    GetAvailableSandboxEditorKMeansDomains(
        const ECS::Scene::Registry& registry,
        const ECS::EntityHandle entity)
    {
        using Domain = SandboxEditorGeometryProcessingDomain;
        const Domain domains =
            GetSandboxEditorGeometryProcessingCapabilities(registry, entity)
                .Domains &
            GetSandboxEditorSupportedGeometryProcessingDomains(
                SandboxEditorGeometryProcessingAlgorithm::KMeans);

        std::vector<Domain> result{};
        result.reserve(3u);
        if (HasAnySandboxEditorGeometryProcessingDomain(
                domains,
                Domain::MeshVertices))
        {
            result.push_back(Domain::MeshVertices);
        }
        if (HasAnySandboxEditorGeometryProcessingDomain(
                domains,
                Domain::GraphVertices))
        {
            result.push_back(Domain::GraphVertices);
        }
        if (HasAnySandboxEditorGeometryProcessingDomain(
                domains,
                Domain::PointCloudPoints))
        {
            result.push_back(Domain::PointCloudPoints);
        }
        return result;
    }

    const char* DebugNameForSandboxEditorGeometryProcessingDomain(
        const SandboxEditorGeometryProcessingDomain domain) noexcept
    {
        using Domain = SandboxEditorGeometryProcessingDomain;
        switch (domain)
        {
        case Domain::None:
            return "None";
        case Domain::MeshVertices:
            return "Mesh Vertices";
        case Domain::MeshEdges:
            return "Mesh Edges";
        case Domain::MeshHalfedges:
            return "Mesh Halfedges";
        case Domain::MeshFaces:
            return "Mesh Faces";
        case Domain::GraphVertices:
            return "Graph Nodes";
        case Domain::GraphEdges:
            return "Graph Edges";
        case Domain::GraphHalfedges:
            return "Graph Halfedges";
        case Domain::PointCloudPoints:
            return "Point Cloud Points";
        }
        return "Mixed";
    }

    const char* DebugNameForSandboxEditorGeometryProcessingAlgorithm(
        const SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
        case SandboxEditorGeometryProcessingAlgorithm::KMeans:
            return "K-Means";
        case SandboxEditorGeometryProcessingAlgorithm::Remeshing:
            return "Remeshing";
        case SandboxEditorGeometryProcessingAlgorithm::Simplification:
            return "Simplification";
        case SandboxEditorGeometryProcessingAlgorithm::Smoothing:
            return "Smoothing";
        case SandboxEditorGeometryProcessingAlgorithm::Subdivision:
            return "Subdivision";
        case SandboxEditorGeometryProcessingAlgorithm::Repair:
            return "Repair";
        case SandboxEditorGeometryProcessingAlgorithm::NormalEstimation:
            return "Normal Estimation";
        case SandboxEditorGeometryProcessingAlgorithm::ShortestPath:
            return "Shortest Path";
        case SandboxEditorGeometryProcessingAlgorithm::ConvexHull:
            return "Convex Hull";
        case SandboxEditorGeometryProcessingAlgorithm::SurfaceReconstruction:
            return "Surface Reconstruction";
        case SandboxEditorGeometryProcessingAlgorithm::VectorHeat:
            return "Vector Heat Method";
        case SandboxEditorGeometryProcessingAlgorithm::Parameterization:
            return "Parameterization";
        case SandboxEditorGeometryProcessingAlgorithm::BooleanCSG:
            return "Boolean CSG";
        case SandboxEditorGeometryProcessingAlgorithm::Registration:
            return "ICP Registration";
        case SandboxEditorGeometryProcessingAlgorithm::BilateralFilter:
            return "Bilateral Filter";
        case SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation:
            return "Outlier Estimation";
        case SandboxEditorGeometryProcessingAlgorithm::KernelDensity:
            return "Kernel Density";
        }
        return "Unknown";
    }

    SandboxEditorPanelFrame BuildSandboxEditorPanelFrame(
        const SandboxEditorContext& context)
    {
        SandboxEditorPanelFrame frame{};
        if (context.Scene == nullptr)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingScene,
                          "Scene registry is unavailable.");
        }
        else
        {
            const entt::registry& raw = context.Scene->Raw();
            raw.view<entt::entity>().each(
                [&frame, &raw](const ECS::EntityHandle entity)
                {
                    frame.Hierarchy.push_back(BuildEntityRow(raw, entity));
                });
            std::sort(frame.Hierarchy.begin(),
                      frame.Hierarchy.end(),
                      [](const SandboxEditorEntityRow& lhs,
                         const SandboxEditorEntityRow& rhs)
                      {
                          if (lhs.StableEntityId != rhs.StableEntityId)
                              return lhs.StableEntityId < rhs.StableEntityId;
                          return lhs.Name < rhs.Name;
                      });
        }

        if (context.Selection == nullptr)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingSelectionController,
                          "Selection controller is unavailable.");
        }
        if (!context.ImGuiAdapterAvailable)
        {
            AddDiagnostic(frame.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingImGuiAdapter,
                          "Runtime ImGui adapter is unavailable.");
        }

        frame.Inspector = BuildInspectorModel(context);
        frame.Selection = BuildSelectionModel(context);
        frame.Document = BuildDocumentModel(context);
        frame.SceneFile = BuildSceneFileModel(context);
        frame.FileImport = BuildFileImportModel(context);
        frame.RenderGraph = BuildRenderGraphModel(context);
        frame.CameraRender = BuildCameraRenderModel(context);
        frame.Visualization = BuildVisualizationModel(context);
        return frame;
    }

    SandboxEditorDomainWindowModel BuildSandboxEditorDomainWindowModel(
        const SandboxEditorContext& context,
        const SandboxEditorDomainWindowKind kind)
    {
        SandboxEditorDomainWindowModel model{};
        model.Kind = kind;
        model.ExpectedDomain = ExpectedDomainForWindowKind(kind);
        model.VisualizationControlsAvailable =
            context.VisualizationCommandsAvailable;

        if (context.Scene == nullptr)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingScene,
                          "Scene registry is unavailable for domain window.");
            return model;
        }
        if (context.Selection == nullptr)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::MissingSelectionController,
                          "Selection controller is unavailable for domain window.");
            return model;
        }

        const std::optional<ECS::EntityHandle> selected =
            ResolveFirstSelectedEntity(context);
        if (!selected.has_value())
        {
            const bool hadStaleSelection =
                !context.Selection->SelectedStableIds().empty();
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::NoSelectedEntity,
                          hadStaleSelection
                              ? "Selected entity is stale or no longer live."
                              : "No selected entity is available for domain window.");
            return model;
        }

        const entt::registry& raw = context.Scene->Raw();
        model.HasSelectedEntity = true;
        model.SelectedEntity = BuildEntityRow(raw, *selected);
        model.SelectedStableId = SelectionController::ToStableEntityId(*selected);
        model.RenderHints = BuildRenderHintModel(raw, *selected);
        model.SelectedDomain = GS::BuildConstView(raw, *selected).ActiveDomain;
        model.DomainMatches = model.SelectedDomain == model.ExpectedDomain;
        if (model.DomainMatches)
        {
            model.Processing = BuildGeometryProcessingModel(context);
            AppendDiagnostics(model.Diagnostics, model.Processing.Diagnostics);
        }

        if (!model.DomainMatches)
        {
            std::string message =
                std::string(DebugNameForSandboxEditorDomainWindowKind(kind));
            message += " window requires ";
            message += DebugNameForSandboxEditorGeometryDomain(model.ExpectedDomain);
            message += "-domain selection; selected domain is ";
            message += DebugNameForSandboxEditorGeometryDomain(model.SelectedDomain);
            message += ".";
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                          std::move(message));
        }

        if (!context.VisualizationCommandsAvailable)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::VisualizationCommandsUnavailable,
                          "Visualization command seams are unavailable.");
        }
        else
        {
            model.Visualization = BuildVisualizationModel(context);
            AppendDiagnostics(model.Diagnostics, model.Visualization.Diagnostics);
        }

        if (context.LastRefinedPrimitive != nullptr &&
            context.LastRefinedPrimitive->has_value())
        {
            const PrimitiveSelectionResult& primitive =
                **context.LastRefinedPrimitive;
            const bool sameEntity =
                primitive.EntityId == model.SelectedStableId ||
                primitive.StableId == model.SelectedStableId;
            if (sameEntity && primitive.Domain == model.SelectedDomain)
                model.Primitive = BuildPrimitiveDetailModel(primitive);
        }

        return model;
    }

    bool SelectSandboxEditorEntity(const SandboxEditorContext& context,
                                   const std::uint32_t stableEntityId)
    {
        if (context.Scene == nullptr || context.Selection == nullptr)
            return false;
        if (context.CommandHistory != nullptr)
        {
            std::optional<std::uint32_t> before{};
            const auto selected = context.Selection->SelectedStableIds();
            if (selected.size() == 1u)
                before = selected.front();
            else if (!selected.empty())
                return context.Selection->SetSelectedByStableEntityId(
                    *context.Scene,
                    stableEntityId);

            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    MakeSelectionReplaceCommand(
                        EditorSelectionReplaceCommand{
                            .Scene = context.Scene,
                            .Selection = context.Selection,
                            .BeforeStableEntityId = before,
                            .AfterStableEntityId = stableEntityId,
                            .Label = "Select Entity",
                        }));
            return result.Succeeded();
        }
        return context.Selection->SetSelectedByStableEntityId(*context.Scene,
                                                              stableEntityId);
    }

    SandboxEditorFileImportResult ApplySandboxEditorFileImportCommand(
        const SandboxEditorContext& context,
        const SandboxEditorFileImportCommand& command)
    {
        if (!context.AssetImportCommands.Available())
        {
            return SandboxEditorFileImportResult{
                .Status = SandboxEditorCommandStatus::MissingAssetImportCommands,
                .PayloadKind = command.PayloadKind,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Asset import command surface is unavailable.",
            };
        }
        if (command.Path.empty())
        {
            return SandboxEditorFileImportResult{
                .Status = SandboxEditorCommandStatus::AssetImportFailed,
                .PayloadKind = command.PayloadKind,
                .Error = Core::ErrorCode::InvalidPath,
                .Message = BuildImportFailureMessage(Core::ErrorCode::InvalidPath),
            };
        }

        SandboxEditorFileImportResult result =
            context.AssetImportCommands.Import(command);
        if (result.Status == SandboxEditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildImportSuccessMessage(command, result);
            result.Error = Core::ErrorCode::Success;
        }
        else if (result.Message.empty())
        {
            result.Message = BuildImportFailureMessage(result.Error);
        }
        return result;
    }

    SandboxEditorSceneFileResult ApplySandboxEditorSceneSaveCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSceneFileCommand& command)
    {
        if (!context.SceneFileCommands.Available())
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::MissingSceneFileCommands,
                .Operation = SandboxEditorSceneFileOperation::Save,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Scene file command surface is unavailable.",
            };
        }
        if (command.Path.empty())
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::SceneSaveFailed,
                .Operation = SandboxEditorSceneFileOperation::Save,
                .Error = Core::ErrorCode::InvalidPath,
                .Message = BuildSceneFileFailureMessage(
                    SandboxEditorSceneFileOperation::Save,
                    Core::ErrorCode::InvalidPath),
            };
        }

        SandboxEditorSceneFileResult result = context.SceneFileCommands.Save(command);
        result.Operation = SandboxEditorSceneFileOperation::Save;
        if (result.Status == SandboxEditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage(command, result);
            result.Error = Core::ErrorCode::Success;
        }
        else if (result.Message.empty())
        {
            result.Message = BuildSceneFileFailureMessage(result.Operation, result.Error);
        }
        return result;
    }

    SandboxEditorSceneFileResult ApplySandboxEditorSceneLoadCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSceneFileCommand& command)
    {
        if (!context.SceneFileCommands.Available())
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::MissingSceneFileCommands,
                .Operation = SandboxEditorSceneFileOperation::Load,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Scene file command surface is unavailable.",
            };
        }
        if (command.Path.empty())
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::SceneLoadFailed,
                .Operation = SandboxEditorSceneFileOperation::Load,
                .Error = Core::ErrorCode::InvalidPath,
                .Message = BuildSceneFileFailureMessage(
                    SandboxEditorSceneFileOperation::Load,
                    Core::ErrorCode::InvalidPath),
            };
        }

        SandboxEditorSceneFileResult result = context.SceneFileCommands.Load(command);
        result.Operation = SandboxEditorSceneFileOperation::Load;
        if (result.Status == SandboxEditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage(command, result);
            result.Error = Core::ErrorCode::Success;
        }
        else if (result.Message.empty())
        {
            result.Message = BuildSceneFileFailureMessage(result.Operation, result.Error);
        }
        return result;
    }

    SandboxEditorSceneFileResult ApplySandboxEditorNewSceneCommand(
        const SandboxEditorContext& context)
    {
        if (!context.SceneFileCommands.New)
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::MissingSceneFileCommands,
                .Operation = SandboxEditorSceneFileOperation::New,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "New scene command surface is unavailable.",
            };
        }

        SandboxEditorSceneFileResult result = context.SceneFileCommands.New();
        result.Operation = SandboxEditorSceneFileOperation::New;
        if (result.Status == SandboxEditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage({}, result);
            result.Error = Core::ErrorCode::Success;
        }
        else if (result.Message.empty())
        {
            result.Message = BuildSceneFileFailureMessage(result.Operation,
                                                          result.Error);
        }
        return result;
    }

    SandboxEditorSceneFileResult ApplySandboxEditorCloseSceneCommand(
        const SandboxEditorContext& context)
    {
        if (!context.SceneFileCommands.Close)
        {
            return SandboxEditorSceneFileResult{
                .Status = SandboxEditorCommandStatus::MissingSceneFileCommands,
                .Operation = SandboxEditorSceneFileOperation::Close,
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Close scene command surface is unavailable.",
            };
        }

        SandboxEditorSceneFileResult result = context.SceneFileCommands.Close();
        result.Operation = SandboxEditorSceneFileOperation::Close;
        if (result.Status == SandboxEditorCommandStatus::Applied)
        {
            if (result.Message.empty())
                result.Message = BuildSceneFileSuccessMessage({}, result);
            result.Error = Core::ErrorCode::Success;
        }
        else if (result.Message.empty())
        {
            result.Message = BuildSceneFileFailureMessage(result.Operation,
                                                          result.Error);
        }
        return result;
    }

    SandboxEditorCommandStatus ApplySandboxEditorTransformEdit(
        const SandboxEditorContext& context,
        const SandboxEditorTransformEditCommand& command)
    {
        if (!command.SetPosition && !command.SetRotation && !command.SetScale)
            return SandboxEditorCommandStatus::NoChange;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (context.Selection == nullptr)
            return SandboxEditorCommandStatus::MissingSelectionController;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        auto* transform = raw.try_get<ECSC::Transform::Component>(entity);
        if (transform == nullptr)
            return SandboxEditorCommandStatus::MissingTransform;

        if (context.CommandHistory != nullptr)
        {
            ECSC::Transform::Component next = *transform;
            if (command.SetPosition)
                next.Position = command.Position;
            if (command.SetRotation)
                next.Rotation = command.Rotation;
            if (command.SetScale)
                next.Scale = command.Scale;

            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    MakeTransformEditCommand(
                        EditorTransformEditCommand{
                            .Scene = context.Scene,
                            .StableEntityId = command.StableEntityId,
                            .Before = *transform,
                            .After = next,
                            .Label = "Edit Transform",
                        }));
            return ToSandboxEditorCommandStatus(result.Status);
        }

        if (command.SetPosition)
            transform->Position = command.Position;
        if (command.SetRotation)
            transform->Rotation = command.Rotation;
        if (command.SetScale)
            transform->Scale = command.Scale;
        raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(entity);
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorCameraControllerCommand(
        const SandboxEditorContext& context,
        const SandboxEditorCameraControllerCommand& command)
    {
        if (context.CameraControllers == nullptr)
            return SandboxEditorCommandStatus::MissingCameraControllerRegistry;

        ICameraController* existing =
            context.CameraControllers->ResolveOrNull(command.Slot);
        if (existing != nullptr && existing->Kind() == command.Kind &&
            command.PreserveCurrentView)
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        Graphics::CameraViewInput seed{};
        if (command.PreserveCurrentView && existing != nullptr)
        {
            seed = existing->GetView(
                SafeViewport(command.Viewport, context.CameraViewport));
        }

        context.CameraControllers->Replace(
            command.Slot,
            CreateCameraController(command.Kind, seed));
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorPrimitiveViewCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPrimitiveViewCommand& command)
    {
        if (!command.SetEdgeView &&
            !command.SetVertexView &&
            !command.SetVertexRenderMode &&
            !command.SetVertexPointRadius)
        {
            return SandboxEditorCommandStatus::NoChange;
        }
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (command.SetVertexPointRadius &&
            !IsFinitePositive(command.VertexPointRadiusPx))
        {
            return SandboxEditorCommandStatus::InvalidProcessingParameters;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        if (view.ActiveDomain != GS::Domain::Mesh)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const SandboxEditorRenderHintState before =
            ReadRenderHintState(raw, entity);
        SandboxEditorRenderHintState after = before;
        if (command.SetEdgeView)
        {
            if (command.EnableEdgeView)
            {
                after.Edges = after.Edges.value_or(G::RenderEdges{});
            }
            else
            {
                after.Edges.reset();
            }
        }
        if (command.SetVertexView)
        {
            if (command.EnableVertexView)
            {
                after.Points = after.Points.value_or(G::RenderPoints{});
            }
            else
            {
                after.Points.reset();
            }
        }
        if (after.Points.has_value())
        {
            if (command.SetVertexRenderMode)
                after.Points->Type = ToRenderPointType(command.VertexRenderMode);
            if (command.SetVertexPointRadius)
                after.Points->SizeSource = command.VertexPointRadiusPx;
        }

        if (SameRenderHintState(before, after))
            return SandboxEditorCommandStatus::NoChange;
        if (context.CommandHistory != nullptr)
        {
            const std::uint32_t stableEntityId = command.StableEntityId;
            ECS::Scene::Registry* scene = context.Scene;
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    EditorCommandRecord{
                        .Label = "Change Render Hints",
                        .Redo =
                            [scene, stableEntityId, after]()
                            {
                                return ApplyRenderHintState(
                                    scene, stableEntityId, after);
                            },
                        .Undo =
                            [scene, stableEntityId, before]()
                            {
                                return ApplyRenderHintState(
                                    scene, stableEntityId, before);
                            },
                    });
            return ToSandboxEditorCommandStatus(result.Status);
        }
        return ToSandboxEditorCommandStatus(
            ApplyRenderHintState(context.Scene, command.StableEntityId, after));
    }

    SandboxEditorCommandStatus ApplySandboxEditorRenderHintCommand(
        const SandboxEditorContext& context,
        const SandboxEditorRenderHintCommand& command)
    {
        if (!AnyRenderHintEdit(command))
            return SandboxEditorCommandStatus::NoChange;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if ((command.SetUniformEdgeWidth &&
             !IsFinitePositive(command.UniformEdgeWidth)) ||
            (command.SetUniformPointSize &&
             !IsFinitePositive(command.UniformPointSize)))
        {
            return SandboxEditorCommandStatus::InvalidProcessingParameters;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const GS::Domain domain = GS::BuildConstView(raw, entity).ActiveDomain;
        if (!RenderHintCommandMatchesDomain(command, domain))
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const SandboxEditorRenderHintState before =
            ReadRenderHintState(raw, entity);
        const SandboxEditorRenderHintState after =
            ApplyRenderHintCommandToState(before, command);
        if (SameRenderHintState(before, after))
            return SandboxEditorCommandStatus::NoChange;

        if (context.CommandHistory != nullptr)
        {
            const std::uint32_t stableEntityId = command.StableEntityId;
            ECS::Scene::Registry* scene = context.Scene;
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    EditorCommandRecord{
                        .Label = "Change Render Hints",
                        .Redo =
                            [scene, stableEntityId, after]()
                            {
                                return ApplyRenderHintState(
                                    scene, stableEntityId, after);
                            },
                        .Undo =
                            [scene, stableEntityId, before]()
                            {
                                return ApplyRenderHintState(
                                    scene, stableEntityId, before);
                            },
                        .Dirtying = true,
                    });
            return ToSandboxEditorCommandStatus(result.Status);
        }

        return ToSandboxEditorCommandStatus(
            ApplyRenderHintState(context.Scene, command.StableEntityId, after));
    }

    SandboxEditorCommandStatus ApplySandboxEditorSpatialDebugBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorSpatialDebugBindingCommand& command)
    {
        if (!context.VisualizationCommandsAvailable)
            return SandboxEditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const auto* current = raw.try_get<ECSC::SpatialDebugBinding>(entity);
        const std::optional<ECSC::SpatialDebugBinding> before =
            current != nullptr
                ? std::optional<ECSC::SpatialDebugBinding>{*current}
                : std::nullopt;
        const std::optional<ECSC::SpatialDebugBinding> after =
            command.EnableBinding
                ? std::optional<ECSC::SpatialDebugBinding>{ToSpatialDebugBinding(command)}
                : std::nullopt;

        if (!after.has_value())
        {
            if (!before.has_value())
                return SandboxEditorCommandStatus::NoChange;
        }
        else if (before.has_value() &&
                 SameSpatialDebugBinding(*before, *after))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    MakeSpatialDebugBindingCommand(
                        EditorSpatialDebugBindingCommand{
                            .Scene = context.Scene,
                            .StableEntityId = command.StableEntityId,
                            .Before = before,
                            .After = after,
                            .Label = "Change Spatial Debug Binding",
                        }));
            return ToSandboxEditorCommandStatus(result.Status);
        }

        if (after.has_value())
            raw.emplace_or_replace<ECSC::SpatialDebugBinding>(entity, *after);
        else
            raw.remove<ECSC::SpatialDebugBinding>(entity);
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationConfigCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationConfigCommand& command)
    {
        if (!context.VisualizationCommandsAvailable)
            return SandboxEditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const auto* current = raw.try_get<G::VisualizationConfig>(entity);
        const std::optional<G::VisualizationConfig> before =
            current != nullptr
                ? std::optional<G::VisualizationConfig>{*current}
                : std::nullopt;
        const std::optional<G::VisualizationConfig> after =
            command.EnableConfig
                ? std::optional<G::VisualizationConfig>{ToVisualizationConfig(command)}
                : std::nullopt;

        if (!after.has_value())
        {
            if (!before.has_value())
                return SandboxEditorCommandStatus::NoChange;
        }
        else if (before.has_value() && SameVisualizationConfig(*before, *after))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(
                    MakeVisualizationConfigCommand(
                        EditorVisualizationConfigCommand{
                            .Scene = context.Scene,
                            .StableEntityId = command.StableEntityId,
                            .Before = before,
                            .After = after,
                            .Label = "Change Visualization",
                        }));
            return ToSandboxEditorCommandStatus(result.Status);
        }

        if (after.has_value())
            raw.emplace_or_replace<G::VisualizationConfig>(entity, *after);
        else
            raw.remove<G::VisualizationConfig>(entity);
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationPropertyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationPropertyCommand& command)
    {
        if (!context.VisualizationCommandsAvailable)
            return SandboxEditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;
        if (command.PropertyName.empty())
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const Geometry::PropertySet* properties =
            PropertySetForVisualizationDomain(view, command.Domain);
        if (properties == nullptr)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        std::optional<SandboxEditorVisualizationPropertyInfo> matched{};
        std::vector<SandboxEditorVisualizationPropertyInfo> allProperties{};
        AppendVisualizationPropertiesForDomain(
            allProperties,
            *properties,
            command.Domain);
        for (const SandboxEditorVisualizationPropertyInfo& property :
             allProperties)
        {
            if (property.Name == command.PropertyName)
            {
                matched = property;
                break;
            }
        }
        if (!matched.has_value() ||
            !PropertySupportsPreset(*matched, command.Preset))
        {
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;
        }

        SandboxEditorVisualizationConfigCommand configCommand{
            .StableEntityId = command.StableEntityId,
            .EnableConfig = true,
            .ScalarFieldName = command.PropertyName,
            .ScalarDomain = ToVisualizationConfigDomain(command.Domain),
            .ColorBufferName = command.PropertyName,
            .ScalarAutoRange = command.ScalarAutoRange,
            .ScalarRangeMin = command.ScalarRangeMin,
            .ScalarRangeMax = command.ScalarRangeMax,
            .ScalarBinCount = command.ScalarBinCount,
        };

        switch (command.Preset)
        {
        case SandboxEditorVisualizationPropertyPreset::Scalar:
            if (!command.ScalarAutoRange &&
                !(command.ScalarRangeMin < command.ScalarRangeMax))
            {
                return SandboxEditorCommandStatus::InvalidVisualizationProperty;
            }
            configCommand.Source =
                G::VisualizationConfig::ColorSource::ScalarField;
            configCommand.IsolineCount = 0u;
            break;
        case SandboxEditorVisualizationPropertyPreset::Isoline:
            if (command.IsolineCount == 0u ||
                (!command.ScalarAutoRange &&
                 !(command.ScalarRangeMin < command.ScalarRangeMax)))
            {
                return SandboxEditorCommandStatus::InvalidVisualizationProperty;
            }
            configCommand.Source =
                G::VisualizationConfig::ColorSource::ScalarField;
            configCommand.IsolineCount = command.IsolineCount;
            break;
        case SandboxEditorVisualizationPropertyPreset::ColorBuffer:
            configCommand.Source = ToColorBufferSource(command.Domain);
            configCommand.IsolineCount = 0u;
            break;
        }

        return ApplySandboxEditorVisualizationConfigCommand(
            context,
            configCommand);
    }

    SandboxEditorCommandStatus ApplySandboxEditorVisualizationAdapterBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVisualizationAdapterBindingCommand& command)
    {
        if (!context.VisualizationCommandsAvailable ||
            !context.VisualizationAdapterBindings.Available())
            return SandboxEditorCommandStatus::MissingVisualizationCommands;
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        if (GS::BuildConstView(raw, entity).ActiveDomain == GS::Domain::None)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const std::optional<RenderExtractionCache::VisualizationAdapterBinding>
            current =
            context.VisualizationAdapterBindings.GetBinding(command.StableEntityId);
        if (!command.EnableBinding)
        {
            if (!current.has_value())
                return SandboxEditorCommandStatus::NoChange;
            context.VisualizationAdapterBindings.ClearBinding(command.StableEntityId);
            return SandboxEditorCommandStatus::Applied;
        }

        const RenderExtractionCache::VisualizationAdapterBinding next =
            ToVisualizationAdapterBinding(command);
        if (current.has_value() &&
            SameVisualizationAdapterBinding(*current, next))
            return SandboxEditorCommandStatus::NoChange;

        context.VisualizationAdapterBindings.SetBinding(
            command.StableEntityId,
            next);
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorKMeansResult ApplySandboxEditorKMeansCommand(
        const SandboxEditorContext& context,
        const SandboxEditorKMeansCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::MissingScene,
                command.Domain,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for K-Means.");
        }
        if (!IsKMeansExecutionDomain(command.Domain) ||
            command.ClusterCount == 0u ||
            command.MaxIterations == 0u)
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                command.Domain,
                Core::ErrorCode::InvalidArgument,
                "K-Means requires mesh vertices, graph nodes, or point-cloud points with positive cluster and iteration counts.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::StaleEntity,
                command.Domain,
                Core::ErrorCode::ResourceNotFound,
                "K-Means target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        if (!view.Valid() || !SourceViewSupportsKMeansDomain(view, command.Domain))
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                command.Domain,
                Core::ErrorCode::InvalidArgument,
                "Selected entity does not expose the requested K-Means GeometrySources domain.");
        }

        Geometry::PropertySet* properties =
            KMeansTargetProperties(view, command.Domain);
        if (properties == nullptr)
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                command.Domain,
                Core::ErrorCode::InvalidArgument,
                "Requested K-Means GeometrySources domain has no writable property set.");
        }

        std::optional<std::vector<glm::vec3>> points =
            CollectKMeansPositions(*properties);
        if (!points.has_value())
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                command.Domain,
                Core::ErrorCode::InvalidArgument,
                "K-Means requires a non-empty finite v:position property on the requested domain.");
        }

        GK::KMeansParams params{};
        params.ClusterCount = command.ClusterCount;
        params.MaxIterations = command.MaxIterations;
        params.Seed = command.Seed;
        params.Init = command.UseHierarchicalInitialization
            ? GK::Initialization::Hierarchical
            : GK::Initialization::Random;
        params.Compute = GK::Backend::CPU;

        const std::optional<GK::KMeansResult> clustered =
            GK::Cluster(std::span<const glm::vec3>{points->data(), points->size()},
                        params);
        if (!clustered.has_value())
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::GeometryProcessingFailed,
                command.Domain,
                Core::ErrorCode::Unknown,
                "Geometry.KMeans returned no result for the requested points.");
        }

        if (!PublishKMeansProperties(*properties, command.Domain, *clustered))
        {
            return MakeKMeansResult(
                SandboxEditorCommandStatus::GeometryProcessingFailed,
                command.Domain,
                Core::ErrorCode::TypeMismatch,
                "K-Means result publication failed because output properties have incompatible types or sizes.");
        }

        Dirty::MarkVertexAttributesDirty(raw, *entity);
        SandboxEditorKMeansResult result{
            .Status = SandboxEditorCommandStatus::Applied,
            .Domain = command.Domain,
            .LabelCount = static_cast<std::uint32_t>(clustered->Labels.size()),
            .ClusterCount = static_cast<std::uint32_t>(clustered->Centroids.size()),
            .Iterations = clustered->Iterations,
            .Converged = clustered->Converged,
            .Inertia = clustered->Inertia,
            .MaxDistanceIndex = clustered->MaxDistanceIndex,
            .Error = Core::ErrorCode::Success,
        };
        result.Message = BuildKMeansSuccessMessage(command.Domain, result);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty("Run K-Means");
        return result;
    }

    void DrawSandboxEditorPanelFrame(const SandboxEditorPanelFrame& frame)
    {
        DrawPanelFrame(frame,
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr);
    }

    SandboxEditorUi::~SandboxEditorUi()
    {
        Detach();
    }

    void SandboxEditorUi::Attach(Engine& engine)
    {
        Detach();
        m_Engine = &engine;
        engine.SetImGuiEditorCallback(
            [this]
            {
                if (m_Engine == nullptr)
                    return;
                const std::optional<RuntimeAssetImportEvent>& runtimeImport =
                    m_Engine->GetLastAssetImportEvent();
                if (runtimeImport.has_value() &&
                    runtimeImport->Sequence != m_LastObservedRuntimeImportSequence)
                {
                    m_LastImportResult =
                        BuildFileImportResultFromRuntimeEvent(*runtimeImport);
                    m_LastObservedRuntimeImportSequence = runtimeImport->Sequence;
                }
                SandboxEditorContext context = BuildContextFromEngine(*m_Engine);
                context.PendingAssetImportPath =
                    std::string(m_ImportPathBuffer.data());
                context.PendingAssetImportPayloadKind =
                    m_ImportPayloadKind;
                context.PendingSceneFilePath =
                    std::string(m_ScenePathBuffer.data());
                if (m_LastSceneFileResult.has_value())
                    context.LastSceneFileResult = &*m_LastSceneFileResult;
                if (m_LastImportResult.has_value())
                    context.LastAssetImportResult = &*m_LastImportResult;
                if (m_LastKMeansResult.has_value())
                    context.LastKMeansResult = &*m_LastKMeansResult;
                m_LastFrame = BuildSandboxEditorPanelFrame(context);
                KMeansUiState kmeansState{
                    .LastResult = &m_LastKMeansResult,
                    .Domain = &m_KMeansDomain,
                    .ClusterCount = &m_KMeansClusterCount,
                    .MaxIterations = &m_KMeansMaxIterations,
                    .Seed = &m_KMeansSeed,
                    .UseHierarchicalInitialization =
                        &m_KMeansUseHierarchicalInitialization,
                };
                DrawPanelFrame(
                    m_LastFrame,
                    &context,
                    &m_ImportPathBuffer,
                    &m_ScenePathBuffer,
                    &m_ImportPayloadKind,
                    &m_LastImportResult,
                    &m_LastSceneFileResult,
                    &m_DomainWindowOpen,
                    &kmeansState);
            });
    }

    void SandboxEditorUi::Detach()
    {
        if (m_Engine != nullptr)
        {
            m_Engine->SetImGuiEditorCallback({});
            m_Engine = nullptr;
        }
    }
}
