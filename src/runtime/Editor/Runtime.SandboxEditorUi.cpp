module;

#include <algorithm>
#include <chrono>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "ProgressivePoissonReference.hpp"

module Extrinsic.Runtime.SandboxEditorUi;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectedMeshTextureBake;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Graph;
import Geometry.Graph.Vertex.Normals;
import Geometry.Curvature;
import Geometry.CatmullClark;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.AdaptiveRemeshing;
import Geometry.HalfedgeMesh.SubdivisionSqrt3;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.KMeans;
import Geometry.Mesh.Conversion;
import Geometry.MeshOperator;
import Geometry.MeshSoup;
import Geometry.PointCloud;
import Geometry.PointCloud.Normals;
import Geometry.PointCloud.SurfaceSampling;
import Geometry.PointCloud.Utils;
import Geometry.Properties;
import Geometry.Remeshing;
import Geometry.Simplification;
import Geometry.Smoothing;
import Geometry.Subdivision;
import Geometry.UvAtlas;

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
        namespace GN = Geometry::HalfedgeMesh::VertexNormals;
        namespace GraphNormals = Geometry::Graph::VertexNormals;
        namespace PointNormals = Geometry::PointCloud::Normals;
        namespace SurfaceSampling = Geometry::PointCloud::SurfaceSampling;
        namespace Smooth = Geometry::Smoothing;
        namespace Curv = Geometry::Curvature;
        namespace Remesh = Geometry::Remeshing;
        namespace AdaptiveRemesh = Geometry::AdaptiveRemeshing;
        namespace LoopSubdivide = Geometry::Subdivision;
        namespace CatmullClark = Geometry::CatmullClark;
        namespace Sqrt3Subdivide = Geometry::SubdivisionSqrt3;
        namespace Simpl = Geometry::Simplification;
        namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

        inline constexpr std::array<A::AssetPayloadKind, 6> kImportPayloadKinds{{
            A::AssetPayloadKind::Unknown,
            A::AssetPayloadKind::Mesh,
            A::AssetPayloadKind::PointCloud,
            A::AssetPayloadKind::Graph,
            A::AssetPayloadKind::ModelScene,
            A::AssetPayloadKind::Texture2D,
        }};

        inline constexpr std::array<ProgressiveSlotSemantic, 6> kTextureBakeTargetSemantics{{
            ProgressiveSlotSemantic::Albedo,
            ProgressiveSlotSemantic::Normal,
            ProgressiveSlotSemantic::Roughness,
            ProgressiveSlotSemantic::Metallic,
            ProgressiveSlotSemantic::ScalarField,
            ProgressiveSlotSemantic::Displacement,
        }};

        inline constexpr std::array<MeshAttributeTextureBakeEncoder, 8> kTextureBakeEncoders{{
            MeshAttributeTextureBakeEncoder::Auto,
            MeshAttributeTextureBakeEncoder::RgbaColor,
            MeshAttributeTextureBakeEncoder::Normal,
            MeshAttributeTextureBakeEncoder::ScalarColormap,
            MeshAttributeTextureBakeEncoder::LinearScalar,
            MeshAttributeTextureBakeEncoder::LabelPalette,
            MeshAttributeTextureBakeEncoder::Vector2,
            MeshAttributeTextureBakeEncoder::Vector3,
        }};

        inline constexpr std::array<GN::AveragingMode, 4>
            kMeshVertexNormalWeightings{{
                GN::AveragingMode::UniformFace,
                GN::AveragingMode::AreaWeighted,
                GN::AveragingMode::AngleWeighted,
                GN::AveragingMode::MaxWeighted,
            }};

        inline constexpr std::array<PointNormals::OrientationMode, 2>
            kPointCloudNormalOrientations{{
                PointNormals::OrientationMode::None,
                PointNormals::OrientationMode::MinimumSpanningTree,
            }};

        inline constexpr std::array<SandboxEditorProgressivePoissonChannel, 4>
            kProgressivePoissonChannels{{
                SandboxEditorProgressivePoissonChannel::Level,
                SandboxEditorProgressivePoissonChannel::Phase,
                SandboxEditorProgressivePoissonChannel::SplatRadius,
                SandboxEditorProgressivePoissonChannel::PrefixVisible,
            }};

        inline constexpr std::array<SandboxEditorProgressivePoissonBackend, 2>
            kProgressivePoissonBackends{{
                SandboxEditorProgressivePoissonBackend::CpuReference,
                SandboxEditorProgressivePoissonBackend::VulkanCompute,
            }};

        inline constexpr std::array<SandboxEditorMeshDenoiseStage, 1>
            kMeshDenoiseStages{{
                SandboxEditorMeshDenoiseStage::FullBilateral,
            }};

        inline constexpr std::array<SandboxEditorMeshCurvatureOutput, 4>
            kMeshCurvatureOutputs{{
                SandboxEditorMeshCurvatureOutput::All,
                SandboxEditorMeshCurvatureOutput::Mean,
                SandboxEditorMeshCurvatureOutput::Gaussian,
                SandboxEditorMeshCurvatureOutput::PrincipalDirections,
            }};

        inline constexpr std::array<SandboxEditorMeshRemeshMode, 2>
            kMeshRemeshModes{{
                SandboxEditorMeshRemeshMode::Uniform,
                SandboxEditorMeshRemeshMode::Adaptive,
            }};

        inline constexpr std::array<SandboxEditorMeshRemeshSizingLaw, 2>
            kMeshRemeshSizingLaws{{
                SandboxEditorMeshRemeshSizingLaw::MeanCurvature,
                SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
            }};

        inline constexpr std::array<SandboxEditorMeshSimplifyMetric, 2>
            kMeshSimplifyMetrics{{
                SandboxEditorMeshSimplifyMetric::ClassicalQEM,
                SandboxEditorMeshSimplifyMetric::FA_QEM,
            }};

        inline constexpr std::array<SandboxEditorMeshSubdivideOperator, 3>
            kMeshSubdivideOperators{{
                SandboxEditorMeshSubdivideOperator::Loop,
                SandboxEditorMeshSubdivideOperator::CatmullClark,
                SandboxEditorMeshSubdivideOperator::Sqrt3,
            }};

        [[nodiscard]] const char* DebugNameForTextureBakeEncoder(
            const MeshAttributeTextureBakeEncoder encoder) noexcept
        {
            switch (encoder)
            {
            case MeshAttributeTextureBakeEncoder::Auto: return "auto";
            case MeshAttributeTextureBakeEncoder::LinearScalar: return "linear scalar";
            case MeshAttributeTextureBakeEncoder::ScalarColormap: return "scalar colormap";
            case MeshAttributeTextureBakeEncoder::LabelPalette: return "label palette";
            case MeshAttributeTextureBakeEncoder::Vector2: return "vector2";
            case MeshAttributeTextureBakeEncoder::Vector3: return "vector3";
            case MeshAttributeTextureBakeEncoder::Normal: return "normal";
            case MeshAttributeTextureBakeEncoder::RgbaColor: return "rgba color";
            }
            return "unknown";
        }

        struct KMeansUiState
        {
            std::optional<SandboxEditorKMeansResult>* LastResult{nullptr};
            SandboxEditorGeometryProcessingDomain* Domain{nullptr};
            std::int32_t* ClusterCount{nullptr};
            std::int32_t* MaxIterations{nullptr};
            std::int32_t* Seed{nullptr};
            bool* UseHierarchicalInitialization{nullptr};
        };

        struct MeshDenoiseUiState
        {
            std::optional<SandboxEditorMeshDenoiseResult>* LastResult{nullptr};
            std::int32_t* Stage{nullptr};
            std::int32_t* NormalIterations{nullptr};
            std::int32_t* VertexIterations{nullptr};
            float* SigmaSpatial{nullptr};
            float* SigmaRange{nullptr};
            bool* PreserveBoundary{nullptr};
        };

        struct MeshCurvatureUiState
        {
            std::optional<SandboxEditorMeshCurvatureResult>* LastResult{nullptr};
            std::int32_t* Output{nullptr};
            bool* PublishPrincipalDirections{nullptr};
        };

        struct MeshRemeshUiState
        {
            std::optional<SandboxEditorMeshRemeshResult>* LastResult{nullptr};
            std::int32_t* Mode{nullptr};
            std::int32_t* SizingLaw{nullptr};
            std::int32_t* Iterations{nullptr};
            float* TargetEdgeLength{nullptr};
            bool* ProjectToSurface{nullptr};
        };

        struct MeshSubdivideUiState
        {
            std::optional<SandboxEditorMeshSubdivideResult>* LastResult{nullptr};
            std::int32_t* Operator{nullptr};
            std::int32_t* Iterations{nullptr};
            bool* PreserveLoopFeatures{nullptr};
        };

        struct MeshSimplifyUiState
        {
            std::optional<SandboxEditorMeshSimplifyResult>* LastResult{nullptr};
            std::int32_t* Metric{nullptr};
            std::int32_t* TargetFaces{nullptr};
            float* MaxError{nullptr};
            bool* PreserveBoundary{nullptr};
            float* FeatureAngleThresholdDegrees{nullptr};
            float* NormalWeight{nullptr};
            float* BoundaryWeight{nullptr};
            float* CurvatureWeight{nullptr};
            bool* PreserveSharpFeatures{nullptr};
            bool* PreserveUvSeams{nullptr};
        };

        struct MeshVertexNormalsUiState
        {
            std::optional<SandboxEditorMeshVertexNormalsResult>* LastResult{nullptr};
            std::int32_t* Weighting{nullptr};
            glm::vec3* FallbackNormal{nullptr};
        };

        struct GraphVertexNormalsUiState
        {
            std::optional<SandboxEditorGraphVertexNormalsResult>* LastResult{nullptr};
            glm::vec3* FallbackNormal{nullptr};
            bool* OrientTowardFallback{nullptr};
        };

        struct PointCloudVertexNormalsUiState
        {
            std::optional<SandboxEditorPointCloudVertexNormalsResult>* LastResult{nullptr};
            std::int32_t* KNeighbors{nullptr};
            std::int32_t* MinimumNeighbors{nullptr};
            bool* UseRadiusSearch{nullptr};
            float* Radius{nullptr};
            std::int32_t* Orientation{nullptr};
            glm::vec3* FallbackNormal{nullptr};
        };

        struct PointCloudOutlierRemovalUiState
        {
            std::optional<SandboxEditorPointCloudOutlierRemovalResult>*
                LastResult{nullptr};
            std::int32_t* Method{nullptr};  // 0 = statistical, 1 = radius.
            std::int32_t* KNeighbors{nullptr};
            float*        StdDevMultiplier{nullptr};
            float*        SearchRadius{nullptr};
            std::int32_t* MinNeighbors{nullptr};
        };

        struct ProgressivePoissonUiState
        {
            std::optional<SandboxEditorProgressivePoissonResult>*
                LastResult{nullptr};
            std::optional<SandboxEditorProgressivePoissonConfigResult>*
                LastConfigResult{nullptr};
            std::int32_t* Dimension{nullptr};
            std::int32_t* GridWidth{nullptr};
            std::int32_t* MaxLevels{nullptr};
            float* HashLoadFactor{nullptr};
            float* RadiusAlpha{nullptr};
            bool* RandomizeGridOrigin{nullptr};
            std::int32_t* GridOriginSeed{nullptr};
            bool* ShuffleWithinLevels{nullptr};
            std::int32_t* ShuffleSeed{nullptr};
            std::int32_t* PrefixCount{nullptr};
            std::int32_t* Channel{nullptr};
            std::int32_t* Backend{nullptr};
            std::int32_t* MeshSurfaceSampleCount{nullptr};
            std::int32_t* MeshSurfaceSampleSeed{nullptr};
            float* MeshSurfaceMinTriangleArea{nullptr};
            bool* MeshSurfaceInterpolateNormals{nullptr};
            bool* AutoRunOnEdit{nullptr};
            float* DebounceSeconds{nullptr};
            bool* AutoRunPending{nullptr};
            double* LastEditTime{nullptr};
            std::uint32_t* PendingStableEntityId{nullptr};
        };

        struct TextureBakeUiState
        {
            std::int32_t* SourceIndex{nullptr};
            std::int32_t* TargetSemanticIndex{nullptr};
            std::int32_t* EncoderIndex{nullptr};
            std::int32_t* Width{nullptr};
            std::int32_t* Height{nullptr};
            std::int32_t* UvResolution{nullptr};
            std::int32_t* UvPadding{nullptr};
            float* UvTexelsPerUnit{nullptr};
            bool* UvForceRegenerate{nullptr};
            bool* UvPreserveAuthored{nullptr};
        };

        enum class DomainWindowSection : std::uint8_t
        {
            Render = 0,
            Properties = 1,
            Visualization = 2,
            Selection = 3,
            Processing = 4,
            Count = 5,
        };

        enum class SandboxEditorPanelWindowKind : std::uint8_t
        {
            Shell = 0,
            SceneHierarchy,
            Inspector,
            SelectionDetails,
            FileScene,
            FileImport,
            FrameGraph,
            RenderRecipe,
            CameraRender,
            GeometryVisualization,
            Count,
        };

        static_assert(
            static_cast<std::size_t>(SandboxEditorPanelWindowKind::Count) ==
            Detail::kSandboxEditorPanelWindowCount);

        inline constexpr std::array<SandboxEditorPanelWindowKind,
                                    Detail::kSandboxEditorPanelWindowCount>
            kSandboxEditorPanelWindows{{
                SandboxEditorPanelWindowKind::Shell,
                SandboxEditorPanelWindowKind::SceneHierarchy,
                SandboxEditorPanelWindowKind::Inspector,
                SandboxEditorPanelWindowKind::SelectionDetails,
                SandboxEditorPanelWindowKind::FileScene,
                SandboxEditorPanelWindowKind::FileImport,
                SandboxEditorPanelWindowKind::FrameGraph,
                SandboxEditorPanelWindowKind::RenderRecipe,
                SandboxEditorPanelWindowKind::CameraRender,
                SandboxEditorPanelWindowKind::GeometryVisualization,
            }};

        [[nodiscard]] const char* PanelWindowTitle(
            const SandboxEditorPanelWindowKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorPanelWindowKind::Shell:
                return "Sandbox Editor";
            case SandboxEditorPanelWindowKind::SceneHierarchy:
                return "Scene Hierarchy";
            case SandboxEditorPanelWindowKind::Inspector:
                return "Inspector";
            case SandboxEditorPanelWindowKind::SelectionDetails:
                return "Selection Details";
            case SandboxEditorPanelWindowKind::FileScene:
                return "File / Scene";
            case SandboxEditorPanelWindowKind::FileImport:
                return "File / Import";
            case SandboxEditorPanelWindowKind::FrameGraph:
                return "Frame Graph";
            case SandboxEditorPanelWindowKind::RenderRecipe:
                return "Render Recipes";
            case SandboxEditorPanelWindowKind::CameraRender:
                return "Camera / Render";
            case SandboxEditorPanelWindowKind::GeometryVisualization:
                return "Geometry Visualization";
            case SandboxEditorPanelWindowKind::Count:
                break;
            }
            return "Sandbox Window";
        }

        [[nodiscard]] std::size_t PanelWindowIndex(
            const SandboxEditorPanelWindowKind kind) noexcept
        {
            return static_cast<std::size_t>(kind);
        }

        [[nodiscard]] bool BeginPanelWindow(
            std::array<bool, Detail::kSandboxEditorPanelWindowCount>* panelWindowOpen,
            const SandboxEditorPanelWindowKind kind,
            const ImVec2 firstUseSize)
        {
            if (panelWindowOpen == nullptr)
                return false;

            bool& open = (*panelWindowOpen)[PanelWindowIndex(kind)];
            if (!open)
                return false;

            if (firstUseSize.x > 0.0f && firstUseSize.y > 0.0f)
                ImGui::SetNextWindowSize(firstUseSize, ImGuiCond_FirstUseEver);

            if (ImGui::Begin(PanelWindowTitle(kind), &open))
                return true;

            ImGui::End();
            return false;
        }

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
                case DomainWindowSection::Properties: return "Mesh / Properties";
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
                case DomainWindowSection::Properties: return "Graph / Properties";
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
                case DomainWindowSection::Properties: return "PointCloud / Properties";
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
                .GeneratedTextureAssetsCreated = imported.GeneratedTextureAssetsCreated,
                .TextureUploadRequests = imported.TextureUploadRequests,
                .GeneratedTextureUploadRequests = imported.GeneratedTextureUploadRequests,
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

        [[nodiscard]] const std::optional<G::VisualizationConfig>*
        LaneOverrideForTarget(const G::VisualizationLaneOverrides& overrides,
                              const SandboxEditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case SandboxEditorVisualizationTarget::Surface:
                return &overrides.Surface;
            case SandboxEditorVisualizationTarget::Edges:
                return &overrides.Edges;
            case SandboxEditorVisualizationTarget::Points:
                return &overrides.Points;
            case SandboxEditorVisualizationTarget::Entity:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<G::VisualizationConfig>*
        MutableLaneOverrideForTarget(G::VisualizationLaneOverrides& overrides,
                                     const SandboxEditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case SandboxEditorVisualizationTarget::Surface:
                return &overrides.Surface;
            case SandboxEditorVisualizationTarget::Edges:
                return &overrides.Edges;
            case SandboxEditorVisualizationTarget::Points:
                return &overrides.Points;
            case SandboxEditorVisualizationTarget::Entity:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] std::optional<G::VisualizationConfig>
        StoredVisualizationConfigForTarget(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorVisualizationTarget target)
        {
            if (target == SandboxEditorVisualizationTarget::Entity)
            {
                if (const auto* config = raw.try_get<G::VisualizationConfig>(entity))
                    return *config;
                return std::nullopt;
            }

            const auto* overrides =
                raw.try_get<G::VisualizationLaneOverrides>(entity);
            if (overrides == nullptr)
                return std::nullopt;

            const std::optional<G::VisualizationConfig>* lane =
                LaneOverrideForTarget(*overrides, target);
            return lane != nullptr ? *lane : std::nullopt;
        }

        [[nodiscard]] std::optional<G::VisualizationConfig>
        EffectiveVisualizationConfigForTarget(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorVisualizationTarget target)
        {
            if (std::optional<G::VisualizationConfig> stored =
                    StoredVisualizationConfigForTarget(raw, entity, target);
                stored.has_value())
            {
                return stored;
            }
            if (target == SandboxEditorVisualizationTarget::Entity)
                return std::nullopt;
            return StoredVisualizationConfigForTarget(
                raw,
                entity,
                SandboxEditorVisualizationTarget::Entity);
        }

        [[nodiscard]] SandboxEditorVisualizationConfigModel
        BuildVisualizationConfigModelForTarget(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorVisualizationTarget target)
        {
            const std::optional<G::VisualizationConfig> config =
                EffectiveVisualizationConfigForTarget(raw, entity, target);
            return config.has_value()
                ? FromVisualizationConfig(*config)
                : SandboxEditorVisualizationConfigModel{};
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyVisualizationConfigTarget(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const SandboxEditorVisualizationTarget target,
            const std::optional<G::VisualizationConfig>& config)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            if (target == SandboxEditorVisualizationTarget::Entity)
            {
                if (config.has_value())
                    raw.emplace_or_replace<G::VisualizationConfig>(entity, *config);
                else
                    raw.remove<G::VisualizationConfig>(entity);
                return EditorCommandHistoryStatus::Applied;
            }

            if (!config.has_value() &&
                !raw.all_of<G::VisualizationLaneOverrides>(entity))
            {
                return EditorCommandHistoryStatus::Applied;
            }

            auto& overrides =
                raw.get_or_emplace<G::VisualizationLaneOverrides>(entity);
            std::optional<G::VisualizationConfig>* lane =
                MutableLaneOverrideForTarget(overrides, target);
            if (lane == nullptr)
                return EditorCommandHistoryStatus::UnsupportedOperation;

            *lane = config;
            if (overrides.Empty())
                raw.remove<G::VisualizationLaneOverrides>(entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandRecord MakeVisualizationConfigTargetCommand(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const SandboxEditorVisualizationTarget target,
            std::optional<G::VisualizationConfig> before,
            std::optional<G::VisualizationConfig> after,
            std::string label)
        {
            return EditorCommandRecord{
                .Label = std::move(label),
                .Redo = [scene, stableEntityId, target, after]()
                {
                    return ApplyVisualizationConfigTarget(
                        scene,
                        stableEntityId,
                        target,
                        after);
                },
                .Undo = [scene, stableEntityId, target, before]()
                {
                    return ApplyVisualizationConfigTarget(
                        scene,
                        stableEntityId,
                        target,
                        before);
                },
                .Dirtying = true,
            };
        }

        [[nodiscard]] SandboxEditorVisualizationConfigCommand
        MakeUniformVisualizationConfigCommandFromModel(
            const std::uint32_t stableEntityId,
            const SandboxEditorVisualizationConfigModel& model,
            const SandboxEditorVisualizationTarget target,
            const glm::vec4 color)
        {
            return SandboxEditorVisualizationConfigCommand{
                .StableEntityId = stableEntityId,
                .Target = target,
                .EnableConfig = true,
                .Source = G::VisualizationConfig::ColorSource::UniformColor,
                .Color = color,
                .ScalarFieldName = model.ScalarFieldName,
                .ScalarDomain = model.ScalarDomain,
                .ColorBufferName = model.ColorBufferName,
                .ScalarAutoRange = model.ScalarAutoRange,
                .ScalarRangeMin = model.ScalarRangeMin,
                .ScalarRangeMax = model.ScalarRangeMax,
                .ScalarBinCount = model.ScalarBinCount,
                .IsolineCount = model.IsolineCount,
            };
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

        [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            using Domain = SandboxEditorVisualizationPropertyDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return GeometryElementDomain::MeshVertex;
            case Domain::MeshEdges:
                return GeometryElementDomain::MeshEdge;
            case Domain::MeshFaces:
                return GeometryElementDomain::MeshFace;
            case Domain::GraphVertices:
                return GeometryElementDomain::GraphNode;
            case Domain::GraphEdges:
                return GeometryElementDomain::GraphEdge;
            case Domain::PointCloudPoints:
                return GeometryElementDomain::PointCloudPoint;
            }
            return GeometryElementDomain::Unknown;
        }

        [[nodiscard]] const Geometry::PropertySet* PropertySetForVisualizationDomain(
            const GeometryEntityAvailability& availability,
            const SandboxEditorVisualizationPropertyDomain domain) noexcept
        {
            return ResolveGeometryPropertySet(
                availability,
                ToGeometryElementDomain(domain));
        }

        void AppendVisualizationPropertiesForDomain(
            std::vector<SandboxEditorVisualizationPropertyInfo>& out,
            const Geometry::PropertySet& properties,
            SandboxEditorVisualizationPropertyDomain domain);

        [[nodiscard]] SandboxEditorVisualizationTarget
        VisualizationTargetForWindowKind(
            const SandboxEditorDomainWindowKind kind) noexcept
        {
            switch (kind)
            {
            case SandboxEditorDomainWindowKind::Mesh:
                return SandboxEditorVisualizationTarget::Surface;
            case SandboxEditorDomainWindowKind::Graph:
                return SandboxEditorVisualizationTarget::Edges;
            case SandboxEditorDomainWindowKind::PointCloud:
                return SandboxEditorVisualizationTarget::Points;
            }
            return SandboxEditorVisualizationTarget::Entity;
        }

        [[nodiscard]] bool VisualizationTargetAvailableForView(
            const GeometryEntityAvailability& availability,
            const SandboxEditorVisualizationTarget target) noexcept
        {
            switch (target)
            {
            case SandboxEditorVisualizationTarget::Entity:
                return availability.HasGeometry();
            case SandboxEditorVisualizationTarget::Surface:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Surface).Ready();
            case SandboxEditorVisualizationTarget::Edges:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Edges).Ready();
            case SandboxEditorVisualizationTarget::Points:
                return ResolveRenderLaneAvailability(
                    availability,
                    GeometryRenderLane::Points).Ready();
            }
            return false;
        }

        void AppendVisualizationPropertiesForTarget(
            std::vector<SandboxEditorVisualizationPropertyInfo>& out,
            const GeometryEntityAvailability& availability,
            const SandboxEditorVisualizationTarget target)
        {
            const auto append =
                [&](const SandboxEditorVisualizationPropertyDomain domain)
                {
                    if (const Geometry::PropertySet* properties =
                            PropertySetForVisualizationDomain(availability, domain))
                    {
                        AppendVisualizationPropertiesForDomain(
                            out,
                            *properties,
                            domain);
                    }
                };

            switch (target)
            {
            case SandboxEditorVisualizationTarget::Entity:
                append(SandboxEditorVisualizationPropertyDomain::MeshVertices);
                append(SandboxEditorVisualizationPropertyDomain::MeshEdges);
                append(SandboxEditorVisualizationPropertyDomain::MeshFaces);
                append(SandboxEditorVisualizationPropertyDomain::GraphVertices);
                append(SandboxEditorVisualizationPropertyDomain::GraphEdges);
                append(SandboxEditorVisualizationPropertyDomain::PointCloudPoints);
                break;
            case SandboxEditorVisualizationTarget::Surface:
                append(SandboxEditorVisualizationPropertyDomain::MeshVertices);
                append(SandboxEditorVisualizationPropertyDomain::MeshFaces);
                break;
            case SandboxEditorVisualizationTarget::Edges:
                append(SandboxEditorVisualizationPropertyDomain::MeshEdges);
                append(SandboxEditorVisualizationPropertyDomain::GraphEdges);
                break;
            case SandboxEditorVisualizationTarget::Points:
                append(SandboxEditorVisualizationPropertyDomain::MeshVertices);
                append(SandboxEditorVisualizationPropertyDomain::GraphVertices);
                append(SandboxEditorVisualizationPropertyDomain::PointCloudPoints);
                break;
            }
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
        BuildVisualizationProperties(const GeometryEntityAvailability& availability)
        {
            std::vector<SandboxEditorVisualizationPropertyInfo> out{};
            AppendVisualizationPropertiesForTarget(
                out,
                availability,
                SandboxEditorVisualizationTarget::Entity);
            return out;
        }

        [[nodiscard]] ProgressivePropertyValueKind DefaultExpectedValueKindForSlot(
            ProgressiveSlotSemantic semantic) noexcept;

        [[nodiscard]] ProgressiveGeometryDomain DefaultDomainForProgressiveSlot(
            GS::Domain sourceDomain,
            ProgressiveRenderLane lane,
            ProgressiveSlotSemantic semantic) noexcept;
        void AddDiagnostic(
            std::vector<SandboxEditorDiagnostic>& diagnostics,
            SandboxEditorDiagnosticCode code,
            std::string message);

        [[nodiscard]] const Geometry::PropertySet* PropertySetForCatalogDomain(
            const GeometryEntityAvailability& availability,
            const SandboxEditorPropertyCatalogDomain domain) noexcept
        {
            using Domain = SandboxEditorPropertyCatalogDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::MeshVertex);
            case Domain::MeshEdges:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::MeshEdge);
            case Domain::MeshHalfedges:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::MeshHalfedge);
            case Domain::MeshFaces:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::MeshFace);
            case Domain::GraphVertices:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::GraphNode);
            case Domain::GraphEdges:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::GraphEdge);
            case Domain::PointCloudPoints:
                return ResolveGeometryPropertySet(
                    availability,
                    GeometryElementDomain::PointCloudPoint);
            }
            return nullptr;
        }

        [[nodiscard]] ProgressiveGeometryDomain ToProgressiveGeometryDomain(
            const SandboxEditorPropertyCatalogDomain domain) noexcept
        {
            using Domain = SandboxEditorPropertyCatalogDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return ProgressiveGeometryDomain::MeshVertex;
            case Domain::MeshEdges:
                return ProgressiveGeometryDomain::MeshEdge;
            case Domain::MeshHalfedges:
                return ProgressiveGeometryDomain::MeshHalfedge;
            case Domain::MeshFaces:
                return ProgressiveGeometryDomain::MeshFace;
            case Domain::GraphVertices:
                return ProgressiveGeometryDomain::GraphVertex;
            case Domain::GraphEdges:
                return ProgressiveGeometryDomain::GraphEdge;
            case Domain::PointCloudPoints:
                return ProgressiveGeometryDomain::Point;
            }
            return ProgressiveGeometryDomain::Unknown;
        }

        [[nodiscard]] SandboxEditorPropertyCatalogValueKind ToPropertyCatalogValueKind(
            const ProgressivePropertyValueKind kind) noexcept
        {
            using Out = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case ProgressivePropertyValueKind::ScalarFloat:
                return Out::ScalarFloat;
            case ProgressivePropertyValueKind::ScalarDouble:
                return Out::ScalarDouble;
            case ProgressivePropertyValueKind::UInt32:
                return Out::UInt32;
            case ProgressivePropertyValueKind::Vec2:
                return Out::Vec2;
            case ProgressivePropertyValueKind::Vec3:
                return Out::Vec3;
            case ProgressivePropertyValueKind::Vec4:
                return Out::Vec4;
            case ProgressivePropertyValueKind::Any:
            case ProgressivePropertyValueKind::Unknown:
                break;
            }
            return Out::Unknown;
        }

        [[nodiscard]] ProgressivePropertyValueKind ToProgressivePropertyValueKind(
            const SandboxEditorPropertyCatalogValueKind kind) noexcept
        {
            using Kind = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case Kind::ScalarFloat:
                return ProgressivePropertyValueKind::ScalarFloat;
            case Kind::ScalarDouble:
                return ProgressivePropertyValueKind::ScalarDouble;
            case Kind::UInt32:
                return ProgressivePropertyValueKind::UInt32;
            case Kind::Vec2:
                return ProgressivePropertyValueKind::Vec2;
            case Kind::Vec3:
                return ProgressivePropertyValueKind::Vec3;
            case Kind::Vec4:
                return ProgressivePropertyValueKind::Vec4;
            case Kind::Unknown:
                break;
            }
            return ProgressivePropertyValueKind::Unknown;
        }

        [[nodiscard]] std::uint8_t ComponentCountForPropertyCatalogKind(
            const SandboxEditorPropertyCatalogValueKind kind) noexcept
        {
            using Kind = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case Kind::ScalarFloat:
            case Kind::ScalarDouble:
            case Kind::UInt32:
                return 1u;
            case Kind::Vec2:
                return 2u;
            case Kind::Vec3:
                return 3u;
            case Kind::Vec4:
                return 4u;
            case Kind::Unknown:
                break;
            }
            return 0u;
        }

        [[nodiscard]] bool IsGeneratedCatalogProperty(
            const std::string& name) noexcept
        {
            return name.find("kmeans") != std::string::npos ||
                   name.find("generated") != std::string::npos ||
                   name.find("bake") != std::string::npos;
        }

        [[nodiscard]] std::string FormatVec2(const glm::vec2 value)
        {
            return "(" + std::to_string(value.x) + ", " +
                   std::to_string(value.y) + ")";
        }

        [[nodiscard]] std::string FormatVec3(const glm::vec3 value)
        {
            return "(" + std::to_string(value.x) + ", " +
                   std::to_string(value.y) + ", " +
                   std::to_string(value.z) + ")";
        }

        [[nodiscard]] std::string FormatVec4(const glm::vec4 value)
        {
            return "(" + std::to_string(value.x) + ", " +
                   std::to_string(value.y) + ", " +
                   std::to_string(value.z) + ", " +
                   std::to_string(value.w) + ")";
        }

        [[nodiscard]] SandboxEditorPropertyValuePreview BuildPropertyValuePreview(
            const Geometry::PropertySet& properties,
            const std::string& name,
            const SandboxEditorPropertyCatalogValueKind kind,
            const std::optional<std::size_t> index)
        {
            if (!index.has_value() || *index >= properties.Size())
                return {};

            SandboxEditorPropertyValuePreview preview{
                .HasValue = true,
                .ElementIndex = *index,
            };

            using Kind = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case Kind::ScalarFloat:
                if (const auto prop = properties.Get<float>(name); prop)
                    preview.Text = std::to_string(prop.Vector()[*index]);
                break;
            case Kind::ScalarDouble:
                if (const auto prop = properties.Get<double>(name); prop)
                    preview.Text = std::to_string(prop.Vector()[*index]);
                break;
            case Kind::UInt32:
                if (const auto prop = properties.Get<std::uint32_t>(name); prop)
                    preview.Text = std::to_string(prop.Vector()[*index]);
                break;
            case Kind::Vec2:
                if (const auto prop = properties.Get<glm::vec2>(name); prop)
                    preview.Text = FormatVec2(prop.Vector()[*index]);
                break;
            case Kind::Vec3:
                if (const auto prop = properties.Get<glm::vec3>(name); prop)
                    preview.Text = FormatVec3(prop.Vector()[*index]);
                break;
            case Kind::Vec4:
                if (const auto prop = properties.Get<glm::vec4>(name); prop)
                    preview.Text = FormatVec4(prop.Vector()[*index]);
                break;
            case Kind::Unknown:
                preview.HasValue = false;
                break;
            }

            if (preview.Text.empty())
                preview.HasValue = false;
            return preview;
        }

        [[nodiscard]] std::optional<std::size_t> PreviewIndexForCatalogDomain(
            const SandboxEditorPropertyCatalogDomain domain,
            const PrimitiveSelectionResult* primitive,
            const std::uint32_t selectedStableId) noexcept
        {
            if (primitive == nullptr)
                return std::nullopt;
            const bool sameEntity =
                primitive->EntityId == selectedStableId ||
                primitive->StableId == selectedStableId;
            if (!sameEntity)
                return std::nullopt;

            using Domain = SandboxEditorPropertyCatalogDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                if (primitive->Domain == GS::Domain::Mesh &&
                    primitive->VertexId != kInvalidPrimitiveIndex)
                    return primitive->VertexId;
                break;
            case Domain::MeshEdges:
                if (primitive->Domain == GS::Domain::Mesh &&
                    primitive->EdgeId != kInvalidPrimitiveIndex)
                    return primitive->EdgeId;
                break;
            case Domain::MeshFaces:
                if (primitive->Domain == GS::Domain::Mesh &&
                    primitive->FaceId != kInvalidPrimitiveIndex)
                    return primitive->FaceId;
                break;
            case Domain::GraphVertices:
                if (primitive->Domain == GS::Domain::Graph &&
                    primitive->VertexId != kInvalidPrimitiveIndex)
                    return primitive->VertexId;
                break;
            case Domain::GraphEdges:
                if (primitive->Domain == GS::Domain::Graph &&
                    primitive->EdgeId != kInvalidPrimitiveIndex)
                    return primitive->EdgeId;
                break;
            case Domain::PointCloudPoints:
                if (primitive->Domain == GS::Domain::PointCloud &&
                    primitive->PointId != kInvalidPrimitiveIndex)
                    return primitive->PointId;
                break;
            case Domain::MeshHalfedges:
                break;
            }
            return std::nullopt;
        }

        void AppendPropertyCatalogRowsForDomain(
            std::vector<SandboxEditorPropertyCatalogRow>& out,
            const Geometry::PropertySet& properties,
            const SandboxEditorPropertyCatalogDomain domain,
            const std::optional<std::size_t> previewIndex)
        {
            for (const std::string& name : properties.Properties())
            {
                const SandboxEditorPropertyCatalogValueKind kind =
                    ToPropertyCatalogValueKind(
                        DetectPropertyValueKind(properties, name));
                const bool supported =
                    kind != SandboxEditorPropertyCatalogValueKind::Unknown;
                SandboxEditorPropertyCatalogRow row{
                    .Name = name,
                    .Domain = domain,
                    .ValueKind = kind,
                    .ElementCount = properties.Size(),
                    .ComponentCount = ComponentCountForPropertyCatalogKind(kind),
                    .Supported = supported,
                    .Bindable = supported,
                    .Canonical = IsInternalVisualizationProperty(name),
                    .Internal = IsInternalVisualizationProperty(name),
                    .Connectivity = IsConnectivityVisualizationProperty(name),
                    .Generated = IsGeneratedCatalogProperty(name),
                    .Descriptor = ProgressivePropertyBindingDescriptor{
                        .Domain = ToProgressiveGeometryDomain(domain),
                        .PropertyName = name,
                        .ExpectedValueKind =
                            ToProgressivePropertyValueKind(kind),
                        .ExpectedElementCount = properties.Size(),
                    },
                    .Preview = BuildPropertyValuePreview(
                        properties,
                        name,
                        kind,
                        previewIndex),
                };
                if (!supported)
                    row.UnsupportedReason = "unsupported property value type";
                out.push_back(std::move(row));
            }
        }

        void AppendPropertyCatalogRows(
            std::vector<SandboxEditorPropertyCatalogRow>& out,
            const GS::ConstSourceView& view,
            const PrimitiveSelectionResult* primitive,
            const std::uint32_t selectedStableId)
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            const auto append =
                [&](const SandboxEditorPropertyCatalogDomain domain)
                {
                    const Geometry::PropertySet* properties =
                        PropertySetForCatalogDomain(availability, domain);
                    if (properties == nullptr)
                        return;
                    AppendPropertyCatalogRowsForDomain(
                        out,
                        *properties,
                        domain,
                        PreviewIndexForCatalogDomain(
                            domain,
                            primitive,
                            selectedStableId));
                };

            append(SandboxEditorPropertyCatalogDomain::MeshVertices);
            append(SandboxEditorPropertyCatalogDomain::MeshEdges);
            append(SandboxEditorPropertyCatalogDomain::MeshHalfedges);
            append(SandboxEditorPropertyCatalogDomain::MeshFaces);
            append(SandboxEditorPropertyCatalogDomain::GraphVertices);
            append(SandboxEditorPropertyCatalogDomain::GraphEdges);
            append(SandboxEditorPropertyCatalogDomain::PointCloudPoints);
        }

        [[nodiscard]] SandboxEditorPropertyBindingTargetModel
        BuildPropertyBindingTargetModel(
            const GS::ConstSourceView& view,
            const ProgressiveSlotExtraction& slot)
        {
            ProgressiveGeometryDomain domain = slot.Property.Domain;
            if (domain == ProgressiveGeometryDomain::Unknown)
            {
                const GS::SourceAvailability availability =
                    GS::BuildSourceAvailability(view);
                domain = DefaultDomainForProgressiveSlot(
                    availability.ProvenanceDomain,
                    slot.Lane,
                    slot.Semantic);
            }

            ProgressivePropertyValueKind expected =
                slot.Property.ExpectedValueKind;
            if (expected == ProgressivePropertyValueKind::Any ||
                expected == ProgressivePropertyValueKind::Unknown)
            {
                expected = DefaultExpectedValueKindForSlot(slot.Semantic);
            }

            SandboxEditorPropertyBindingTargetModel model{
                .Lane = slot.Lane,
                .PresentationKey = slot.PresentationKey,
                .PresentationKind = slot.PresentationKind,
                .Semantic = slot.Semantic,
                .SourceKind = slot.SourceKind,
                .RequiredDomain = domain,
                .ExpectedValueKind = expected,
                .ExpectedElementCount = ResolvePropertyElementCount(
                    view,
                    domain),
            };

            if (domain != ProgressiveGeometryDomain::Unknown)
            {
                std::vector<ProgressivePropertyOption> options =
                    EnumeratePropertyOptions(
                        view,
                        domain,
                        expected,
                        model.ExpectedElementCount);
                model.Options.reserve(options.size());
                for (const ProgressivePropertyOption& option : options)
                {
                    model.Options.push_back(
                        SandboxEditorProgressivePropertyOptionModel{
                            .Descriptor = option.Descriptor,
                            .ActualValueKind = option.ActualValueKind,
                            .ElementCount = option.ElementCount,
                            .Compatible = option.Compatible,
                            .DisabledReason = option.DisabledReason,
                        });
                }
            }
            return model;
        }

        [[nodiscard]] std::optional<SandboxEditorPropertyCatalogDomain>
        VertexChannelCatalogDomainForView(
            const GS::ConstSourceView& view) noexcept
        {
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            using Domain = SandboxEditorPropertyCatalogDomain;
            switch (availability.ProvenanceDomain)
            {
            case GS::Domain::Mesh:
                return Domain::MeshVertices;
            case GS::Domain::Graph:
                return Domain::GraphVertices;
            case GS::Domain::PointCloud:
                return Domain::PointCloudPoints;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return std::nullopt;
        }

        [[nodiscard]] const Geometry::PropertySet*
        VertexChannelPropertySetForView(
            const GS::ConstSourceView& view,
            const SandboxEditorPropertyCatalogDomain domain) noexcept
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            return PropertySetForCatalogDomain(availability, domain);
        }

        [[nodiscard]] std::optional<AttributeSourceType>
        ToAttributeSourceType(
            const SandboxEditorPropertyCatalogValueKind kind) noexcept
        {
            using Kind = SandboxEditorPropertyCatalogValueKind;
            switch (kind)
            {
            case Kind::ScalarFloat:
                return AttributeSourceType::Float32;
            case Kind::Vec2:
                return AttributeSourceType::Vec2;
            case Kind::Vec3:
                return AttributeSourceType::Vec3;
            case Kind::Vec4:
                return AttributeSourceType::Vec4;
            case Kind::ScalarDouble:
            case Kind::UInt32:
            case Kind::Unknown:
                break;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool SourceTypeAllowedForVertexChannel(
            const VertexChannel channel,
            const AttributeSourceType type) noexcept
        {
            switch (channel)
            {
            case VertexChannel::Normal:
                return type == AttributeSourceType::Vec3;
            case VertexChannel::Color:
                return type == AttributeSourceType::Vec3 ||
                       type == AttributeSourceType::Vec4;
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return false;
        }

        [[nodiscard]] const char* VertexChannelExpectedTypeText(
            const VertexChannel channel) noexcept
        {
            switch (channel)
            {
            case VertexChannel::Normal:
                return "requires vec3";
            case VertexChannel::Color:
                return "requires vec3 or vec4";
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return "unsupported vertex channel";
        }

        [[nodiscard]] AttributeBindResult EvaluateVertexChannelBinding(
            const Geometry::PropertySet& properties,
            const VertexChannel channel,
            const std::string_view propertyName,
            const AttributeSourceType sourceType,
            const std::size_t elementCount)
        {
            if (propertyName.empty())
            {
                return AttributeBindResult{
                    .Status = AttributeBindStatus::EmptyBinding,
                    .FullyPopulated = false,
                };
            }
            if (elementCount > std::numeric_limits<std::uint32_t>::max())
            {
                return AttributeBindResult{
                    .Status = AttributeBindStatus::CountMismatch,
                    .FullyPopulated = false,
                };
            }
            if (!SourceTypeAllowedForVertexChannel(channel, sourceType))
            {
                return AttributeBindResult{
                    .Status = AttributeBindStatus::TypeMismatch,
                    .FullyPopulated = false,
                };
            }

            const std::uint32_t count =
                static_cast<std::uint32_t>(elementCount);
            const VertexAttributeBinding binding{
                .Channel = channel,
                .SourceType = sourceType,
                .SourceProperty = propertyName,
                .AllowFallback = false,
                .Normalize = channel == VertexChannel::Normal,
                .Fallback = channel == VertexChannel::Normal
                    ? glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}
                    : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            };

            if (channel == VertexChannel::Normal)
            {
                std::vector<glm::vec3> scratch(elementCount);
                return ResolveVec3Channel(properties, binding, count, scratch);
            }
            if (channel == VertexChannel::Color)
            {
                std::vector<std::uint32_t> scratch(elementCount);
                return ResolveColorChannelPackedUnorm8(
                    properties,
                    binding,
                    count,
                    scratch);
            }
            return AttributeBindResult{
                .Status = AttributeBindStatus::TypeMismatch,
                .FullyPopulated = false,
            };
        }

        [[nodiscard]] std::string BuildVertexChannelResolverDiagnostic(
            const AttributeBindResult& resolver)
        {
            std::string diagnostic =
                std::string(DebugNameForAttributeBindStatus(resolver.Status));
            diagnostic += " source=";
            diagnostic += std::to_string(resolver.SourceCount);
            diagnostic += " fallback=";
            diagnostic += std::to_string(resolver.FallbackCount);
            diagnostic += " nonFinite=";
            diagnostic += std::to_string(resolver.NonFiniteCount);
            return diagnostic;
        }

        [[nodiscard]] const VertexChannelSourceBinding*
        FindVertexChannelBinding(
            const VertexChannelBindingSet* bindings,
            const VertexChannel channel) noexcept
        {
            if (bindings == nullptr)
                return nullptr;
            switch (channel)
            {
            case VertexChannel::Normal:
                return &bindings->Normal;
            case VertexChannel::Color:
                return &bindings->Color;
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] VertexChannelSourceBinding*
        FindMutableVertexChannelBinding(
            VertexChannelBindingSet& bindings,
            const VertexChannel channel) noexcept
        {
            switch (channel)
            {
            case VertexChannel::Normal:
                return &bindings.Normal;
            case VertexChannel::Color:
                return &bindings.Color;
            case VertexChannel::Position:
            case VertexChannel::Texcoord:
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                break;
            }
            return nullptr;
        }

        [[nodiscard]] bool AnyVertexChannelBindingEnabled(
            const VertexChannelBindingSet& bindings) noexcept
        {
            return IsVertexChannelBindingEnabled(bindings.Normal) ||
                   IsVertexChannelBindingEnabled(bindings.Color);
        }

        [[nodiscard]] bool SameVertexChannelSourceBinding(
            const VertexChannelSourceBinding& lhs,
            const VertexChannelSourceBinding& rhs) noexcept
        {
            return lhs.Enabled == rhs.Enabled &&
                   lhs.SourceType == rhs.SourceType &&
                   lhs.SourceProperty == rhs.SourceProperty;
        }

        void MarkVertexChannelDirty(entt::registry& raw,
                                    const entt::entity entity,
                                    const VertexChannel channel)
        {
            switch (channel)
            {
            case VertexChannel::Position:
                Dirty::MarkVertexPositionsDirty(raw, entity);
                break;
            case VertexChannel::Texcoord:
                Dirty::MarkVertexTexcoordsDirty(raw, entity);
                break;
            case VertexChannel::Normal:
                Dirty::MarkVertexNormalsDirty(raw, entity);
                break;
            case VertexChannel::Color:
                Dirty::MarkVertexColorsDirty(raw, entity);
                break;
            case VertexChannel::Tangent:
            case VertexChannel::Custom:
                Dirty::MarkVertexAttributesDirty(raw, entity);
                break;
            }
        }

        [[nodiscard]] SandboxEditorVertexChannelBindingTargetModel
        BuildVertexChannelBindingTargetModel(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& view,
            const std::vector<SandboxEditorPropertyCatalogRow>& rows,
            const SandboxEditorPropertyCatalogDomain domain,
            const VertexChannel channel)
        {
            SandboxEditorVertexChannelBindingTargetModel model{
                .Channel = channel,
            };

            const Geometry::PropertySet* properties =
                VertexChannelPropertySetForView(view, domain);
            if (properties == nullptr)
                return model;

            const std::size_t expectedCount = properties->Size();
            const auto* bindings = raw.try_get<VertexChannelBindingSet>(entity);
            if (const VertexChannelSourceBinding* binding =
                    FindVertexChannelBinding(bindings, channel);
                binding != nullptr && IsVertexChannelBindingEnabled(*binding))
            {
                model.HasBinding = true;
                model.Binding = *binding;
                model.Resolver = EvaluateVertexChannelBinding(
                    *properties,
                    channel,
                    binding->SourceProperty,
                    binding->SourceType,
                    expectedCount);
                model.Diagnostic =
                    BuildVertexChannelResolverDiagnostic(model.Resolver);
            }

            for (const SandboxEditorPropertyCatalogRow& row : rows)
            {
                if (row.Domain != domain || !row.Supported)
                    continue;

                SandboxEditorVertexChannelBindingOptionModel option{
                    .PropertyName = row.Name,
                    .Domain = row.Domain,
                    .ValueKind = row.ValueKind,
                    .ElementCount = row.ElementCount,
                };
                const std::optional<AttributeSourceType> sourceType =
                    ToAttributeSourceType(row.ValueKind);
                if (!sourceType.has_value())
                {
                    option.Resolver = AttributeBindResult{
                        .Status = AttributeBindStatus::TypeMismatch,
                        .FullyPopulated = false,
                    };
                    option.Compatible = false;
                    option.DisabledReason =
                        VertexChannelExpectedTypeText(channel);
                    model.Options.push_back(std::move(option));
                    continue;
                }

                option.SourceType = *sourceType;
                option.Resolver = EvaluateVertexChannelBinding(
                    *properties,
                    channel,
                    row.Name,
                    *sourceType,
                    expectedCount);
                option.Compatible =
                    SourceTypeAllowedForVertexChannel(channel, *sourceType) &&
                    option.Resolver.Ok();
                if (!option.Compatible)
                {
                    option.DisabledReason =
                        !SourceTypeAllowedForVertexChannel(channel, *sourceType)
                            ? VertexChannelExpectedTypeText(channel)
                            : BuildVertexChannelResolverDiagnostic(
                                  option.Resolver);
                }
                model.Options.push_back(std::move(option));
            }
            return model;
        }

        void AppendVertexChannelBindingTargets(
            SandboxEditorPropertyCatalogModel& model,
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& view)
        {
            const std::optional<SandboxEditorPropertyCatalogDomain> domain =
                VertexChannelCatalogDomainForView(view);
            if (!domain.has_value())
                return;

            model.VertexChannelTargets.push_back(
                BuildVertexChannelBindingTargetModel(
                    raw,
                    entity,
                    view,
                    model.Rows,
                    *domain,
                    VertexChannel::Normal));
            model.VertexChannelTargets.push_back(
                BuildVertexChannelBindingTargetModel(
                    raw,
                    entity,
                    view,
                    model.Rows,
                    *domain,
                    VertexChannel::Color));
        }

        [[nodiscard]] SandboxEditorPropertyCatalogModel BuildPropertyCatalogModel(
            const SandboxEditorContext& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorPropertyCatalogModel model{};
            model.HasSelectedEntity = true;
            model.SelectedStableId = SelectionController::ToStableEntityId(entity);
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            model.SelectedDomain = view.ActiveDomain;

            const PrimitiveSelectionResult* primitive = nullptr;
            if (context.LastRefinedPrimitive != nullptr &&
                context.LastRefinedPrimitive->has_value())
            {
                primitive = &**context.LastRefinedPrimitive;
            }

            AppendPropertyCatalogRows(
                model.Rows,
                view,
                primitive,
                model.SelectedStableId);

            if (const auto* bindings =
                    raw.try_get<ProgressivePresentationBindings>(entity);
                bindings != nullptr)
            {
                const ProgressivePresentationExtractionSnapshot snapshot =
                    BuildProgressivePresentationSnapshot(view, *bindings);
                model.BindingTargets.reserve(snapshot.Slots.size());
                for (const ProgressiveSlotExtraction& slot : snapshot.Slots)
                    model.BindingTargets.push_back(
                        BuildPropertyBindingTargetModel(view, slot));
            }

            AppendVertexChannelBindingTargets(model, raw, entity, view);

            if (!view.Valid() && model.Rows.empty())
            {
                AddDiagnostic(
                    model.Diagnostics,
                    SandboxEditorDiagnosticCode::UnsupportedGeometryDomain,
                    "Selected entity has no valid geometry property catalog.");
            }
            return model;
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
            const GeometryEntityAvailability& availability) noexcept
        {
            const bool editsSurface = command.SetSurface;
            const bool editsEdges =
                command.SetEdges || command.SetUniformEdgeWidth;
            const bool editsPoints =
                command.SetPoints ||
                command.SetPointRenderType ||
                command.SetUniformPointSize;

            if (!editsSurface && !editsEdges && !editsPoints)
                return false;

            if (editsSurface &&
                availability.Sources.ProvenanceDomain != GS::Domain::Mesh)
                return false;

            if (editsSurface &&
                (!availability.Sources.Has(GS::SourceCapability::VertexPoints) ||
                 !availability.Sources.Has(GS::SourceCapability::Halfedges) ||
                 !availability.Sources.Has(GS::SourceCapability::Faces)))
                return false;

            if (editsEdges)
            {
                if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud ||
                    !availability.Sources.HasPointSource())
                    return false;

                const bool hasExplicitEdges =
                    availability.Sources.Has(GS::SourceCapability::Edges);
                const bool hasMeshWireTopology =
                    availability.Sources.ProvenanceDomain == GS::Domain::Mesh &&
                    availability.Sources.Has(GS::SourceCapability::Halfedges) &&
                    availability.Sources.Has(GS::SourceCapability::Faces);
                if (!hasExplicitEdges && !hasMeshWireTopology)
                    return false;
            }

            if (editsPoints && !availability.Sources.HasPointSource())
                return false;

            return true;
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

        [[nodiscard]] bool SameProgressiveDefaultValue(
            const ProgressiveDefaultValue& lhs,
            const ProgressiveDefaultValue& rhs) noexcept
        {
            return lhs.Kind == rhs.Kind &&
                   SameVec4(lhs.Vector, rhs.Vector) &&
                   lhs.Scalar == rhs.Scalar &&
                   lhs.UInt == rhs.UInt;
        }

        [[nodiscard]] bool IsFiniteDefaultValue(
            const ProgressiveDefaultValue& value) noexcept
        {
            return std::isfinite(value.Vector.x) &&
                   std::isfinite(value.Vector.y) &&
                   std::isfinite(value.Vector.z) &&
                   std::isfinite(value.Vector.w) &&
                   std::isfinite(value.Scalar);
        }

        [[nodiscard]] bool SameProgressivePropertyDescriptor(
            const ProgressivePropertyBindingDescriptor& lhs,
            const ProgressivePropertyBindingDescriptor& rhs) noexcept
        {
            return lhs.Domain == rhs.Domain &&
                   lhs.PropertyName == rhs.PropertyName &&
                   lhs.ExpectedValueKind == rhs.ExpectedValueKind &&
                   lhs.ExpectedElementCount == rhs.ExpectedElementCount &&
                   lhs.SourceGeneration == rhs.SourceGeneration;
        }

        [[nodiscard]] bool IsActiveDerivedJobStatus(
            const DerivedJobStatus status) noexcept
        {
            return status == DerivedJobStatus::Blocked ||
                   status == DerivedJobStatus::Queued ||
                   status == DerivedJobStatus::Running ||
                   status == DerivedJobStatus::Applying;
        }

        [[nodiscard]] bool IsFailedDerivedJobStatus(
            const DerivedJobStatus status) noexcept
        {
            return status == DerivedJobStatus::Failed ||
                   status == DerivedJobStatus::Cancelled ||
                   status == DerivedJobStatus::StaleDiscarded;
        }

        [[nodiscard]] ProgressivePropertyValueKind DefaultExpectedValueKindForSlot(
            const ProgressiveSlotSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case ProgressiveSlotSemantic::Normal:
            case ProgressiveSlotSemantic::PointNormalOrientation:
                return ProgressivePropertyValueKind::Vec3;
            case ProgressiveSlotSemantic::Albedo:
            case ProgressiveSlotSemantic::PointColor:
            case ProgressiveSlotSemantic::LineColor:
                return ProgressivePropertyValueKind::Vec4;
            case ProgressiveSlotSemantic::Roughness:
            case ProgressiveSlotSemantic::Metallic:
            case ProgressiveSlotSemantic::ScalarField:
            case ProgressiveSlotSemantic::Displacement:
            case ProgressiveSlotSemantic::PointScalarField:
            case ProgressiveSlotSemantic::PointSize:
            case ProgressiveSlotSemantic::LineScalarField:
            case ProgressiveSlotSemantic::LineWidth:
                return ProgressivePropertyValueKind::ScalarFloat;
            }
            return ProgressivePropertyValueKind::Any;
        }

        [[nodiscard]] ProgressiveGeometryDomain DefaultDomainForProgressiveSlot(
            const GS::Domain sourceDomain,
            const ProgressiveRenderLane lane,
            const ProgressiveSlotSemantic semantic) noexcept
        {
            switch (sourceDomain)
            {
            case GS::Domain::Mesh:
                if (semantic == ProgressiveSlotSemantic::LineColor ||
                    semantic == ProgressiveSlotSemantic::LineScalarField ||
                    semantic == ProgressiveSlotSemantic::LineWidth)
                {
                    return ProgressiveGeometryDomain::MeshEdge;
                }
                if (semantic == ProgressiveSlotSemantic::ScalarField)
                    return ProgressiveGeometryDomain::MeshFace;
                if (lane == ProgressiveRenderLane::Edges)
                    return ProgressiveGeometryDomain::MeshEdge;
                return ProgressiveGeometryDomain::MeshVertex;
            case GS::Domain::Graph:
                if (lane == ProgressiveRenderLane::Edges ||
                    semantic == ProgressiveSlotSemantic::LineColor ||
                    semantic == ProgressiveSlotSemantic::LineScalarField ||
                    semantic == ProgressiveSlotSemantic::LineWidth)
                {
                    return ProgressiveGeometryDomain::GraphEdge;
                }
                return ProgressiveGeometryDomain::GraphVertex;
            case GS::Domain::PointCloud:
                return ProgressiveGeometryDomain::Point;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return ProgressiveGeometryDomain::Unknown;
        }

        [[nodiscard]] SandboxEditorProgressivePropertyOptionModel
        ToProgressivePropertyOptionModel(const ProgressivePropertyOption& option)
        {
            return SandboxEditorProgressivePropertyOptionModel{
                .Descriptor = option.Descriptor,
                .ActualValueKind = option.ActualValueKind,
                .ElementCount = option.ElementCount,
                .Compatible = option.Compatible,
                .DisabledReason = option.DisabledReason,
            };
        }

        [[nodiscard]] SandboxEditorProgressiveJobDependencyModel
        ToProgressiveJobDependencyModel(const DerivedJobDependency& dependency)
        {
            return SandboxEditorProgressiveJobDependencyModel{
                .Job = dependency.Job,
                .Reason = dependency.Reason,
            };
        }

        [[nodiscard]] SandboxEditorProgressiveJobModel ToProgressiveJobModel(
            const DerivedJobSnapshot& job)
        {
            SandboxEditorProgressiveJobModel model{
                .Handle = job.Handle,
                .Key = job.Key,
                .Name = job.Name,
                .RequestedJobDomain = job.RequestedJobDomain,
                .ResolvedJobDomain = job.ResolvedJobDomain,
                .Status = job.Status,
                .NormalizedProgress = job.NormalizedProgress,
                .ProgressDeterminate = job.ProgressDeterminate,
                .PreviousOutputRetained = job.PreviousOutputRetained,
                .PayloadToken = job.PayloadToken,
                .ElapsedMilliseconds = job.ElapsedMilliseconds,
                .Diagnostic = job.Diagnostic,
            };
            model.Dependencies.reserve(job.Dependencies.size());
            for (const DerivedJobDependency& dependency : job.Dependencies)
                model.Dependencies.push_back(
                    ToProgressiveJobDependencyModel(dependency));
            return model;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyProgressiveBindingsState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const ProgressivePresentationBindings& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
                return EditorCommandHistoryStatus::StaleEntity;

            raw.emplace_or_replace<ProgressivePresentationBindings>(entity, state);
            return EditorCommandHistoryStatus::Applied;
        }

        struct ProgressiveSlotLookup
        {
            ProgressivePresentationBinding* Presentation{nullptr};
            ProgressiveSlotBinding* Slot{nullptr};
        };

        [[nodiscard]] ProgressiveSlotLookup FindMutableProgressiveSlot(
            ProgressivePresentationBindings& bindings,
            const std::string& presentationKey,
            const ProgressiveSlotSemantic semantic)
        {
            if (!presentationKey.empty())
            {
                ProgressivePresentationBinding* presentation =
                    FindPresentationBinding(bindings, presentationKey);
                if (presentation == nullptr)
                    return {};
                return ProgressiveSlotLookup{
                    .Presentation = presentation,
                    .Slot = FindSlotBinding(*presentation, semantic),
                };
            }

            for (ProgressivePresentationBinding& presentation :
                 bindings.Presentations)
            {
                if (ProgressiveSlotBinding* slot =
                        FindSlotBinding(presentation, semantic))
                {
                    return ProgressiveSlotLookup{
                        .Presentation = &presentation,
                        .Slot = slot,
                    };
                }
            }
            return {};
        }

        [[nodiscard]] SandboxEditorCommandStatus ToSandboxEditorCommandStatus(
            EditorCommandHistoryStatus status) noexcept;

        [[nodiscard]] SandboxEditorCommandStatus CommitProgressiveBindingsChange(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            ProgressivePresentationBindings before,
            ProgressivePresentationBindings after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult result =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = "Change Progressive Presentation",
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyProgressiveBindingsState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyProgressiveBindingsState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(result.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyProgressiveBindingsState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        [[nodiscard]] bool PropertySourceKindAllowedForProgressiveSlotCommand(
            const ProgressiveSlotSourceKind sourceKind) noexcept
        {
            return sourceKind == ProgressiveSlotSourceKind::PropertyBake ||
                   sourceKind == ProgressiveSlotSourceKind::PropertyBuffer;
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

        [[nodiscard]] constexpr bool IsSurfaceTopologyAlgorithm(
            const SandboxEditorGeometryProcessingAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
            case SandboxEditorGeometryProcessingAlgorithm::MeshDenoise:
            case SandboxEditorGeometryProcessingAlgorithm::Curvature:
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
            case SandboxEditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
            case SandboxEditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            case SandboxEditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
                return false;
            }
            return false;
        }

        [[nodiscard]] SandboxEditorGeometryProcessingDomain
        DomainsForSourceView(const GS::ConstSourceView& view) noexcept
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            SandboxEditorGeometryProcessingDomain domains =
                SandboxEditorGeometryProcessingDomain::None;

            if (availability.Sources.ProvenanceDomain == GS::Domain::Mesh)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshVertex))
                    domains |= SandboxEditorGeometryProcessingDomain::MeshVertices;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshEdge))
                    domains |= SandboxEditorGeometryProcessingDomain::MeshEdges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshHalfedge))
                    domains |= SandboxEditorGeometryProcessingDomain::MeshHalfedges;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::MeshFace))
                    domains |= SandboxEditorGeometryProcessingDomain::MeshFaces;
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::Graph)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphNode))
                    domains |= SandboxEditorGeometryProcessingDomain::GraphVertices;
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::GraphEdge))
                    domains |= SandboxEditorGeometryProcessingDomain::GraphEdges;
                if (availability.Sources.Has(
                        GS::SourceCapability::Halfedges))
                    domains |= SandboxEditorGeometryProcessingDomain::GraphHalfedges;
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud)
            {
                if (SupportsGeometryElementDomain(
                        availability,
                        GeometryElementDomain::PointCloudPoint))
                    domains |= SandboxEditorGeometryProcessingDomain::PointCloudPoints;
            }

            return domains;
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

        [[nodiscard]] ProgressiveEntityShape InferProgressiveEntityShape(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GS::ConstSourceView& view)
        {
            if (const auto* hierarchy =
                    raw.try_get<ECSC::Hierarchy::Component>(entity);
                hierarchy != nullptr && hierarchy->ChildCount > 0u)
            {
                return ProgressiveEntityShape::Composition;
            }

            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            switch (availability.ProvenanceDomain)
            {
            case GS::Domain::Mesh:
                return ProgressiveEntityShape::MeshLeaf;
            case GS::Domain::Graph:
                return ProgressiveEntityShape::GraphLeaf;
            case GS::Domain::PointCloud:
                return ProgressiveEntityShape::PointCloudLeaf;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                break;
            }
            return ProgressiveEntityShape::Unknown;
        }

        void AppendProgressiveJobRowsForEntity(
            SandboxEditorProgressiveRenderDataModel& model,
            const DerivedJobQueueSnapshot* jobs,
            const std::uint32_t stableEntityId)
        {
            if (jobs == nullptr)
                return;

            for (const DerivedJobSnapshot& job : jobs->Entries)
            {
                if (job.Key.EntityId == stableEntityId)
                    model.Jobs.push_back(ToProgressiveJobModel(job));
            }
        }

        void AccumulateProgressiveJobSummaryForEntity(
            SandboxEditorProgressiveCompositionSummary& summary,
            const DerivedJobQueueSnapshot* jobs,
            const std::uint32_t stableEntityId)
        {
            if (jobs == nullptr)
                return;

            for (const DerivedJobSnapshot& job : jobs->Entries)
            {
                if (job.Key.EntityId != stableEntityId)
                    continue;

                ++summary.ChildJobCount;
                if (IsActiveDerivedJobStatus(job.Status))
                    ++summary.ChildActiveJobCount;
                if (IsFailedDerivedJobStatus(job.Status))
                    ++summary.ChildFailedJobCount;
            }
        }

        [[nodiscard]] std::vector<SandboxEditorProgressivePropertyOptionModel>
        BuildProgressiveSlotPropertyOptions(
            const GS::ConstSourceView& view,
            const ProgressiveSlotExtraction& extractedSlot)
        {
            ProgressiveGeometryDomain domain = extractedSlot.Property.Domain;
            if (domain == ProgressiveGeometryDomain::Unknown)
            {
                const GS::SourceAvailability availability =
                    GS::BuildSourceAvailability(view);
                domain = DefaultDomainForProgressiveSlot(
                    availability.ProvenanceDomain,
                    extractedSlot.Lane,
                    extractedSlot.Semantic);
            }
            if (domain == ProgressiveGeometryDomain::Unknown)
                return {};

            ProgressivePropertyValueKind expected =
                extractedSlot.Property.ExpectedValueKind;
            if (expected == ProgressivePropertyValueKind::Any ||
                expected == ProgressivePropertyValueKind::Unknown)
            {
                expected = DefaultExpectedValueKindForSlot(extractedSlot.Semantic);
            }

            const std::size_t expectedCount =
                ResolvePropertyElementCount(view, domain);
            std::vector<ProgressivePropertyOption> options =
                EnumeratePropertyOptions(view, domain, expected, expectedCount);

            std::vector<SandboxEditorProgressivePropertyOptionModel> out{};
            out.reserve(options.size());
            for (const ProgressivePropertyOption& option : options)
                out.push_back(ToProgressivePropertyOptionModel(option));
            return out;
        }

        [[nodiscard]] SandboxEditorProgressiveSlotModel ToProgressiveSlotModel(
            const GS::ConstSourceView& view,
            const ProgressivePresentationBindings& bindings,
            const ProgressiveSlotExtraction& extractedSlot)
        {
            SandboxEditorProgressiveSlotModel model{
                .Lane = extractedSlot.Lane,
                .PresentationKey = extractedSlot.PresentationKey,
                .PresentationKind = extractedSlot.PresentationKind,
                .Semantic = extractedSlot.Semantic,
                .SourceKind = extractedSlot.SourceKind,
                .Readiness = extractedSlot.Readiness,
                .UniformDefault = extractedSlot.UniformDefault,
                .Property = extractedSlot.Property,
                .PropertyResolution = extractedSlot.PropertyResolution,
                .TextureAsset = extractedSlot.TextureAsset,
                .Enabled = extractedSlot.Enabled,
                .UsesUniformDefault = extractedSlot.UsesUniformDefault,
                .TextureReady = extractedSlot.TextureReady,
                .PropertyBufferReady = extractedSlot.PropertyBufferReady,
                .PreviousOutputRetained = extractedSlot.PreviousOutputRetained,
                .Unsupported = extractedSlot.Unsupported,
                .Diagnostic = extractedSlot.Diagnostic,
            };

            if (const ProgressivePresentationBinding* presentation =
                    FindPresentationBinding(bindings, extractedSlot.PresentationKey))
            {
                if (const ProgressiveSlotBinding* slot =
                        FindSlotBinding(*presentation, extractedSlot.Semantic))
                {
                    model.AuthoredTexture = slot->AuthoredTexture;
                    model.GeneratedTexture = slot->GeneratedTexture;
                }
            }

            model.PropertyOptions =
                BuildProgressiveSlotPropertyOptions(view, extractedSlot);
            return model;
        }

        void AccumulateProgressiveChildSummary(
            const entt::registry& raw,
            SandboxEditorProgressiveCompositionSummary& summary,
            const ECS::EntityHandle child,
            const DerivedJobQueueSnapshot* jobs)
        {
            if (!raw.valid(child))
                return;

            ++summary.ChildCount;
            const std::uint32_t childStableId =
                SelectionController::ToStableEntityId(child);
            AccumulateProgressiveJobSummaryForEntity(summary, jobs, childStableId);

            const auto* bindings =
                raw.try_get<ProgressivePresentationBindings>(child);
            if (bindings == nullptr)
                return;

            ++summary.ChildBindingsCount;
            const GS::ConstSourceView childView = GS::BuildConstView(raw, child);
            const ProgressivePresentationExtractionSnapshot snapshot =
                BuildProgressivePresentationSnapshot(childView, *bindings);

            summary.ChildSlotCount += snapshot.Stats.SlotCount;
            summary.ChildPendingSlotCount += snapshot.Stats.PendingSlotCount;
            summary.ChildFailedSlotCount += snapshot.Stats.FailedSlotCount;
            summary.ChildFailedSlotCount += snapshot.Stats.UnsupportedSlotCount;
        }

        void AccumulateProgressiveCompositionSummary(
            const entt::registry& raw,
            SandboxEditorProgressiveCompositionSummary& summary,
            const ECS::EntityHandle entity,
            const DerivedJobQueueSnapshot* jobs)
        {
            const auto* hierarchy =
                raw.try_get<ECSC::Hierarchy::Component>(entity);
            if (hierarchy == nullptr || hierarchy->ChildCount == 0u)
                return;

            summary.HasChildren = true;
            ECS::EntityHandle child = hierarchy->FirstChild;
            std::uint32_t guard = 0u;
            while (child != ECS::InvalidEntityHandle &&
                   raw.valid(child) &&
                   guard < hierarchy->ChildCount)
            {
                AccumulateProgressiveChildSummary(raw, summary, child, jobs);
                const auto* childHierarchy =
                    raw.try_get<ECSC::Hierarchy::Component>(child);
                child = childHierarchy != nullptr
                    ? childHierarchy->NextSibling
                    : ECS::InvalidEntityHandle;
                ++guard;
            }
        }

        [[nodiscard]] SandboxEditorProgressiveRenderDataModel
        BuildProgressiveRenderDataModel(
            const SandboxEditorContext& context,
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            SandboxEditorProgressiveRenderDataModel model{};
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            model.Shape = InferProgressiveEntityShape(raw, entity, view);

            const std::uint32_t stableEntityId =
                SelectionController::ToStableEntityId(entity);
            AppendProgressiveJobRowsForEntity(
                model,
                context.DerivedJobs,
                stableEntityId);
            AccumulateProgressiveCompositionSummary(
                raw,
                model.Composition,
                entity,
                context.DerivedJobs);

            const auto* bindings =
                raw.try_get<ProgressivePresentationBindings>(entity);
            if (bindings == nullptr)
                return model;

            model.HasBindings = true;
            model.Shape = bindings->Shape;
            model.BindingGeneration = bindings->BindingGeneration;

            const ProgressivePresentationExtractionSnapshot snapshot =
                BuildProgressivePresentationSnapshot(view, *bindings);
            model.Stats = snapshot.Stats;
            model.Slots.reserve(snapshot.Slots.size());
            for (const ProgressiveSlotExtraction& slot : snapshot.Slots)
                model.Slots.push_back(ToProgressiveSlotModel(view, *bindings, slot));

            if (snapshot.Stats.DiagnosticCount > 0u)
            {
                AddDiagnostic(
                    model.Diagnostics,
                    SandboxEditorDiagnosticCode::InvalidVisualizationProperty,
                    "Progressive render-data has slot diagnostics.");
            }
            return model;
        }

        [[nodiscard]] std::optional<std::size_t> FindCatalogMatchIndex(
            const SandboxEditorPropertyCatalogModel& catalog,
            const ProgressivePropertyBindingDescriptor& descriptor)
        {
            if (descriptor.Domain == ProgressiveGeometryDomain::Unknown ||
                descriptor.PropertyName.empty())
            {
                return std::nullopt;
            }

            for (std::size_t i = 0u; i < catalog.Rows.size(); ++i)
            {
                const SandboxEditorPropertyCatalogRow& row = catalog.Rows[i];
                if (row.Descriptor.Domain == descriptor.Domain &&
                    row.Name == descriptor.PropertyName)
                {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] SandboxEditorBoundRenderStateRow MakeRenderHintRow(
            std::string label,
            const ProgressiveRenderLane lane,
            const bool enabled,
            std::string sourceDescription,
            std::string disabledReason = {})
        {
            return SandboxEditorBoundRenderStateRow{
                .Kind = SandboxEditorBoundRenderStateRowKind::RenderHint,
                .Label = std::move(label),
                .Lane = lane,
                .Readiness = enabled
                    ? ProgressiveReadinessState::Ready
                    : ProgressiveReadinessState::Unset,
                .Enabled = enabled,
                .SourceDescription = std::move(sourceDescription),
                .DisabledReason = std::move(disabledReason),
            };
        }

        void AppendRenderHintRows(
            std::vector<SandboxEditorBoundRenderStateRow>& rows,
            const SandboxEditorRenderHintModel& hints)
        {
            rows.push_back(
                MakeRenderHintRow(
                    "Surface render hint",
                    ProgressiveRenderLane::Surface,
                    hints.HasRenderSurface,
                    hints.HasRenderSurface ? hints.SurfaceDomain : "not enabled",
                    hints.HasRenderSurface ? std::string{} : "surface rendering is not enabled"));

            std::string edgeSource = "not enabled";
            if (hints.HasRenderEdges)
            {
                if (hints.HasNamedEdgeWidth)
                    edgeSource = "property:" + hints.EdgeWidthName;
                else if (hints.HasUniformEdgeWidth)
                    edgeSource = "uniform:" + std::to_string(hints.UniformEdgeWidth);
                else
                    edgeSource = hints.EdgeDomain;
            }
            rows.push_back(
                MakeRenderHintRow(
                    "Edge render hint",
                    ProgressiveRenderLane::Edges,
                    hints.HasRenderEdges,
                    std::move(edgeSource),
                    hints.HasRenderEdges ? std::string{} : "edge rendering is not enabled"));

            std::string pointSource = "not enabled";
            if (hints.HasRenderPoints)
            {
                if (hints.HasNamedPointSize)
                    pointSource = "property:" + hints.PointSizeName;
                else if (hints.HasUniformPointSize)
                    pointSource = "uniform:" + std::to_string(hints.UniformPointSize);
                else
                    pointSource = hints.PointRenderType;
            }
            rows.push_back(
                MakeRenderHintRow(
                    "Point render hint",
                    ProgressiveRenderLane::Points,
                    hints.HasRenderPoints,
                    std::move(pointSource),
                    hints.HasRenderPoints ? std::string{} : "point rendering is not enabled"));
        }

        void AppendBoundSlotRows(
            std::vector<SandboxEditorBoundRenderStateRow>& rows,
            const SandboxEditorPropertyCatalogModel& catalog,
            const SandboxEditorProgressiveRenderDataModel& progressive)
        {
            for (const SandboxEditorProgressiveSlotModel& slot :
                 progressive.Slots)
            {
                const std::optional<std::size_t> match =
                    FindCatalogMatchIndex(catalog, slot.Property);
                rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::ProgressiveSlot,
                    .Label = std::string{ToString(slot.Semantic)},
                    .Lane = slot.Lane,
                    .PresentationKey = slot.PresentationKey,
                    .PresentationKind = slot.PresentationKind,
                    .Semantic = slot.Semantic,
                    .SourceKind = slot.SourceKind,
                    .Readiness = slot.Readiness,
                    .Property = slot.Property,
                    .PropertyResolution = slot.PropertyResolution,
                    .AuthoredTexture = slot.AuthoredTexture,
                    .GeneratedTexture = slot.GeneratedTexture,
                    .TextureAsset = slot.TextureAsset,
                    .Enabled = slot.Enabled,
                    .UsesUniformDefault = slot.UsesUniformDefault,
                    .TextureReady = slot.TextureReady,
                    .PropertyBufferReady = slot.PropertyBufferReady,
                    .PreviousOutputRetained = slot.PreviousOutputRetained,
                    .Unsupported = slot.Unsupported,
                    .HasCatalogMatch = match.has_value(),
                    .CatalogRowIndex = match,
                    .SourceDescription = std::string{ToString(slot.SourceKind)},
                    .DisabledReason = slot.Enabled ? std::string{} : "slot is disabled",
                    .Diagnostic = slot.Diagnostic,
                });
            }
        }

        void AppendBoundJobRows(
            std::vector<SandboxEditorBoundRenderStateRow>& rows,
            const SandboxEditorProgressiveRenderDataModel& progressive)
        {
            for (const SandboxEditorProgressiveJobModel& job :
                 progressive.Jobs)
            {
                rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::DerivedJob,
                    .Label = job.Name,
                    .Lane = ProgressiveRenderLane::Surface,
                    .Semantic = job.Key.OutputSemantic,
                    .Readiness = IsFailedDerivedJobStatus(job.Status)
                        ? ProgressiveReadinessState::Failed
                        : (IsActiveDerivedJobStatus(job.Status)
                               ? ProgressiveReadinessState::Pending
                               : ProgressiveReadinessState::Ready),
                    .Job = job.Handle,
                    .JobStatus = job.Status,
                    .JobProgress = job.NormalizedProgress,
                    .JobProgressDeterminate = job.ProgressDeterminate,
                    .Enabled = true,
                    .PreviousOutputRetained = job.PreviousOutputRetained,
                    .SourceDescription = job.Key.OutputName,
                    .Diagnostic = job.Diagnostic,
                });
            }
        }

        [[nodiscard]] SandboxEditorBoundRenderStateModel BuildBoundRenderStateModel(
            const SandboxEditorPropertyCatalogModel& catalog,
            const SandboxEditorProgressiveRenderDataModel& progressive,
            const SandboxEditorRenderHintModel& renderHints,
            const SandboxEditorGeometryDomainModel& geometry,
            const std::uint32_t stableEntityId)
        {
            SandboxEditorBoundRenderStateModel model{};
            model.HasSelectedEntity = true;
            model.SelectedStableId = stableEntityId;
            model.Shape = progressive.Shape;
            model.BindingGeneration = progressive.BindingGeneration;
            model.Composition = progressive.Composition;

            AppendRenderHintRows(model.Rows, renderHints);
            AppendBoundSlotRows(model.Rows, catalog, progressive);
            AppendBoundJobRows(model.Rows, progressive);

            if (progressive.Composition.HasChildren)
            {
                model.Rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::CompositionSummary,
                    .Label = "Composition summary",
                    .Readiness = progressive.Composition.ChildFailedSlotCount > 0u ||
                                         progressive.Composition.ChildFailedJobCount > 0u
                                     ? ProgressiveReadinessState::Failed
                                     : (progressive.Composition.ChildPendingSlotCount > 0u ||
                                                progressive.Composition.ChildActiveJobCount > 0u
                                            ? ProgressiveReadinessState::Pending
                                            : ProgressiveReadinessState::Ready),
                    .Enabled = true,
                    .SourceDescription =
                        "children:" + std::to_string(progressive.Composition.ChildCount),
                });
            }

            if (!progressive.HasBindings)
            {
                model.Rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::DisabledCommand,
                    .Label = "Progressive bindings",
                    .Readiness = ProgressiveReadinessState::Unsupported,
                    .Enabled = false,
                    .DisabledReason =
                        "selected entity has no progressive presentation bindings",
                });
            }

            if (geometry.Domain == GS::Domain::Graph ||
                geometry.Domain == GS::Domain::PointCloud)
            {
                model.Rows.push_back(SandboxEditorBoundRenderStateRow{
                    .Kind = SandboxEditorBoundRenderStateRowKind::DisabledCommand,
                    .Label = "Texture bake",
                    .Readiness = ProgressiveReadinessState::Unsupported,
                    .Enabled = false,
                    .DisabledReason =
                        "texture baking is available for mesh surface slots only",
                });
            }

            if (model.Rows.empty())
            {
                AddDiagnostic(
                    model.Diagnostics,
                    SandboxEditorDiagnosticCode::InvalidVisualizationProperty,
                    "No bound render state rows were available.");
            }
            return model;
        }

        enum class MeshFaceRingStatus : std::uint8_t
        {
            Triangulate,
            Skip,
            Invalid,
        };

        [[nodiscard]] MeshFaceRingStatus BuildMeshFaceRing(
            const std::vector<std::uint32_t>& faceHalfedges,
            const std::vector<std::uint32_t>& halfedgeFaces,
            const std::vector<std::uint32_t>& nextHalfedges,
            const std::vector<std::uint32_t>& toVertices,
            const std::size_t faceIndex,
            const std::uint32_t vertexCount,
            std::vector<std::uint32_t>& outRing)
        {
            constexpr std::uint32_t invalid = std::numeric_limits<std::uint32_t>::max();
            outRing.clear();
            if (faceIndex >= faceHalfedges.size())
                return MeshFaceRingStatus::Invalid;

            const std::size_t halfedgeCount = toVertices.size();
            const std::uint32_t first = faceHalfedges[faceIndex];
            if (first == invalid)
                return MeshFaceRingStatus::Skip;
            if (first >= halfedgeCount)
                return MeshFaceRingStatus::Invalid;

            const std::uint32_t owner = halfedgeFaces[first];
            if (owner == invalid || owner >= faceHalfedges.size())
                return MeshFaceRingStatus::Skip;
            if (owner != static_cast<std::uint32_t>(faceIndex))
                return MeshFaceRingStatus::Skip;

            std::uint32_t halfedge = first;
            for (std::size_t step = 0u; step <= halfedgeCount; ++step)
            {
                if (halfedge >= halfedgeCount)
                    return MeshFaceRingStatus::Invalid;
                if (halfedgeFaces[halfedge] != static_cast<std::uint32_t>(faceIndex))
                    return MeshFaceRingStatus::Invalid;

                const std::uint32_t vertex = toVertices[halfedge];
                if (vertex >= vertexCount)
                    return MeshFaceRingStatus::Invalid;
                outRing.push_back(vertex);

                const std::uint32_t next = nextHalfedges[halfedge];
                if (next == first)
                    break;
                if (next == invalid || step == halfedgeCount)
                    return MeshFaceRingStatus::Invalid;
                halfedge = next;
            }

            return outRing.size() >= 3u
                ? MeshFaceRingStatus::Triangulate
                : MeshFaceRingStatus::Skip;
        }

        struct MeshSoupFromGeometrySourcesResult
        {
            Geometry::MeshSoup::IndexedMesh Mesh{};
            std::vector<std::uint32_t> SourceFaceForSoupFace{};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshSoupFromGeometrySourcesResult BuildMeshSoupFromGeometrySources(
            const GS::ConstSourceView& view)
        {
            MeshSoupFromGeometrySourcesResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                result.Diagnostic = "UV regeneration requires selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
            {
                result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has no vertex position property";
                return result;
            }
            if (positions.Vector().size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has too many vertices for UV regeneration";
                return result;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            if (!toVertices || !nextHalfedges || !halfedgeFaces || !faceHalfedges ||
                toVertices.Vector().size() != nextHalfedges.Vector().size() ||
                toVertices.Vector().size() != halfedgeFaces.Vector().size() ||
                faceHalfedges.Vector().empty())
            {
                result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has invalid halfedge/face topology";
                return result;
            }

            for (const glm::vec3 position : positions.Vector())
                (void)result.Mesh.AddVertex(position);

            std::vector<std::uint32_t> ring;
            ring.reserve(8u);
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceHalfedges.Vector().size();
                 ++faceIndex)
            {
                const MeshFaceRingStatus status = BuildMeshFaceRing(
                    faceHalfedges.Vector(),
                    halfedgeFaces.Vector(),
                    nextHalfedges.Vector(),
                    toVertices.Vector(),
                    faceIndex,
                    static_cast<std::uint32_t>(positions.Vector().size()),
                    ring);
                if (status == MeshFaceRingStatus::Invalid)
                {
                    result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                    result.Diagnostic = "selected mesh topology is not valid for UV regeneration";
                    return result;
                }
                if (status == MeshFaceRingStatus::Skip)
                    continue;

                for (std::size_t i = 1u; i + 1u < ring.size(); ++i)
                {
                    (void)result.Mesh.AddTriangle(ring[0u], ring[i], ring[i + 1u]);
                    result.SourceFaceForSoupFace.push_back(
                        static_cast<std::uint32_t>(faceIndex));
                }
            }

            if (result.Mesh.FaceCount() == 0u)
            {
                result.Status = SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Diagnostic = "selected mesh has no valid surface faces";
                return result;
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            return result;
        }

        template <typename T>
        void CopyTypedPropertyByXref(
            const Geometry::ConstPropertySet& source,
            const std::string& name,
            const std::span<const std::uint32_t> xrefs,
            Geometry::PropertySet& target)
        {
            const auto sourceProperty = source.Get<T>(name);
            if (!sourceProperty)
                return;

            auto targetProperty = target.GetOrAdd<T>(name, T{});
            for (std::size_t outputIndex = 0u; outputIndex < xrefs.size(); ++outputIndex)
            {
                const std::uint32_t sourceIndex = xrefs[outputIndex];
                if (sourceIndex < sourceProperty.Vector().size())
                    targetProperty[outputIndex] = sourceProperty[sourceIndex];
            }
        }

        void CopyKnownPropertiesByXref(
            const Geometry::ConstPropertySet& source,
            const std::span<const std::uint32_t> xrefs,
            Geometry::PropertySet& target)
        {
            for (const std::string& name : source.Properties())
            {
                CopyTypedPropertyByXref<float>(source, name, xrefs, target);
                CopyTypedPropertyByXref<double>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::uint32_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<std::int32_t>(source, name, xrefs, target);
                CopyTypedPropertyByXref<bool>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec2>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec3>(source, name, xrefs, target);
                CopyTypedPropertyByXref<glm::vec4>(source, name, xrefs, target);
            }
        }

        void CopyUvOutputPropertiesToHalfedgeMesh(
            const GS::ConstSourceView& source,
            const MeshSoupFromGeometrySourcesResult& soup,
            Geometry::UvAtlas::UvAtlasResult& atlas,
            Geometry::HalfedgeMesh::Mesh& mesh)
        {
            std::vector<std::uint32_t> sourceVertexForOutputVertex;
            sourceVertexForOutputVertex.reserve(atlas.OutputMesh.VertexCount());
            for (std::size_t i = 0u; i < atlas.OutputMesh.VertexCount(); ++i)
                sourceVertexForOutputVertex.push_back(static_cast<std::uint32_t>(i));

            CopyKnownPropertiesByXref(
                Geometry::ConstPropertySet(atlas.OutputMesh.VertexProperties()),
                sourceVertexForOutputVertex,
                mesh.VertexProperties());

            if (source.FaceSource == nullptr)
                return;

            std::vector<std::uint32_t> sourceFaceForOutputFace;
            sourceFaceForOutputFace.reserve(atlas.SourceFaceForOutputFace.size());
            for (const std::uint32_t soupFace : atlas.SourceFaceForOutputFace)
            {
                sourceFaceForOutputFace.push_back(
                    soupFace < soup.SourceFaceForSoupFace.size()
                        ? soup.SourceFaceForSoupFace[soupFace]
                        : std::numeric_limits<std::uint32_t>::max());
            }

            CopyKnownPropertiesByXref(
                Geometry::ConstPropertySet(source.FaceSource->Properties),
                sourceFaceForOutputFace,
                mesh.FaceProperties());
        }

        [[nodiscard]] bool IsTextureBakeSourceDomain(
            const SandboxEditorPropertyCatalogDomain domain) noexcept
        {
            return domain == SandboxEditorPropertyCatalogDomain::MeshVertices ||
                   domain == SandboxEditorPropertyCatalogDomain::MeshFaces;
        }

        [[nodiscard]] bool IsNonBakeableMeshAttribute(
            const std::string& name) noexcept
        {
            return name == GS::PropertyNames::kPosition ||
                   name == "v:point" ||
                   name == "v:texcoord" ||
                   name == "v:tex";
        }

        [[nodiscard]] SandboxEditorTextureBakeSourceRow BuildTextureBakeSourceRow(
            const SandboxEditorPropertyCatalogRow& row)
        {
            SandboxEditorTextureBakeSourceRow out{
                .Name = row.Name,
                .CatalogDomain = row.Domain,
                .BakeDomain = ToProgressiveGeometryDomain(row.Domain),
                .ValueKind = row.ValueKind,
                .ExpectedValueKind = ToProgressivePropertyValueKind(row.ValueKind),
                .ElementCount = row.ElementCount,
                .Descriptor = row.Descriptor,
            };

            if (!IsTextureBakeSourceDomain(row.Domain))
            {
                out.Category = SandboxEditorTextureBakeSourceCategory::WrongDomain;
                out.DisabledReason = "texture baking supports mesh vertex and face properties";
                return out;
            }
            if (row.Connectivity || IsNonBakeableMeshAttribute(row.Name))
            {
                out.Category = row.Connectivity
                    ? SandboxEditorTextureBakeSourceCategory::Connectivity
                    : SandboxEditorTextureBakeSourceCategory::Internal;
                out.DisabledReason = row.Connectivity
                    ? "connectivity properties are visible but not texture-bake sources"
                    : "internal mesh coordinate properties are visible but not bake sources";
                return out;
            }
            if (!row.Supported ||
                row.ValueKind == SandboxEditorPropertyCatalogValueKind::Unknown)
            {
                out.Category = SandboxEditorTextureBakeSourceCategory::Unsupported;
                out.DisabledReason = row.UnsupportedReason.empty()
                    ? "unsupported property value type"
                    : row.UnsupportedReason;
                return out;
            }

            out.Category = row.Internal
                ? SandboxEditorTextureBakeSourceCategory::Internal
                : SandboxEditorTextureBakeSourceCategory::Bakeable;
            out.Bakeable = true;
            return out;
        }

        [[nodiscard]] SandboxEditorUvDiagnosticsModel BuildUvDiagnosticsModel(
            const GS::ConstSourceView& view)
        {
            SandboxEditorUvDiagnosticsModel model{};
            model.HasSelectedEntity = true;
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            model.IsMesh =
                availability.ProvenanceDomain == GS::Domain::Mesh &&
                availability.Has(GS::SourceCapability::VertexPoints) &&
                availability.Has(GS::SourceCapability::Halfedges) &&
                availability.Has(GS::SourceCapability::Faces);
            if (!model.IsMesh)
            {
                model.Provenance = "unavailable";
                model.UvRegenerationDisabledReason =
                    "UV diagnostics require a selected mesh";
                return model;
            }

            model.VertexCount = view.VerticesAlive();
            model.FaceCount = view.FacesAlive();
            model.BackendId = "xatlas";
            model.Provenance = "missing";
            model.UvRegenerationAvailable = true;

            if (view.VertexSource == nullptr)
                return model;

            const auto texcoords =
                view.VertexSource->Properties.Get<glm::vec2>(
                    model.TexcoordPropertyName);
            model.HasTexcoords = texcoords.IsValid();
            if (model.HasTexcoords)
            {
                model.TexcoordCount = texcoords.Vector().size();
                model.TexcoordCountMatchesVertices =
                    model.TexcoordCount == model.VertexCount;
                model.Provenance = "geometry property";
                model.CheckerPreviewAvailable =
                    model.TexcoordCountMatchesVertices;
                if (!model.TexcoordCountMatchesVertices)
                {
                    model.LastFailure =
                        "texcoord count does not match mesh vertex count";
                }
                else
                {
                    model.TexcoordsFinite = true;
                    for (const glm::vec2 uv : texcoords.Vector())
                    {
                        if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                        {
                            model.TexcoordsFinite = false;
                            model.LastFailure =
                                "texcoord property contains non-finite values";
                            break;
                        }
                    }
                    model.CheckerPreviewAvailable = model.TexcoordsFinite;
                }
            }
            return model;
        }

        [[nodiscard]] SandboxEditorTextureBakeControlsModel
        BuildTextureBakeControlsModel(
            const SandboxEditorContext& context,
            const GS::ConstSourceView& view,
            const SandboxEditorPropertyCatalogModel& catalog,
            const std::uint32_t stableEntityId)
        {
            SandboxEditorTextureBakeControlsModel model{};
            model.HasSelectedEntity = true;
            model.SelectedStableId = stableEntityId;
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            model.IsMesh =
                availability.ProvenanceDomain == GS::Domain::Mesh &&
                availability.Has(GS::SourceCapability::VertexPoints) &&
                availability.Has(GS::SourceCapability::Halfedges) &&
                availability.Has(GS::SourceCapability::Faces);
            model.HasRuntimeBakeCommand = context.AssetService != nullptr;
            model.Uv = BuildUvDiagnosticsModel(view);

            model.Sources.reserve(catalog.Rows.size());
            for (const SandboxEditorPropertyCatalogRow& row : catalog.Rows)
                model.Sources.push_back(BuildTextureBakeSourceRow(row));

            const bool hasBakeableSource =
                std::any_of(
                    model.Sources.begin(),
                    model.Sources.end(),
                    [](const SandboxEditorTextureBakeSourceRow& row)
                    {
                        return row.Bakeable;
                    });

            model.CanBake = model.IsMesh &&
                            model.HasRuntimeBakeCommand &&
                            model.Uv.HasTexcoords &&
                            model.Uv.TexcoordCountMatchesVertices &&
                            model.Uv.TexcoordsFinite &&
                            hasBakeableSource;
            if (!model.IsMesh)
                model.DisabledReason = "texture baking requires a selected mesh";
            else if (!model.HasRuntimeBakeCommand)
                model.DisabledReason = "runtime selected-mesh bake command is unavailable";
            else if (!model.Uv.HasTexcoords)
                model.DisabledReason = "selected mesh has no resolved texcoord property";
            else if (!model.Uv.TexcoordCountMatchesVertices)
                model.DisabledReason = model.Uv.LastFailure;
            else if (!model.Uv.TexcoordsFinite)
                model.DisabledReason = model.Uv.LastFailure;
            else if (!hasBakeableSource)
                model.DisabledReason = "no bakeable mesh vertex or face properties";
            return model;
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
            const GS::SourceAvailability sources =
                GS::BuildSourceAvailability(view);
            using Domain = SandboxEditorGeometryProcessingDomain;
            switch (domain)
            {
            case Domain::MeshVertices:
                return sources.ProvenanceDomain == GS::Domain::Mesh &&
                       sources.Has(GS::SourceCapability::VertexPoints);
            case Domain::GraphVertices:
                return sources.ProvenanceDomain == GS::Domain::Graph &&
                       sources.Has(GS::SourceCapability::NodePoints);
            case Domain::PointCloudPoints:
                return sources.ProvenanceDomain == GS::Domain::PointCloud &&
                       sources.Has(GS::SourceCapability::VertexPoints);
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

        inline constexpr const char* kProgressivePoissonLevelProperty =
            "p:poisson_level";
        inline constexpr const char* kProgressivePoissonPhaseProperty =
            "p:poisson_phase";
        inline constexpr const char* kProgressivePoissonSplatRadiusProperty =
            "p:poisson_splat_radius";
        inline constexpr const char* kProgressivePoissonPrefixVisibleProperty =
            "p:poisson_prefix_visible";
        inline constexpr const char* kProgressivePoissonCpuBackendDisplayName =
            "CPU reference";
        inline constexpr const char* kProgressivePoissonGpuBackendId =
            "gpu_vulkan_compute";
        inline constexpr const char* kProgressivePoissonGpuBackendDisplayName =
            "Vulkan compute";

        [[nodiscard]] const char* ProgressivePoissonBackendId(
            const SandboxEditorProgressivePoissonBackend backend) noexcept
        {
            switch (backend)
            {
            case SandboxEditorProgressivePoissonBackend::CpuReference:
                return PPR::kBackendId;
            case SandboxEditorProgressivePoissonBackend::VulkanCompute:
                return kProgressivePoissonGpuBackendId;
            }
            return PPR::kBackendId;
        }

        [[nodiscard]] const char* ProgressivePoissonBackendDisplayName(
            const SandboxEditorProgressivePoissonBackend backend) noexcept
        {
            switch (backend)
            {
            case SandboxEditorProgressivePoissonBackend::CpuReference:
                return kProgressivePoissonCpuBackendDisplayName;
            case SandboxEditorProgressivePoissonBackend::VulkanCompute:
                return kProgressivePoissonGpuBackendDisplayName;
            }
            return kProgressivePoissonCpuBackendDisplayName;
        }

        [[nodiscard]] const char* ProgressivePoissonChannelPropertyName(
            const SandboxEditorProgressivePoissonChannel channel) noexcept
        {
            switch (channel)
            {
            case SandboxEditorProgressivePoissonChannel::Level:
                return kProgressivePoissonLevelProperty;
            case SandboxEditorProgressivePoissonChannel::Phase:
                return kProgressivePoissonPhaseProperty;
            case SandboxEditorProgressivePoissonChannel::SplatRadius:
                return kProgressivePoissonSplatRadiusProperty;
            case SandboxEditorProgressivePoissonChannel::PrefixVisible:
                return kProgressivePoissonPrefixVisibleProperty;
            }
            return kProgressivePoissonLevelProperty;
        }

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        MakeProgressivePoissonResult(
            const SandboxEditorCommandStatus status,
            const SandboxEditorProgressivePoissonChannel channel,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorProgressivePoissonResult{
                .Status = status,
                .Channel = channel,
                .Error = error,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] bool IsValidProgressivePoissonConfig(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            return (config.Dimension == 2u || config.Dimension == 3u) &&
                   config.GridWidth > 0u &&
                   config.MaxLevels > 0u &&
                   std::isfinite(config.HashLoadFactor) &&
                   config.HashLoadFactor > 0.0f &&
                   std::isfinite(config.RadiusAlpha);
        }

        [[nodiscard]] bool IsValidProgressivePoissonMeshSurfaceConfig(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            return config.MeshSurfaceSampleCount > 0u &&
                   std::isfinite(config.MeshSurfaceMinTriangleArea) &&
                   config.MeshSurfaceMinTriangleArea > 0.0;
        }

        [[nodiscard]] std::uint32_t SaturatingUint32(
            const std::size_t value) noexcept
        {
            return value > std::numeric_limits<std::uint32_t>::max()
                ? std::numeric_limits<std::uint32_t>::max()
                : static_cast<std::uint32_t>(value);
        }

        [[nodiscard]] PPR::Config ToProgressivePoissonReferenceConfig(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            PPR::Config out{};
            out.Dimension = config.Dimension;
            out.GridWidth = config.GridWidth;
            out.MaxLevels = config.MaxLevels;
            out.HashLoadFactor = config.HashLoadFactor;
            out.RadiusAlpha = config.RadiusAlpha;
            out.RandomizeGridOrigin = config.RandomizeGridOrigin;
            out.GridOriginSeed = config.GridOriginSeed;
            out.ShuffleWithinLevels = config.ShuffleWithinLevels;
            out.ShuffleSeed = config.ShuffleSeed;
            return out;
        }

        [[nodiscard]] SurfaceSampling::Params ToProgressivePoissonSurfaceParams(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            SurfaceSampling::Params out{};
            out.SampleCount =
                static_cast<std::int64_t>(config.MeshSurfaceSampleCount);
            out.Seed = config.MeshSurfaceSampleSeed;
            out.MinTriangleArea = config.MeshSurfaceMinTriangleArea;
            out.InterpolateVertexNormals =
                config.MeshSurfaceInterpolateNormals;
            return out;
        }

        [[nodiscard]] std::uint32_t ClampProgressivePoissonPrefix(
            const std::uint32_t requested,
            const std::uint32_t accepted) noexcept
        {
            if (requested == 0u)
                return accepted;
            return std::min(requested, accepted);
        }

        [[nodiscard]] bool PublishProgressivePoissonProperties(
            Geometry::PropertySet& properties,
            const PPR::Result& method,
            const SandboxEditorProgressivePoissonConfig& config,
            const std::uint32_t prefixCount)
        {
            const std::size_t pointCount = properties.Size();
            std::vector<float> levels(pointCount, -1.0f);
            std::vector<float> phases(pointCount, -1.0f);
            std::vector<float> splatRadii(pointCount, 0.0f);
            std::vector<float> prefixVisible(pointCount, 0.0f);

            const std::uint32_t phaseCount = config.Dimension == 3u ? 8u : 4u;
            for (std::size_t level = 0u;
                 level + 1u < method.LevelOffsets.size();
                 ++level)
            {
                const std::uint32_t begin = method.LevelOffsets[level];
                const std::uint32_t end = method.LevelOffsets[level + 1u];
                for (std::uint32_t rank = begin; rank < end; ++rank)
                {
                    if (rank >= method.Order.size())
                        return false;
                    const std::uint32_t pointIndex = method.Order[rank];
                    if (pointIndex >= pointCount)
                        return false;

                    levels[pointIndex] = static_cast<float>(level);
                    phases[pointIndex] = static_cast<float>(
                        (rank - begin) % phaseCount);
                    if (rank < method.SplatRadii.size())
                        splatRadii[pointIndex] = method.SplatRadii[rank];
                    prefixVisible[pointIndex] = rank < prefixCount ? 1.0f : 0.0f;
                }
            }

            auto levelProp = properties.GetOrAdd<float>(
                kProgressivePoissonLevelProperty,
                -1.0f);
            auto phaseProp = properties.GetOrAdd<float>(
                kProgressivePoissonPhaseProperty,
                -1.0f);
            auto splatProp = properties.GetOrAdd<float>(
                kProgressivePoissonSplatRadiusProperty,
                0.0f);
            auto prefixProp = properties.GetOrAdd<float>(
                kProgressivePoissonPrefixVisibleProperty,
                0.0f);
            if (!levelProp || !phaseProp || !splatProp || !prefixProp)
                return false;

            levelProp.Vector() = std::move(levels);
            phaseProp.Vector() = std::move(phases);
            splatProp.Vector() = std::move(splatRadii);
            prefixProp.Vector() = std::move(prefixVisible);
            return true;
        }

        [[nodiscard]] std::string FormatProgressivePoissonLevelCounts(
            const std::vector<std::uint32_t>& counts)
        {
            if (counts.empty())
                return "none";

            std::string text{};
            for (std::size_t i = 0u; i < counts.size(); ++i)
            {
                if (i != 0u)
                    text += ", ";
                text += std::to_string(i);
                text += ":";
                text += std::to_string(counts[i]);
            }
            return text;
        }

        struct ProgressivePoissonBackendResolution
        {
            SandboxEditorProgressivePoissonBackend Requested{
                SandboxEditorProgressivePoissonBackend::CpuReference};
            SandboxEditorProgressivePoissonBackend Actual{
                SandboxEditorProgressivePoissonBackend::CpuReference};
            std::string FallbackReason{};
        };

        [[nodiscard]] ProgressivePoissonGpuConfig ToProgressivePoissonGpuConfig(
            const SandboxEditorProgressivePoissonConfig& config) noexcept
        {
            return ProgressivePoissonGpuConfig{
                .Dimension = config.Dimension,
                .GridWidth = config.GridWidth,
                .MaxLevels = config.MaxLevels,
                .HashLoadFactor = config.HashLoadFactor,
                .RadiusAlpha = config.RadiusAlpha,
                .RandomizeGridOrigin = config.RandomizeGridOrigin,
                .GridOriginSeed = config.GridOriginSeed,
                .ShuffleWithinLevels = config.ShuffleWithinLevels,
                .ShuffleSeed = config.ShuffleSeed,
            };
        }

        [[nodiscard]] ProgressivePoissonBackendResolution
        ResolveProgressivePoissonBackend(
            const SandboxEditorProgressivePoissonBackend requested,
            const SandboxEditorProgressivePoissonConfig& config,
            const std::uint32_t inputCount,
            RHI::IDevice* device)
        {
            ProgressivePoissonBackendResolution resolved{};
            resolved.Requested = requested;
            if (requested == SandboxEditorProgressivePoissonBackend::CpuReference)
            {
                resolved.Actual = SandboxEditorProgressivePoissonBackend::CpuReference;
                return resolved;
            }

            resolved.Actual = SandboxEditorProgressivePoissonBackend::CpuReference;
            const ProgressivePoissonGpuResolveResult gpu =
                ResolveProgressivePoissonGpuRequest(
                    ProgressivePoissonGpuResolveDesc{
                        .Device = device,
                        .Plan = ProgressivePoissonGpuPlanDesc{
                            .InputCount = inputCount,
                            .Config = ToProgressivePoissonGpuConfig(config),
                        },
                    });
            if (gpu.GpuExecutionAvailable)
            {
                resolved.Actual =
                    SandboxEditorProgressivePoissonBackend::VulkanCompute;
                return resolved;
            }

            resolved.FallbackReason = gpu.Diagnostic;
            if (!resolved.FallbackReason.empty())
            {
                resolved.FallbackReason += " Ran CPU reference.";
            }
            else
            {
                resolved.FallbackReason =
                    "Vulkan compute requested but GPU execution is unavailable; ran CPU reference.";
            }
            return resolved;
        }

        [[nodiscard]] SandboxEditorProgressivePoissonResult
        RunProgressivePoissonAndPublish(
            const std::span<const glm::vec3> positions,
            Geometry::PropertySet& properties,
            const SandboxEditorProgressivePoissonConfig& config,
            RHI::IDevice* device)
        {
            const ProgressivePoissonBackendResolution backend =
                ResolveProgressivePoissonBackend(
                    config.Backend,
                    config,
                    static_cast<std::uint32_t>(positions.size()),
                    device);
            const PPR::Config methodConfig =
                ToProgressivePoissonReferenceConfig(config);
            const PPR::Result method = PPR::Compute(positions, methodConfig);
            SandboxEditorProgressivePoissonResult result{};
            result.Channel = config.Channel;
            result.InputCount = method.Diag.InputCount;
            result.AcceptedCount = method.Diag.AcceptedCount;
            result.LevelCount = static_cast<std::uint32_t>(
                method.Diag.LevelCounts.size());
            result.RequestedBackend = backend.Requested;
            result.ActualBackend = backend.Actual;
            result.RequestedBackendId =
                ProgressivePoissonBackendId(backend.Requested);
            result.RequestedBackendDisplayName =
                ProgressivePoissonBackendDisplayName(backend.Requested);
            result.BackendId = ProgressivePoissonBackendId(backend.Actual);
            result.BackendDisplayName =
                ProgressivePoissonBackendDisplayName(backend.Actual);
            result.FellBackToCpu =
                backend.Requested != backend.Actual &&
                backend.Actual ==
                    SandboxEditorProgressivePoissonBackend::CpuReference;
            result.BackendFallbackReason = backend.FallbackReason;
            result.LevelAcceptedCounts = method.Diag.LevelCounts;
            result.BaseRadius = method.BaseRadius;
            result.UsedAlpha = method.Diag.UsedAlpha;
            result.AlphaDefaulted = method.Diag.AlphaDefaulted;
            result.ClampedGridWidth = method.Diag.ClampedGridWidth;
            result.ClampedMaxLevels = method.Diag.ClampedMaxLevels;

            if (method.Diag.Code != PPR::ValidationCode::Valid)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error =
                    method.Diag.Code == PPR::ValidationCode::InvalidDimension
                    ? Core::ErrorCode::InvalidArgument
                    : Core::ErrorCode::InvalidState;
                result.Message =
                    "Progressive Poisson CPU reference rejected the input/config.";
                return result;
            }

            result.PrefixCount = ClampProgressivePoissonPrefix(
                config.PrefixCount,
                result.AcceptedCount);
            if (!PublishProgressivePoissonProperties(
                    properties,
                    method,
                    config,
                    result.PrefixCount))
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidState;
                result.Message =
                    "Progressive Poisson property publication failed.";
                return result;
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        void AppendProgressivePoissonSuccessMessage(
            SandboxEditorProgressivePoissonResult& result)
        {
            result.Message =
                "Progressive Poisson (requested " +
                (result.RequestedBackendId.empty()
                     ? result.BackendId
                     : result.RequestedBackendId) +
                ", actual " +
                result.BackendId +
                ") accepted " +
                std::to_string(result.AcceptedCount) +
                " of " +
                std::to_string(result.InputCount) +
                " points across " +
                std::to_string(result.LevelCount) +
                " levels; prefix=" +
                std::to_string(result.PrefixCount) +
                ", channel=" +
                DebugNameForSandboxEditorProgressivePoissonChannel(
                    result.Channel);
            if (!result.LevelAcceptedCounts.empty())
            {
                result.Message += ", level_counts=[";
                result.Message += FormatProgressivePoissonLevelCounts(
                    result.LevelAcceptedCounts);
                result.Message += "]";
            }
            if (result.MeshSurfaceSamplingUsed)
            {
                result.Message +=
                    ", mesh samples=" +
                    std::to_string(result.MeshSurfaceSampleCount) +
                    ", accepted triangles=" +
                    std::to_string(result.MeshSurfaceAcceptedTriangleCount) +
                    "/" +
                    std::to_string(result.MeshSurfaceTotalFaceCount);
            }
            if (!result.BackendFallbackReason.empty())
            {
                result.Message += ", fallback=\"";
                result.Message += result.BackendFallbackReason;
                result.Message += "\"";
            }
            result.Message += ".";
        }

        void ApplyProgressivePoissonVisualization(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const SandboxEditorProgressivePoissonChannel channel)
        {
            G::RenderPoints points = raw.all_of<G::RenderPoints>(entity)
                ? raw.get<G::RenderPoints>(entity)
                : G::RenderPoints{};
            if (!std::holds_alternative<float>(points.SizeSource) &&
                !std::holds_alternative<std::string>(points.SizeSource))
            {
                points.SizeSource = 4.0f;
            }
            raw.emplace_or_replace<G::RenderPoints>(entity, points);

            G::VisualizationConfig config = raw.all_of<G::VisualizationConfig>(entity)
                ? raw.get<G::VisualizationConfig>(entity)
                : G::VisualizationConfig{};
            config.Source = G::VisualizationConfig::ColorSource::ScalarField;
            config.ScalarDomain = G::VisualizationConfig::Domain::Vertex;
            config.ScalarFieldName = ProgressivePoissonChannelPropertyName(channel);
            config.Scalar.AutoRange = true;
            config.Scalar.BinCount = 0u;
            config.Scalar.Isolines.Num = 0u;
            raw.emplace_or_replace<G::VisualizationConfig>(entity, config);
        }

        struct MeshForVertexNormalsResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshForVertexNormalsResult
        BuildHalfedgeMeshForVertexNormalRecompute(const GS::MutableSourceView& view)
        {
            MeshForVertexNormalsResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status = SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh vertex normals require selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty() ||
                positions.Vector().size() != view.VertexSource->Properties.Size())
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh requires count-matched v:position for normal recompute";
                return result;
            }
            if (positions.Vector().size() >
                static_cast<std::size_t>(
                    std::numeric_limits<Geometry::PropertyIndex>::max()))
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh has too many vertices for normal recompute";
                return result;
            }

            const auto toVertices =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeToVertex);
            const auto nextHalfedges =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeNext);
            const auto halfedgeFaces =
                view.HalfedgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kHalfedgeFace);
            const auto faceHalfedges =
                view.FaceSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kFaceHalfedge);
            if (!toVertices || !nextHalfedges || !halfedgeFaces ||
                !faceHalfedges ||
                toVertices.Vector().size() != nextHalfedges.Vector().size() ||
                toVertices.Vector().size() != halfedgeFaces.Vector().size() ||
                faceHalfedges.Vector().size() !=
                    view.FaceSource->Properties.Size())
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected mesh has invalid halfedge/face topology for normal recompute";
                return result;
            }

            result.Mesh.Reserve(positions.Vector().size(),
                                view.EdgeSource != nullptr
                                    ? view.EdgeSource->Properties.Size()
                                    : 0u,
                                faceHalfedges.Vector().size());
            for (const glm::vec3 position : positions.Vector())
                (void)result.Mesh.AddVertex(position);

            std::vector<std::uint32_t> ring{};
            ring.reserve(8u);
            std::vector<Geometry::VertexHandle> faceVertices{};
            faceVertices.reserve(8u);
            for (std::size_t faceIndex = 0u;
                 faceIndex < faceHalfedges.Vector().size();
                 ++faceIndex)
            {
                const MeshFaceRingStatus status = BuildMeshFaceRing(
                    faceHalfedges.Vector(),
                    halfedgeFaces.Vector(),
                    nextHalfedges.Vector(),
                    toVertices.Vector(),
                    faceIndex,
                    static_cast<std::uint32_t>(positions.Vector().size()),
                    ring);
                if (status == MeshFaceRingStatus::Invalid)
                {
                    result.Status =
                        SandboxEditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "selected mesh topology is not valid for normal recompute";
                    return result;
                }
                if (status == MeshFaceRingStatus::Skip)
                    continue;

                faceVertices.clear();
                for (const std::uint32_t vertex : ring)
                {
                    faceVertices.push_back(
                        Geometry::VertexHandle{
                            static_cast<Geometry::PropertyIndex>(vertex)});
                }
                if (!result.Mesh.AddFace(faceVertices).has_value())
                {
                    result.Status =
                        SandboxEditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "selected mesh face ring could not be reconstructed for normal recompute";
                    return result;
                }
            }

            result.Status = SandboxEditorCommandStatus::Applied;
            return result;
        }

        [[nodiscard]] SandboxEditorMeshVertexNormalsResult MakeMeshNormalsResult(
            const SandboxEditorCommandStatus status,
            const GN::RecomputeStatus normalStatus,
            const GN::AveragingMode weighting,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorMeshVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .Weighting = weighting,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyMeshNormalCounters(const GN::Result& source,
                                    SandboxEditorMeshVertexNormalsResult& target)
        {
            target.NormalStatus = source.Status;
            target.Weighting = source.Weighting;
            target.VertexSlotCount = source.VertexSlotCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalVertexCount = source.ValidNormalVertexCount;
            target.ProcessedFaceCount = source.ProcessedFaceCount;
            target.DegenerateFaceCount = source.DegenerateFaceCount;
            target.NonFiniteFaceCount = source.NonFiniteFaceCount;
            target.InvalidTopologyFaceCount = source.InvalidTopologyFaceCount;
            target.DegenerateCornerCount = source.DegenerateCornerCount;
            target.FallbackVertexCount = source.FallbackVertexCount;
            target.SkippedDeletedFaceCount = source.SkippedDeletedFaceCount;
            target.SkippedDeletedVertexCount = source.SkippedDeletedVertexCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] bool PublishMeshVertexNormals(
            GS::MutableSourceView& view,
            const GN::Result& result)
        {
            if (view.VertexSource == nullptr || !result.Normals.IsValid())
                return false;

            Geometry::PropertySet& properties = view.VertexSource->Properties;
            if (properties.Exists(GS::PropertyNames::kNormal) &&
                !properties.Get<glm::vec3>(GS::PropertyNames::kNormal))
            {
                return false;
            }

            auto normals = properties.GetOrAdd<glm::vec3>(
                std::string{GS::PropertyNames::kNormal},
                glm::vec3{0.0f, 1.0f, 0.0f});
            if (!normals ||
                normals.Vector().size() != result.Normals.Vector().size())
            {
                return false;
            }

            normals.Vector() = result.Normals.Vector();
            return true;
        }

        [[nodiscard]] bool PublishCanonicalVec3Normals(
            Geometry::PropertySet& properties,
            const std::vector<glm::vec3>& normals)
        {
            if (normals.empty() || normals.size() != properties.Size())
                return false;
            if (properties.Exists(GS::PropertyNames::kNormal) &&
                !properties.Get<glm::vec3>(GS::PropertyNames::kNormal))
            {
                return false;
            }

            auto target = properties.GetOrAdd<glm::vec3>(
                std::string{GS::PropertyNames::kNormal},
                glm::vec3{0.0f, 0.0f, 1.0f});
            if (!target || target.Vector().size() != normals.size())
                return false;

            target.Vector() = normals;
            return true;
        }

        [[nodiscard]] std::string BuildMeshNormalsSuccessMessage(
            const SandboxEditorMeshVertexNormalsResult& result)
        {
            std::string message = "Mesh vertex normals recomputed (weighting=";
            message += std::string(GN::DebugName(result.Weighting));
            message += ", written=";
            message += std::to_string(result.WrittenCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackVertexCount);
            message += ").";
            return message;
        }

        [[nodiscard]] bool IsPositiveFinite(const double value) noexcept
        {
            return std::isfinite(value) && value > 0.0;
        }

        [[nodiscard]] SandboxEditorGraphVertexNormalsResult MakeGraphNormalsResult(
            const SandboxEditorCommandStatus status,
            const GraphNormals::RecomputeStatus normalStatus,
            const bool orientTowardFallback,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorGraphVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .OrientTowardFallback = orientTowardFallback,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyGraphNormalCounters(
            const GraphNormals::Diagnostics& source,
            SandboxEditorGraphVertexNormalsResult& target)
        {
            target.VertexSlotCount = source.VertexSlotCount;
            target.EdgeSlotCount = source.EdgeSlotCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalVertexCount = source.ValidNormalVertexCount;
            target.FallbackVertexCount = source.FallbackVertexCount;
            target.IsolatedVertexCount = source.IsolatedVertexCount;
            target.DegreeOneVertexCount = source.DegreeOneVertexCount;
            target.CollinearNeighborhoodCount =
                source.CollinearNeighborhoodCount;
            target.DuplicatePositionCount = source.DuplicatePositionCount;
            target.NonFinitePositionCount = source.NonFinitePositionCount;
            target.InvalidEdgeCount = source.InvalidEdgeCount;
            target.SkippedDeletedVertexCount =
                source.SkippedDeletedVertexCount;
            target.SkippedDeletedEdgeCount = source.SkippedDeletedEdgeCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] Core::ErrorCode ErrorForGraphNormalStatus(
            const GraphNormals::RecomputeStatus status) noexcept
        {
            using Status = GraphNormals::RecomputeStatus;
            switch (status)
            {
            case Status::Success:
                return Core::ErrorCode::Success;
            case Status::PropertyTypeConflict:
                return Core::ErrorCode::TypeMismatch;
            case Status::EmptyGraph:
            case Status::InvalidPositionProperty:
            case Status::InvalidTopologyProperty:
            case Status::InvalidOutputProperty:
            case Status::CountMismatch:
                return Core::ErrorCode::InvalidArgument;
            }
            return Core::ErrorCode::Unknown;
        }

        struct GraphForVertexNormalsResult
        {
            Geometry::Halfedges Halfedges{};
            std::size_t EdgeSlotCount{0};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] GraphForVertexNormalsResult
        BuildGraphConnectivityForVertexNormalRecompute(
            const GS::MutableSourceView& view)
        {
            GraphForVertexNormalsResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Graph ||
                view.NodeSource == nullptr ||
                view.EdgeSource == nullptr)
            {
                result.Status =
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Graph vertex normals require selected graph GeometrySources.";
                return result;
            }

            const auto edgeV0 =
                view.EdgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV0);
            const auto edgeV1 =
                view.EdgeSource->Properties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV1);
            const std::size_t edgeSlotCount =
                view.EdgeSource->Properties.Size();
            if (!edgeV0 || !edgeV1 ||
                edgeV0.Vector().size() != edgeSlotCount ||
                edgeV1.Vector().size() != edgeSlotCount)
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "selected graph requires count-matched edge endpoint properties for normal recompute";
                return result;
            }

            result.Halfedges.Resize(edgeSlotCount * 2u);
            auto connectivity =
                result.Halfedges.GetOrAdd<Geometry::Graph::HalfedgeConnectivity>(
                    "h:connectivity",
                    {});
            if (!connectivity ||
                connectivity.Vector().size() != edgeSlotCount * 2u)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::Unknown;
                result.Diagnostic =
                    "graph normal recompute could not build halfedge connectivity scratch storage";
                return result;
            }

            for (std::size_t edgeIndex = 0u;
                 edgeIndex < edgeSlotCount;
                 ++edgeIndex)
            {
                const std::size_t h0 = edgeIndex * 2u;
                const std::size_t h1 = h0 + 1u;
                connectivity.Vector()[h0].Vertex =
                    Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(
                            edgeV1.Vector()[edgeIndex])};
                connectivity.Vector()[h1].Vertex =
                    Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(
                            edgeV0.Vector()[edgeIndex])};
            }

            result.EdgeSlotCount = edgeSlotCount;
            result.Status = SandboxEditorCommandStatus::Applied;
            return result;
        }

        [[nodiscard]] std::string BuildGraphNormalsSuccessMessage(
            const SandboxEditorGraphVertexNormalsResult& result)
        {
            std::string message = "Graph vertex normals recomputed (written=";
            message += std::to_string(result.WrittenCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackVertexCount);
            message += ", invalidEdges=";
            message += std::to_string(result.InvalidEdgeCount);
            message += ").";
            return message;
        }

        [[nodiscard]] SandboxEditorPointCloudVertexNormalsResult
        MakePointCloudNormalsResult(
            const SandboxEditorCommandStatus status,
            const PointNormals::RecomputeStatus normalStatus,
            const SandboxEditorPointCloudVertexNormalsCommand& command,
            const Core::ErrorCode error,
            std::string message)
        {
            return SandboxEditorPointCloudVertexNormalsResult{
                .Status = status,
                .NormalStatus = normalStatus,
                .Orientation = command.Orientation,
                .KNeighbors = command.KNeighbors,
                .MinimumNeighbors = command.MinimumNeighbors,
                .UseRadiusSearch = command.UseRadiusSearch,
                .Radius = command.Radius,
                .Error = error,
                .Message = std::move(message),
            };
        }

        void CopyPointCloudNormalCounters(
            const PointNormals::Diagnostics& source,
            SandboxEditorPointCloudVertexNormalsResult& target)
        {
            target.PointSlotCount = source.PointSlotCount;
            target.FinitePointCount = source.FinitePointCount;
            target.WrittenCount = source.WrittenCount;
            target.ValidNormalPointCount = source.ValidNormalPointCount;
            target.FallbackPointCount = source.FallbackPointCount;
            target.DegenerateNeighborhoodCount =
                source.DegenerateNeighborhoodCount;
            target.TooFewNeighborCount = source.TooFewNeighborCount;
            target.CollinearNeighborhoodCount =
                source.CollinearNeighborhoodCount;
            target.DuplicatePositionCount = source.DuplicatePositionCount;
            target.NonFinitePointCount = source.NonFinitePointCount;
            target.SkippedDeletedPointCount =
                source.SkippedDeletedPointCount;
            target.SpatialQueryFailureCount =
                source.SpatialQueryFailureCount;
            target.FlippedOrientationCount = source.FlippedOrientationCount;
            target.KNNVisitedNodeCount = source.KNNVisitedNodeCount;
            target.KNNDistanceEvaluationCount =
                source.KNNDistanceEvaluationCount;
            target.FallbackNormalWasRepaired =
                source.FallbackNormalWasRepaired;
        }

        [[nodiscard]] Core::ErrorCode ErrorForPointCloudNormalStatus(
            const PointNormals::RecomputeStatus status) noexcept
        {
            using Status = PointNormals::RecomputeStatus;
            switch (status)
            {
            case Status::Success:
                return Core::ErrorCode::Success;
            case Status::PropertyTypeConflict:
                return Core::ErrorCode::TypeMismatch;
            case Status::EmptyInput:
            case Status::TooFewFinitePoints:
            case Status::InvalidPositionProperty:
            case Status::InvalidOutputProperty:
            case Status::CountMismatch:
                return Core::ErrorCode::InvalidArgument;
            case Status::SpatialIndexBuildFailed:
            case Status::SpatialIndexQueryFailed:
                return Core::ErrorCode::Unknown;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] std::string BuildPointCloudNormalsSuccessMessage(
            const SandboxEditorPointCloudVertexNormalsResult& result)
        {
            std::string message =
                "Point-cloud vertex normals recomputed (backend=";
            message += std::string(PointNormals::DebugName(result.Backend));
            message += ", written=";
            message += std::to_string(result.WrittenCount);
            message += ", fallback=";
            message += std::to_string(result.FallbackPointCount);
            message += ").";
            return message;
        }

        [[nodiscard]] const char* DebugNameForSandboxEditorMeshDenoiseStage(
            const SandboxEditorMeshDenoiseStage stage) noexcept
        {
            switch (stage)
            {
            case SandboxEditorMeshDenoiseStage::FullBilateral:
                return "Full bilateral";
            }
            return "Unknown";
        }

        [[nodiscard]] SandboxEditorMeshCurvatureOutput
        MeshCurvatureOutputFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(kMeshCurvatureOutputs.size() - 1u));
            return kMeshCurvatureOutputs[static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] SandboxEditorMeshRemeshMode
        MeshRemeshModeFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(kMeshRemeshModes.size() - 1u));
            return kMeshRemeshModes[static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] SandboxEditorMeshRemeshSizingLaw
        MeshRemeshSizingLawFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(kMeshRemeshSizingLaws.size() - 1u));
            return kMeshRemeshSizingLaws[static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] SandboxEditorMeshSimplifyMetric
        MeshSimplifyMetricFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(kMeshSimplifyMetrics.size() - 1u));
            return kMeshSimplifyMetrics[static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] SandboxEditorMeshSubdivideOperator
        MeshSubdivideOperatorFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(kMeshSubdivideOperators.size() - 1u));
            return kMeshSubdivideOperators[static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] Core::ErrorCode ErrorForDenoiseStatus(
            const Smooth::DenoiseStatus status) noexcept
        {
            switch (status)
            {
            case Smooth::DenoiseStatus::Success:
                return Core::ErrorCode::Success;
            case Smooth::DenoiseStatus::EmptyMesh:
                return Core::ErrorCode::ResourceNotFound;
            case Smooth::DenoiseStatus::NonManifoldInput:
            case Smooth::DenoiseStatus::DegenerateGeometry:
            case Smooth::DenoiseStatus::NonFiniteInput:
            case Smooth::DenoiseStatus::InvalidParams:
                return Core::ErrorCode::InvalidArgument;
            }
            return Core::ErrorCode::Unknown;
        }

        [[nodiscard]] bool IsFiniteVec3(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool AllFiniteVec3(
            const std::span<const glm::vec3> values) noexcept
        {
            for (const glm::vec3 value : values)
            {
                if (!IsFiniteVec3(value))
                    return false;
            }
            return true;
        }

        struct MeshDenoiseSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<glm::vec3> BeforePositions{};
            std::vector<bool> DeletedVertices{};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshDenoiseSourceResult BuildHalfedgeMeshForDenoise(
            const GS::ConstSourceView& view)
        {
            MeshDenoiseSourceResult result{};
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                result.Status =
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise requires selected mesh GeometrySources.";
                return result;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().empty())
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise requires a non-empty v:position property.";
                return result;
            }

            result.BeforePositions = positions.Vector();
            result.DeletedVertices.assign(result.BeforePositions.size(), false);
            if (const auto deleted =
                    view.VertexSource->Properties.Get<bool>("v:deleted"))
            {
                if (deleted.Vector().size() != result.BeforePositions.size())
                {
                    result.Status =
                        SandboxEditorCommandStatus::InvalidProcessingParameters;
                    result.Error = Core::ErrorCode::InvalidArgument;
                    result.Diagnostic =
                        "Mesh denoise requires v:deleted to match v:position when present.";
                    return result;
                }
                for (std::size_t i = 0u; i < deleted.Vector().size(); ++i)
                    result.DeletedVertices[i] = deleted.Vector()[i];
            }

            MeshSoupFromGeometrySourcesResult soup =
                BuildMeshSoupFromGeometrySources(view);
            if (!soup.Succeeded())
            {
                result.Status = soup.Status;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic = soup.Diagnostic.empty()
                    ? "Mesh denoise could not build a triangle soup from GeometrySources."
                    : soup.Diagnostic;
                return result;
            }

            auto converted =
                Geometry::Mesh::Conversion::ToHalfedgeMesh(soup.Mesh);
            if (!converted.Succeeded())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise could not convert selected GeometrySources to halfedge topology.";
                return result;
            }
            if (converted.Mesh.VerticesSize() != result.BeforePositions.size())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Diagnostic =
                    "Mesh denoise conversion changed the vertex slot count.";
                return result;
            }

            result.Mesh = std::move(converted.Mesh);
            result.Status = SandboxEditorCommandStatus::Applied;
            result.Error = Core::ErrorCode::Success;
            return result;
        }

        [[nodiscard]] std::vector<glm::vec3> ExtractMeshPositions(
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            std::vector<glm::vec3> positions(mesh.VerticesSize());
            for (std::size_t i = 0u; i < positions.size(); ++i)
            {
                positions[i] = mesh.Position(
                    Geometry::VertexHandle{
                        static_cast<Geometry::PropertyIndex>(i)});
            }
            return positions;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshDenoisePositionState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const std::vector<glm::vec3>& positions)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            auto currentPositions =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
            if (!currentPositions ||
                currentPositions.Vector().size() != positions.size() ||
                !AllFiniteVec3(std::span<const glm::vec3>{
                    positions.data(),
                    positions.size()}))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }

            currentPositions.Vector() = positions;
            Dirty::MarkVertexPositionsDirty(raw, *entity);
            Dirty::MarkVertexAttributesDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitMeshDenoisePositions(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            std::vector<glm::vec3> before,
            std::vector<glm::vec3> after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = "Denoise mesh vertices",
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyMeshDenoisePositionState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyMeshDenoisePositionState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyMeshDenoisePositionState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        void CopyMeshDenoiseCounters(
            const Smooth::BilateralDenoiseResult& source,
            SandboxEditorMeshDenoiseResult& target)
        {
            target.NormalIterations =
                static_cast<std::uint32_t>(
                    source.NormalIterationsPerformed);
            target.VertexIterations =
                static_cast<std::uint32_t>(
                    source.VertexIterationsPerformed);
            target.VertexSlotCount = source.VertexCount;
            target.MovedVertexCount = source.MovedVertexCount;
            target.ProcessedFaceCount = source.ProcessedFaceCount;
            target.DegenerateFaceCount = source.DegenerateFaceCount;
            target.NonFiniteFaceCount = source.NonFiniteFaceCount;
            target.SkippedDeletedFaceCount = source.SkippedDeletedFaceCount;
            target.PinnedBoundaryVertexCount =
                source.PinnedBoundaryVertexCount;
            target.SigmaSpatialUsed = source.SigmaSpatialUsed;
            target.SigmaRangeUsed = source.SigmaRangeUsed;
        }

        [[nodiscard]] std::string BuildMeshDenoiseSuccessMessage(
            const SandboxEditorMeshDenoiseResult& result)
        {
            std::string message = "Mesh denoise completed (written=";
            message += std::to_string(result.WrittenCount);
            message += ", moved=";
            message += std::to_string(result.MovedVertexCount);
            message += ", sigmaSpatial=";
            message += std::to_string(result.SigmaSpatialUsed);
            message += ", sigmaRange=";
            message += std::to_string(result.SigmaRangeUsed);
            message += ").";
            return message;
        }

        struct MeshCurvatureSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::size_t VertexSlotCount{0u};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshCurvatureSourceResult BuildHalfedgeMeshForCurvature(
            const GS::ConstSourceView& view)
        {
            MeshDenoiseSourceResult source = BuildHalfedgeMeshForDenoise(view);
            MeshCurvatureSourceResult result{};
            result.VertexSlotCount = source.BeforePositions.size();
            result.Status = source.Status;
            result.Error = source.Error;
            if (source.Succeeded())
            {
                result.Mesh = std::move(source.Mesh);
                result.Diagnostic.clear();
            }
            else if (source.Status ==
                     SandboxEditorCommandStatus::UnsupportedGeometryDomain)
            {
                result.Diagnostic =
                    "Mesh curvature requires selected mesh GeometrySources.";
            }
            else if (source.Status ==
                     SandboxEditorCommandStatus::InvalidProcessingParameters)
            {
                result.Diagnostic =
                    "Mesh curvature requires finite count-matched vertex positions and valid mesh topology.";
            }
            else
            {
                result.Diagnostic = source.Diagnostic.empty()
                    ? "Mesh curvature could not build a halfedge mesh from GeometrySources."
                    : source.Diagnostic;
            }
            return result;
        }

        struct MeshCurvaturePropertyState
        {
            bool HadMean{false};
            bool HadGaussian{false};
            bool HadDir1{false};
            bool HadDir2{false};
            std::vector<double> Mean{};
            std::vector<double> Gaussian{};
            std::vector<glm::vec3> Dir1{};
            std::vector<glm::vec3> Dir2{};
        };

        template <typename T>
        [[nodiscard]] bool CaptureCurvatureProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const std::size_t expectedCount,
            bool& hadProperty,
            std::vector<T>& values,
            std::string& diagnostic)
        {
            hadProperty = false;
            values.clear();
            if (!properties.Exists(name))
                return true;

            auto property = properties.Get<T>(name);
            if (!property || property.Vector().size() != expectedCount)
            {
                diagnostic = "existing curvature property has an incompatible type or count: ";
                diagnostic += std::string{name};
                return false;
            }

            hadProperty = true;
            values = property.Vector();
            return true;
        }

        [[nodiscard]] bool CaptureMeshCurvaturePropertyState(
            Geometry::PropertySet& properties,
            const std::size_t expectedCount,
            MeshCurvaturePropertyState& out,
            std::string& diagnostic)
        {
            return CaptureCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kMeanCurvature,
                       expectedCount,
                       out.HadMean,
                       out.Mean,
                       diagnostic) &&
                   CaptureCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kGaussianCurvature,
                       expectedCount,
                       out.HadGaussian,
                       out.Gaussian,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir1,
                       expectedCount,
                       out.HadDir1,
                       out.Dir1,
                       diagnostic) &&
                   CaptureCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir2,
                       expectedCount,
                       out.HadDir2,
                       out.Dir2,
                       diagnostic);
        }

        template <typename T>
        [[nodiscard]] bool ApplyCurvatureProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const bool hasProperty,
            const std::vector<T>& values,
            const T& defaultValue)
        {
            if (!hasProperty)
            {
                auto property = properties.Get<T>(name);
                if (property)
                {
                    properties.Remove(property);
                    return true;
                }
                return !properties.Exists(name);
            }

            auto property =
                properties.GetOrAdd<T>(std::string{name}, defaultValue);
            if (!property || property.Vector().size() != values.size())
                return false;
            property.Vector() = values;
            return true;
        }

        [[nodiscard]] bool ApplyMeshCurvaturePropertyState(
            Geometry::PropertySet& properties,
            const MeshCurvaturePropertyState& state)
        {
            return ApplyCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kMeanCurvature,
                       state.HadMean,
                       state.Mean,
                       0.0) &&
                   ApplyCurvatureProperty<double>(
                       properties,
                       GS::PropertyNames::kGaussianCurvature,
                       state.HadGaussian,
                       state.Gaussian,
                       0.0) &&
                   ApplyCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir1,
                       state.HadDir1,
                       state.Dir1,
                       glm::vec3{0.0f}) &&
                   ApplyCurvatureProperty<glm::vec3>(
                       properties,
                       GS::PropertyNames::kPrincipalDir2,
                       state.HadDir2,
                       state.Dir2,
                       glm::vec3{0.0f});
        }

        [[nodiscard]] std::size_t CountNonFiniteScalars(
            const std::span<const double> values) noexcept
        {
            std::size_t count = 0u;
            for (const double value : values)
            {
                if (!std::isfinite(value))
                    ++count;
            }
            return count;
        }

        [[nodiscard]] std::size_t CountNonFiniteVectors(
            const std::span<const glm::vec3> values) noexcept
        {
            std::size_t count = 0u;
            for (const glm::vec3 value : values)
            {
                if (!IsFiniteVec3(value))
                    ++count;
            }
            return count;
        }

        [[nodiscard]] bool CurvatureOutputRequestsDirections(
            const SandboxEditorMeshCurvatureOutput output) noexcept
        {
            return output == SandboxEditorMeshCurvatureOutput::All ||
                   output == SandboxEditorMeshCurvatureOutput::PrincipalDirections;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshCurvatureState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const MeshCurvaturePropertyState& state)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
            const GS::SourceAvailability availability =
                GS::BuildSourceAvailability(view);
            if (availability.ProvenanceDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }

            if (!ApplyMeshCurvaturePropertyState(
                    view.VertexSource->Properties,
                    state))
            {
                return EditorCommandHistoryStatus::CommandFailed;
            }

            Dirty::MarkVertexAttributesDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitMeshCurvatureProperties(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            MeshCurvaturePropertyState before,
            MeshCurvaturePropertyState after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = "Compute mesh curvature",
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyMeshCurvatureState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyMeshCurvatureState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyMeshCurvatureState(context.Scene, stableEntityId, after));
        }

        [[nodiscard]] std::string BuildMeshCurvatureSuccessMessage(
            const SandboxEditorMeshCurvatureResult& result)
        {
            std::string message = "Mesh curvature computed (vertices=";
            message += std::to_string(result.VertexSlotCount);
            message += ", scalars=";
            message += std::to_string(result.ScalarWrittenCount);
            message += ", directions=";
            message += result.DirectionsPublished ? "published" : "not published";
            message += ").";
            if (result.DirectionsRequested && !result.DirectionsPublished)
                message += " Principal directions were not published for this run.";
            return message;
        }

        [[nodiscard]] bool ValidMeshRemeshMode(
            const SandboxEditorMeshRemeshMode mode) noexcept
        {
            return std::find(kMeshRemeshModes.begin(),
                             kMeshRemeshModes.end(),
                             mode) != kMeshRemeshModes.end();
        }

        [[nodiscard]] bool ValidMeshRemeshSizingLaw(
            const SandboxEditorMeshRemeshSizingLaw sizingLaw) noexcept
        {
            return std::find(kMeshRemeshSizingLaws.begin(),
                             kMeshRemeshSizingLaws.end(),
                             sizingLaw) != kMeshRemeshSizingLaws.end();
        }

        [[nodiscard]] bool ValidMeshSubdivideOperator(
            const SandboxEditorMeshSubdivideOperator op) noexcept
        {
            return std::find(kMeshSubdivideOperators.begin(),
                             kMeshSubdivideOperators.end(),
                             op) != kMeshSubdivideOperators.end();
        }

        [[nodiscard]] bool ValidMeshSimplifyMetric(
            const SandboxEditorMeshSimplifyMetric metric) noexcept
        {
            return std::find(kMeshSimplifyMetrics.begin(),
                             kMeshSimplifyMetrics.end(),
                             metric) != kMeshSimplifyMetrics.end();
        }

        struct MeshTopologySourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            SandboxEditorCommandStatus Status{
                SandboxEditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == SandboxEditorCommandStatus::Applied;
            }
        };

        [[nodiscard]] MeshTopologySourceResult BuildHalfedgeMeshForTopologyEdit(
            const GS::ConstSourceView& view,
            std::string_view operationName)
        {
            MeshDenoiseSourceResult source = BuildHalfedgeMeshForDenoise(view);
            MeshTopologySourceResult result{};
            result.Status = source.Status;
            result.Error = source.Error;
            if (source.Succeeded())
            {
                result.Mesh = std::move(source.Mesh);
                return result;
            }

            result.Diagnostic = std::string{operationName};
            if (source.Status ==
                SandboxEditorCommandStatus::UnsupportedGeometryDomain)
            {
                result.Diagnostic +=
                    " requires selected mesh GeometrySources.";
            }
            else if (source.Status ==
                     SandboxEditorCommandStatus::InvalidProcessingParameters)
            {
                result.Diagnostic +=
                    " requires a non-empty finite mesh with valid topology.";
            }
            else
            {
                result.Diagnostic +=
                    " could not build a halfedge mesh from GeometrySources.";
            }
            if (!source.Diagnostic.empty())
            {
                result.Diagnostic += " ";
                result.Diagnostic += source.Diagnostic;
            }
            return result;
        }

        void MarkMeshTopologyReplacementDirty(
            entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            Dirty::MarkEdgeTopologyDirty(raw, entity);
            Dirty::MarkFaceTopologyDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyMeshTopologyState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            Geometry::HalfedgeMesh::Mesh published = mesh;
            if (published.HasGarbage())
                published.GarbageCollection();
            GS::PopulateFromMesh(raw, *entity, published);
            MarkMeshTopologyReplacementDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitMeshTopologyReplacement(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            const char* label,
            Geometry::HalfedgeMesh::Mesh before,
            Geometry::HalfedgeMesh::Mesh after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = label,
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyMeshTopologyState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyMeshTopologyState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyMeshTopologyState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        // UI-027: outlier removal changes the point count, so the published
        // point GeometrySources must be fully rebuilt (mirrors the mesh
        // topology-replacement path but for point clouds). A full re-upload is
        // requested because the count changed.
        void MarkPointCloudReplacementDirty(
            entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            Dirty::MarkGpuDirty(raw, entity);
            Dirty::MarkVertexPositionsDirty(raw, entity);
            Dirty::MarkVertexAttributesDirty(raw, entity);
            Dirty::MarkVertexNormalsDirty(raw, entity);
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyPointCloudPointState(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const Geometry::PointCloud::Cloud& cloud)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const std::optional<ECS::EntityHandle> entity =
                ResolveStableEntity(raw, stableEntityId);
            if (!entity.has_value())
                return EditorCommandHistoryStatus::StaleEntity;

            // PopulateFromCloud takes a mutable cloud (it may garbage-collect);
            // operate on an owned copy so the captured undo/redo state is not
            // mutated.
            Geometry::PointCloud::Cloud published = cloud;
            GS::PopulateFromCloud(raw, *entity, published);
            MarkPointCloudReplacementDirty(raw, *entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] bool MirrorGeometrySourcePositionsToPointCloudStorage(
            Geometry::PropertySet& properties)
        {
            const auto positions =
                properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
            if (!positions || positions.Vector().size() != properties.Size())
                return false;

            // GeometrySources uses `v:position`; Geometry.PointCloud::Cloud
            // uses `v:point`. Seed the cloud slot before borrowing the property
            // set so point-cloud algorithms see the promoted source positions.
            auto points = properties.GetOrAdd<glm::vec3>(
                "v:point",
                glm::vec3{0.0f});
            if (!points)
                return false;
            points.Vector() = positions.Vector();
            return true;
        }

        [[nodiscard]] SandboxEditorCommandStatus CommitPointCloudReplacement(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            const char* label,
            Geometry::PointCloud::Cloud before,
            Geometry::PointCloud::Cloud after)
        {
            if (context.CommandHistory != nullptr)
            {
                ECS::Scene::Registry* scene = context.Scene;
                const EditorCommandHistoryResult history =
                    context.CommandHistory->Execute(
                        EditorCommandRecord{
                            .Label = label,
                            .Redo =
                                [scene, stableEntityId, after]()
                                {
                                    return ApplyPointCloudPointState(
                                        scene,
                                        stableEntityId,
                                        after);
                                },
                            .Undo =
                                [scene, stableEntityId, before]()
                                {
                                    return ApplyPointCloudPointState(
                                        scene,
                                        stableEntityId,
                                        before);
                                },
                            .Dirtying = true,
                        });
                return ToSandboxEditorCommandStatus(history.Status);
            }

            return ToSandboxEditorCommandStatus(
                ApplyPointCloudPointState(
                    context.Scene,
                    stableEntityId,
                    after));
        }

        [[nodiscard]] const char* DebugNameForOutlierRemovalStatus(
            const Geometry::PointCloud::OutlierRemovalStatus status) noexcept
        {
            using Status = Geometry::PointCloud::OutlierRemovalStatus;
            switch (status)
            {
            case Status::Success:
                return "Success";
            case Status::EmptyInput:
                return "EmptyInput";
            case Status::InsufficientPoints:
                return "InsufficientPoints";
            case Status::InvalidParameters:
                return "InvalidParameters";
            case Status::BuildFailed:
                return "BuildFailed";
            }
            return "Unknown";
        }

        [[nodiscard]] AdaptiveRemesh::SizingLaw ToAdaptiveSizingLaw(
            const SandboxEditorMeshRemeshSizingLaw sizingLaw) noexcept
        {
            switch (sizingLaw)
            {
            case SandboxEditorMeshRemeshSizingLaw::MeanCurvature:
                return AdaptiveRemesh::SizingLaw::MeanCurvature;
            case SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin:
                return AdaptiveRemesh::SizingLaw::ErrorBoundedTaubin;
            }
            return AdaptiveRemesh::SizingLaw::MeanCurvature;
        }

        void CopyRemeshCounters(
            const Geometry::RemeshingOperationResult& source,
            SandboxEditorMeshRemeshResult& target)
        {
            target.IterationsPerformed =
                static_cast<std::uint32_t>(source.IterationsPerformed);
            target.OutputVertexCount = source.FinalVertexCount;
            target.OutputFaceCount = source.FinalFaceCount;
            target.SplitCount = source.SplitCount;
            target.CollapseCount = source.CollapseCount;
            target.FlipCount = source.FlipCount;
        }

        [[nodiscard]] std::string BuildMeshRemeshSuccessMessage(
            const SandboxEditorMeshRemeshResult& result)
        {
            std::string message = "Mesh remesh completed (mode=";
            message += DebugNameForSandboxEditorMeshRemeshMode(result.Mode);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ").";
            return message;
        }

        [[nodiscard]] std::string BuildMeshSubdivideSuccessMessage(
            const SandboxEditorMeshSubdivideResult& result)
        {
            std::string message = "Mesh subdivide completed (operator=";
            message += DebugNameForSandboxEditorMeshSubdivideOperator(
                result.Operator);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", iterations=";
            message += std::to_string(result.IterationsPerformed);
            message += ").";
            return message;
        }

        [[nodiscard]] std::string BuildMeshSimplifySuccessMessage(
            const SandboxEditorMeshSimplifyResult& result)
        {
            std::string message = "Mesh simplify completed (metric=";
            message += DebugNameForSandboxEditorMeshSimplifyMetric(
                result.Metric);
            message += ", inputFaces=";
            message += std::to_string(result.InputFaceCount);
            message += ", outputFaces=";
            message += std::to_string(result.OutputFaceCount);
            message += ", collapses=";
            message += std::to_string(result.CollapseCount);
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
            model.MeshDenoiseAvailable =
                context.MeshDenoiseKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
            model.MeshCurvatureAvailable =
                context.MeshCurvatureKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
            model.MeshCurvatureDirectionsAvailable =
                model.MeshCurvatureAvailable &&
                context.MeshCurvatureDirectionsAvailable;
            model.MeshRemeshUniformAvailable =
                context.MeshRemeshUniformKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshRemeshAdaptiveAvailable =
                context.MeshRemeshAdaptiveKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshRemeshAvailable =
                model.MeshRemeshUniformAvailable ||
                model.MeshRemeshAdaptiveAvailable;
            model.MeshRemeshProjectToSurfaceAvailable =
                model.MeshRemeshAvailable &&
                context.MeshRemeshProjectToSurfaceAvailable;
            model.MeshRemeshErrorBoundedSizingAvailable =
                model.MeshRemeshAdaptiveAvailable &&
                context.MeshRemeshErrorBoundedSizingAvailable;
            model.MeshSubdivideLoopAvailable =
                context.MeshSubdivideLoopKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshSubdivideCatmullClarkAvailable =
                context.MeshSubdivideCatmullClarkKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshSubdivideSqrt3Available =
                context.MeshSubdivideSqrt3KernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh;
            model.MeshSubdivideAvailable =
                model.MeshSubdivideLoopAvailable ||
                model.MeshSubdivideCatmullClarkAvailable ||
                model.MeshSubdivideSqrt3Available;
            model.MeshSubdivideLoopFeatureEdgesAvailable =
                model.MeshSubdivideLoopAvailable &&
                context.MeshSubdivideLoopFeatureEdgesAvailable;
            model.MeshSimplifyAvailable =
                context.MeshSimplifyKernelAvailable &&
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
            model.MeshVertexNormalsAvailable =
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
            model.GraphVertexNormalsAvailable =
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::GraphVertices);
            model.PointCloudVertexNormalsAvailable =
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::PointCloudPoints);
            model.PointCloudOutlierRemovalAvailable =
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::PointCloudPoints);
            model.PointCloudProgressivePoissonAvailable =
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::PointCloudPoints);
            model.MeshProgressivePoissonAvailable =
                model.Capabilities.HasEditableSurfaceMesh &&
                HasAnySandboxEditorGeometryProcessingDomain(
                    model.Capabilities.Domains,
                    SandboxEditorGeometryProcessingDomain::MeshVertices);
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
            if (context.LastMeshDenoiseResult != nullptr)
            {
                model.LastMeshDenoiseResult =
                    *context.LastMeshDenoiseResult;
                if (!context.LastMeshDenoiseResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshDenoiseResult->Message.empty()
                            ? "Last mesh denoise command failed."
                            : context.LastMeshDenoiseResult->Message);
                }
            }
            if (context.LastMeshCurvatureResult != nullptr)
            {
                model.LastMeshCurvatureResult =
                    *context.LastMeshCurvatureResult;
                if (!context.LastMeshCurvatureResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshCurvatureResult->Message.empty()
                            ? "Last mesh curvature command failed."
                            : context.LastMeshCurvatureResult->Message);
                }
            }
            if (context.LastMeshRemeshResult != nullptr)
            {
                model.LastMeshRemeshResult =
                    *context.LastMeshRemeshResult;
                if (!context.LastMeshRemeshResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshRemeshResult->Message.empty()
                            ? "Last mesh remesh command failed."
                            : context.LastMeshRemeshResult->Message);
                }
            }
            if (context.LastMeshSubdivideResult != nullptr)
            {
                model.LastMeshSubdivideResult =
                    *context.LastMeshSubdivideResult;
                if (!context.LastMeshSubdivideResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshSubdivideResult->Message.empty()
                            ? "Last mesh subdivide command failed."
                            : context.LastMeshSubdivideResult->Message);
                }
            }
            if (context.LastMeshSimplifyResult != nullptr)
            {
                model.LastMeshSimplifyResult =
                    *context.LastMeshSimplifyResult;
                if (!context.LastMeshSimplifyResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshSimplifyResult->Message.empty()
                            ? "Last mesh simplify command failed."
                            : context.LastMeshSimplifyResult->Message);
                }
            }
            if (context.LastMeshVertexNormalsResult != nullptr)
            {
                model.LastMeshVertexNormalsResult =
                    *context.LastMeshVertexNormalsResult;
                if (!context.LastMeshVertexNormalsResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastMeshVertexNormalsResult->Message.empty()
                            ? "Last mesh vertex-normal command failed."
                            : context.LastMeshVertexNormalsResult->Message);
                }
            }
            if (context.LastGraphVertexNormalsResult != nullptr)
            {
                model.LastGraphVertexNormalsResult =
                    *context.LastGraphVertexNormalsResult;
                if (!context.LastGraphVertexNormalsResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastGraphVertexNormalsResult->Message.empty()
                            ? "Last graph vertex-normal command failed."
                            : context.LastGraphVertexNormalsResult->Message);
                }
            }
            if (context.LastPointCloudVertexNormalsResult != nullptr)
            {
                model.LastPointCloudVertexNormalsResult =
                    *context.LastPointCloudVertexNormalsResult;
                if (!context.LastPointCloudVertexNormalsResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastPointCloudVertexNormalsResult->Message.empty()
                            ? "Last point-cloud vertex-normal command failed."
                            : context.LastPointCloudVertexNormalsResult->Message);
                }
            }
            if (context.LastPointCloudOutlierRemovalResult != nullptr)
            {
                model.LastPointCloudOutlierRemovalResult =
                    *context.LastPointCloudOutlierRemovalResult;
                if (!context.LastPointCloudOutlierRemovalResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastPointCloudOutlierRemovalResult->Message.empty()
                            ? "Last point-cloud outlier-removal command failed."
                            : context.LastPointCloudOutlierRemovalResult->Message);
                }
            }
            if (context.LastProgressivePoissonResult != nullptr)
            {
                model.LastProgressivePoissonResult =
                    *context.LastProgressivePoissonResult;
                if (!context.LastProgressivePoissonResult->Succeeded())
                {
                    AddDiagnostic(
                        model.Diagnostics,
                        SandboxEditorDiagnosticCode::GeometryProcessingFailed,
                        context.LastProgressivePoissonResult->Message.empty()
                            ? "Last progressive-Poisson command failed."
                            : context.LastProgressivePoissonResult->Message);
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
            model.PropertyCatalog =
                BuildPropertyCatalogModel(context, raw, *selected);
            model.Progressive =
                BuildProgressiveRenderDataModel(context, raw, *selected);
            model.BoundState =
                BuildBoundRenderStateModel(
                    model.PropertyCatalog,
                    model.Progressive,
                    model.RenderHints,
                    model.Geometry,
                    model.Entity.StableEntityId);
            model.TextureBake =
                BuildTextureBakeControlsModel(
                    context,
                    GS::BuildConstView(raw, *selected),
                    model.PropertyCatalog,
                    model.Entity.StableEntityId);
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

        [[nodiscard]] double QueueElapsedSeconds(
            const RuntimeAssetImportQueueEntry& entry,
            const RuntimeAssetImportQueueTimePoint now) noexcept
        {
            const RuntimeAssetImportQueueTimePoint end =
                entry.FinishedAt.value_or(now);
            if (end <= entry.EnqueuedAt)
            {
                return 0.0;
            }
            return std::chrono::duration<double>(end - entry.EnqueuedAt).count();
        }

        [[nodiscard]] SandboxEditorAssetImportQueueRow
        BuildAssetImportQueueRow(
            const RuntimeAssetImportQueueEntry& entry,
            const RuntimeAssetImportQueueTimePoint now)
        {
            SandboxEditorAssetImportQueueRow row{};
            row.Operation = entry.Operation;
            row.Sequence = entry.Sequence;
            row.Source = entry.Source;
            row.SourcePath = entry.SourcePath;
            row.PathBasename = entry.PathBasename.empty()
                ? entry.SourcePath
                : entry.PathBasename;
            row.PayloadKind = entry.PayloadKind;
            row.Asset = entry.Asset;
            row.Stage = entry.Stage;
            row.TerminalStatus = entry.TerminalStatus;
            row.ProgressDeterminate = entry.ProgressDeterminate;
            row.NormalizedProgress = std::clamp(entry.NormalizedProgress, 0.0f, 1.0f);
            row.StageText = entry.StageText.empty()
                ? DebugNameForRuntimeAssetImportQueueStage(entry.Stage)
                : entry.StageText;
            row.DiagnosticText = entry.DiagnosticText;
            row.ElapsedSeconds = QueueElapsedSeconds(entry, now);
            row.CanCancel = entry.CanCancel;
            row.CancelDisabledReason = entry.CancelDisabledReason;
            return row;
        }

        [[nodiscard]] SandboxEditorAssetImportQueueModel
        BuildAssetImportQueueModel(const SandboxEditorContext& context)
        {
            SandboxEditorAssetImportQueueModel model{};
            model.ActiveCount = context.AssetImportQueue.ActiveCount;
            model.TerminalCount = context.AssetImportQueue.TerminalCount;
            model.ClearCompletedAvailable =
                context.AssetImportQueueCommands.ClearAvailable();
            model.CanClearCompleted =
                model.ClearCompletedAvailable &&
                context.AssetImportQueue.CanClearCompleted;
            model.ClearCompletedDisabledReason =
                context.AssetImportQueue.ClearCompletedDisabledReason;

            const RuntimeAssetImportQueueTimePoint now =
                std::chrono::steady_clock::now();
            model.Rows.reserve(context.AssetImportQueue.Entries.size());
            for (const RuntimeAssetImportQueueEntry& entry :
                 context.AssetImportQueue.Entries)
            {
                model.Rows.push_back(BuildAssetImportQueueRow(entry, now));
            }

            if (model.Rows.empty())
            {
                model.StatusText = "No asset imports are queued.";
            }
            else
            {
                model.StatusText =
                    "AssetIO queue: active=" +
                    std::to_string(model.ActiveCount) +
                    " terminal=" +
                    std::to_string(model.TerminalCount) + ".";
            }

            if (!model.ClearCompletedAvailable)
            {
                AddDiagnostic(model.Diagnostics,
                              SandboxEditorDiagnosticCode::AssetImportUnavailable,
                              "Asset import queue commands are unavailable.");
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
            const SandboxEditorContext& context,
            const SandboxEditorVisualizationTarget target =
                SandboxEditorVisualizationTarget::Entity)
        {
            SandboxEditorVisualizationModel model{};
            model.GeometryDomainControlsAvailable = context.VisualizationCommandsAvailable;
            model.AdapterBindingControlsAvailable =
                context.VisualizationCommandsAvailable &&
                context.VisualizationAdapterBindings.Available();
            model.Target = target;
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
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(raw, *selected);
            model.SelectedDomain = sourceView.ActiveDomain;
            model.TargetAvailable =
                VisualizationTargetAvailableForView(availability, target);
            model.Properties =
                target == SandboxEditorVisualizationTarget::Entity
                    ? BuildVisualizationProperties(availability)
                    : [&availability, target]()
                      {
                          std::vector<SandboxEditorVisualizationPropertyInfo> out{};
                          AppendVisualizationPropertiesForTarget(
                              out,
                              availability,
                              target);
                          return out;
                      }();

            if (const auto* binding =
                    raw.try_get<ECSC::SpatialDebugBinding>(*selected);
                binding != nullptr)
            {
                model.SpatialDebug = FromSpatialDebugBinding(*binding);
            }

            model.Visualization =
                BuildVisualizationConfigModelForTarget(raw, *selected, target);
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
                .Device = &engine.GetDevice(),
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
                                .GeneratedTextureAssetsCreated =
                                    imported->GeneratedTextureAssetsCreated,
                                .TextureUploadRequests =
                                    imported->TextureUploadRequests,
                                .GeneratedTextureUploadRequests =
                                    imported->GeneratedTextureUploadRequests,
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
                .AssetImportQueueCommands = SandboxEditorAssetImportQueueCommandSurface{
                    .ClearCompleted =
                        [&engine]()
                        {
                            return engine.ClearCompletedAssetImports();
                        },
                    .Cancel =
                        [&engine](const RuntimeAssetIngestHandle operation)
                        {
                            return engine.CancelAssetImport(operation);
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
                .AssetImportQueue = engine.GetAssetImportQueueSnapshot(),
                .RenderGraphStats = &engine.GetRenderer().GetLastRenderGraphStats(),
                .RenderRecipeRuntimeState = &engine.GetRenderRecipeState(),
                .PreviewRenderRecipeDocument =
                    [&engine](const std::string& document,
                              const std::string& sourceId)
                    {
                        return engine.PreviewRenderRecipeConfigDocument(
                            document,
                            sourceId);
                    },
                .ApplyRenderRecipePreview =
                    [&engine](const Graphics::RenderRecipeConfigLoadResult& loadResult)
                    {
                        return engine.ApplyRenderRecipeConfigPreview(
                            loadResult,
                            RuntimeRenderRecipeActivationSource::Editor);
                    },
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

        [[nodiscard]] std::string ProgressOverlayText(
            const SandboxEditorAssetImportQueueRow& row)
        {
            if (!row.ProgressDeterminate)
            {
                return row.StageText.empty() ? "active" : row.StageText;
            }
            const int percent = static_cast<int>(
                std::round(std::clamp(row.NormalizedProgress, 0.0f, 1.0f) * 100.0f));
            return std::to_string(percent) + "%";
        }

        void DrawAssetImportQueue(
            const SandboxEditorAssetImportQueueModel& model,
            const SandboxEditorContext* context)
        {
            ImGui::SeparatorText("AssetIO Queue");
            ImGui::TextWrapped("%s", model.StatusText.c_str());

            const bool clearAvailable =
                model.CanClearCompleted &&
                context != nullptr &&
                context->AssetImportQueueCommands.ClearAvailable();
            if (!clearAvailable)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Clear completed") && clearAvailable)
            {
                (void)context->AssetImportQueueCommands.ClearCompleted();
            }
            if (!clearAvailable)
            {
                ImGui::EndDisabled();
                if (!model.ClearCompletedDisabledReason.empty())
                {
                    ImGui::TextDisabled(
                        "%s",
                        model.ClearCompletedDisabledReason.c_str());
                }
            }

            if (model.Rows.empty())
            {
                ImGui::TextDisabled("No asset import rows.");
                DrawDiagnostics(model.Diagnostics);
                return;
            }

            constexpr ImGuiTableFlags tableFlags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("AssetIOQueueTable", 8, tableFlags))
            {
                ImGui::TableSetupColumn("ID");
                ImGui::TableSetupColumn("Payload");
                ImGui::TableSetupColumn("Path");
                ImGui::TableSetupColumn("Stage");
                ImGui::TableSetupColumn("Progress");
                ImGui::TableSetupColumn("Elapsed");
                ImGui::TableSetupColumn("Diagnostic");
                ImGui::TableSetupColumn("Cancel");
                ImGui::TableHeadersRow();

                for (const SandboxEditorAssetImportQueueRow& row : model.Rows)
                {
                    ImGui::PushID(static_cast<int>(row.Sequence));
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu",
                                static_cast<unsigned long long>(row.Sequence));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        A::DebugNameForAssetPayloadKind(row.PayloadKind));

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(row.PathBasename.c_str());

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(row.StageText.c_str());

                    ImGui::TableSetColumnIndex(4);
                    const std::string overlay = ProgressOverlayText(row);
                    ImGui::ProgressBar(
                        row.ProgressDeterminate ? row.NormalizedProgress : 0.0f,
                        ImVec2(-1.0f, 0.0f),
                        overlay.c_str());

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.2fs", row.ElapsedSeconds);

                    ImGui::TableSetColumnIndex(6);
                    if (row.DiagnosticText.empty())
                    {
                        ImGui::TextDisabled("-");
                    }
                    else
                    {
                        ImGui::TextWrapped("%s", row.DiagnosticText.c_str());
                    }

                    ImGui::TableSetColumnIndex(7);
                    const bool cancelAvailable =
                        row.CanCancel &&
                        context != nullptr &&
                        context->AssetImportQueueCommands.CancelAvailable();
                    if (!cancelAvailable)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::Button("Cancel") && cancelAvailable)
                    {
                        (void)context->AssetImportQueueCommands.Cancel(row.Operation);
                    }
                    if (!cancelAvailable)
                    {
                        ImGui::EndDisabled();
                        if (!row.CancelDisabledReason.empty())
                        {
                            ImGui::TextDisabled("%s",
                                                row.CancelDisabledReason.c_str());
                        }
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            DrawDiagnostics(model.Diagnostics);
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
            std::array<bool, 15>* domainWindowOpen)
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
                    "Properties",
                    nullptr,
                    &(*domainWindowOpen)[DomainWindowSlotIndex(kind, DomainWindowSection::Properties)]);
                ImGui::MenuItem(
                    "Visualization",
                    nullptr,
                    &(*domainWindowOpen)[DomainWindowSlotIndex(kind, DomainWindowSection::Visualization)]);
                ImGui::MenuItem(
                    "Selection details",
                    nullptr,
                    &(*domainWindowOpen)[DomainWindowSlotIndex(kind, DomainWindowSection::Selection)]);
                if (ImGui::BeginMenu("Processing"))
                {
                    bool& processingOpen =
                        (*domainWindowOpen)[DomainWindowSlotIndex(
                            kind,
                            DomainWindowSection::Processing)];
                    const std::vector<SandboxEditorGeometryProcessingMenuItem>
                        menuItems =
                            GetSandboxEditorGeometryProcessingMenuItems(kind);
                    const bool hasDenoiseLeaf =
                        std::any_of(
                            menuItems.begin(),
                            menuItems.end(),
                            [](const SandboxEditorGeometryProcessingMenuItem& item)
                            {
                                return item.HasDenoiseMethod;
                            });
                    if (hasDenoiseLeaf && ImGui::MenuItem("Denoise"))
                    {
                        processingOpen = true;
                    }
                    const bool hasCurvatureLeaf =
                        std::any_of(
                            menuItems.begin(),
                            menuItems.end(),
                            [](const SandboxEditorGeometryProcessingMenuItem& item)
                            {
                                return item.HasCurvatureMethod;
                            });
                    if (hasCurvatureLeaf && ImGui::MenuItem("Curvature"))
                    {
                        processingOpen = true;
                    }
                    const bool hasRemeshLeaf =
                        std::any_of(
                            menuItems.begin(),
                            menuItems.end(),
                            [](const SandboxEditorGeometryProcessingMenuItem& item)
                            {
                                return item.HasRemeshMethod;
                            });
                    if (hasRemeshLeaf && ImGui::MenuItem("Remesh"))
                    {
                        processingOpen = true;
                    }
                    const bool hasSubdivideLeaf =
                        std::any_of(
                            menuItems.begin(),
                            menuItems.end(),
                            [](const SandboxEditorGeometryProcessingMenuItem& item)
                            {
                                return item.HasSubdivideMethod;
                            });
                    if (hasSubdivideLeaf && ImGui::MenuItem("Subdivide"))
                    {
                        processingOpen = true;
                    }
                    const bool hasSimplifyLeaf =
                        std::any_of(
                            menuItems.begin(),
                            menuItems.end(),
                            [](const SandboxEditorGeometryProcessingMenuItem& item)
                            {
                                return item.HasSimplifyMethod;
                            });
                    if (hasSimplifyLeaf && ImGui::MenuItem("Simplify"))
                    {
                        processingOpen = true;
                    }
                    for (const SandboxEditorGeometryProcessingMenuItem& item :
                         menuItems)
                    {
                        if (item.HasNormalsMethod)
                        {
                            if (ImGui::BeginMenu(item.Label))
                            {
                                if (item.HasNormalsMethod &&
                                    ImGui::MenuItem("Normals"))
                                {
                                    processingOpen = true;
                                }
                                ImGui::EndMenu();
                            }
                        }
                        else if (ImGui::MenuItem(item.Label))
                        {
                            processingOpen = true;
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            else
            {
                (void)ImGui::MenuItem("Render hints", nullptr, false, false);
                (void)ImGui::MenuItem("Properties", nullptr, false, false);
                (void)ImGui::MenuItem("Visualization", nullptr, false, false);
                (void)ImGui::MenuItem("Selection details", nullptr, false, false);
                if (ImGui::BeginMenu("Processing", false))
                    ImGui::EndMenu();
            }

            if (!menuEnabled)
                ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        void DrawPanelWindowMenu(
            std::array<bool, Detail::kSandboxEditorPanelWindowCount>*
                panelWindowOpen)
        {
            if (!ImGui::BeginMenu("View"))
                return;

            const bool menuEnabled = panelWindowOpen != nullptr;
            if (!menuEnabled)
                ImGui::BeginDisabled();

            if (panelWindowOpen != nullptr)
            {
                for (const SandboxEditorPanelWindowKind kind :
                     kSandboxEditorPanelWindows)
                {
                    ImGui::MenuItem(
                        PanelWindowTitle(kind),
                        nullptr,
                        &(*panelWindowOpen)[PanelWindowIndex(kind)]);
                }
            }
            else
            {
                for (const SandboxEditorPanelWindowKind kind :
                     kSandboxEditorPanelWindows)
                {
                    (void)ImGui::MenuItem(PanelWindowTitle(kind),
                                          nullptr,
                                          false,
                                          false);
                }
            }

            if (!menuEnabled)
                ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        void DrawMainMenuBar(
            std::array<bool, Detail::kSandboxEditorPanelWindowCount>*
                panelWindowOpen,
            std::array<bool, 15>* domainWindowOpen)
        {
            if (!ImGui::BeginMainMenuBar())
                return;
            DrawPanelWindowMenu(panelWindowOpen);
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

        void DrawPropertyCatalogRows(
            const SandboxEditorPropertyCatalogModel& catalog)
        {
            ImGui::Text("Properties: %zu", catalog.Rows.size());
            if (catalog.Rows.empty())
            {
                ImGui::TextDisabled("No geometry properties.");
                return;
            }

            constexpr ImGuiTableFlags tableFlags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("PropertyCatalog", 7, tableFlags))
            {
                ImGui::TableSetupColumn("Domain");
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("Tags");
                ImGui::TableSetupColumn("Preview");
                ImGui::TableSetupColumn("Reason");
                ImGui::TableHeadersRow();

                for (const SandboxEditorPropertyCatalogRow& row : catalog.Rows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(
                        DebugNameForSandboxEditorPropertyCatalogDomain(row.Domain));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(row.Name.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s/%u",
                                DebugNameForSandboxEditorPropertyCatalogValueKind(
                                    row.ValueKind),
                                row.ComponentCount);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%zu", row.ElementCount);
                    ImGui::TableSetColumnIndex(4);
                    std::string tags{};
                    if (row.Bindable)
                        tags += "bindable ";
                    if (row.Internal)
                        tags += "internal ";
                    if (row.Connectivity)
                        tags += "connectivity ";
                    if (row.Generated)
                        tags += "generated ";
                    ImGui::TextUnformatted(tags.empty() ? "-" : tags.c_str());
                    ImGui::TableSetColumnIndex(5);
                    if (row.Preview.HasValue)
                    {
                        ImGui::Text("[%zu] %s",
                                    row.Preview.ElementIndex,
                                    row.Preview.Text.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextDisabled("%s",
                                        row.UnsupportedReason.empty()
                                            ? "-"
                                            : row.UnsupportedReason.c_str());
                }
                ImGui::EndTable();
            }
        }

        void DrawPropertyBindingTargets(
            const SandboxEditorPropertyCatalogModel& catalog)
        {
            if (catalog.BindingTargets.empty())
                return;

            ImGui::SeparatorText("Binding targets");
            for (std::size_t i = 0u; i < catalog.BindingTargets.size(); ++i)
            {
                const SandboxEditorPropertyBindingTargetModel& target =
                    catalog.BindingTargets[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s / %s / %s requires %s %zu",
                            std::string(ToString(target.Lane)).c_str(),
                            target.PresentationKey.c_str(),
                            std::string(ToString(target.Semantic)).c_str(),
                            std::string(ToString(target.ExpectedValueKind)).c_str(),
                            target.ExpectedElementCount);
                for (const SandboxEditorProgressivePropertyOptionModel& option :
                     target.Options)
                {
                    if (option.Compatible)
                    {
                        ImGui::BulletText("%s",
                                          option.Descriptor.PropertyName.c_str());
                    }
                    else
                    {
                        ImGui::BulletText("%s",
                                          option.Descriptor.PropertyName.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s",
                                            option.DisabledReason.c_str());
                    }
                }
                ImGui::PopID();
            }
        }

        void DrawVertexChannelBindingTargets(
            const SandboxEditorPropertyCatalogModel& catalog,
            const SandboxEditorContext* context)
        {
            if (catalog.VertexChannelTargets.empty())
                return;

            ImGui::SeparatorText("Vertex channels");
            const bool commandsAvailable =
                context != nullptr && context->Scene != nullptr;
            for (std::size_t i = 0u; i < catalog.VertexChannelTargets.size(); ++i)
            {
                const SandboxEditorVertexChannelBindingTargetModel& target =
                    catalog.VertexChannelTargets[i];
                ImGui::PushID(static_cast<int>(i));

                const char* channelName =
                    DebugNameForVertexChannel(target.Channel);
                const std::string currentLabel =
                    target.HasBinding ? target.Binding.SourceProperty
                                      : std::string{"Default"};
                ImGui::Text("%s", channelName);
                ImGui::SameLine();

                if (!commandsAvailable)
                    ImGui::BeginDisabled();
                if (ImGui::BeginCombo("##VertexChannelBinding",
                                      currentLabel.c_str()))
                {
                    if (ImGui::Selectable("Default", !target.HasBinding) &&
                        commandsAvailable)
                    {
                        (void)ApplySandboxEditorVertexChannelBindingCommand(
                            *context,
                            SandboxEditorVertexChannelBindingCommand{
                                .StableEntityId = catalog.SelectedStableId,
                                .Channel = target.Channel,
                                .EnableBinding = false,
                            });
                    }

                    for (const SandboxEditorVertexChannelBindingOptionModel& option :
                         target.Options)
                    {
                        const bool selected =
                            target.HasBinding &&
                            target.Binding.SourceProperty == option.PropertyName;
                        if (!option.Compatible)
                            ImGui::BeginDisabled();
                        const std::string label =
                            option.PropertyName + " (" +
                            DebugNameForSandboxEditorPropertyCatalogValueKind(
                                option.ValueKind) +
                            ", " + std::to_string(option.ElementCount) + ")";
                        if (ImGui::Selectable(label.c_str(), selected) &&
                            option.Compatible &&
                            commandsAvailable)
                        {
                            (void)ApplySandboxEditorVertexChannelBindingCommand(
                                *context,
                                SandboxEditorVertexChannelBindingCommand{
                                    .StableEntityId = catalog.SelectedStableId,
                                    .Channel = target.Channel,
                                    .EnableBinding = true,
                                    .PropertyName = option.PropertyName,
                                });
                        }
                        if (!option.Compatible)
                        {
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s",
                                                option.DisabledReason.c_str());
                        }
                    }
                    ImGui::EndCombo();
                }
                if (!commandsAvailable)
                    ImGui::EndDisabled();

                if (target.HasBinding && !target.Diagnostic.empty())
                    ImGui::TextDisabled("%s", target.Diagnostic.c_str());
                ImGui::PopID();
            }
        }

        void DrawBoundRenderStateRows(
            const SandboxEditorBoundRenderStateModel& bound)
        {
            ImGui::SeparatorText("Bound render state");
            ImGui::Text("Rows: %zu generation=%llu",
                        bound.Rows.size(),
                        static_cast<unsigned long long>(
                            bound.BindingGeneration));
            if (bound.Rows.empty())
            {
                ImGui::TextDisabled("No bound render state rows.");
                DrawDiagnostics(bound.Diagnostics);
                return;
            }

            constexpr ImGuiTableFlags tableFlags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("BoundRenderState", 8, tableFlags))
            {
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("Lane");
                ImGui::TableSetupColumn("Label");
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Readiness");
                ImGui::TableSetupColumn("Property");
                ImGui::TableSetupColumn("Job");
                ImGui::TableSetupColumn("Diagnostic");
                ImGui::TableHeadersRow();

                for (const SandboxEditorBoundRenderStateRow& row :
                     bound.Rows)
                {
                    const std::string laneText{ToString(row.Lane)};
                    const std::string sourceText =
                        row.SourceDescription.empty()
                            ? std::string{ToString(row.SourceKind)}
                            : row.SourceDescription;
                    const std::string readinessText{ToString(row.Readiness)};
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(
                        DebugNameForSandboxEditorBoundRenderStateRowKind(row.Kind));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(laneText.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(row.Label.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(sourceText.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(readinessText.c_str());
                    ImGui::TableSetColumnIndex(5);
                    if (!row.Property.PropertyName.empty())
                    {
                        ImGui::Text("%s%s",
                                    row.Property.PropertyName.c_str(),
                                    row.HasCatalogMatch ? " catalog" : "");
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableSetColumnIndex(6);
                    if (row.Kind == SandboxEditorBoundRenderStateRowKind::DerivedJob)
                    {
                        ImGui::Text("%s %.2f",
                                    std::string(ToString(row.JobStatus)).c_str(),
                                    row.JobProgress);
                    }
                    else if (row.TextureAsset.IsValid() ||
                             row.AuthoredTexture.IsValid() ||
                             row.GeneratedTexture.IsValid())
                    {
                        ImGui::Text("texture");
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableSetColumnIndex(7);
                    if (!row.Diagnostic.empty())
                        ImGui::TextWrapped("%s", row.Diagnostic.c_str());
                    else if (!row.DisabledReason.empty())
                        ImGui::TextDisabled("%s", row.DisabledReason.c_str());
                    else
                        ImGui::TextDisabled("-");
                }
                ImGui::EndTable();
            }
            DrawDiagnostics(bound.Diagnostics);
        }

        void DrawTextureBakeControls(
            const SandboxEditorTextureBakeControlsModel& model,
            const SandboxEditorContext* context,
            TextureBakeUiState* state)
        {
            std::int32_t fallbackSourceIndex{0};
            std::int32_t fallbackSemanticIndex{0};
            std::int32_t fallbackEncoderIndex{0};
            std::int32_t fallbackWidth{
                static_cast<std::int32_t>(model.DefaultWidth)};
            std::int32_t fallbackHeight{
                static_cast<std::int32_t>(model.DefaultHeight)};
            std::int32_t fallbackUvResolution{1024};
            std::int32_t fallbackUvPadding{2};
            float fallbackUvTexelsPerUnit{0.0f};
            bool fallbackUvForceRegenerate{true};
            bool fallbackUvPreserveAuthored{false};

            std::int32_t& sourceIndex =
                state != nullptr && state->SourceIndex != nullptr
                    ? *state->SourceIndex
                    : fallbackSourceIndex;
            std::int32_t& semanticIndex =
                state != nullptr && state->TargetSemanticIndex != nullptr
                    ? *state->TargetSemanticIndex
                    : fallbackSemanticIndex;
            std::int32_t& encoderIndex =
                state != nullptr && state->EncoderIndex != nullptr
                    ? *state->EncoderIndex
                    : fallbackEncoderIndex;
            std::int32_t& bakeWidth =
                state != nullptr && state->Width != nullptr
                    ? *state->Width
                    : fallbackWidth;
            std::int32_t& bakeHeight =
                state != nullptr && state->Height != nullptr
                    ? *state->Height
                    : fallbackHeight;
            std::int32_t& uvResolution =
                state != nullptr && state->UvResolution != nullptr
                    ? *state->UvResolution
                    : fallbackUvResolution;
            std::int32_t& uvPadding =
                state != nullptr && state->UvPadding != nullptr
                    ? *state->UvPadding
                    : fallbackUvPadding;
            float& uvTexelsPerUnit =
                state != nullptr && state->UvTexelsPerUnit != nullptr
                    ? *state->UvTexelsPerUnit
                    : fallbackUvTexelsPerUnit;
            bool& uvForceRegenerate =
                state != nullptr && state->UvForceRegenerate != nullptr
                    ? *state->UvForceRegenerate
                    : fallbackUvForceRegenerate;
            bool& uvPreserveAuthored =
                state != nullptr && state->UvPreserveAuthored != nullptr
                    ? *state->UvPreserveAuthored
                    : fallbackUvPreserveAuthored;

            semanticIndex = std::clamp<std::int32_t>(
                semanticIndex,
                0,
                static_cast<std::int32_t>(kTextureBakeTargetSemantics.size() - 1u));
            encoderIndex = std::clamp<std::int32_t>(
                encoderIndex,
                0,
                static_cast<std::int32_t>(kTextureBakeEncoders.size() - 1u));
            bakeWidth = std::clamp<std::int32_t>(bakeWidth, 1, 8192);
            bakeHeight = std::clamp<std::int32_t>(bakeHeight, 1, 8192);
            uvResolution = std::clamp<std::int32_t>(uvResolution, 1, 16384);
            uvPadding = std::clamp<std::int32_t>(uvPadding, 0, uvResolution - 1);
            if (!std::isfinite(uvTexelsPerUnit) || uvTexelsPerUnit < 0.0f)
                uvTexelsPerUnit = 0.0f;

            ImGui::SeparatorText("UV / texture bake");
            ImGui::Text("UV: %s texcoords=%s count=%zu/%zu",
                        model.Uv.Provenance.c_str(),
                        model.Uv.HasTexcoords ? "yes" : "no",
                        model.Uv.TexcoordCount,
                        model.Uv.VertexCount);
            if (!model.Uv.LastFailure.empty())
                ImGui::TextDisabled("%s", model.Uv.LastFailure.c_str());
            if (!model.Uv.UvRegenerationAvailable)
                ImGui::TextDisabled("%s",
                                    model.Uv.UvRegenerationDisabledReason.c_str());

            ImGui::Checkbox("Force regenerate", &uvForceRegenerate);
            ImGui::SameLine();
            ImGui::Checkbox("Preserve valid authored", &uvPreserveAuthored);
            ImGui::InputInt("UV resolution", &uvResolution);
            ImGui::InputInt("UV padding", &uvPadding);
            ImGui::InputFloat("Texels per unit", &uvTexelsPerUnit, 0.0f, 0.0f, "%.3f");
            uvResolution = std::clamp<std::int32_t>(uvResolution, 1, 16384);
            uvPadding = std::clamp<std::int32_t>(uvPadding, 0, uvResolution - 1);
            if (!std::isfinite(uvTexelsPerUnit) || uvTexelsPerUnit < 0.0f)
                uvTexelsPerUnit = 0.0f;

            const bool canRegenerateUvs =
                model.Uv.UvRegenerationAvailable &&
                context != nullptr &&
                model.SelectedStableId != 0u;
            if (!canRegenerateUvs)
                ImGui::BeginDisabled();
            if (ImGui::Button("Regenerate UVs") && canRegenerateUvs)
            {
                (void)ApplySandboxEditorUvRegenerationCommand(
                    *context,
                    SandboxEditorUvRegenerationCommand{
                        .StableEntityId = model.SelectedStableId,
                        .PreserveValidAuthoredUvs = uvPreserveAuthored,
                        .ForceRegenerate = uvForceRegenerate,
                        .Resolution = static_cast<std::uint32_t>(uvResolution),
                        .Padding = static_cast<std::uint32_t>(uvPadding),
                        .TexelsPerUnit = uvTexelsPerUnit,
                    });
            }
            if (!canRegenerateUvs)
                ImGui::EndDisabled();

            std::vector<std::size_t> bakeableIndices;
            bakeableIndices.reserve(model.Sources.size());
            for (std::size_t i = 0u; i < model.Sources.size(); ++i)
            {
                if (model.Sources[i].Bakeable)
                    bakeableIndices.push_back(i);
            }
            if (bakeableIndices.empty())
                sourceIndex = 0;
            else
                sourceIndex = std::clamp<std::int32_t>(
                    sourceIndex,
                    0,
                    static_cast<std::int32_t>(bakeableIndices.size() - 1u));

            const SandboxEditorTextureBakeSourceRow* selectedSource =
                bakeableIndices.empty()
                    ? nullptr
                    : &model.Sources[bakeableIndices[static_cast<std::size_t>(sourceIndex)]];

            if (ImGui::BeginCombo("Bake source",
                                  selectedSource != nullptr
                                      ? selectedSource->Name.c_str()
                                      : "none"))
            {
                for (std::size_t i = 0u; i < bakeableIndices.size(); ++i)
                {
                    const SandboxEditorTextureBakeSourceRow& row =
                        model.Sources[bakeableIndices[i]];
                    const bool selected = sourceIndex == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(row.Name.c_str(), selected))
                        sourceIndex = static_cast<std::int32_t>(i);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::BeginCombo(
                    "Target",
                    std::string(ToString(kTextureBakeTargetSemantics[
                        static_cast<std::size_t>(semanticIndex)])).c_str()))
            {
                for (std::size_t i = 0u; i < kTextureBakeTargetSemantics.size(); ++i)
                {
                    const std::string label{
                        ToString(kTextureBakeTargetSemantics[i])};
                    const bool selected = semanticIndex == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(label.c_str(), selected))
                        semanticIndex = static_cast<std::int32_t>(i);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::BeginCombo(
                    "Encoder",
                    DebugNameForTextureBakeEncoder(
                        kTextureBakeEncoders[static_cast<std::size_t>(encoderIndex)])))
            {
                for (std::size_t i = 0u; i < kTextureBakeEncoders.size(); ++i)
                {
                    const bool selected = encoderIndex == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForTextureBakeEncoder(kTextureBakeEncoders[i]),
                            selected))
                    {
                        encoderIndex = static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::InputInt("Bake width", &bakeWidth);
            ImGui::InputInt("Bake height", &bakeHeight);
            bakeWidth = std::clamp<std::int32_t>(bakeWidth, 1, 8192);
            bakeHeight = std::clamp<std::int32_t>(bakeHeight, 1, 8192);

            const bool canBake =
                model.CanBake &&
                context != nullptr &&
                selectedSource != nullptr;
            if (!canBake)
                ImGui::BeginDisabled();
            if (ImGui::Button("Bake") && canBake)
            {
                (void)ApplySandboxEditorTextureBakeCommand(
                    *context,
                    SandboxEditorTextureBakeCommand{
                        .StableEntityId = model.SelectedStableId,
                        .TargetSemantic =
                            kTextureBakeTargetSemantics[
                                static_cast<std::size_t>(semanticIndex)],
                        .SourceDomain = selectedSource->BakeDomain,
                        .ExpectedValueKind = selectedSource->ExpectedValueKind,
                        .PropertyName = selectedSource->Name,
                        .Encoder =
                            kTextureBakeEncoders[
                                static_cast<std::size_t>(encoderIndex)],
                        .Width = static_cast<std::uint32_t>(bakeWidth),
                        .Height = static_cast<std::uint32_t>(bakeHeight),
                        .GeneratedKey = selectedSource->Name,
                        .BindGeneratedTexture = true,
                    });
            }
            if (!canBake)
            {
                ImGui::EndDisabled();
                if (!model.DisabledReason.empty())
                    ImGui::TextDisabled("%s", model.DisabledReason.c_str());
            }

            if (ImGui::BeginTable("TextureBakeSources", 5,
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property");
                ImGui::TableSetupColumn("Domain");
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("Bake");
                ImGui::TableSetupColumn("Reason");
                ImGui::TableHeadersRow();

                const std::size_t limit =
                    std::min<std::size_t>(model.Sources.size(), 12u);
                for (std::size_t i = 0u; i < limit; ++i)
                {
                    const SandboxEditorTextureBakeSourceRow& row =
                        model.Sources[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(row.Name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        DebugNameForSandboxEditorPropertyCatalogDomain(
                            row.CatalogDomain));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(
                        DebugNameForSandboxEditorPropertyCatalogValueKind(
                            row.ValueKind));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(row.Bakeable ? "yes" : "no");
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextDisabled("%s",
                                        row.DisabledReason.empty()
                                            ? "-"
                                            : row.DisabledReason.c_str());
                }
                ImGui::EndTable();
            }
            DrawDiagnostics(model.Diagnostics);
        }

        void DrawDomainPropertyWindow(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext* context,
            TextureBakeUiState* textureBakeState)
        {
            DrawDomainWindowHeader(model);
            if (!DomainWindowReady(model))
                return;
            DrawBoundRenderStateRows(model.BoundState);
            DrawTextureBakeControls(model.TextureBake, context, textureBakeState);
            DrawPropertyCatalogRows(model.PropertyCatalog);
            DrawPropertyBindingTargets(model.PropertyCatalog);
            DrawVertexChannelBindingTargets(model.PropertyCatalog, context);
            DrawDiagnostics(model.PropertyCatalog.Diagnostics);
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
                        "Point size", &pointSize, 0.05f, 0.5f, 32.0f) &&
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
            const SandboxEditorVisualizationTarget target,
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
                                .Target = target,
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
                                .Target = target,
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
                                .Target = target,
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

        void DrawUniformVisualizationColorEdit(
            const SandboxEditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const SandboxEditorVisualizationTarget target,
            const bool canEditVisualization)
        {
            if (!visualization.HasConfig ||
                visualization.Source != G::VisualizationConfig::ColorSource::UniformColor)
            {
                return;
            }

            glm::vec4 color = visualization.Color;
            if (ImGui::ColorEdit4("Color##uniform-visualization-color",
                                  &color.x) &&
                canEditVisualization)
            {
                (void)ApplySandboxEditorVisualizationConfigCommand(
                    context,
                    MakeUniformVisualizationConfigCommandFromModel(
                        selectedStableId,
                        visualization,
                        target,
                        color));
            }
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
                model.VisualizationTargetAvailable &&
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
                    MakeUniformVisualizationConfigCommandFromModel(
                        model.SelectedStableId,
                        visualization.Visualization,
                        model.VisualizationTarget,
                        visualization.Visualization.Color));
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear vis") && canEditVisualization)
            {
                (void)ApplySandboxEditorVisualizationConfigCommand(
                    context,
                    SandboxEditorVisualizationConfigCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Target = model.VisualizationTarget,
                        .EnableConfig = false,
                    });
            }

            DrawUniformVisualizationColorEdit(
                visualization.Visualization,
                context,
                model.SelectedStableId,
                model.VisualizationTarget,
                canEditVisualization);

            if (!canEditVisualization)
                ImGui::EndDisabled();

            DrawVisualizationPropertyPresets(
                visualization.Properties,
                context,
                model.SelectedStableId,
                model.VisualizationTarget,
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

        [[nodiscard]] SandboxEditorMeshDenoiseStage MeshDenoiseStageFromIndex(
            const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(kMeshDenoiseStages.size() - 1u));
            return kMeshDenoiseStages[static_cast<std::size_t>(clamped)];
        }

        void DrawMeshDenoiseResultStatus(
            const std::optional<SandboxEditorMeshDenoiseResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last denoise run: none");
                return;
            }

            const SandboxEditorMeshDenoiseResult& result = *lastResult;
            ImGui::Text("Last denoise run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Geometry status: %s",
                        std::string(Smooth::DebugName(result.DenoiseStatus)).c_str());
            ImGui::Text("Stage: %s",
                        DebugNameForSandboxEditorMeshDenoiseStage(result.Stage));
            if (result.Succeeded())
            {
                ImGui::Text("Written: %zu / %zu  moved: %zu  deleted: %zu",
                            result.WrittenCount,
                            result.VertexSlotCount,
                            result.MovedVertexCount,
                            result.SkippedDeletedVertexCount);
                ImGui::Text("Iterations: normals=%u  vertices=%u",
                            result.NormalIterations,
                            result.VertexIterations);
                ImGui::Text("Faces: processed=%zu  degenerate=%zu  nonfinite=%zu  deleted=%zu",
                            result.ProcessedFaceCount,
                            result.DegenerateFaceCount,
                            result.NonFiniteFaceCount,
                            result.SkippedDeletedFaceCount);
                ImGui::Text("Pinned boundary vertices: %zu",
                            result.PinnedBoundaryVertexCount);
                ImGui::Text("Sigma used: spatial=%.6f  range=%.6f",
                            result.SigmaSpatialUsed,
                            result.SigmaRangeUsed);
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawMeshDenoiseControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            MeshDenoiseUiState* denoiseState)
        {
            ImGui::SeparatorText("Denoise");
            if (!processing.MeshDenoiseAvailable)
            {
                ImGui::TextDisabled("Mesh denoise is unavailable for this selection.");
                return;
            }
            if (denoiseState == nullptr ||
                denoiseState->LastResult == nullptr ||
                denoiseState->Stage == nullptr ||
                denoiseState->NormalIterations == nullptr ||
                denoiseState->VertexIterations == nullptr ||
                denoiseState->SigmaSpatial == nullptr ||
                denoiseState->SigmaRange == nullptr ||
                denoiseState->PreserveBoundary == nullptr)
            {
                ImGui::TextDisabled("Mesh denoise controls are not bound.");
                return;
            }

            *denoiseState->Stage = std::clamp(
                *denoiseState->Stage,
                0,
                static_cast<std::int32_t>(kMeshDenoiseStages.size() - 1u));
            const SandboxEditorMeshDenoiseStage stage =
                MeshDenoiseStageFromIndex(*denoiseState->Stage);
            if (ImGui::BeginCombo(
                    "Stage",
                    DebugNameForSandboxEditorMeshDenoiseStage(stage)))
            {
                for (std::size_t i = 0u; i < kMeshDenoiseStages.size(); ++i)
                {
                    const bool selected =
                        *denoiseState->Stage == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForSandboxEditorMeshDenoiseStage(
                                kMeshDenoiseStages[i]),
                            selected))
                    {
                        *denoiseState->Stage = static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            *denoiseState->NormalIterations =
                std::clamp(*denoiseState->NormalIterations, 1, 4096);
            *denoiseState->VertexIterations =
                std::clamp(*denoiseState->VertexIterations, 1, 4096);
            *denoiseState->SigmaSpatial =
                std::clamp(*denoiseState->SigmaSpatial, 0.0f, 1.0e6f);
            *denoiseState->SigmaRange =
                std::clamp(*denoiseState->SigmaRange, 0.0f, 1.0e6f);
            ImGui::DragInt(
                "Normal iterations",
                denoiseState->NormalIterations,
                1.0f,
                1,
                4096);
            ImGui::DragInt(
                "Vertex iterations",
                denoiseState->VertexIterations,
                1.0f,
                1,
                4096);
            ImGui::DragFloat(
                "Sigma spatial",
                denoiseState->SigmaSpatial,
                0.01f,
                0.0f,
                1.0e6f);
            ImGui::DragFloat(
                "Sigma range",
                denoiseState->SigmaRange,
                0.01f,
                0.0f,
                1.0e6f);
            ImGui::Checkbox(
                "Preserve boundary",
                denoiseState->PreserveBoundary);

            if (ImGui::Button("Denoise"))
            {
                *denoiseState->LastResult =
                    ApplySandboxEditorMeshDenoiseCommand(
                        context,
                        SandboxEditorMeshDenoiseCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Stage = MeshDenoiseStageFromIndex(
                                *denoiseState->Stage),
                            .NormalIterations = static_cast<std::uint32_t>(
                                *denoiseState->NormalIterations),
                            .VertexIterations = static_cast<std::uint32_t>(
                                *denoiseState->VertexIterations),
                            .SigmaSpatial = static_cast<double>(
                                *denoiseState->SigmaSpatial),
                            .SigmaRange = static_cast<double>(
                                *denoiseState->SigmaRange),
                            .PreserveBoundary =
                                *denoiseState->PreserveBoundary,
                        });
            }

            const std::optional<SandboxEditorMeshDenoiseResult>& result =
                denoiseState->LastResult->has_value()
                    ? *denoiseState->LastResult
                    : processing.LastMeshDenoiseResult;
            DrawMeshDenoiseResultStatus(result);
        }

        void DrawMeshCurvatureResultStatus(
            const std::optional<SandboxEditorMeshCurvatureResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last curvature run: none");
                return;
            }

            const SandboxEditorMeshCurvatureResult& result = *lastResult;
            ImGui::Text("Last curvature run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Output: %s",
                        DebugNameForSandboxEditorMeshCurvatureOutput(result.Output));
            if (result.Succeeded())
            {
                ImGui::Text("Vertices: %zu  scalar values: %zu  nonfinite scalars: %zu",
                            result.VertexSlotCount,
                            result.ScalarWrittenCount,
                            result.NonFiniteScalarCount);
                ImGui::Text("Directions: %s  values: %zu  nonfinite: %zu",
                            result.DirectionsPublished ? "published" : "not published",
                            result.DirectionWrittenCount,
                            result.NonFiniteDirectionCount);
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawMeshCurvatureControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            MeshCurvatureUiState* curvatureState)
        {
            ImGui::SeparatorText("Curvature");
            if (!processing.MeshCurvatureAvailable)
            {
                ImGui::TextDisabled("Mesh curvature is unavailable for this selection.");
                return;
            }
            if (curvatureState == nullptr ||
                curvatureState->LastResult == nullptr ||
                curvatureState->Output == nullptr ||
                curvatureState->PublishPrincipalDirections == nullptr)
            {
                ImGui::TextDisabled("Mesh curvature controls are not bound.");
                return;
            }

            *curvatureState->Output = std::clamp(
                *curvatureState->Output,
                0,
                static_cast<std::int32_t>(kMeshCurvatureOutputs.size() - 1u));
            const SandboxEditorMeshCurvatureOutput output =
                MeshCurvatureOutputFromIndex(*curvatureState->Output);
            if (ImGui::BeginCombo(
                    "Output",
                    DebugNameForSandboxEditorMeshCurvatureOutput(output)))
            {
                for (std::size_t i = 0u; i < kMeshCurvatureOutputs.size(); ++i)
                {
                    const bool selected =
                        *curvatureState->Output ==
                        static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForSandboxEditorMeshCurvatureOutput(
                                kMeshCurvatureOutputs[i]),
                            selected))
                    {
                        *curvatureState->Output =
                            static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (!processing.MeshCurvatureDirectionsAvailable)
                ImGui::BeginDisabled();
            ImGui::Checkbox(
                "Principal directions",
                curvatureState->PublishPrincipalDirections);
            if (!processing.MeshCurvatureDirectionsAvailable)
                ImGui::EndDisabled();

            if (ImGui::Button("Compute"))
            {
                *curvatureState->LastResult =
                    ApplySandboxEditorMeshCurvatureCommand(
                        context,
                        SandboxEditorMeshCurvatureCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Output = MeshCurvatureOutputFromIndex(
                                *curvatureState->Output),
                            .PublishPrincipalDirections =
                                *curvatureState->PublishPrincipalDirections,
                        });
            }

            const std::optional<SandboxEditorMeshCurvatureResult>& result =
                curvatureState->LastResult->has_value()
                    ? *curvatureState->LastResult
                    : processing.LastMeshCurvatureResult;
            DrawMeshCurvatureResultStatus(result);
        }

        void DrawMeshRemeshResultStatus(
            const std::optional<SandboxEditorMeshRemeshResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last remesh run: none");
                return;
            }

            const SandboxEditorMeshRemeshResult& result = *lastResult;
            ImGui::Text("Last remesh run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Mode: %s  sizing: %s",
                        DebugNameForSandboxEditorMeshRemeshMode(result.Mode),
                        DebugNameForSandboxEditorMeshRemeshSizingLaw(result.SizingLaw));
            if (result.Succeeded())
            {
                ImGui::Text("Vertices: %zu -> %zu  faces: %zu -> %zu",
                            result.InputVertexCount,
                            result.OutputVertexCount,
                            result.InputFaceCount,
                            result.OutputFaceCount);
                ImGui::Text("Iterations: %u / %u  splits=%zu  collapses=%zu  flips=%zu",
                            result.IterationsPerformed,
                            result.IterationsRequested,
                            result.SplitCount,
                            result.CollapseCount,
                            result.FlipCount);
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawMeshRemeshControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            MeshRemeshUiState* remeshState)
        {
            ImGui::SeparatorText("Remesh");
            if (!processing.MeshRemeshAvailable)
            {
                ImGui::TextDisabled("Mesh remesh is unavailable for this selection.");
                return;
            }
            if (remeshState == nullptr ||
                remeshState->LastResult == nullptr ||
                remeshState->Mode == nullptr ||
                remeshState->SizingLaw == nullptr ||
                remeshState->Iterations == nullptr ||
                remeshState->TargetEdgeLength == nullptr ||
                remeshState->ProjectToSurface == nullptr)
            {
                ImGui::TextDisabled("Mesh remesh controls are not bound.");
                return;
            }

            *remeshState->Mode = std::clamp(
                *remeshState->Mode,
                0,
                static_cast<std::int32_t>(kMeshRemeshModes.size() - 1u));
            *remeshState->SizingLaw = std::clamp(
                *remeshState->SizingLaw,
                0,
                static_cast<std::int32_t>(kMeshRemeshSizingLaws.size() - 1u));
            *remeshState->Iterations =
                std::clamp(*remeshState->Iterations, 1, 64);
            *remeshState->TargetEdgeLength =
                std::clamp(*remeshState->TargetEdgeLength, 0.0f, 1.0e6f);

            const SandboxEditorMeshRemeshMode mode =
                MeshRemeshModeFromIndex(*remeshState->Mode);
            if (ImGui::BeginCombo(
                    "Mode",
                    DebugNameForSandboxEditorMeshRemeshMode(mode)))
            {
                for (std::size_t i = 0u; i < kMeshRemeshModes.size(); ++i)
                {
                    const SandboxEditorMeshRemeshMode option =
                        kMeshRemeshModes[i];
                    const bool available =
                        option == SandboxEditorMeshRemeshMode::Uniform
                            ? processing.MeshRemeshUniformAvailable
                            : processing.MeshRemeshAdaptiveAvailable;
                    if (!available)
                        ImGui::BeginDisabled();
                    const bool selected =
                        *remeshState->Mode == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForSandboxEditorMeshRemeshMode(option),
                            selected))
                    {
                        *remeshState->Mode = static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    if (!available)
                        ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }

            ImGui::DragInt("Iterations", remeshState->Iterations, 1.0f, 1, 64);
            ImGui::DragFloat(
                "Target edge length",
                remeshState->TargetEdgeLength,
                0.01f,
                0.0f,
                1.0e6f);

            const bool adaptive =
                mode == SandboxEditorMeshRemeshMode::Adaptive;
            if (!adaptive)
                ImGui::BeginDisabled();
            const SandboxEditorMeshRemeshSizingLaw sizingLaw =
                MeshRemeshSizingLawFromIndex(*remeshState->SizingLaw);
            if (ImGui::BeginCombo(
                    "Sizing law",
                    DebugNameForSandboxEditorMeshRemeshSizingLaw(sizingLaw)))
            {
                for (std::size_t i = 0u; i < kMeshRemeshSizingLaws.size(); ++i)
                {
                    const SandboxEditorMeshRemeshSizingLaw option =
                        kMeshRemeshSizingLaws[i];
                    const bool available =
                        option !=
                            SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin ||
                        processing.MeshRemeshErrorBoundedSizingAvailable;
                    if (!available)
                        ImGui::BeginDisabled();
                    const bool selected =
                        *remeshState->SizingLaw == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForSandboxEditorMeshRemeshSizingLaw(option),
                            selected))
                    {
                        *remeshState->SizingLaw =
                            static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    if (!available)
                        ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            if (!adaptive)
                ImGui::EndDisabled();

            if (!processing.MeshRemeshProjectToSurfaceAvailable)
                ImGui::BeginDisabled();
            ImGui::Checkbox(
                "Project to surface",
                remeshState->ProjectToSurface);
            if (!processing.MeshRemeshProjectToSurfaceAvailable)
                ImGui::EndDisabled();

            const bool modeAvailable =
                mode == SandboxEditorMeshRemeshMode::Uniform
                    ? processing.MeshRemeshUniformAvailable
                    : processing.MeshRemeshAdaptiveAvailable;
            const bool sizingAvailable =
                sizingLaw != SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin ||
                processing.MeshRemeshErrorBoundedSizingAvailable;
            const bool projectionAvailable =
                !*remeshState->ProjectToSurface ||
                processing.MeshRemeshProjectToSurfaceAvailable;
            const bool canRun =
                modeAvailable && sizingAvailable && projectionAvailable;
            if (!canRun)
                ImGui::BeginDisabled();
            if (ImGui::Button("Remesh"))
            {
                *remeshState->LastResult =
                    ApplySandboxEditorMeshRemeshCommand(
                        context,
                        SandboxEditorMeshRemeshCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Mode = mode,
                            .SizingLaw = sizingLaw,
                            .Iterations = static_cast<std::uint32_t>(
                                *remeshState->Iterations),
                            .TargetEdgeLength = static_cast<double>(
                                *remeshState->TargetEdgeLength),
                            .ProjectToSurface =
                                *remeshState->ProjectToSurface,
                        });
            }
            if (!canRun)
                ImGui::EndDisabled();

            const std::optional<SandboxEditorMeshRemeshResult>& result =
                remeshState->LastResult->has_value()
                    ? *remeshState->LastResult
                    : processing.LastMeshRemeshResult;
            DrawMeshRemeshResultStatus(result);
        }

        void DrawMeshSubdivideResultStatus(
            const std::optional<SandboxEditorMeshSubdivideResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last subdivide run: none");
                return;
            }

            const SandboxEditorMeshSubdivideResult& result = *lastResult;
            ImGui::Text("Last subdivide run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Operator: %s",
                        DebugNameForSandboxEditorMeshSubdivideOperator(result.Operator));
            if (result.Succeeded())
            {
                ImGui::Text("Vertices: %zu -> %zu  faces: %zu -> %zu",
                            result.InputVertexCount,
                            result.OutputVertexCount,
                            result.InputFaceCount,
                            result.OutputFaceCount);
                ImGui::Text("Iterations: %u / %u  Loop features: %s",
                            result.IterationsPerformed,
                            result.IterationsRequested,
                            result.PreserveLoopFeatureEdges ? "yes" : "no");
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawMeshSubdivideControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            MeshSubdivideUiState* subdivideState)
        {
            ImGui::SeparatorText("Subdivide");
            if (!processing.MeshSubdivideAvailable)
            {
                ImGui::TextDisabled("Mesh subdivision is unavailable for this selection.");
                return;
            }
            if (subdivideState == nullptr ||
                subdivideState->LastResult == nullptr ||
                subdivideState->Operator == nullptr ||
                subdivideState->Iterations == nullptr ||
                subdivideState->PreserveLoopFeatures == nullptr)
            {
                ImGui::TextDisabled("Mesh subdivision controls are not bound.");
                return;
            }

            *subdivideState->Operator = std::clamp(
                *subdivideState->Operator,
                0,
                static_cast<std::int32_t>(kMeshSubdivideOperators.size() - 1u));
            *subdivideState->Iterations =
                std::clamp(*subdivideState->Iterations, 1, 10);

            const SandboxEditorMeshSubdivideOperator op =
                MeshSubdivideOperatorFromIndex(*subdivideState->Operator);
            if (ImGui::BeginCombo(
                    "Operator",
                    DebugNameForSandboxEditorMeshSubdivideOperator(op)))
            {
                for (std::size_t i = 0u; i < kMeshSubdivideOperators.size(); ++i)
                {
                    const SandboxEditorMeshSubdivideOperator option =
                        kMeshSubdivideOperators[i];
                    const bool available =
                        option == SandboxEditorMeshSubdivideOperator::Loop
                            ? processing.MeshSubdivideLoopAvailable
                            : option ==
                                      SandboxEditorMeshSubdivideOperator::CatmullClark
                                  ? processing.MeshSubdivideCatmullClarkAvailable
                                  : processing.MeshSubdivideSqrt3Available;
                    if (!available)
                        ImGui::BeginDisabled();
                    const bool selected =
                        *subdivideState->Operator ==
                        static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForSandboxEditorMeshSubdivideOperator(option),
                            selected))
                    {
                        *subdivideState->Operator =
                            static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    if (!available)
                        ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }

            ImGui::DragInt(
                "Iterations",
                subdivideState->Iterations,
                1.0f,
                1,
                10);

            const bool loop =
                op == SandboxEditorMeshSubdivideOperator::Loop;
            if (!loop)
                *subdivideState->PreserveLoopFeatures = false;
            if (!loop ||
                !processing.MeshSubdivideLoopFeatureEdgesAvailable)
            {
                ImGui::BeginDisabled();
            }
            ImGui::Checkbox(
                "Preserve Loop features",
                subdivideState->PreserveLoopFeatures);
            if (!loop ||
                !processing.MeshSubdivideLoopFeatureEdgesAvailable)
            {
                ImGui::EndDisabled();
            }

            const bool operatorAvailable =
                op == SandboxEditorMeshSubdivideOperator::Loop
                    ? processing.MeshSubdivideLoopAvailable
                    : op == SandboxEditorMeshSubdivideOperator::CatmullClark
                          ? processing.MeshSubdivideCatmullClarkAvailable
                          : processing.MeshSubdivideSqrt3Available;
            const bool featureAvailable =
                !*subdivideState->PreserveLoopFeatures ||
                processing.MeshSubdivideLoopFeatureEdgesAvailable;
            const bool canRun = operatorAvailable && featureAvailable;
            if (!canRun)
                ImGui::BeginDisabled();
            if (ImGui::Button("Subdivide"))
            {
                *subdivideState->LastResult =
                    ApplySandboxEditorMeshSubdivideCommand(
                        context,
                        SandboxEditorMeshSubdivideCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Operator = op,
                            .Iterations = static_cast<std::uint32_t>(
                                *subdivideState->Iterations),
                            .PreserveLoopFeatureEdges =
                                *subdivideState->PreserveLoopFeatures,
                        });
            }
            if (!canRun)
                ImGui::EndDisabled();

            const std::optional<SandboxEditorMeshSubdivideResult>& result =
                subdivideState->LastResult->has_value()
                    ? *subdivideState->LastResult
                    : processing.LastMeshSubdivideResult;
            DrawMeshSubdivideResultStatus(result);
        }

        void DrawMeshSimplifyResultStatus(
            const std::optional<SandboxEditorMeshSimplifyResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last simplify run: none");
                return;
            }

            const SandboxEditorMeshSimplifyResult& result = *lastResult;
            ImGui::Text("Last simplify run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Metric: %s",
                        DebugNameForSandboxEditorMeshSimplifyMetric(result.Metric));
            if (result.Succeeded())
            {
                ImGui::Text("Vertices: %zu -> %zu  faces: %zu -> %zu",
                            result.InputVertexCount,
                            result.OutputVertexCount,
                            result.InputFaceCount,
                            result.OutputFaceCount);
                ImGui::Text("Collapses: %zu  max error: %.6g",
                            result.CollapseCount,
                            result.MaxCollapseError);
                ImGui::Text("Rejected: topology %zu  quality %zu",
                            result.CollapsesRejectedTopology,
                            result.CollapsesRejectedQuality);
                ImGui::Text("Pinned: sharp features %zu  UV seams %zu",
                            result.SharpFeatureVerticesPinned,
                            result.SeamVerticesPinned);
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawMeshSimplifyControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            MeshSimplifyUiState* simplifyState)
        {
            ImGui::SeparatorText("Simplify");
            if (!processing.MeshSimplifyAvailable)
            {
                ImGui::TextDisabled("Mesh simplification is unavailable for this selection.");
                return;
            }
            if (simplifyState == nullptr ||
                simplifyState->LastResult == nullptr ||
                simplifyState->Metric == nullptr ||
                simplifyState->TargetFaces == nullptr ||
                simplifyState->MaxError == nullptr ||
                simplifyState->PreserveBoundary == nullptr ||
                simplifyState->FeatureAngleThresholdDegrees == nullptr ||
                simplifyState->NormalWeight == nullptr ||
                simplifyState->BoundaryWeight == nullptr ||
                simplifyState->CurvatureWeight == nullptr ||
                simplifyState->PreserveSharpFeatures == nullptr ||
                simplifyState->PreserveUvSeams == nullptr)
            {
                ImGui::TextDisabled("Mesh simplification controls are not bound.");
                return;
            }

            *simplifyState->Metric = std::clamp(
                *simplifyState->Metric,
                0,
                static_cast<std::int32_t>(kMeshSimplifyMetrics.size() - 1u));
            *simplifyState->TargetFaces =
                std::max(*simplifyState->TargetFaces, 0);
            *simplifyState->MaxError =
                std::max(*simplifyState->MaxError, 0.0f);

            const SandboxEditorMeshSimplifyMetric metric =
                MeshSimplifyMetricFromIndex(*simplifyState->Metric);
            if (ImGui::BeginCombo(
                    "Metric",
                    DebugNameForSandboxEditorMeshSimplifyMetric(metric)))
            {
                for (std::size_t i = 0u; i < kMeshSimplifyMetrics.size(); ++i)
                {
                    const SandboxEditorMeshSimplifyMetric option =
                        kMeshSimplifyMetrics[i];
                    const bool selected =
                        *simplifyState->Metric == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForSandboxEditorMeshSimplifyMetric(option),
                            selected))
                    {
                        *simplifyState->Metric = static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::DragInt(
                "Target faces",
                simplifyState->TargetFaces,
                1.0f,
                0,
                1000000000);
            ImGui::DragFloat(
                "Max error (0 = unlimited)",
                simplifyState->MaxError,
                0.001f,
                0.0f,
                1.0e30f,
                "%.6g");
            ImGui::Checkbox("Preserve boundary", simplifyState->PreserveBoundary);

            const bool faQem =
                metric == SandboxEditorMeshSimplifyMetric::FA_QEM;
            if (!faQem)
                ImGui::BeginDisabled();
            if (ImGui::CollapsingHeader("Feature-aware (FA-QEM) weights"))
            {
                ImGui::DragFloat(
                    "Feature angle (deg)",
                    simplifyState->FeatureAngleThresholdDegrees,
                    0.5f,
                    0.0f,
                    180.0f,
                    "%.1f");
                ImGui::DragFloat(
                    "Normal weight",
                    simplifyState->NormalWeight,
                    0.01f,
                    0.0f,
                    1000.0f,
                    "%.3f");
                ImGui::DragFloat(
                    "Boundary weight",
                    simplifyState->BoundaryWeight,
                    0.01f,
                    0.0f,
                    1000.0f,
                    "%.3f");
                ImGui::DragFloat(
                    "Curvature weight",
                    simplifyState->CurvatureWeight,
                    0.01f,
                    0.0f,
                    1000.0f,
                    "%.3f");
                ImGui::Checkbox(
                    "Preserve sharp features",
                    simplifyState->PreserveSharpFeatures);
                ImGui::Checkbox(
                    "Preserve UV seams",
                    simplifyState->PreserveUvSeams);
            }
            if (!faQem)
                ImGui::EndDisabled();

            const bool canRun =
                *simplifyState->TargetFaces > 0 ||
                *simplifyState->MaxError > 0.0f;
            if (!canRun)
                ImGui::BeginDisabled();
            if (ImGui::Button("Simplify"))
            {
                *simplifyState->LastResult =
                    ApplySandboxEditorMeshSimplifyCommand(
                        context,
                        SandboxEditorMeshSimplifyCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Metric = metric,
                            .TargetFaces = static_cast<std::size_t>(
                                std::max(*simplifyState->TargetFaces, 0)),
                            .MaxError = static_cast<double>(
                                *simplifyState->MaxError),
                            .PreserveBoundary =
                                *simplifyState->PreserveBoundary,
                            .FeatureAngleThresholdDegrees = static_cast<double>(
                                *simplifyState->FeatureAngleThresholdDegrees),
                            .NormalWeight = static_cast<double>(
                                *simplifyState->NormalWeight),
                            .BoundaryWeight = static_cast<double>(
                                *simplifyState->BoundaryWeight),
                            .CurvatureWeight = static_cast<double>(
                                *simplifyState->CurvatureWeight),
                            .PreserveSharpFeatures =
                                *simplifyState->PreserveSharpFeatures,
                            .PreserveUvSeams =
                                *simplifyState->PreserveUvSeams,
                        });
            }
            if (!canRun)
                ImGui::EndDisabled();

            const std::optional<SandboxEditorMeshSimplifyResult>& result =
                simplifyState->LastResult->has_value()
                    ? *simplifyState->LastResult
                    : processing.LastMeshSimplifyResult;
            DrawMeshSimplifyResultStatus(result);
        }

        [[nodiscard]] GN::AveragingMode MeshVertexNormalWeightingFromIndex(
            const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(
                    kMeshVertexNormalWeightings.size() - 1u));
            return kMeshVertexNormalWeightings[static_cast<std::size_t>(clamped)];
        }

        void DrawMeshVertexNormalsResultStatus(
            const std::optional<SandboxEditorMeshVertexNormalsResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last normals run: none");
                return;
            }

            const SandboxEditorMeshVertexNormalsResult& result = *lastResult;
            ImGui::Text("Last normals run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Geometry status: %s",
                        std::string(GN::DebugName(result.NormalStatus)).c_str());
            ImGui::Text("Weighting: %s",
                        std::string(GN::DebugName(result.Weighting)).c_str());
            if (result.Succeeded())
            {
                ImGui::Text("Written: %zu / %zu  valid: %zu  fallback: %zu",
                            result.WrittenCount,
                            result.VertexSlotCount,
                            result.ValidNormalVertexCount,
                            result.FallbackVertexCount);
                ImGui::Text("Faces: processed=%zu  degenerate=%zu  nonfinite=%zu  invalid=%zu",
                            result.ProcessedFaceCount,
                            result.DegenerateFaceCount,
                            result.NonFiniteFaceCount,
                            result.InvalidTopologyFaceCount);
                ImGui::Text("Corners: degenerate=%zu  deleted faces=%zu  deleted vertices=%zu",
                            result.DegenerateCornerCount,
                            result.SkippedDeletedFaceCount,
                            result.SkippedDeletedVertexCount);
                ImGui::Text("Fallback repaired: %s",
                            result.FallbackNormalWasRepaired ? "yes" : "no");
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawMeshVertexNormalsControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            MeshVertexNormalsUiState* normalsState)
        {
            ImGui::SeparatorText("Normals");
            if (!processing.MeshVertexNormalsAvailable)
            {
                ImGui::TextDisabled("Mesh vertex normals are unavailable for this selection.");
                return;
            }
            if (normalsState == nullptr ||
                normalsState->LastResult == nullptr ||
                normalsState->Weighting == nullptr ||
                normalsState->FallbackNormal == nullptr)
            {
                ImGui::TextDisabled("Mesh vertex-normal controls are not bound.");
                return;
            }

            *normalsState->Weighting = std::clamp(
                *normalsState->Weighting,
                0,
                static_cast<std::int32_t>(
                    kMeshVertexNormalWeightings.size() - 1u));
            const GN::AveragingMode currentWeighting =
                MeshVertexNormalWeightingFromIndex(*normalsState->Weighting);
            if (ImGui::BeginCombo(
                    "Weighting",
                    std::string(GN::DebugName(currentWeighting)).c_str()))
            {
                for (std::size_t i = 0u;
                     i < kMeshVertexNormalWeightings.size();
                     ++i)
                {
                    const bool selected =
                        *normalsState->Weighting ==
                        static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            std::string(
                                GN::DebugName(kMeshVertexNormalWeightings[i]))
                                .c_str(),
                            selected))
                    {
                        *normalsState->Weighting =
                            static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::DragFloat3(
                "Fallback normal",
                &normalsState->FallbackNormal->x,
                0.01f,
                -1.0f,
                1.0f);

            if (ImGui::Button("Recompute"))
            {
                *normalsState->LastResult =
                    ApplySandboxEditorMeshVertexNormalsCommand(
                        context,
                        SandboxEditorMeshVertexNormalsCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Weighting = MeshVertexNormalWeightingFromIndex(
                                *normalsState->Weighting),
                            .FallbackNormal = *normalsState->FallbackNormal,
                        });
            }

            const std::optional<SandboxEditorMeshVertexNormalsResult>& result =
                normalsState->LastResult->has_value()
                    ? *normalsState->LastResult
                    : processing.LastMeshVertexNormalsResult;
            DrawMeshVertexNormalsResultStatus(result);
        }

        void DrawGraphVertexNormalsResultStatus(
            const std::optional<SandboxEditorGraphVertexNormalsResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last normals run: none");
                return;
            }

            const SandboxEditorGraphVertexNormalsResult& result = *lastResult;
            ImGui::Text("Last normals run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Geometry status: %s",
                        std::string(GraphNormals::DebugName(result.NormalStatus)).c_str());
            if (result.Succeeded())
            {
                ImGui::Text("Written: %zu / %zu  valid: %zu  fallback: %zu",
                            result.WrittenCount,
                            result.VertexSlotCount,
                            result.ValidNormalVertexCount,
                            result.FallbackVertexCount);
                ImGui::Text("Edges: %zu  invalid=%zu  deleted=%zu",
                            result.EdgeSlotCount,
                            result.InvalidEdgeCount,
                            result.SkippedDeletedEdgeCount);
                ImGui::Text("Neighborhoods: isolated=%zu  degree1=%zu  collinear=%zu",
                            result.IsolatedVertexCount,
                            result.DegreeOneVertexCount,
                            result.CollinearNeighborhoodCount);
                ImGui::Text("Positions: duplicate=%zu  nonfinite=%zu",
                            result.DuplicatePositionCount,
                            result.NonFinitePositionCount);
                ImGui::Text("Fallback repaired: %s",
                            result.FallbackNormalWasRepaired ? "yes" : "no");
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawGraphVertexNormalsControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            GraphVertexNormalsUiState* normalsState)
        {
            ImGui::SeparatorText("Normals");
            if (!processing.GraphVertexNormalsAvailable)
            {
                ImGui::TextDisabled("Graph vertex normals are unavailable for this selection.");
                return;
            }
            if (normalsState == nullptr ||
                normalsState->LastResult == nullptr ||
                normalsState->FallbackNormal == nullptr ||
                normalsState->OrientTowardFallback == nullptr)
            {
                ImGui::TextDisabled("Graph vertex-normal controls are not bound.");
                return;
            }

            ImGui::DragFloat3(
                "Fallback normal",
                &normalsState->FallbackNormal->x,
                0.01f,
                -1.0f,
                1.0f);
            ImGui::Checkbox("Orient toward fallback",
                            normalsState->OrientTowardFallback);

            if (ImGui::Button("Recompute"))
            {
                *normalsState->LastResult =
                    ApplySandboxEditorGraphVertexNormalsCommand(
                        context,
                        SandboxEditorGraphVertexNormalsCommand{
                            .StableEntityId = model.SelectedStableId,
                            .FallbackNormal = *normalsState->FallbackNormal,
                            .OrientTowardFallback =
                                *normalsState->OrientTowardFallback,
                        });
            }

            const std::optional<SandboxEditorGraphVertexNormalsResult>& result =
                normalsState->LastResult->has_value()
                    ? *normalsState->LastResult
                    : processing.LastGraphVertexNormalsResult;
            DrawGraphVertexNormalsResultStatus(result);
        }

        [[nodiscard]] PointNormals::OrientationMode
        PointCloudNormalOrientationFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(
                    kPointCloudNormalOrientations.size() - 1u));
            return kPointCloudNormalOrientations[
                static_cast<std::size_t>(clamped)];
        }

        void DrawPointCloudVertexNormalsResultStatus(
            const std::optional<SandboxEditorPointCloudVertexNormalsResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last normals run: none");
                return;
            }

            const SandboxEditorPointCloudVertexNormalsResult& result =
                *lastResult;
            ImGui::Text("Last normals run: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Geometry status: %s",
                        std::string(PointNormals::DebugName(result.NormalStatus)).c_str());
            ImGui::Text("Backend: %s  orientation: %s",
                        std::string(PointNormals::DebugName(result.Backend)).c_str(),
                        std::string(PointNormals::DebugName(result.Orientation)).c_str());
            if (result.Succeeded())
            {
                ImGui::Text("Written: %zu / %zu  finite: %zu  valid: %zu",
                            result.WrittenCount,
                            result.PointSlotCount,
                            result.FinitePointCount,
                            result.ValidNormalPointCount);
                ImGui::Text("Fallback: %zu  tooFew=%zu  degenerate=%zu  collinear=%zu",
                            result.FallbackPointCount,
                            result.TooFewNeighborCount,
                            result.DegenerateNeighborhoodCount,
                            result.CollinearNeighborhoodCount);
                ImGui::Text("Positions: duplicate=%zu  nonfinite=%zu  deleted=%zu",
                            result.DuplicatePositionCount,
                            result.NonFinitePointCount,
                            result.SkippedDeletedPointCount);
                ImGui::Text("Queries: failures=%zu  visited=%zu  distances=%zu",
                            result.SpatialQueryFailureCount,
                            result.KNNVisitedNodeCount,
                            result.KNNDistanceEvaluationCount);
                ImGui::Text("Flipped: %zu  fallback repaired: %s",
                            result.FlippedOrientationCount,
                            result.FallbackNormalWasRepaired ? "yes" : "no");
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawPointCloudVertexNormalsControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            PointCloudVertexNormalsUiState* normalsState)
        {
            ImGui::SeparatorText("Normals");
            if (!processing.PointCloudVertexNormalsAvailable)
            {
                ImGui::TextDisabled("Point-cloud vertex normals are unavailable for this selection.");
                return;
            }
            if (normalsState == nullptr ||
                normalsState->LastResult == nullptr ||
                normalsState->KNeighbors == nullptr ||
                normalsState->MinimumNeighbors == nullptr ||
                normalsState->UseRadiusSearch == nullptr ||
                normalsState->Radius == nullptr ||
                normalsState->Orientation == nullptr ||
                normalsState->FallbackNormal == nullptr)
            {
                ImGui::TextDisabled("Point-cloud vertex-normal controls are not bound.");
                return;
            }

            *normalsState->KNeighbors =
                std::clamp(*normalsState->KNeighbors, 1, 512);
            *normalsState->MinimumNeighbors =
                std::clamp(*normalsState->MinimumNeighbors, 1, 512);
            *normalsState->Orientation = std::clamp(
                *normalsState->Orientation,
                0,
                static_cast<std::int32_t>(
                    kPointCloudNormalOrientations.size() - 1u));
            ImGui::DragInt("K", normalsState->KNeighbors, 1.0f, 1, 512);
            ImGui::DragInt(
                "Minimum neighbors",
                normalsState->MinimumNeighbors,
                1.0f,
                1,
                512);
            ImGui::Checkbox("Use radius", normalsState->UseRadiusSearch);
            if (*normalsState->UseRadiusSearch)
            {
                *normalsState->Radius =
                    std::max(*normalsState->Radius, 0.001f);
                ImGui::DragFloat(
                    "Radius",
                    normalsState->Radius,
                    0.01f,
                    0.001f,
                    1000.0f);
            }

            const PointNormals::OrientationMode orientation =
                PointCloudNormalOrientationFromIndex(
                    *normalsState->Orientation);
            if (ImGui::BeginCombo(
                    "Orientation",
                    std::string(PointNormals::DebugName(orientation)).c_str()))
            {
                for (std::size_t i = 0u;
                     i < kPointCloudNormalOrientations.size();
                     ++i)
                {
                    const bool selected =
                        *normalsState->Orientation ==
                        static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            std::string(
                                PointNormals::DebugName(
                                    kPointCloudNormalOrientations[i]))
                                .c_str(),
                            selected))
                    {
                        *normalsState->Orientation =
                            static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::DragFloat3(
                "Fallback normal",
                &normalsState->FallbackNormal->x,
                0.01f,
                -1.0f,
                1.0f);

            if (ImGui::Button("Recompute"))
            {
                *normalsState->LastResult =
                    ApplySandboxEditorPointCloudVertexNormalsCommand(
                        context,
                        SandboxEditorPointCloudVertexNormalsCommand{
                            .StableEntityId = model.SelectedStableId,
                            .KNeighbors = static_cast<std::uint32_t>(
                                *normalsState->KNeighbors),
                            .MinimumNeighbors = static_cast<std::uint32_t>(
                                *normalsState->MinimumNeighbors),
                            .UseRadiusSearch =
                                *normalsState->UseRadiusSearch,
                            .Radius = *normalsState->Radius,
                            .Orientation =
                                PointCloudNormalOrientationFromIndex(
                                    *normalsState->Orientation),
                            .FallbackNormal =
                                *normalsState->FallbackNormal,
                        });
            }

            const std::optional<SandboxEditorPointCloudVertexNormalsResult>&
                result = normalsState->LastResult->has_value()
                    ? *normalsState->LastResult
                    : processing.LastPointCloudVertexNormalsResult;
            DrawPointCloudVertexNormalsResultStatus(result);
        }

        void DrawPointCloudOutlierRemovalResultStatus(
            const std::optional<SandboxEditorPointCloudOutlierRemovalResult>&
                lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last outlier removal: none");
                return;
            }

            const SandboxEditorPointCloudOutlierRemovalResult& result =
                *lastResult;
            ImGui::Text("Last outlier removal: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text(
                "Method: %s",
                result.Method ==
                        SandboxEditorPointCloudOutlierMethod::Statistical
                    ? "Statistical"
                    : "Radius");
            if (result.Succeeded())
            {
                ImGui::Text("Kept %zu / %zu  rejected %zu  non-finite %zu",
                            result.KeptCount,
                            result.OriginalCount,
                            result.RejectedCount,
                            result.NonFiniteCount);
                if (result.Method ==
                    SandboxEditorPointCloudOutlierMethod::Statistical)
                {
                    ImGui::Text("Mean %.4f  stddev %.4f  threshold %.4f",
                                static_cast<double>(result.MeanDistance),
                                static_cast<double>(result.StdDevDistance),
                                static_cast<double>(result.DistanceThreshold));
                }
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawPointCloudOutlierRemovalControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            PointCloudOutlierRemovalUiState* outlierState)
        {
            ImGui::SeparatorText("Remove Outliers");
            if (!processing.PointCloudOutlierRemovalAvailable)
            {
                ImGui::TextDisabled("Point-cloud outlier removal is unavailable for this selection.");
                return;
            }
            if (outlierState == nullptr ||
                outlierState->LastResult == nullptr ||
                outlierState->Method == nullptr ||
                outlierState->KNeighbors == nullptr ||
                outlierState->StdDevMultiplier == nullptr ||
                outlierState->SearchRadius == nullptr ||
                outlierState->MinNeighbors == nullptr)
            {
                ImGui::TextDisabled("Point-cloud outlier-removal controls are not bound.");
                return;
            }

            *outlierState->Method = std::clamp(*outlierState->Method, 0, 1);
            const bool statistical = *outlierState->Method == 0;
            if (ImGui::BeginCombo(
                    "Method",
                    statistical ? "Statistical" : "Radius"))
            {
                if (ImGui::Selectable("Statistical", statistical))
                    *outlierState->Method = 0;
                if (statistical)
                    ImGui::SetItemDefaultFocus();
                if (ImGui::Selectable("Radius", !statistical))
                    *outlierState->Method = 1;
                if (!statistical)
                    ImGui::SetItemDefaultFocus();
                ImGui::EndCombo();
            }

            if (statistical)
            {
                ImGui::TextDisabled("Reject points beyond mean + k*stddev of mean-kNN distance.");
                *outlierState->KNeighbors =
                    std::clamp(*outlierState->KNeighbors, 1, 512);
                *outlierState->StdDevMultiplier =
                    std::clamp(*outlierState->StdDevMultiplier, 0.0f, 100.0f);
                ImGui::DragInt(
                    "K neighbors",
                    outlierState->KNeighbors,
                    1.0f,
                    1,
                    512);
                ImGui::DragFloat(
                    "Std-dev multiplier",
                    outlierState->StdDevMultiplier,
                    0.05f,
                    0.0f,
                    100.0f);
            }
            else
            {
                ImGui::TextDisabled("Reject points with too few neighbors inside the search radius.");
                *outlierState->SearchRadius =
                    std::max(*outlierState->SearchRadius, 0.0f);
                *outlierState->MinNeighbors =
                    std::clamp(*outlierState->MinNeighbors, 0, 512);
                ImGui::DragFloat(
                    "Search radius",
                    outlierState->SearchRadius,
                    0.01f,
                    0.0f,
                    1000.0f);
                ImGui::DragInt(
                    "Min neighbors",
                    outlierState->MinNeighbors,
                    1.0f,
                    0,
                    512);
            }

            if (ImGui::Button("Remove Outliers"))
            {
                *outlierState->LastResult =
                    ApplySandboxEditorPointCloudOutlierRemovalCommand(
                        context,
                        SandboxEditorPointCloudOutlierRemovalCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Method = statistical
                                ? SandboxEditorPointCloudOutlierMethod::Statistical
                                : SandboxEditorPointCloudOutlierMethod::Radius,
                            .KNeighbors = static_cast<std::uint32_t>(
                                *outlierState->KNeighbors),
                            .StdDevMultiplier =
                                *outlierState->StdDevMultiplier,
                            .SearchRadius = *outlierState->SearchRadius,
                            .MinNeighbors = static_cast<std::uint32_t>(
                                *outlierState->MinNeighbors),
                        });
            }

            const std::optional<SandboxEditorPointCloudOutlierRemovalResult>&
                result = outlierState->LastResult->has_value()
                    ? *outlierState->LastResult
                    : processing.LastPointCloudOutlierRemovalResult;
            DrawPointCloudOutlierRemovalResultStatus(result);
        }

        [[nodiscard]] SandboxEditorProgressivePoissonChannel
        ProgressivePoissonChannelFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(
                    kProgressivePoissonChannels.size() - 1u));
            return kProgressivePoissonChannels[
                static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] std::int32_t ProgressivePoissonChannelIndex(
            const SandboxEditorProgressivePoissonChannel channel) noexcept
        {
            for (std::size_t i = 0u; i < kProgressivePoissonChannels.size(); ++i)
            {
                if (kProgressivePoissonChannels[i] == channel)
                {
                    return static_cast<std::int32_t>(i);
                }
            }
            return 0;
        }

        [[nodiscard]] SandboxEditorProgressivePoissonBackend
        ProgressivePoissonBackendFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(
                    kProgressivePoissonBackends.size() - 1u));
            return kProgressivePoissonBackends[
                static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] std::int32_t ProgressivePoissonBackendIndex(
            const SandboxEditorProgressivePoissonBackend backend) noexcept
        {
            for (std::size_t i = 0u; i < kProgressivePoissonBackends.size(); ++i)
            {
                if (kProgressivePoissonBackends[i] == backend)
                {
                    return static_cast<std::int32_t>(i);
                }
            }
            return 0;
        }

        void DrawProgressivePoissonTooltip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("%s", text);
            }
        }

        void SyncProgressivePoissonUiStateFromConfig(
            ProgressivePoissonUiState& state,
            const Core::Config::ProgressivePoissonPlaygroundConfig& config)
        {
            *state.Dimension = static_cast<std::int32_t>(config.Dimension);
            *state.GridWidth = static_cast<std::int32_t>(config.GridWidth);
            *state.MaxLevels = static_cast<std::int32_t>(config.MaxLevels);
            *state.HashLoadFactor = static_cast<float>(config.HashLoadFactor);
            *state.RadiusAlpha = static_cast<float>(config.RadiusAlpha);
            *state.RandomizeGridOrigin = config.RandomizeGridOrigin;
            *state.GridOriginSeed = static_cast<std::int32_t>(config.GridOriginSeed);
            *state.ShuffleWithinLevels = config.ShuffleWithinLevels;
            *state.ShuffleSeed = static_cast<std::int32_t>(config.ShuffleSeed);
            *state.PrefixCount = static_cast<std::int32_t>(config.PrefixCount);
            *state.Channel = ProgressivePoissonChannelIndex(
                MakeSandboxEditorProgressivePoissonChannel(config.Channel));
            if (state.Backend != nullptr)
            {
                *state.Backend = ProgressivePoissonBackendIndex(
                    MakeSandboxEditorProgressivePoissonBackend(config.Backend));
            }
            if (state.MeshSurfaceSampleCount != nullptr)
            {
                *state.MeshSurfaceSampleCount =
                    static_cast<std::int32_t>(config.MeshSurfaceSampleCount);
            }
            if (state.MeshSurfaceSampleSeed != nullptr)
            {
                *state.MeshSurfaceSampleSeed =
                    static_cast<std::int32_t>(config.MeshSurfaceSampleSeed);
            }
            if (state.MeshSurfaceMinTriangleArea != nullptr)
            {
                *state.MeshSurfaceMinTriangleArea =
                    static_cast<float>(config.MeshSurfaceMinTriangleArea);
            }
            if (state.MeshSurfaceInterpolateNormals != nullptr)
            {
                *state.MeshSurfaceInterpolateNormals =
                    config.MeshSurfaceInterpolateNormals;
            }
            if (state.AutoRunOnEdit != nullptr)
            {
                *state.AutoRunOnEdit = config.AutoRunOnEdit;
            }
            if (state.DebounceSeconds != nullptr)
            {
                *state.DebounceSeconds =
                    static_cast<float>(config.DebounceSeconds);
            }
        }

        [[nodiscard]] SandboxEditorProgressivePoissonConfig
        BuildProgressivePoissonConfigFromUiState(
            const ProgressivePoissonUiState& state)
        {
            SandboxEditorProgressivePoissonConfig config{
                .Dimension = static_cast<std::uint32_t>(*state.Dimension),
                .GridWidth = static_cast<std::uint32_t>(*state.GridWidth),
                .MaxLevels = static_cast<std::uint32_t>(*state.MaxLevels),
                .HashLoadFactor = *state.HashLoadFactor,
                .RadiusAlpha = *state.RadiusAlpha,
                .RandomizeGridOrigin = *state.RandomizeGridOrigin,
                .GridOriginSeed = static_cast<std::uint32_t>(*state.GridOriginSeed),
                .ShuffleWithinLevels = *state.ShuffleWithinLevels,
                .ShuffleSeed = static_cast<std::uint32_t>(*state.ShuffleSeed),
                .PrefixCount = static_cast<std::uint32_t>(*state.PrefixCount),
                .Channel = ProgressivePoissonChannelFromIndex(*state.Channel),
                .Backend = state.Backend == nullptr
                    ? SandboxEditorProgressivePoissonBackend::CpuReference
                    : ProgressivePoissonBackendFromIndex(*state.Backend),
            };
            if (state.MeshSurfaceSampleCount != nullptr)
            {
                config.MeshSurfaceSampleCount =
                    static_cast<std::uint32_t>(*state.MeshSurfaceSampleCount);
            }
            if (state.MeshSurfaceSampleSeed != nullptr)
            {
                config.MeshSurfaceSampleSeed =
                    static_cast<std::uint32_t>(*state.MeshSurfaceSampleSeed);
            }
            if (state.MeshSurfaceMinTriangleArea != nullptr)
            {
                config.MeshSurfaceMinTriangleArea =
                    static_cast<double>(*state.MeshSurfaceMinTriangleArea);
            }
            if (state.MeshSurfaceInterpolateNormals != nullptr)
            {
                config.MeshSurfaceInterpolateNormals =
                    *state.MeshSurfaceInterpolateNormals;
            }
            return config;
        }

        [[nodiscard]] Core::Config::ProgressivePoissonPlaygroundConfig
        BuildCoreProgressivePoissonConfigFromUiState(
            const ProgressivePoissonUiState& state,
            const Core::Config::ProgressivePoissonPlaygroundConfig& defaults)
        {
            Core::Config::ProgressivePoissonPlaygroundConfig config =
                MakeCoreProgressivePoissonPlaygroundConfig(
                    BuildProgressivePoissonConfigFromUiState(state),
                    defaults);
            if (state.AutoRunOnEdit != nullptr)
            {
                config.AutoRunOnEdit = *state.AutoRunOnEdit;
            }
            if (state.DebounceSeconds != nullptr)
            {
                config.DebounceSeconds =
                    static_cast<double>(*state.DebounceSeconds);
            }
            return config;
        }

        void DrawProgressivePoissonResultStatus(
            const std::optional<SandboxEditorProgressivePoissonResult>&
                lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last progressive Poisson run: none");
                return;
            }

            const SandboxEditorProgressivePoissonResult& result =
                *lastResult;
            ImGui::Text(
                "Last progressive Poisson run: %s",
                DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text(
                "Channel: %s",
                DebugNameForSandboxEditorProgressivePoissonChannel(
                    result.Channel));
            if (!result.BackendId.empty())
            {
                if (!result.BackendDisplayName.empty())
                {
                    ImGui::Text("Backend: %s (%s)",
                                result.BackendDisplayName.c_str(),
                                result.BackendId.c_str());
                }
                else
                {
                    ImGui::Text("Backend: %s", result.BackendId.c_str());
                }
            }
            if (!result.RequestedBackendId.empty() &&
                result.RequestedBackendId != result.BackendId)
            {
                if (!result.RequestedBackendDisplayName.empty())
                {
                    ImGui::Text("Requested backend: %s (%s)",
                                result.RequestedBackendDisplayName.c_str(),
                                result.RequestedBackendId.c_str());
                }
                else
                {
                    ImGui::Text("Requested backend: %s",
                                result.RequestedBackendId.c_str());
                }
            }
            if (result.Succeeded())
            {
                ImGui::Text("Accepted %u / %u  prefix %u  levels %u",
                            result.AcceptedCount,
                            result.InputCount,
                            result.PrefixCount,
                            result.LevelCount);
                ImGui::Text("Base radius %.6f  alpha %.6f",
                            static_cast<double>(result.BaseRadius),
                            static_cast<double>(result.UsedAlpha));
                ImGui::TextWrapped(
                    "Level counts: %s",
                    FormatProgressivePoissonLevelCounts(
                        result.LevelAcceptedCounts)
                        .c_str());
                if (result.AlphaDefaulted ||
                    result.ClampedGridWidth ||
                    result.ClampedMaxLevels)
                {
                    ImGui::Text("Defaults: alpha=%s grid=%s levels=%s",
                                result.AlphaDefaulted ? "yes" : "no",
                                result.ClampedGridWidth ? "yes" : "no",
                                result.ClampedMaxLevels ? "yes" : "no");
                }
                if (result.MeshSurfaceSamplingUsed)
                {
                    ImGui::Text(
                        "Surface samples %u  triangles %u/%u  area %.6f",
                        result.MeshSurfaceSampleCount,
                        result.MeshSurfaceAcceptedTriangleCount,
                        result.MeshSurfaceTotalFaceCount,
                        result.MeshSurfaceArea);
                }
            }
            if (!result.BackendFallbackReason.empty())
            {
                ImGui::TextWrapped(
                    "Backend fallback: %s",
                    result.BackendFallbackReason.c_str());
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawProgressivePoissonControls(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            const SandboxEditorGeometryProcessingModel& processing,
            ProgressivePoissonUiState* poissonState)
        {
            ImGui::SeparatorText("Progressive Poisson");
            const bool meshInput =
                model.Kind == SandboxEditorDomainWindowKind::Mesh;
            const bool progressivePoissonAvailable = meshInput
                ? processing.MeshProgressivePoissonAvailable
                : processing.PointCloudProgressivePoissonAvailable;
            if (!progressivePoissonAvailable)
            {
                ImGui::TextDisabled("Progressive Poisson is unavailable for this selection.");
                return;
            }
            if (poissonState == nullptr ||
                poissonState->LastResult == nullptr ||
                poissonState->LastConfigResult == nullptr ||
                poissonState->Dimension == nullptr ||
                poissonState->GridWidth == nullptr ||
                poissonState->MaxLevels == nullptr ||
                poissonState->HashLoadFactor == nullptr ||
                poissonState->RadiusAlpha == nullptr ||
                poissonState->RandomizeGridOrigin == nullptr ||
                poissonState->GridOriginSeed == nullptr ||
                poissonState->ShuffleWithinLevels == nullptr ||
                poissonState->ShuffleSeed == nullptr ||
                poissonState->PrefixCount == nullptr ||
                poissonState->Channel == nullptr ||
                poissonState->AutoRunOnEdit == nullptr ||
                poissonState->DebounceSeconds == nullptr ||
                poissonState->AutoRunPending == nullptr ||
                poissonState->LastEditTime == nullptr ||
                poissonState->PendingStableEntityId == nullptr ||
                (meshInput &&
                 (poissonState->MeshSurfaceSampleCount == nullptr ||
                  poissonState->MeshSurfaceSampleSeed == nullptr ||
                  poissonState->MeshSurfaceMinTriangleArea == nullptr ||
                  poissonState->MeshSurfaceInterpolateNormals == nullptr)))
            {
                ImGui::TextDisabled("Progressive Poisson controls are not bound.");
                return;
            }

            const bool configFacadeAvailable =
                context.EngineConfigControlState != nullptr &&
                context.PreviewEngineConfigDocument &&
                context.ApplyEngineConfigHotSubset &&
                context.EngineConfigCommandsAvailable;
            if (!configFacadeAvailable)
            {
                ImGui::TextDisabled(
                    "Progressive Poisson requires engine config-control.");
                return;
            }

            const Core::Config::ProgressivePoissonPlaygroundConfig&
                activeConfig =
                    context.EngineConfigControlState->ActiveConfig.Sandbox
                        .ProgressivePoisson;
            SyncProgressivePoissonUiStateFromConfig(*poissonState, activeConfig);

            *poissonState->Dimension =
                *poissonState->Dimension <= 2 ? 2 : 3;
            bool configChanged = false;
            if (ImGui::BeginCombo(
                    "Dimension",
                    *poissonState->Dimension == 2 ? "2D" : "3D"))
            {
                if (ImGui::Selectable("2D", *poissonState->Dimension == 2))
                {
                    *poissonState->Dimension = 2;
                    configChanged = true;
                }
                if (*poissonState->Dimension == 2)
                    ImGui::SetItemDefaultFocus();
                if (ImGui::Selectable("3D", *poissonState->Dimension == 3))
                {
                    *poissonState->Dimension = 3;
                    configChanged = true;
                }
                if (*poissonState->Dimension == 3)
                    ImGui::SetItemDefaultFocus();
                ImGui::EndCombo();
            }
            DrawProgressivePoissonTooltip(
                "Input dimensionality used by the reference sampler.");

            *poissonState->GridWidth =
                std::clamp(*poissonState->GridWidth, 1, 4096);
            *poissonState->MaxLevels =
                std::clamp(*poissonState->MaxLevels, 1, 32);
            *poissonState->HashLoadFactor =
                std::clamp(*poissonState->HashLoadFactor, 0.01f, 16.0f);
            if (!std::isfinite(*poissonState->RadiusAlpha))
                *poissonState->RadiusAlpha = -1.0f;
            *poissonState->GridOriginSeed =
                std::clamp(*poissonState->GridOriginSeed,
                           0,
                           std::numeric_limits<std::int32_t>::max());
            *poissonState->ShuffleSeed =
                std::clamp(*poissonState->ShuffleSeed,
                           0,
                           std::numeric_limits<std::int32_t>::max());
            *poissonState->PrefixCount =
                std::clamp(*poissonState->PrefixCount, 0, 10'000'000);
            *poissonState->Channel = std::clamp(
                *poissonState->Channel,
                0,
                static_cast<std::int32_t>(
                    kProgressivePoissonChannels.size() - 1u));
            if (poissonState->Backend != nullptr)
            {
                *poissonState->Backend = std::clamp(
                    *poissonState->Backend,
                    0,
                    static_cast<std::int32_t>(
                        kProgressivePoissonBackends.size() - 1u));
            }
            if (meshInput)
            {
                *poissonState->MeshSurfaceSampleCount =
                    std::clamp(*poissonState->MeshSurfaceSampleCount,
                               1,
                               10'000'000);
                *poissonState->MeshSurfaceSampleSeed =
                    std::clamp(*poissonState->MeshSurfaceSampleSeed,
                               0,
                               std::numeric_limits<std::int32_t>::max());
                if (!std::isfinite(
                        *poissonState->MeshSurfaceMinTriangleArea) ||
                    *poissonState->MeshSurfaceMinTriangleArea <= 0.0f)
                {
                    *poissonState->MeshSurfaceMinTriangleArea = 1.0e-14f;
                }
            }
            *poissonState->DebounceSeconds =
                std::clamp(*poissonState->DebounceSeconds, 0.0f, 10.0f);

            configChanged |=
                ImGui::DragInt("Grid width", poissonState->GridWidth, 1.0f, 1, 4096);
            DrawProgressivePoissonTooltip(
                "Spatial hash grid width used before method-side clamping.");
            configChanged |=
                ImGui::DragInt("Max levels", poissonState->MaxLevels, 1.0f, 1, 32);
            DrawProgressivePoissonTooltip(
                "Maximum progressive hierarchy levels to emit.");
            configChanged |= ImGui::DragFloat(
                "Hash load",
                poissonState->HashLoadFactor,
                0.01f,
                0.01f,
                16.0f);
            DrawProgressivePoissonTooltip(
                "Target hash load factor used by the CPU reference backend.");
            configChanged |= ImGui::DragFloat(
                "Radius alpha",
                poissonState->RadiusAlpha,
                0.01f,
                -1.0f,
                0.999f);
            DrawProgressivePoissonTooltip(
                "Negative values keep the reference backend's default radius alpha.");
            configChanged |= ImGui::Checkbox(
                "Randomize grid origin",
                poissonState->RandomizeGridOrigin);
            DrawProgressivePoissonTooltip(
                "Jitter the grid origin with the configured seed.");
            configChanged |= ImGui::DragInt(
                "Grid seed",
                poissonState->GridOriginSeed,
                1.0f,
                0,
                std::numeric_limits<std::int32_t>::max());
            DrawProgressivePoissonTooltip(
                "Seed for grid-origin randomization.");
            configChanged |= ImGui::Checkbox(
                "Shuffle within levels",
                poissonState->ShuffleWithinLevels);
            DrawProgressivePoissonTooltip(
                "Shuffle accepted samples inside each progressive level.");
            configChanged |= ImGui::DragInt(
                "Shuffle seed",
                poissonState->ShuffleSeed,
                1.0f,
                0,
                std::numeric_limits<std::int32_t>::max());
            DrawProgressivePoissonTooltip(
                "Seed for deterministic level-local shuffling.");
            configChanged |= ImGui::DragInt(
                "Prefix count",
                poissonState->PrefixCount,
                1.0f,
                0,
                10'000'000);
            DrawProgressivePoissonTooltip(
                "Visible prefix count; zero shows all accepted points.");
            configChanged |= ImGui::Checkbox(
                "Auto run on edit",
                poissonState->AutoRunOnEdit);
            DrawProgressivePoissonTooltip(
                "Rerun the sampler after knob edits settle.");
            configChanged |= ImGui::DragFloat(
                "Debounce seconds",
                poissonState->DebounceSeconds,
                0.01f,
                0.0f,
                10.0f);
            DrawProgressivePoissonTooltip(
                "Delay after the last edit before auto-running.");

            if (meshInput)
            {
                ImGui::SeparatorText("Surface input");
                configChanged |= ImGui::DragInt(
                    "Surface samples",
                    poissonState->MeshSurfaceSampleCount,
                    1.0f,
                    1,
                    10'000'000);
                DrawProgressivePoissonTooltip(
                    "Number of deterministic points sampled from the mesh surface.");
                configChanged |= ImGui::DragInt(
                    "Surface seed",
                    poissonState->MeshSurfaceSampleSeed,
                    1.0f,
                    0,
                    std::numeric_limits<std::int32_t>::max());
                DrawProgressivePoissonTooltip(
                    "Seed for deterministic mesh surface sampling.");
                configChanged |= ImGui::InputFloat(
                    "Min triangle area",
                    poissonState->MeshSurfaceMinTriangleArea,
                    0.0f,
                    0.0f,
                    "%.3e");
                DrawProgressivePoissonTooltip(
                    "Triangles below this area are rejected before sampling.");
                if (!std::isfinite(
                        *poissonState->MeshSurfaceMinTriangleArea) ||
                    *poissonState->MeshSurfaceMinTriangleArea <= 0.0f)
                {
                    *poissonState->MeshSurfaceMinTriangleArea = 1.0e-14f;
                }
                configChanged |= ImGui::Checkbox(
                    "Interpolate normals",
                    poissonState->MeshSurfaceInterpolateNormals);
                DrawProgressivePoissonTooltip(
                    "Interpolate vertex normals onto sampled surface points.");
            }

            const SandboxEditorProgressivePoissonChannel channel =
                ProgressivePoissonChannelFromIndex(*poissonState->Channel);
            if (ImGui::BeginCombo(
                    "Color channel",
                    DebugNameForSandboxEditorProgressivePoissonChannel(channel)))
            {
                for (std::size_t i = 0u;
                     i < kProgressivePoissonChannels.size();
                     ++i)
                {
                    const bool selected =
                        *poissonState->Channel ==
                        static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForSandboxEditorProgressivePoissonChannel(
                                kProgressivePoissonChannels[i]),
                            selected))
                    {
                        *poissonState->Channel =
                            static_cast<std::int32_t>(i);
                        configChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            DrawProgressivePoissonTooltip(
                "Scalar property used for point color visualization.");

            auto applyConfig = [&]() -> SandboxEditorProgressivePoissonConfigResult
            {
                const Core::Config::ProgressivePoissonPlaygroundConfig config =
                    BuildCoreProgressivePoissonConfigFromUiState(
                        *poissonState,
                        activeConfig);
                return ApplySandboxEditorProgressivePoissonConfigCommand(
                    context,
                    SandboxEditorProgressivePoissonConfigCommand{
                        .Config = config,
                        .SourceId = "sandbox.progressive_poisson",
                    });
            };

            auto runSampler = [&]()
            {
                *poissonState->LastResult =
                    ApplySandboxEditorProgressivePoissonCommand(
                        context,
                        SandboxEditorProgressivePoissonCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Config = BuildProgressivePoissonConfigFromUiState(
                                *poissonState),
                        });
                *poissonState->AutoRunPending = false;
                *poissonState->PendingStableEntityId = 0u;
            };

            if (configChanged)
            {
                *poissonState->LastConfigResult = applyConfig();
                if ((*poissonState->LastConfigResult)->Succeeded() &&
                    *poissonState->AutoRunOnEdit)
                {
                    *poissonState->AutoRunPending = true;
                    *poissonState->PendingStableEntityId =
                        model.SelectedStableId;
                    *poissonState->LastEditTime = ImGui::GetTime();
                }
                else
                {
                    *poissonState->AutoRunPending = false;
                    *poissonState->PendingStableEntityId = 0u;
                }
            }

            if (ImGui::Button("Run Progressive Poisson"))
            {
                *poissonState->LastConfigResult = applyConfig();
                if ((*poissonState->LastConfigResult)->Succeeded())
                {
                    runSampler();
                }
            }

            if (*poissonState->AutoRunPending &&
                *poissonState->PendingStableEntityId == model.SelectedStableId)
            {
                const double elapsed =
                    ImGui::GetTime() - *poissonState->LastEditTime;
                if (*poissonState->AutoRunOnEdit &&
                    elapsed >= static_cast<double>(*poissonState->DebounceSeconds))
                {
                    runSampler();
                }
            }
            else if (*poissonState->AutoRunPending)
            {
                *poissonState->AutoRunPending = false;
                *poissonState->PendingStableEntityId = 0u;
            }

            if (poissonState->LastConfigResult->has_value() &&
                !(*poissonState->LastConfigResult)->Succeeded())
            {
                ImGui::TextWrapped(
                    "%s",
                    (*poissonState->LastConfigResult)->Message.c_str());
            }

            const std::optional<SandboxEditorProgressivePoissonResult>& result =
                poissonState->LastResult->has_value()
                    ? *poissonState->LastResult
                    : processing.LastProgressivePoissonResult;
            DrawProgressivePoissonResultStatus(result);
        }

        void DrawDomainProcessingWindow(
            const SandboxEditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            KMeansUiState* kmeansState,
            MeshDenoiseUiState* denoiseState,
            MeshCurvatureUiState* curvatureState,
            MeshRemeshUiState* remeshState,
            MeshSubdivideUiState* subdivideState,
            MeshSimplifyUiState* simplifyState,
            MeshVertexNormalsUiState* meshNormalsState,
            GraphVertexNormalsUiState* graphNormalsState,
            PointCloudVertexNormalsUiState* pointCloudNormalsState,
            PointCloudOutlierRemovalUiState* pointCloudOutlierState,
            ProgressivePoissonUiState* progressivePoissonState)
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
            switch (model.Kind)
            {
            case SandboxEditorDomainWindowKind::Mesh:
                DrawMeshDenoiseControls(
                    model,
                    context,
                    processing,
                    denoiseState);
                DrawMeshCurvatureControls(
                    model,
                    context,
                    processing,
                    curvatureState);
                DrawMeshRemeshControls(
                    model,
                    context,
                    processing,
                    remeshState);
                DrawMeshSubdivideControls(
                    model,
                    context,
                    processing,
                    subdivideState);
                DrawMeshSimplifyControls(
                    model,
                    context,
                    processing,
                    simplifyState);
                DrawMeshVertexNormalsControls(
                    model,
                    context,
                    processing,
                    meshNormalsState);
                DrawProgressivePoissonControls(
                    model,
                    context,
                    processing,
                    progressivePoissonState);
                break;
            case SandboxEditorDomainWindowKind::Graph:
                DrawGraphVertexNormalsControls(
                    model,
                    context,
                    processing,
                    graphNormalsState);
                break;
            case SandboxEditorDomainWindowKind::PointCloud:
                DrawPointCloudVertexNormalsControls(
                    model,
                    context,
                    processing,
                    pointCloudNormalsState);
                DrawPointCloudOutlierRemovalControls(
                    model,
                    context,
                    processing,
                    pointCloudOutlierState);
                DrawProgressivePoissonControls(
                    model,
                    context,
                    processing,
                    progressivePoissonState);
                break;
            }
        }

        void DrawOneDomainWindow(
            const SandboxEditorContext& context,
            const SandboxEditorDomainWindowKind kind,
            const DomainWindowSection section,
            std::array<bool, 15>& domainWindowOpen,
            KMeansUiState* kmeansState,
            MeshDenoiseUiState* denoiseState,
            MeshCurvatureUiState* curvatureState,
            MeshRemeshUiState* remeshState,
            MeshSubdivideUiState* subdivideState,
            MeshSimplifyUiState* simplifyState,
            MeshVertexNormalsUiState* normalsState,
            GraphVertexNormalsUiState* graphNormalsState,
            PointCloudVertexNormalsUiState* pointCloudNormalsState,
            PointCloudOutlierRemovalUiState* pointCloudOutlierState,
            ProgressivePoissonUiState* progressivePoissonState,
            TextureBakeUiState* textureBakeState)
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
                case DomainWindowSection::Properties:
                    DrawDomainPropertyWindow(model, &context, textureBakeState);
                    break;
                case DomainWindowSection::Visualization:
                    DrawDomainVisualizationWindow(model, context);
                    break;
                case DomainWindowSection::Selection:
                    DrawDomainSelectionWindow(model);
                    break;
                case DomainWindowSection::Processing:
                    DrawDomainProcessingWindow(
                        model,
                        context,
                        kmeansState,
                        denoiseState,
                        curvatureState,
                        remeshState,
                        subdivideState,
                        simplifyState,
                        normalsState,
                        graphNormalsState,
                        pointCloudNormalsState,
                        pointCloudOutlierState,
                        progressivePoissonState);
                    break;
                case DomainWindowSection::Count:
                    break;
                }
            }
            ImGui::End();
        }

        void DrawDomainWindows(
            const SandboxEditorContext* context,
            std::array<bool, 15>* domainWindowOpen,
            KMeansUiState* kmeansState,
            MeshDenoiseUiState* denoiseState,
            MeshCurvatureUiState* curvatureState,
            MeshRemeshUiState* remeshState,
            MeshSubdivideUiState* subdivideState,
            MeshSimplifyUiState* simplifyState,
            MeshVertexNormalsUiState* normalsState,
            GraphVertexNormalsUiState* graphNormalsState,
            PointCloudVertexNormalsUiState* pointCloudNormalsState,
            PointCloudOutlierRemovalUiState* pointCloudOutlierState,
            ProgressivePoissonUiState* progressivePoissonState,
            TextureBakeUiState* textureBakeState)
        {
            if (context == nullptr || domainWindowOpen == nullptr)
                return;

            constexpr std::array<SandboxEditorDomainWindowKind, 3> kKinds{
                SandboxEditorDomainWindowKind::PointCloud,
                SandboxEditorDomainWindowKind::Graph,
                SandboxEditorDomainWindowKind::Mesh,
            };
            constexpr std::array<DomainWindowSection, 5> kSections{
                DomainWindowSection::Render,
                DomainWindowSection::Properties,
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
                    kmeansState,
                    denoiseState,
                    curvatureState,
                    remeshState,
                    subdivideState,
                    simplifyState,
                    normalsState,
                    graphNormalsState,
                    pointCloudNormalsState,
                    pointCloudOutlierState,
                    progressivePoissonState,
                    textureBakeState);
            }
            }
        }

        void DrawRenderRecipeEditor(
            const SandboxEditorRenderRecipeEditorModel& model,
            const SandboxEditorContext* context,
            std::array<char, 8192>* draftBuffer)
        {
            if (!model.Available)
            {
                ImGui::TextDisabled("Render recipe editor is unavailable.");
                DrawDiagnostics(model.Diagnostics);
                return;
            }

            ImGui::Text("Renderer: %s", model.RendererId.c_str());
            ImGui::Text("Active recipe: %s", model.ActiveRecipeId.c_str());
            ImGui::Text("View/output: %s / %s / %s",
                        model.ActiveViewOutputRecipeId.c_str(),
                        model.ViewKind.c_str(),
                        model.OutputTarget.c_str());
            ImGui::Text("Draft: %s revision=%llu active=%llu",
                        DebugNameForSandboxEditorRenderRecipeDraftState(
                            model.DraftState),
                        static_cast<unsigned long long>(model.DraftRevision),
                        static_cast<unsigned long long>(model.ActiveRevision));
            ImGui::Text("Validation: %s parsed slots=%u bindings=%u",
                        std::string(Graphics::ToString(model.ValidationState)).c_str(),
                        model.ParsedSlotCount,
                        model.ParsedBindingOverrideCount);

            const bool commandsAvailable =
                context != nullptr &&
                context->RenderRecipeContext != nullptr &&
                context->RenderRecipeEditorState != nullptr &&
                context->RenderRecipeCommandsAvailable;

            if (draftBuffer != nullptr)
            {
                SandboxEditorRenderRecipeEditorState* state =
                    context != nullptr ? context->RenderRecipeEditorState : nullptr;
                if (state != nullptr &&
                    !state->DraftDocument.empty() &&
                    draftBuffer->front() == '\0')
                {
                    const std::size_t copyCount =
                        std::min(state->DraftDocument.size(),
                                 draftBuffer->size() - 1u);
                    std::copy_n(state->DraftDocument.data(),
                                copyCount,
                                draftBuffer->data());
                    (*draftBuffer)[copyCount] = '\0';
                }
                if (state != nullptr &&
                    state->DraftDocument.empty() &&
                    state->DraftState ==
                        SandboxEditorRenderRecipeDraftState::Canceled)
                {
                    draftBuffer->fill('\0');
                }

                ImGui::InputTextMultiline("Draft JSON",
                                          draftBuffer->data(),
                                          draftBuffer->size(),
                                          ImVec2(-1.0f, 180.0f));
            }
            else
            {
                ImGui::TextDisabled("Draft buffer unavailable.");
            }

            const auto draftText = [draftBuffer]() -> std::string
            {
                return draftBuffer != nullptr
                    ? std::string{draftBuffer->data()}
                    : std::string{};
            };

            if (!commandsAvailable)
                ImGui::BeginDisabled();

            if (ImGui::Button("Update Draft"))
            {
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::UpdateDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }
            ImGui::SameLine();
            if (ImGui::Button("Debounce"))
            {
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::UpdateDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                        .Debounced = true,
                    });
            }
            ImGui::SameLine();
            if (!model.CanValidate)
                ImGui::BeginDisabled();
            if (ImGui::Button("Validate"))
            {
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::ValidateDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }
            if (!model.CanValidate)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (!model.CanPreview)
                ImGui::BeginDisabled();
            if (ImGui::Button("Preview"))
            {
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::PreviewDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }
            if (!model.CanPreview)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (!model.CanActivate)
                ImGui::BeginDisabled();
            if (ImGui::Button("Activate Preview"))
            {
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::ActivatePreview,
                    });
            }
            if (!model.CanActivate)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (!model.CanCancel)
                ImGui::BeginDisabled();
            if (ImGui::Button("Cancel"))
            {
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::CancelDraft,
                    });
            }
            if (!model.CanCancel)
                ImGui::EndDisabled();

            if (!commandsAvailable)
                ImGui::EndDisabled();

            constexpr ImGuiTableFlags tableFlags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp;

            if (ImGui::CollapsingHeader("Recipe Slots",
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto slotKindName = [](const Graphics::RecipeSlotKind kind)
                {
                    switch (kind)
                    {
                    case Graphics::RecipeSlotKind::FixedCore:
                        return "FixedCore";
                    case Graphics::RecipeSlotKind::Extension:
                        return "Extension";
                    }
                    return "Unknown";
                };
                if (ImGui::BeginTable("RenderRecipeSlots", 5, tableFlags))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Kind");
                    ImGui::TableSetupColumn("Schema");
                    ImGui::TableSetupColumn("Editable");
                    ImGui::TableSetupColumn("Reason");
                    ImGui::TableHeadersRow();
                    for (const SandboxEditorRenderRecipeSlotModel& slot :
                         model.Slots)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(slot.StableName.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(
                            slotKindName(slot.Kind));
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(slot.SchemaId.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(slot.Editable ? "yes" : "no");
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextWrapped("%s",
                                           slot.DisabledReason.c_str());
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Binding Overrides",
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable("RenderRecipeBindings", 7, tableFlags))
                {
                    ImGui::TableSetupColumn("Semantic");
                    ImGui::TableSetupColumn("Slot");
                    ImGui::TableSetupColumn("Domain");
                    ImGui::TableSetupColumn("Source");
                    ImGui::TableSetupColumn("Type");
                    ImGui::TableSetupColumn("Editable");
                    ImGui::TableSetupColumn("Reason");
                    ImGui::TableHeadersRow();
                    for (const SandboxEditorRenderRecipeBindingOverrideModel& binding :
                         model.BindingOverrides)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(binding.SemanticName.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(binding.Slot.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(binding.SourceDomain.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(binding.SourceIdentity.c_str());
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%s / %s",
                                    binding.ValueType.c_str(),
                                    binding.ValueFormat.c_str());
                        ImGui::TableSetColumnIndex(5);
                        ImGui::TextUnformatted(binding.Editable ? "yes" : "no");
                        ImGui::TableSetColumnIndex(6);
                        ImGui::TextWrapped("%s",
                                           binding.DisabledReason.c_str());
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Outputs"))
            {
                if (ImGui::BeginTable("RenderRecipeOutputs", 4, tableFlags))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Kind");
                    ImGui::TableSetupColumn("Format");
                    ImGui::TableSetupColumn("Required");
                    ImGui::TableHeadersRow();
                    for (const SandboxEditorRenderRecipeOutputModel& output :
                         model.Outputs)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(output.Name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(output.Kind.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(output.Format.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(output.Required ? "yes" : "no");
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Artifacts"))
            {
                if (model.Artifacts.empty())
                {
                    ImGui::TextDisabled("No render artifacts declared.");
                }
                if (ImGui::BeginTable("RenderRecipeArtifacts", 7, tableFlags))
                {
                    ImGui::TableSetupColumn("Artifact");
                    ImGui::TableSetupColumn("Purpose");
                    ImGui::TableSetupColumn("Kind");
                    ImGui::TableSetupColumn("Status");
                    ImGui::TableSetupColumn("Payload");
                    ImGui::TableSetupColumn("Publish");
                    ImGui::TableSetupColumn("Apply");
                    ImGui::TableHeadersRow();
                    for (const SandboxEditorRenderArtifactRow& artifact :
                         model.Artifacts)
                    {
                        ImGui::PushID(artifact.ArtifactId.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(artifact.ArtifactId.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(artifact.Purpose.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(
                            std::string(ToString(artifact.Kind)).c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(
                            std::string(ToString(artifact.Status)).c_str());
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextUnformatted(artifact.PayloadUri.c_str());
                        ImGui::TableSetColumnIndex(5);
                        const bool publishAvailable =
                            commandsAvailable &&
                            context->RenderArtifacts != nullptr &&
                            artifact.CanPublish;
                        if (!publishAvailable)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Publish"))
                        {
                            (void)ApplySandboxEditorRenderRecipeCommand(
                                *context,
                                SandboxEditorRenderRecipeCommand{
                                    .Kind = SandboxEditorRenderRecipeCommandKind::PublishArtifact,
                                    .ArtifactId = artifact.ArtifactId,
                                    .Provenance = "sandbox-editor",
                                });
                        }
                        if (!publishAvailable)
                            ImGui::EndDisabled();
                        ImGui::TableSetColumnIndex(6);
                        const bool applyAvailable =
                            commandsAvailable &&
                            context->RenderArtifacts != nullptr &&
                            artifact.CanApply;
                        if (!applyAvailable)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Apply"))
                        {
                            (void)ApplySandboxEditorRenderRecipeCommand(
                                *context,
                                SandboxEditorRenderRecipeCommand{
                                    .Kind = SandboxEditorRenderRecipeCommandKind::ApplyArtifact,
                                    .ArtifactId = artifact.ArtifactId,
                                    .Provenance = "sandbox-editor",
                                    .ProjectTarget = "sandbox-render-recipe-artifact",
                                });
                        }
                        if (!applyAvailable)
                            ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

            DrawDiagnostics(model.Diagnostics);
            for (const Graphics::RenderRecipeConfigDiagnostic& diagnostic :
                 model.RecipeDiagnostics)
            {
                ImGui::TextDisabled("%s/%s: %s",
                                    std::string(Graphics::ToString(
                                        diagnostic.State)).c_str(),
                                    std::string(Graphics::ToString(
                                        diagnostic.Code)).c_str(),
                                    diagnostic.Message.c_str());
            }
        }

        void DrawPanelFrame(
            const SandboxEditorPanelFrame& frame,
            const SandboxEditorContext* context,
            std::array<char, 1024>* importPathBuffer,
            std::array<char, 1024>* scenePathBuffer,
            std::array<char, 8192>* renderRecipeDraftBuffer,
            A::AssetPayloadKind* importPayloadKind,
            std::optional<SandboxEditorFileImportResult>* lastImportResult,
            std::optional<SandboxEditorSceneFileResult>* lastSceneFileResult,
            std::array<bool, Detail::kSandboxEditorPanelWindowCount>*
                panelWindowOpen,
            std::array<bool, 15>* domainWindowOpen,
            KMeansUiState* kmeansState,
            MeshDenoiseUiState* denoiseState,
            MeshCurvatureUiState* curvatureState,
            MeshRemeshUiState* remeshState,
            MeshSubdivideUiState* subdivideState,
            MeshSimplifyUiState* simplifyState,
            MeshVertexNormalsUiState* normalsState,
            GraphVertexNormalsUiState* graphNormalsState,
            PointCloudVertexNormalsUiState* pointCloudNormalsState,
            PointCloudOutlierRemovalUiState* pointCloudOutlierState,
            ProgressivePoissonUiState* progressivePoissonState,
            TextureBakeUiState* textureBakeState)
        {
            DrawMainMenuBar(panelWindowOpen, domainWindowOpen);
            DrawDomainWindows(
                context,
                domainWindowOpen,
                kmeansState,
                denoiseState,
                curvatureState,
                remeshState,
                subdivideState,
                simplifyState,
                normalsState,
                graphNormalsState,
                pointCloudNormalsState,
                pointCloudOutlierState,
                progressivePoissonState,
                textureBakeState);

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::Shell,
                                 ImVec2(360.0f, 520.0f)))
            {
                ImGui::TextUnformatted("Promoted runtime editor shell");
                DrawDiagnostics(frame.Diagnostics);
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::SceneHierarchy,
                                 ImVec2(280.0f, 420.0f)))
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
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::Inspector,
                                 ImVec2(360.0f, 420.0f)))
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
                    ImGui::SeparatorText("Property catalog");
                    ImGui::Text("Rows: %zu binding targets: %zu",
                                inspector.PropertyCatalog.Rows.size(),
                                inspector.PropertyCatalog.BindingTargets.size());
                    for (std::size_t propertyIndex = 0u;
                         propertyIndex < inspector.PropertyCatalog.Rows.size() &&
                         propertyIndex < 8u;
                         ++propertyIndex)
                    {
                        const SandboxEditorPropertyCatalogRow& row =
                            inspector.PropertyCatalog.Rows[propertyIndex];
                        ImGui::BulletText("%s / %s / %s",
                                          DebugNameForSandboxEditorPropertyCatalogDomain(
                                              row.Domain),
                                          row.Name.c_str(),
                                          DebugNameForSandboxEditorPropertyCatalogValueKind(
                                              row.ValueKind));
                    }
                    const SandboxEditorProgressiveRenderDataModel& progressive =
                        inspector.Progressive;
                    DrawBoundRenderStateRows(inspector.BoundState);
                    DrawTextureBakeControls(inspector.TextureBake, context, textureBakeState);
                    ImGui::SeparatorText("Progressive render data");
                    ImGui::Text("Shape: %s",
                                std::string(ToString(progressive.Shape)).c_str());
                    ImGui::Text("Bindings: %s generation=%llu",
                                progressive.HasBindings ? "yes" : "no",
                                static_cast<unsigned long long>(
                                    progressive.BindingGeneration));
                    if (progressive.Composition.HasChildren)
                    {
                        ImGui::Text("Composition: children=%u bindings=%u slots=%u pending=%u failed=%u jobs=%u active=%u job failures=%u",
                                    progressive.Composition.ChildCount,
                                    progressive.Composition.ChildBindingsCount,
                                    progressive.Composition.ChildSlotCount,
                                    progressive.Composition.ChildPendingSlotCount,
                                    progressive.Composition.ChildFailedSlotCount,
                                    progressive.Composition.ChildJobCount,
                                    progressive.Composition.ChildActiveJobCount,
                                    progressive.Composition.ChildFailedJobCount);
                    }
                    if (!progressive.Slots.empty())
                    {
                        ImGui::Text("Slots: %zu", progressive.Slots.size());
                        for (std::size_t slotIndex = 0u;
                             slotIndex < progressive.Slots.size();
                             ++slotIndex)
                        {
                            const SandboxEditorProgressiveSlotModel& slot =
                                progressive.Slots[slotIndex];
                            ImGui::PushID(static_cast<int>(slotIndex));
                            ImGui::Text("%s / %s / %s / %s",
                                        std::string(ToString(slot.Lane)).c_str(),
                                        slot.PresentationKey.c_str(),
                                        std::string(ToString(slot.Semantic)).c_str(),
                                        std::string(ToString(slot.Readiness)).c_str());
                            ImGui::Text("Source: %s property=%s",
                                        std::string(ToString(slot.SourceKind)).c_str(),
                                        slot.Property.PropertyName.empty()
                                            ? "(none)"
                                            : slot.Property.PropertyName.c_str());
                            if (!slot.Diagnostic.empty())
                                ImGui::TextWrapped("%s", slot.Diagnostic.c_str());

                            if (context != nullptr &&
                                (slot.Semantic == ProgressiveSlotSemantic::Albedo ||
                                 slot.Semantic == ProgressiveSlotSemantic::PointColor ||
                                 slot.Semantic == ProgressiveSlotSemantic::LineColor))
                            {
                                glm::vec4 color = slot.UniformDefault.Vector;
                                if (ImGui::ColorEdit4("Default color", &color.x))
                                {
                                    ProgressiveDefaultValue value =
                                        slot.UniformDefault;
                                    value.Kind = ProgressivePropertyValueKind::Vec4;
                                    value.Vector = color;
                                    (void)ApplySandboxEditorProgressiveSlotDefaultCommand(
                                        *context,
                                        SandboxEditorProgressiveSlotDefaultCommand{
                                            .StableEntityId =
                                                inspector.Entity.StableEntityId,
                                            .PresentationKey =
                                                slot.PresentationKey,
                                            .Semantic = slot.Semantic,
                                            .Value = value,
                                            .Enabled = slot.Enabled,
                                        });
                                }
                            }

                            if (context != nullptr &&
                                !slot.PropertyOptions.empty())
                            {
                                const char* currentProperty =
                                    slot.Property.PropertyName.empty()
                                        ? "(uniform/default)"
                                        : slot.Property.PropertyName.c_str();
                                if (ImGui::BeginCombo("Source property",
                                                      currentProperty))
                                {
                                    for (const SandboxEditorProgressivePropertyOptionModel&
                                             option : slot.PropertyOptions)
                                    {
                                        if (!option.Compatible)
                                            ImGui::BeginDisabled();
                                        const bool selected =
                                            option.Descriptor.PropertyName ==
                                            slot.Property.PropertyName;
                                        if (ImGui::Selectable(
                                                option.Descriptor.PropertyName.c_str(),
                                                selected) &&
                                            option.Compatible)
                                        {
                                            (void)ApplySandboxEditorProgressiveSlotPropertyCommand(
                                                *context,
                                                SandboxEditorProgressiveSlotPropertyCommand{
                                                    .StableEntityId =
                                                        inspector.Entity.StableEntityId,
                                                    .PresentationKey =
                                                        slot.PresentationKey,
                                                    .Semantic = slot.Semantic,
                                                    .SourceKind =
                                                        IsSurfaceTextureSemantic(
                                                            slot.Semantic)
                                                            ? ProgressiveSlotSourceKind::PropertyBake
                                                            : ProgressiveSlotSourceKind::PropertyBuffer,
                                                    .Domain =
                                                        option.Descriptor.Domain,
                                                    .ExpectedValueKind =
                                                        option.Descriptor.ExpectedValueKind,
                                                    .PropertyName =
                                                        option.Descriptor.PropertyName,
                                                });
                                        }
                                        if (!option.Compatible)
                                        {
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("%s",
                                                option.DisabledReason.c_str());
                                            ImGui::EndDisabled();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            ImGui::PopID();
                        }
                    }
                    if (!progressive.Jobs.empty())
                    {
                        ImGui::Text("Derived jobs: %zu", progressive.Jobs.size());
                        for (const SandboxEditorProgressiveJobModel& job :
                             progressive.Jobs)
                        {
                            ImGui::BulletText("%s %s %.0f%% deps=%zu %s",
                                              job.Name.c_str(),
                                              std::string(ToString(job.Status)).c_str(),
                                              job.NormalizedProgress * 100.0f,
                                              job.Dependencies.size(),
                                              job.Diagnostic.c_str());
                        }
                    }
                    DrawDiagnostics(progressive.Diagnostics);
                    DrawDiagnostics(inspector.Diagnostics);
                }
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::SelectionDetails,
                                 ImVec2(0.0f, 0.0f)))
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
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::FileScene,
                                 ImVec2(0.0f, 0.0f)))
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
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::FileImport,
                                 ImVec2(0.0f, 0.0f)))
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
                    if (result->GeneratedTextureAssetsCreated > 0u)
                    {
                        ImGui::Text("Generated textures: %llu",
                                    static_cast<unsigned long long>(
                                        result->GeneratedTextureAssetsCreated));
                    }
                    if (result->TextureUploadRequests > 0u)
                    {
                        ImGui::Text("Texture upload requests: %llu",
                                    static_cast<unsigned long long>(
                                        result->TextureUploadRequests));
                    }
                    if (result->GeneratedTextureUploadRequests > 0u)
                    {
                        ImGui::Text("Generated texture upload requests: %llu",
                                    static_cast<unsigned long long>(
                                        result->GeneratedTextureUploadRequests));
                    }
                }
                DrawAssetImportQueue(frame.AssetImportQueue, context);
                DrawDiagnostics(frame.FileImport.Diagnostics);
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::FrameGraph,
                                 ImVec2(0.0f, 0.0f)))
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
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::RenderRecipe,
                                 ImVec2(0.0f, 0.0f)))
            {
                DrawRenderRecipeEditor(frame.RenderRecipe,
                                       context,
                                       renderRecipeDraftBuffer);
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::CameraRender,
                                 ImVec2(0.0f, 0.0f)))
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
                ImGui::End();
            }

            if (BeginPanelWindow(panelWindowOpen,
                                 SandboxEditorPanelWindowKind::GeometryVisualization,
                                 ImVec2(0.0f, 0.0f)))
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
                                MakeUniformVisualizationConfigCommandFromModel(
                                    frame.Visualization.SelectedStableId,
                                    frame.Visualization.Visualization,
                                    SandboxEditorVisualizationTarget::Entity,
                                    frame.Visualization.Visualization.Color));
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear vis"))
                        {
                            (void)ApplySandboxEditorVisualizationConfigCommand(
                                *context,
                                SandboxEditorVisualizationConfigCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .Target =
                                        SandboxEditorVisualizationTarget::Entity,
                                    .EnableConfig = false,
                                });
                        }
                        DrawUniformVisualizationColorEdit(
                            frame.Visualization.Visualization,
                            *context,
                            frame.Visualization.SelectedStableId,
                            SandboxEditorVisualizationTarget::Entity,
                            true);
                        DrawVisualizationPropertyPresets(
                            frame.Visualization.Properties,
                            *context,
                            frame.Visualization.SelectedStableId,
                            SandboxEditorVisualizationTarget::Entity,
                            true);
                    }
                }
                DrawDiagnostics(frame.Visualization.Diagnostics);
                ImGui::End();
            }
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
        case SandboxEditorDiagnosticCode::RenderRecipeCommandsUnavailable:
            return "RenderRecipeCommandsUnavailable";
        case SandboxEditorDiagnosticCode::InvalidVisualizationProperty:
            return "InvalidVisualizationProperty";
        case SandboxEditorDiagnosticCode::InvalidVertexChannelBinding:
            return "InvalidVertexChannelBinding";
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
        case SandboxEditorCommandStatus::InvalidVertexChannelBinding:
            return "InvalidVertexChannelBinding";
        case SandboxEditorCommandStatus::InvalidProcessingParameters:
            return "InvalidProcessingParameters";
        case SandboxEditorCommandStatus::GeometryProcessingFailed:
            return "GeometryProcessingFailed";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorRenderRecipeDraftState(
        const SandboxEditorRenderRecipeDraftState state) noexcept
    {
        using State = SandboxEditorRenderRecipeDraftState;
        switch (state)
        {
        case State::InactiveDraft:
            return "InactiveDraft";
        case State::Debounced:
            return "Debounced";
        case State::Validated:
            return "Validated";
        case State::Rejected:
            return "Rejected";
        case State::Previewed:
            return "Previewed";
        case State::Activated:
            return "Activated";
        case State::Canceled:
            return "Canceled";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorRenderRecipeCommandKind(
        const SandboxEditorRenderRecipeCommandKind kind) noexcept
    {
        using Kind = SandboxEditorRenderRecipeCommandKind;
        switch (kind)
        {
        case Kind::UpdateDraft:
            return "UpdateDraft";
        case Kind::ValidateDraft:
            return "ValidateDraft";
        case Kind::PreviewDraft:
            return "PreviewDraft";
        case Kind::ActivatePreview:
            return "ActivatePreview";
        case Kind::CancelDraft:
            return "CancelDraft";
        case Kind::PublishArtifact:
            return "PublishArtifact";
        case Kind::ApplyArtifact:
            return "ApplyArtifact";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorRenderRecipeCommandStatus(
        const SandboxEditorRenderRecipeCommandStatus status) noexcept
    {
        using Status = SandboxEditorRenderRecipeCommandStatus;
        switch (status)
        {
        case Status::NoChange:
            return "NoChange";
        case Status::DraftUpdated:
            return "DraftUpdated";
        case Status::Debounced:
            return "Debounced";
        case Status::Validated:
            return "Validated";
        case Status::ValidationFailed:
            return "ValidationFailed";
        case Status::Previewed:
            return "Previewed";
        case Status::PreviewFailed:
            return "PreviewFailed";
        case Status::Activated:
            return "Activated";
        case Status::Canceled:
            return "Canceled";
        case Status::Published:
            return "Published";
        case Status::Applied:
            return "Applied";
        case Status::ActivationFailed:
            return "ActivationFailed";
        case Status::MissingRecipeContext:
            return "MissingRecipeContext";
        case Status::MissingEditorState:
            return "MissingEditorState";
        case Status::MissingArtifactRegistry:
            return "MissingArtifactRegistry";
        case Status::ArtifactCommandFailed:
            return "ArtifactCommandFailed";
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

    const char* DebugNameForSandboxEditorVisualizationTarget(
        const SandboxEditorVisualizationTarget target) noexcept
    {
        using Target = SandboxEditorVisualizationTarget;
        switch (target)
        {
        case Target::Entity:
            return "Entity";
        case Target::Surface:
            return "Surface";
        case Target::Edges:
            return "Edges";
        case Target::Points:
            return "Points";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorPropertyCatalogDomain(
        const SandboxEditorPropertyCatalogDomain domain) noexcept
    {
        using Domain = SandboxEditorPropertyCatalogDomain;
        switch (domain)
        {
        case Domain::MeshVertices:
            return "MeshVertices";
        case Domain::MeshEdges:
            return "MeshEdges";
        case Domain::MeshHalfedges:
            return "MeshHalfedges";
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

    const char* DebugNameForSandboxEditorPropertyCatalogValueKind(
        const SandboxEditorPropertyCatalogValueKind kind) noexcept
    {
        using Kind = SandboxEditorPropertyCatalogValueKind;
        switch (kind)
        {
        case Kind::Unknown:
            return "Unknown";
        case Kind::ScalarFloat:
            return "ScalarFloat";
        case Kind::ScalarDouble:
            return "ScalarDouble";
        case Kind::UInt32:
            return "UInt32";
        case Kind::Vec2:
            return "Vec2";
        case Kind::Vec3:
            return "Vec3";
        case Kind::Vec4:
            return "Vec4";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorBoundRenderStateRowKind(
        const SandboxEditorBoundRenderStateRowKind kind) noexcept
    {
        using Kind = SandboxEditorBoundRenderStateRowKind;
        switch (kind)
        {
        case Kind::RenderHint:
            return "RenderHint";
        case Kind::ProgressiveSlot:
            return "ProgressiveSlot";
        case Kind::DerivedJob:
            return "DerivedJob";
        case Kind::CompositionSummary:
            return "CompositionSummary";
        case Kind::DisabledCommand:
            return "DisabledCommand";
        }
        return "Unknown";
    }

    std::vector<SandboxEditorGeometryProcessingMenuItem>
    GetSandboxEditorGeometryProcessingMenuItems(
        const SandboxEditorDomainWindowKind kind)
    {
        using Domain = SandboxEditorGeometryProcessingDomain;
        switch (kind)
        {
        case SandboxEditorDomainWindowKind::Mesh:
            return {
                {.Domain = Domain::MeshVertices,
                 .Label = "Vertices",
                 .HasNormalsMethod = true,
                 .HasDenoiseMethod = true,
                 .HasCurvatureMethod = true,
                 .HasRemeshMethod = true,
                 .HasSubdivideMethod = true,
                 .HasSimplifyMethod = true},
                {.Domain = Domain::MeshEdges, .Label = "Edges"},
                {.Domain = Domain::MeshFaces, .Label = "Faces"},
            };
        case SandboxEditorDomainWindowKind::Graph:
            return {
                {.Domain = Domain::GraphVertices,
                 .Label = "Vertices",
                 .HasNormalsMethod = true},
                {.Domain = Domain::GraphEdges, .Label = "Edges"},
                {.Domain = Domain::GraphHalfedges, .Label = "Halfedges"},
            };
        case SandboxEditorDomainWindowKind::PointCloud:
            return {
                {.Domain = Domain::PointCloudPoints,
                 .Label = "Vertices",
                 .HasNormalsMethod = true},
            };
        }
        return {};
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
        case SandboxEditorGeometryProcessingAlgorithm::MeshDenoise:
        case SandboxEditorGeometryProcessingAlgorithm::Curvature:
            return Domain::MeshVertices;
        case SandboxEditorGeometryProcessingAlgorithm::Remeshing:
        case SandboxEditorGeometryProcessingAlgorithm::Simplification:
        case SandboxEditorGeometryProcessingAlgorithm::Smoothing:
        case SandboxEditorGeometryProcessingAlgorithm::Subdivision:
        case SandboxEditorGeometryProcessingAlgorithm::Repair:
            return kMeshTopologyDomains;
        case SandboxEditorGeometryProcessingAlgorithm::NormalEstimation:
            return Domain::MeshVertices |
                   Domain::GraphVertices |
                   Domain::PointCloudPoints;
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
        case SandboxEditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
        case SandboxEditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            return Domain::PointCloudPoints;
        case SandboxEditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
            return Domain::MeshVertices | Domain::PointCloudPoints;
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
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        capabilities.Domains = DomainsForSourceView(view);
        capabilities.HasEditableSurfaceMesh =
            availability.Sources.ProvenanceDomain == GS::Domain::Mesh &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshVertex) &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshHalfedge) &&
            SupportsGeometryElementDomain(
                availability,
                GeometryElementDomain::MeshFace);
        return capabilities;
    }

    std::vector<SandboxEditorGeometryProcessingEntry>
    ResolveSandboxEditorGeometryProcessingEntries(
        const SandboxEditorGeometryProcessingCapabilities capabilities)
    {
        static constexpr std::array<SandboxEditorGeometryProcessingAlgorithm, 22>
            kAlgorithmOrder{
                SandboxEditorGeometryProcessingAlgorithm::KMeans,
                SandboxEditorGeometryProcessingAlgorithm::NormalEstimation,
                SandboxEditorGeometryProcessingAlgorithm::MeshDenoise,
                SandboxEditorGeometryProcessingAlgorithm::Curvature,
                SandboxEditorGeometryProcessingAlgorithm::Registration,
                SandboxEditorGeometryProcessingAlgorithm::BilateralFilter,
                SandboxEditorGeometryProcessingAlgorithm::OutlierEstimation,
                SandboxEditorGeometryProcessingAlgorithm::KernelDensity,
                SandboxEditorGeometryProcessingAlgorithm::ProgressivePoissonSampling,
                SandboxEditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval,
                SandboxEditorGeometryProcessingAlgorithm::RadiusOutlierRemoval,
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

    namespace
    {
        [[nodiscard]] const Graphics::RecipeExtensionSlotDescriptor*
        FindRecipeSlot(const Graphics::RenderRecipeDescriptor& recipe,
                       const std::string_view stableName) noexcept
        {
            const auto it = std::find_if(
                recipe.Slots.begin(),
                recipe.Slots.end(),
                [stableName](const Graphics::RecipeExtensionSlotDescriptor& slot)
                {
                    return slot.StableName == stableName;
                });
            return it == recipe.Slots.end() ? nullptr : &*it;
        }

        [[nodiscard]] bool RendererDeclaresSlot(
            const Graphics::RendererDescriptor& renderer,
            const std::string_view stableName) noexcept
        {
            return std::find(renderer.DeclaredRecipeSlots.begin(),
                             renderer.DeclaredRecipeSlots.end(),
                             stableName) != renderer.DeclaredRecipeSlots.end();
        }

        [[nodiscard]] const char* DebugNameForBindingSourceDomain(
            const Graphics::BindingSourceDomain domain) noexcept
        {
            using Domain = Graphics::BindingSourceDomain;
            switch (domain)
            {
            case Domain::Unknown: return "Unknown";
            case Domain::MeshVertex: return "MeshVertex";
            case Domain::MeshFace: return "MeshFace";
            case Domain::GraphNode: return "GraphNode";
            case Domain::GraphEdge: return "GraphEdge";
            case Domain::PointCloudPoint: return "PointCloudPoint";
            case Domain::Scene: return "Scene";
            case Domain::Generated: return "Generated";
            case Domain::Runtime: return "Runtime";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* DebugNameForBindingValueType(
            const Graphics::BindingValueType type) noexcept
        {
            using Type = Graphics::BindingValueType;
            switch (type)
            {
            case Type::Unknown: return "Unknown";
            case Type::Float: return "Float";
            case Type::UInt: return "UInt";
            case Type::Vec2: return "Vec2";
            case Type::Vec3: return "Vec3";
            case Type::Vec4: return "Vec4";
            case Type::Mat4: return "Mat4";
            case Type::Texture2D: return "Texture2D";
            case Type::Buffer: return "Buffer";
            case Type::AccelerationStructure: return "AccelerationStructure";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* DebugNameForViewKind(
            const Graphics::ViewKind view) noexcept
        {
            using View = Graphics::ViewKind;
            switch (view)
            {
            case View::Camera: return "Camera";
            case View::NonCamera: return "NonCamera";
            case View::Picking: return "Picking";
            case View::Metrics: return "Metrics";
            case View::Preview: return "Preview";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* DebugNameForOutputTargetKind(
            const Graphics::OutputTargetKind target) noexcept
        {
            using Target = Graphics::OutputTargetKind;
            switch (target)
            {
            case Target::Window: return "Window";
            case Target::OffscreenTexture: return "OffscreenTexture";
            case Target::File: return "File";
            case Target::ReadbackBuffer: return "ReadbackBuffer";
            case Target::PublishedArtifact: return "PublishedArtifact";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* DebugNameForInteractionMode(
            const Graphics::InteractionMode mode) noexcept
        {
            switch (mode)
            {
            case Graphics::InteractionMode::Interactive:
                return "Interactive";
            case Graphics::InteractionMode::Headless:
                return "Headless";
            }
            return "Unknown";
        }

        [[nodiscard]] std::vector<std::string> CapabilityNames(
            const std::vector<Graphics::RendererCapability>& capabilities)
        {
            std::vector<std::string> names{};
            names.reserve(capabilities.size());
            for (const Graphics::RendererCapability capability : capabilities)
            {
                names.emplace_back(Graphics::ToString(capability));
            }
            return names;
        }

        [[nodiscard]] SandboxEditorRenderRecipeSlotModel BuildRenderRecipeSlotRow(
            const Graphics::RendererDescriptor& renderer,
            const Graphics::RecipeExtensionSlotDescriptor& slot)
        {
            const bool declared = RendererDeclaresSlot(renderer, slot.StableName);
            const bool editable =
                declared && slot.Kind == Graphics::RecipeSlotKind::Extension;
            std::string disabledReason{};
            if (!declared)
                disabledReason = "not declared by renderer";
            else if (slot.Kind == Graphics::RecipeSlotKind::FixedCore)
                disabledReason = "fixed renderer core";

            return SandboxEditorRenderRecipeSlotModel{
                .StableName = slot.StableName,
                .Kind = slot.Kind,
                .SchemaId = slot.SchemaId,
                .Defaults = slot.Defaults,
                .RequiredCapabilities = CapabilityNames(slot.RequiredCapabilities),
                .AllowedBindingRoles = slot.AllowedBindingRoles,
                .UsedBindingRoles = slot.UsedBindingRoles,
                .DeclaredByRenderer = declared,
                .Editable = editable,
                .DisabledReason = std::move(disabledReason),
            };
        }

        [[nodiscard]] SandboxEditorRenderRecipeBindingOverrideModel
        BuildRenderRecipeBindingRow(const Graphics::BindingIntent& intent)
        {
            const bool required =
                intent.Requirement == Graphics::BindingRequirement::Required;
            return SandboxEditorRenderRecipeBindingOverrideModel{
                .SemanticName = intent.SemanticName,
                .Slot = intent.ConsumerRole,
                .SourceDomain = DebugNameForBindingSourceDomain(intent.SourceDomain),
                .SourceIdentity = intent.SourceIdentity,
                .SourceRevision = intent.SourceRevision,
                .ValueType = DebugNameForBindingValueType(intent.ValueType),
                .ValueFormat = intent.ValueFormat,
                .Required = required,
                .Editable = !required,
                .DisabledReason = required ? "required binding" : "",
            };
        }

        [[nodiscard]] SandboxEditorRenderRecipeOutputModel BuildRenderRecipeOutputRow(
            const Graphics::ViewOutputDescriptor& output)
        {
            return SandboxEditorRenderRecipeOutputModel{
                .Name = output.Name,
                .Kind = std::string{Graphics::ToString(output.Kind)},
                .Format = output.Format,
                .Required = output.Required,
            };
        }

        [[nodiscard]] SandboxEditorRenderArtifactRow BuildRenderArtifactRow(
            const RenderArtifactRecord& record)
        {
            const RenderArtifactUiStatus status = ToUiStatus(record);
            const bool canPublish =
                status == RenderArtifactPublicationState::Unpublished;
            const bool canApply =
                status == RenderArtifactPublicationState::Published &&
                record.Kind == RenderArtifactPublicationKind::CandidateProjectResult;
            std::string disabledReason{};
            if (!canPublish && !canApply)
            {
                disabledReason = std::string{"state:"} +
                                 std::string{ToString(status)};
            }
            return SandboxEditorRenderArtifactRow{
                .ArtifactId = record.Metadata.ArtifactId,
                .Purpose = record.Metadata.Purpose,
                .Kind = record.Kind,
                .Status = status,
                .PayloadUri = record.PayloadUri,
                .ProducerLabel = record.ProducerLabel,
                .CanPublish = canPublish,
                .CanApply = canApply,
                .DisabledReason = std::move(disabledReason),
            };
        }

        [[nodiscard]] SandboxEditorRenderRecipeCommandResult
        MakeRenderRecipeCommandResult(
            const SandboxEditorRenderRecipeCommandStatus status,
            const SandboxEditorRenderRecipeEditorState* state)
        {
            SandboxEditorRenderRecipeCommandResult result{
                .Status = status,
            };
            if (state != nullptr)
            {
                result.DraftState = state->DraftState;
                if (state->HasLastPreview)
                {
                    result.ValidationState = state->LastPreview.State;
                    result.RecipeDiagnostics = state->LastPreview.Diagnostics;
                }
            }
            return result;
        }

        [[nodiscard]] SandboxEditorRenderRecipeCommandResult
        MakeRenderRecipeArtifactResult(
            const RenderArtifactOperationResult& operation,
            const SandboxEditorRenderRecipeCommandStatus successStatus)
        {
            return SandboxEditorRenderRecipeCommandResult{
                .Status = operation.Succeeded()
                    ? successStatus
                    : SandboxEditorRenderRecipeCommandStatus::ArtifactCommandFailed,
                .ArtifactStatus = operation.Status,
                .ArtifactState = operation.State,
                .ArtifactId = operation.ArtifactId,
                .Revision = operation.Revision,
                .ProjectMutationAuthorized = operation.ProjectMutationAuthorized,
                .ArtifactDiagnostics = operation.Diagnostics,
            };
        }
    }

    SandboxEditorRenderRecipeEditorModel
    BuildSandboxEditorRenderRecipeEditorModel(const SandboxEditorContext& context)
    {
        SandboxEditorRenderRecipeEditorModel model{};
        if (context.RenderRecipeContext == nullptr)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::RenderRecipeCommandsUnavailable,
                          "Render recipe context is unavailable.");
            return model;
        }

        model.Available = true;
        const Graphics::RenderRecipeConfigContext& recipeContext =
            *context.RenderRecipeContext;
        const SandboxEditorRenderRecipeEditorState* state =
            context.RenderRecipeEditorState;

        const RuntimeRenderRecipeState* runtimeState =
            context.RenderRecipeRuntimeState;
        const bool useRuntimeActiveOverride =
            runtimeState != nullptr && runtimeState->HasActiveConfig;
        const bool useEditorActiveOverride =
            !useRuntimeActiveOverride &&
            state != nullptr &&
            state->HasActiveOverride;
        const Graphics::RenderRecipeDescriptor& recipe =
            useRuntimeActiveOverride
                ? runtimeState->ActiveConfig.Preview.Recipe
                : (useEditorActiveOverride ? state->ActiveRecipe : recipeContext.BaseRecipe);
        const Graphics::ViewOutputRecipeDescriptor& viewOutput =
            useRuntimeActiveOverride
                ? runtimeState->ActiveConfig.Preview.ViewOutput
                : (useEditorActiveOverride ? state->ActiveViewOutput : recipeContext.BaseViewOutput);
        const Graphics::BindingSet& bindings =
            useRuntimeActiveOverride
                ? runtimeState->ActiveConfig.Preview.Bindings
                : (useEditorActiveOverride ? state->ActiveBindings : recipeContext.BaseBindings);

        model.RendererId = recipeContext.Renderer.Id;
        model.ActiveRecipeId = recipe.RecipeId;
        model.ActiveViewOutputRecipeId = viewOutput.RecipeId;
        model.ViewKind = DebugNameForViewKind(viewOutput.View);
        model.OutputTarget = DebugNameForOutputTargetKind(viewOutput.Target);
        model.InteractionMode = DebugNameForInteractionMode(viewOutput.Mode);
        model.ViewportWidth = viewOutput.ViewportWidth;
        model.ViewportHeight = viewOutput.ViewportHeight;
        model.RenderScale = viewOutput.RenderScale;
        model.CaptureRequested = viewOutput.CaptureRequested;
        model.ReadbackRequested = viewOutput.ReadbackRequested;

        if (state != nullptr)
        {
            model.DraftSourceId = state->DraftSourceId;
            model.DraftState = state->DraftState;
            model.DraftRevision = state->DraftRevision;
            model.ActiveRevision = state->ActiveRevision;
            if (state->HasLastPreview)
            {
                model.ValidationState = state->LastPreview.State;
                model.DraftRecipeId = state->LastPreview.Preview.Recipe.RecipeId;
                model.ParsedSlotCount =
                    state->LastPreview.Preview.ParsedSlotCount;
                model.ParsedBindingOverrideCount =
                    state->LastPreview.Preview.ParsedBindingOverrideCount;
                model.RecipeDiagnostics = state->LastPreview.Diagnostics;
            }
        }

        if (!context.RenderRecipeCommandsAvailable)
        {
            AddDiagnostic(model.Diagnostics,
                          SandboxEditorDiagnosticCode::RenderRecipeCommandsUnavailable,
                          "Render recipe command seams are unavailable.");
        }

        model.CanValidate =
            context.RenderRecipeCommandsAvailable &&
            state != nullptr &&
            !state->DraftDocument.empty();
        model.CanPreview = model.CanValidate;
        model.CanActivate =
            context.RenderRecipeCommandsAvailable &&
            state != nullptr &&
            state->HasLastPreview &&
            Graphics::IsConfigUsable(state->LastPreview);
        model.CanCancel =
            context.RenderRecipeCommandsAvailable &&
            state != nullptr &&
            state->DraftState != SandboxEditorRenderRecipeDraftState::InactiveDraft &&
            state->DraftState != SandboxEditorRenderRecipeDraftState::Canceled;

        model.Slots.reserve(recipeContext.Renderer.DeclaredRecipeSlots.size() +
                            recipe.Slots.size());
        for (const std::string& slotName : recipeContext.Renderer.DeclaredRecipeSlots)
        {
            if (const Graphics::RecipeExtensionSlotDescriptor* slot =
                    FindRecipeSlot(recipe, slotName);
                slot != nullptr)
            {
                model.Slots.push_back(
                    BuildRenderRecipeSlotRow(recipeContext.Renderer, *slot));
            }
            else
            {
                model.Slots.push_back(SandboxEditorRenderRecipeSlotModel{
                    .StableName = slotName,
                    .DeclaredByRenderer = true,
                    .Editable = true,
                });
            }
        }
        for (const Graphics::RecipeExtensionSlotDescriptor& slot : recipe.Slots)
        {
            if (!RendererDeclaresSlot(recipeContext.Renderer, slot.StableName))
            {
                model.Slots.push_back(
                    BuildRenderRecipeSlotRow(recipeContext.Renderer, slot));
            }
        }

        model.BindingOverrides.reserve(bindings.Intents.size());
        for (const Graphics::BindingIntent& intent : bindings.Intents)
        {
            model.BindingOverrides.push_back(BuildRenderRecipeBindingRow(intent));
        }

        model.Outputs.reserve(viewOutput.Outputs.size());
        for (const Graphics::ViewOutputDescriptor& output : viewOutput.Outputs)
        {
            model.Outputs.push_back(BuildRenderRecipeOutputRow(output));
        }

        if (context.RenderArtifacts != nullptr)
        {
            std::vector<RenderArtifactRecord> artifacts =
                context.RenderArtifacts->Snapshot();
            std::sort(artifacts.begin(),
                      artifacts.end(),
                      [](const RenderArtifactRecord& lhs,
                         const RenderArtifactRecord& rhs)
                      {
                          return lhs.Metadata.ArtifactId <
                                 rhs.Metadata.ArtifactId;
                      });
            model.Artifacts.reserve(artifacts.size());
            for (const RenderArtifactRecord& artifact : artifacts)
            {
                model.Artifacts.push_back(BuildRenderArtifactRow(artifact));
            }
        }
        return model;
    }

    SandboxEditorRenderRecipeCommandResult
    ApplySandboxEditorRenderRecipeCommand(
        const SandboxEditorContext& context,
        const SandboxEditorRenderRecipeCommand& command)
    {
        SandboxEditorRenderRecipeEditorState* state =
            context.RenderRecipeEditorState;
        if (state == nullptr)
        {
            return SandboxEditorRenderRecipeCommandResult{
                .Status = SandboxEditorRenderRecipeCommandStatus::MissingEditorState,
            };
        }

        using Kind = SandboxEditorRenderRecipeCommandKind;
        switch (command.Kind)
        {
        case Kind::UpdateDraft:
        {
            if (state->DraftDocument == command.Document &&
                state->DraftSourceId == command.SourceId &&
                !command.Debounced)
            {
                return MakeRenderRecipeCommandResult(
                    SandboxEditorRenderRecipeCommandStatus::NoChange,
                    state);
            }
            state->DraftDocument = command.Document;
            if (!command.SourceId.empty())
                state->DraftSourceId = command.SourceId;
            state->HasLastPreview = false;
            ++state->DraftRevision;
            state->DraftState = command.Debounced
                ? SandboxEditorRenderRecipeDraftState::Debounced
                : SandboxEditorRenderRecipeDraftState::InactiveDraft;
            return MakeRenderRecipeCommandResult(
                command.Debounced
                    ? SandboxEditorRenderRecipeCommandStatus::Debounced
                    : SandboxEditorRenderRecipeCommandStatus::DraftUpdated,
                state);
        }
        case Kind::ValidateDraft:
        case Kind::PreviewDraft:
        {
            if (!context.PreviewRenderRecipeDocument)
            {
                return MakeRenderRecipeCommandResult(
                    SandboxEditorRenderRecipeCommandStatus::MissingRecipeContext,
                    state);
            }
            if (!command.Document.empty())
            {
                state->DraftDocument = command.Document;
                ++state->DraftRevision;
            }
            if (!command.SourceId.empty())
                state->DraftSourceId = command.SourceId;

            state->LastPreview = context.PreviewRenderRecipeDocument(
                state->DraftDocument,
                state->DraftSourceId);
            state->HasLastPreview = true;
            const bool usable = Graphics::IsConfigUsable(state->LastPreview);
            if (usable)
            {
                state->DraftState = command.Kind == Kind::ValidateDraft
                    ? SandboxEditorRenderRecipeDraftState::Validated
                    : SandboxEditorRenderRecipeDraftState::Previewed;
                return MakeRenderRecipeCommandResult(
                    command.Kind == Kind::ValidateDraft
                        ? SandboxEditorRenderRecipeCommandStatus::Validated
                        : SandboxEditorRenderRecipeCommandStatus::Previewed,
                    state);
            }

            state->DraftState = SandboxEditorRenderRecipeDraftState::Rejected;
            return MakeRenderRecipeCommandResult(
                command.Kind == Kind::ValidateDraft
                    ? SandboxEditorRenderRecipeCommandStatus::ValidationFailed
                    : SandboxEditorRenderRecipeCommandStatus::PreviewFailed,
                state);
        }
        case Kind::ActivatePreview:
        {
            if (!state->HasLastPreview ||
                !Graphics::IsConfigUsable(state->LastPreview))
            {
                return MakeRenderRecipeCommandResult(
                    SandboxEditorRenderRecipeCommandStatus::PreviewFailed,
                    state);
            }
            if (context.ApplyRenderRecipePreview)
            {
                const RuntimeRenderRecipeApplyResult applyResult =
                    context.ApplyRenderRecipePreview(state->LastPreview);
                if (!applyResult.Succeeded())
                {
                    return MakeRenderRecipeCommandResult(
                        SandboxEditorRenderRecipeCommandStatus::ActivationFailed,
                        state);
                }
            }
            state->ActiveRecipe = state->LastPreview.Preview.Recipe;
            state->ActiveViewOutput = state->LastPreview.Preview.ViewOutput;
            state->ActiveBindings = state->LastPreview.Preview.Bindings;
            state->HasActiveOverride = true;
            state->DraftState = SandboxEditorRenderRecipeDraftState::Activated;
            ++state->ActiveRevision;
            return MakeRenderRecipeCommandResult(
                SandboxEditorRenderRecipeCommandStatus::Activated,
                state);
        }
        case Kind::CancelDraft:
        {
            if (state->DraftDocument.empty() &&
                !state->HasLastPreview &&
                (state->DraftState ==
                     SandboxEditorRenderRecipeDraftState::InactiveDraft ||
                 state->DraftState ==
                     SandboxEditorRenderRecipeDraftState::Canceled))
            {
                return MakeRenderRecipeCommandResult(
                    SandboxEditorRenderRecipeCommandStatus::NoChange,
                    state);
            }
            state->DraftDocument.clear();
            state->HasLastPreview = false;
            state->DraftState = SandboxEditorRenderRecipeDraftState::Canceled;
            ++state->DraftRevision;
            return MakeRenderRecipeCommandResult(
                SandboxEditorRenderRecipeCommandStatus::Canceled,
                state);
        }
        case Kind::PublishArtifact:
        {
            if (context.RenderArtifacts == nullptr)
            {
                return SandboxEditorRenderRecipeCommandResult{
                    .Status = SandboxEditorRenderRecipeCommandStatus::MissingArtifactRegistry,
                };
            }
            const std::string targetUri = command.TargetUri.empty()
                ? "sandbox://render-artifacts/" + command.ArtifactId
                : command.TargetUri;
            RenderArtifactOperationResult operation =
                context.RenderArtifacts->PublishArtifact(
                    RenderArtifactPublishCommand{
                        .ArtifactId = command.ArtifactId,
                        .Provenance = command.Provenance,
                        .TargetUri = targetUri,
                        .Label = command.Label.empty()
                            ? std::string{"Publish Render Artifact"}
                            : command.Label,
                        .UndoLabel = command.UndoLabel.empty()
                            ? std::string{"Unpublish Render Artifact"}
                            : command.UndoLabel,
                    });
            return MakeRenderRecipeArtifactResult(
                operation,
                SandboxEditorRenderRecipeCommandStatus::Published);
        }
        case Kind::ApplyArtifact:
        {
            if (context.RenderArtifacts == nullptr)
            {
                return SandboxEditorRenderRecipeCommandResult{
                    .Status = SandboxEditorRenderRecipeCommandStatus::MissingArtifactRegistry,
                };
            }
            RenderArtifactOperationResult operation =
                context.RenderArtifacts->ApplyArtifact(
                    RenderArtifactApplyCommand{
                        .ArtifactId = command.ArtifactId,
                        .Provenance = command.Provenance,
                        .ProjectTarget = command.ProjectTarget.empty()
                            ? std::string{"sandbox-render-recipe-artifact"}
                            : command.ProjectTarget,
                        .Label = command.Label.empty()
                            ? std::string{"Apply Render Artifact"}
                            : command.Label,
                        .UndoLabel = command.UndoLabel.empty()
                            ? std::string{"Revert Render Artifact Apply"}
                            : command.UndoLabel,
                    });
            return MakeRenderRecipeArtifactResult(
                operation,
                SandboxEditorRenderRecipeCommandStatus::Applied);
        }
        }

        return MakeRenderRecipeCommandResult(
            SandboxEditorRenderRecipeCommandStatus::NoChange,
            state);
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
        case SandboxEditorGeometryProcessingAlgorithm::MeshDenoise:
            return "Mesh Denoise";
        case SandboxEditorGeometryProcessingAlgorithm::Curvature:
            return "Curvature";
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
            return "Normals";
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
        case SandboxEditorGeometryProcessingAlgorithm::StatisticalOutlierRemoval:
            return "Statistical Outlier Removal";
        case SandboxEditorGeometryProcessingAlgorithm::RadiusOutlierRemoval:
            return "Radius Outlier Removal";
        case SandboxEditorGeometryProcessingAlgorithm::ProgressivePoissonSampling:
            return "Progressive Poisson Sampling";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorProgressivePoissonChannel(
        const SandboxEditorProgressivePoissonChannel channel) noexcept
    {
        switch (channel)
        {
        case SandboxEditorProgressivePoissonChannel::Level:
            return "Level";
        case SandboxEditorProgressivePoissonChannel::Phase:
            return "Phase";
        case SandboxEditorProgressivePoissonChannel::SplatRadius:
            return "Splat radius";
        case SandboxEditorProgressivePoissonChannel::PrefixVisible:
            return "Prefix visible";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorProgressivePoissonBackend(
        const SandboxEditorProgressivePoissonBackend backend) noexcept
    {
        switch (backend)
        {
        case SandboxEditorProgressivePoissonBackend::CpuReference:
            return "CPU reference";
        case SandboxEditorProgressivePoissonBackend::VulkanCompute:
            return "Vulkan compute";
        }
        return "Unknown";
    }

    SandboxEditorProgressivePoissonChannel MakeSandboxEditorProgressivePoissonChannel(
        const Core::Config::ProgressivePoissonPlaygroundChannel channel) noexcept
    {
        switch (channel)
        {
        case Core::Config::ProgressivePoissonPlaygroundChannel::Level:
            return SandboxEditorProgressivePoissonChannel::Level;
        case Core::Config::ProgressivePoissonPlaygroundChannel::Phase:
            return SandboxEditorProgressivePoissonChannel::Phase;
        case Core::Config::ProgressivePoissonPlaygroundChannel::SplatRadius:
            return SandboxEditorProgressivePoissonChannel::SplatRadius;
        case Core::Config::ProgressivePoissonPlaygroundChannel::PrefixVisible:
            return SandboxEditorProgressivePoissonChannel::PrefixVisible;
        }
        return SandboxEditorProgressivePoissonChannel::Level;
    }

    Core::Config::ProgressivePoissonPlaygroundChannel
    MakeCoreProgressivePoissonPlaygroundChannel(
        const SandboxEditorProgressivePoissonChannel channel) noexcept
    {
        switch (channel)
        {
        case SandboxEditorProgressivePoissonChannel::Level:
            return Core::Config::ProgressivePoissonPlaygroundChannel::Level;
        case SandboxEditorProgressivePoissonChannel::Phase:
            return Core::Config::ProgressivePoissonPlaygroundChannel::Phase;
        case SandboxEditorProgressivePoissonChannel::SplatRadius:
            return Core::Config::ProgressivePoissonPlaygroundChannel::SplatRadius;
        case SandboxEditorProgressivePoissonChannel::PrefixVisible:
            return Core::Config::ProgressivePoissonPlaygroundChannel::PrefixVisible;
        }
        return Core::Config::ProgressivePoissonPlaygroundChannel::Level;
    }

    SandboxEditorProgressivePoissonBackend MakeSandboxEditorProgressivePoissonBackend(
        const Core::Config::ProgressivePoissonPlaygroundBackend backend) noexcept
    {
        switch (backend)
        {
        case Core::Config::ProgressivePoissonPlaygroundBackend::CpuReference:
            return SandboxEditorProgressivePoissonBackend::CpuReference;
        case Core::Config::ProgressivePoissonPlaygroundBackend::VulkanCompute:
            return SandboxEditorProgressivePoissonBackend::VulkanCompute;
        }
        return SandboxEditorProgressivePoissonBackend::CpuReference;
    }

    Core::Config::ProgressivePoissonPlaygroundBackend
    MakeCoreProgressivePoissonPlaygroundBackend(
        const SandboxEditorProgressivePoissonBackend backend) noexcept
    {
        switch (backend)
        {
        case SandboxEditorProgressivePoissonBackend::CpuReference:
            return Core::Config::ProgressivePoissonPlaygroundBackend::CpuReference;
        case SandboxEditorProgressivePoissonBackend::VulkanCompute:
            return Core::Config::ProgressivePoissonPlaygroundBackend::VulkanCompute;
        }
        return Core::Config::ProgressivePoissonPlaygroundBackend::CpuReference;
    }

    SandboxEditorProgressivePoissonConfig MakeSandboxEditorProgressivePoissonConfig(
        const Core::Config::ProgressivePoissonPlaygroundConfig& config) noexcept
    {
        return SandboxEditorProgressivePoissonConfig{
            .Dimension = config.Dimension,
            .GridWidth = config.GridWidth,
            .MaxLevels = config.MaxLevels,
            .HashLoadFactor = static_cast<float>(config.HashLoadFactor),
            .RadiusAlpha = static_cast<float>(config.RadiusAlpha),
            .RandomizeGridOrigin = config.RandomizeGridOrigin,
            .GridOriginSeed = config.GridOriginSeed,
            .ShuffleWithinLevels = config.ShuffleWithinLevels,
            .ShuffleSeed = config.ShuffleSeed,
            .PrefixCount = config.PrefixCount,
            .Channel = MakeSandboxEditorProgressivePoissonChannel(config.Channel),
            .Backend = MakeSandboxEditorProgressivePoissonBackend(config.Backend),
            .MeshSurfaceSampleCount = config.MeshSurfaceSampleCount,
            .MeshSurfaceSampleSeed = config.MeshSurfaceSampleSeed,
            .MeshSurfaceMinTriangleArea = config.MeshSurfaceMinTriangleArea,
            .MeshSurfaceInterpolateNormals = config.MeshSurfaceInterpolateNormals,
        };
    }

    Core::Config::ProgressivePoissonPlaygroundConfig
    MakeCoreProgressivePoissonPlaygroundConfig(
        const SandboxEditorProgressivePoissonConfig& config,
        const Core::Config::ProgressivePoissonPlaygroundConfig& defaults) noexcept
    {
        Core::Config::ProgressivePoissonPlaygroundConfig out = defaults;
        out.Dimension = config.Dimension;
        out.GridWidth = config.GridWidth;
        out.MaxLevels = config.MaxLevels;
        out.HashLoadFactor = static_cast<double>(config.HashLoadFactor);
        out.RadiusAlpha = static_cast<double>(config.RadiusAlpha);
        out.RandomizeGridOrigin = config.RandomizeGridOrigin;
        out.GridOriginSeed = config.GridOriginSeed;
        out.ShuffleWithinLevels = config.ShuffleWithinLevels;
        out.ShuffleSeed = config.ShuffleSeed;
        out.PrefixCount = config.PrefixCount;
        out.Channel = MakeCoreProgressivePoissonPlaygroundChannel(config.Channel);
        out.Backend = MakeCoreProgressivePoissonPlaygroundBackend(config.Backend);
        out.MeshSurfaceSampleCount = config.MeshSurfaceSampleCount;
        out.MeshSurfaceSampleSeed = config.MeshSurfaceSampleSeed;
        out.MeshSurfaceMinTriangleArea = config.MeshSurfaceMinTriangleArea;
        out.MeshSurfaceInterpolateNormals = config.MeshSurfaceInterpolateNormals;
        return out;
    }

    const char* DebugNameForSandboxEditorMeshCurvatureOutput(
        const SandboxEditorMeshCurvatureOutput output) noexcept
    {
        switch (output)
        {
        case SandboxEditorMeshCurvatureOutput::All:
            return "All";
        case SandboxEditorMeshCurvatureOutput::Mean:
            return "Mean";
        case SandboxEditorMeshCurvatureOutput::Gaussian:
            return "Gaussian";
        case SandboxEditorMeshCurvatureOutput::PrincipalDirections:
            return "Principal";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshRemeshMode(
        const SandboxEditorMeshRemeshMode mode) noexcept
    {
        switch (mode)
        {
        case SandboxEditorMeshRemeshMode::Uniform:
            return "Uniform";
        case SandboxEditorMeshRemeshMode::Adaptive:
            return "Adaptive";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshRemeshSizingLaw(
        const SandboxEditorMeshRemeshSizingLaw sizingLaw) noexcept
    {
        switch (sizingLaw)
        {
        case SandboxEditorMeshRemeshSizingLaw::MeanCurvature:
            return "Mean curvature";
        case SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin:
            return "Error-bounded Taubin";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshSubdivideOperator(
        const SandboxEditorMeshSubdivideOperator op) noexcept
    {
        switch (op)
        {
        case SandboxEditorMeshSubdivideOperator::Loop:
            return "Loop";
        case SandboxEditorMeshSubdivideOperator::CatmullClark:
            return "Catmull-Clark";
        case SandboxEditorMeshSubdivideOperator::Sqrt3:
            return "Sqrt(3)";
        }
        return "Unknown";
    }

    const char* DebugNameForSandboxEditorMeshSimplifyMetric(
        const SandboxEditorMeshSimplifyMetric metric) noexcept
    {
        switch (metric)
        {
        case SandboxEditorMeshSimplifyMetric::ClassicalQEM:
            return "Classical QEM";
        case SandboxEditorMeshSimplifyMetric::FA_QEM:
            return "FA-QEM (feature-aware)";
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
        frame.AssetImportQueue = BuildAssetImportQueueModel(context);
        frame.RenderGraph = BuildRenderGraphModel(context);
        frame.RenderRecipe = BuildSandboxEditorRenderRecipeEditorModel(context);
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
        model.VisualizationTarget = VisualizationTargetForWindowKind(kind);
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
        const GS::ConstSourceView sourceView =
            GS::BuildConstView(raw, *selected);
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, *selected);
        model.SelectedDomain = sourceView.ActiveDomain;
        model.DomainMatches = model.SelectedDomain == model.ExpectedDomain;
        model.VisualizationTargetAvailable =
            VisualizationTargetAvailableForView(
                availability,
                model.VisualizationTarget);
        model.PropertyCatalog =
            BuildPropertyCatalogModel(context, raw, *selected);
        const SandboxEditorGeometryDomainModel geometry =
            BuildGeometryDomainModel(raw, *selected);
        const SandboxEditorProgressiveRenderDataModel progressive =
            BuildProgressiveRenderDataModel(context, raw, *selected);
        model.BoundState =
            BuildBoundRenderStateModel(
                model.PropertyCatalog,
                progressive,
                model.RenderHints,
                geometry,
                model.SelectedStableId);
        model.TextureBake =
            BuildTextureBakeControlsModel(
                context,
                GS::BuildConstView(raw, *selected),
                model.PropertyCatalog,
                model.SelectedStableId);
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
            model.Visualization =
                BuildVisualizationModel(context, model.VisualizationTarget);
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

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (availability.Sources.ProvenanceDomain != GS::Domain::Mesh)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;
        if ((command.SetVertexView && command.EnableVertexView) ||
            command.SetVertexRenderMode ||
            command.SetVertexPointRadius)
        {
            if (!availability.Sources.Has(GS::SourceCapability::VertexPoints))
                return SandboxEditorCommandStatus::UnsupportedGeometryDomain;
        }
        if (command.SetEdgeView && command.EnableEdgeView)
        {
            const bool hasExplicitEdges =
                availability.Sources.Has(GS::SourceCapability::Edges);
            const bool hasMeshWireTopology =
                availability.Sources.Has(GS::SourceCapability::Halfedges) &&
                availability.Sources.Has(GS::SourceCapability::Faces);
            if (!availability.Sources.Has(GS::SourceCapability::VertexPoints) ||
                (!hasExplicitEdges && !hasMeshWireTopology))
            {
                return SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            }
        }

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

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (!RenderHintCommandMatchesDomain(command, availability))
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

        const std::optional<G::VisualizationConfig> before =
            StoredVisualizationConfigForTarget(raw, entity, command.Target);
        const std::optional<G::VisualizationConfig> effectiveBefore =
            EffectiveVisualizationConfigForTarget(raw, entity, command.Target);
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
        else if (!before.has_value() &&
                 effectiveBefore.has_value() &&
                 SameVisualizationConfig(*effectiveBefore, *after))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        if (context.CommandHistory != nullptr)
        {
            const EditorCommandRecord record =
                command.Target == SandboxEditorVisualizationTarget::Entity
                    ? MakeVisualizationConfigCommand(
                          EditorVisualizationConfigCommand{
                              .Scene = context.Scene,
                              .StableEntityId = command.StableEntityId,
                              .Before = before,
                              .After = after,
                              .Label = "Change Visualization",
                          })
                    : MakeVisualizationConfigTargetCommand(
                          context.Scene,
                          command.StableEntityId,
                          command.Target,
                          before,
                          after,
                          "Change Visualization");
            const EditorCommandHistoryResult result =
                context.CommandHistory->Execute(record);
            return ToSandboxEditorCommandStatus(result.Status);
        }

        return ToSandboxEditorCommandStatus(
            ApplyVisualizationConfigTarget(
                context.Scene,
                command.StableEntityId,
                command.Target,
                after));
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
        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(view);
        const Geometry::PropertySet* properties =
            PropertySetForVisualizationDomain(availability, command.Domain);
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
            .Target = command.Target,
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

        const GeometryEntityAvailability availability =
            BuildGeometryAvailability(raw, entity);
        if (!availability.HasGeometry())
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

    SandboxEditorCommandStatus ApplySandboxEditorVertexChannelBindingCommand(
        const SandboxEditorContext& context,
        const SandboxEditorVertexChannelBindingCommand& command)
    {
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (command.Channel != VertexChannel::Normal &&
            command.Channel != VertexChannel::Color)
        {
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
            return SandboxEditorCommandStatus::StaleEntity;

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        const std::optional<SandboxEditorPropertyCatalogDomain> domain =
            VertexChannelCatalogDomainForView(view);
        if (!domain.has_value())
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const Geometry::PropertySet* properties =
            VertexChannelPropertySetForView(view, *domain);
        if (properties == nullptr)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const auto* current = raw.try_get<VertexChannelBindingSet>(*entity);
        VertexChannelBindingSet after = current != nullptr
            ? *current
            : VertexChannelBindingSet{};
        VertexChannelSourceBinding* target =
            FindMutableVertexChannelBinding(after, command.Channel);
        if (target == nullptr)
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;

        if (!command.EnableBinding)
        {
            if (current == nullptr || !IsVertexChannelBindingEnabled(*target))
                return SandboxEditorCommandStatus::NoChange;

            *target = {};
            ++after.BindingGeneration;
            if (AnyVertexChannelBindingEnabled(after))
                raw.emplace_or_replace<VertexChannelBindingSet>(*entity, after);
            else
                raw.remove<VertexChannelBindingSet>(*entity);

            MarkVertexChannelDirty(raw, *entity, command.Channel);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty(
                    "Change vertex channel binding");
            return SandboxEditorCommandStatus::Applied;
        }

        if (command.PropertyName.empty())
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;

        const SandboxEditorPropertyCatalogValueKind valueKind =
            ToPropertyCatalogValueKind(
                DetectPropertyValueKind(*properties, command.PropertyName));
        const std::optional<AttributeSourceType> sourceType =
            ToAttributeSourceType(valueKind);
        if (!sourceType.has_value())
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;

        const AttributeBindResult resolver =
            EvaluateVertexChannelBinding(
                *properties,
                command.Channel,
                command.PropertyName,
                *sourceType,
                properties->Size());
        if (!resolver.Ok())
            return SandboxEditorCommandStatus::InvalidVertexChannelBinding;

        const VertexChannelSourceBinding next{
            .Enabled = true,
            .SourceType = *sourceType,
            .SourceProperty = command.PropertyName,
        };
        if (current != nullptr &&
            SameVertexChannelSourceBinding(*target, next))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        *target = next;
        ++after.BindingGeneration;
        raw.emplace_or_replace<VertexChannelBindingSet>(*entity, after);
        MarkVertexChannelDirty(raw, *entity, command.Channel);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty(
                "Change vertex channel binding");
        return SandboxEditorCommandStatus::Applied;
    }

    SandboxEditorCommandStatus ApplySandboxEditorProgressiveSlotDefaultCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressiveSlotDefaultCommand& command)
    {
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (!IsFiniteDefaultValue(command.Value))
            return SandboxEditorCommandStatus::InvalidProcessingParameters;

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const auto* current =
            raw.try_get<ProgressivePresentationBindings>(entity);
        if (current == nullptr)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        ProgressivePresentationBindings before = *current;
        ProgressivePresentationBindings after = before;
        const ProgressiveSlotLookup lookup = FindMutableProgressiveSlot(
            after,
            command.PresentationKey,
            command.Semantic);
        if (lookup.Slot == nullptr)
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;

        ProgressiveSlotBinding& slot = *lookup.Slot;
        if (slot.SourceKind == ProgressiveSlotSourceKind::UniformDefault &&
            slot.Enabled == command.Enabled &&
            SameProgressiveDefaultValue(slot.UniformDefault, command.Value))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        slot.SourceKind = ProgressiveSlotSourceKind::UniformDefault;
        slot.UniformDefault = command.Value;
        slot.Property = {};
        slot.AuthoredTexture = {};
        slot.GeneratedTexture = {};
        slot.GeneratedPolicy =
            DefaultGeneratedOutputPolicyFor(slot.SourceKind);
        slot.Provenance = ProgressiveGeneratedOutputProvenance::UniformDefault;
        slot.Readiness = ProgressiveReadinessState::DefaultValue;
        slot.LastDiagnostic.clear();
        slot.Enabled = command.Enabled;
        ++after.BindingGeneration;

        return CommitProgressiveBindingsChange(
            context,
            command.StableEntityId,
            std::move(before),
            std::move(after));
    }

    SandboxEditorCommandStatus ApplySandboxEditorProgressiveSlotPropertyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressiveSlotPropertyCommand& command)
    {
        if (context.Scene == nullptr)
            return SandboxEditorCommandStatus::MissingScene;
        if (!PropertySourceKindAllowedForProgressiveSlotCommand(command.SourceKind) ||
            command.PropertyName.empty())
        {
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;
        }

        entt::registry& raw = context.Scene->Raw();
        const ECS::EntityHandle entity =
            SelectionController::ToEntityHandle(command.StableEntityId);
        if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            return SandboxEditorCommandStatus::StaleEntity;

        const auto* current =
            raw.try_get<ProgressivePresentationBindings>(entity);
        if (current == nullptr)
            return SandboxEditorCommandStatus::UnsupportedGeometryDomain;

        const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
        const std::size_t expectedCount =
            ResolvePropertyElementCount(view, command.Domain);
        ProgressivePropertyBindingDescriptor descriptor{
            .Domain = command.Domain,
            .PropertyName = command.PropertyName,
            .ExpectedValueKind = command.ExpectedValueKind,
            .ExpectedElementCount = expectedCount,
        };
        ProgressivePropertyResolution resolution =
            ResolvePropertyBinding(view, descriptor);
        if (!resolution.Compatible())
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;

        ProgressivePresentationBindings before = *current;
        ProgressivePresentationBindings after = before;
        const ProgressiveSlotLookup lookup = FindMutableProgressiveSlot(
            after,
            command.PresentationKey,
            command.Semantic);
        if (lookup.Slot == nullptr)
            return SandboxEditorCommandStatus::InvalidVisualizationProperty;

        ProgressiveSlotBinding& slot = *lookup.Slot;
        const ProgressiveReadinessState nextReadiness =
            command.SourceKind == ProgressiveSlotSourceKind::PropertyBuffer
                ? ProgressiveReadinessState::Ready
                : ProgressiveReadinessState::Pending;
        const ProgressiveGeneratedOutputProvenance nextProvenance =
            command.SourceKind == ProgressiveSlotSourceKind::PropertyBuffer
                ? ProgressiveGeneratedOutputProvenance::PropertyBuffer
                : ProgressiveGeneratedOutputProvenance::PropertyBinding;

        if (slot.SourceKind == command.SourceKind &&
            slot.Enabled &&
            slot.Readiness == nextReadiness &&
            SameProgressivePropertyDescriptor(slot.Property, descriptor))
        {
            return SandboxEditorCommandStatus::NoChange;
        }

        slot.SourceKind = command.SourceKind;
        slot.Property = std::move(descriptor);
        slot.AuthoredTexture = {};
        slot.GeneratedTexture = {};
        slot.GeneratedPolicy =
            DefaultGeneratedOutputPolicyFor(slot.SourceKind);
        slot.Provenance = nextProvenance;
        slot.Readiness = nextReadiness;
        slot.LastDiagnostic.clear();
        slot.Enabled = true;
        ++after.BindingGeneration;

        return CommitProgressiveBindingsChange(
            context,
            command.StableEntityId,
            std::move(before),
            std::move(after));
    }

    SandboxEditorTextureBakeCommandResult ApplySandboxEditorTextureBakeCommand(
        const SandboxEditorContext& context,
        const SandboxEditorTextureBakeCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return SandboxEditorTextureBakeCommandResult{
                .Status = SandboxEditorCommandStatus::MissingScene,
                .BakeStatus = SelectedMeshTextureBakeStatus::MissingScene,
                .Diagnostic = "Scene registry is unavailable.",
            };
        }
        if (context.AssetService == nullptr)
        {
            return SandboxEditorTextureBakeCommandResult{
                .Status = SandboxEditorCommandStatus::AssetImportFailed,
                .BakeStatus = SelectedMeshTextureBakeStatus::MissingAssetService,
                .Diagnostic = "Asset service is unavailable for generated texture payloads.",
            };
        }
        if (command.PropertyName.empty() ||
            command.Width == 0u ||
            command.Height == 0u)
        {
            return SandboxEditorTextureBakeCommandResult{
                .Status = SandboxEditorCommandStatus::InvalidVisualizationProperty,
                .BakeStatus = command.PropertyName.empty()
                    ? SelectedMeshTextureBakeStatus::MissingProperty
                    : SelectedMeshTextureBakeStatus::InvalidResolution,
                .Diagnostic = "Texture bake command has invalid parameters.",
            };
        }

        SelectedMeshTextureBakeRequest request{};
        request.StableEntityId = command.StableEntityId;
        request.SourceDomain = command.SourceDomain;
        request.SourcePropertyName = command.PropertyName;
        request.ExpectedValueKind = command.ExpectedValueKind;
        request.Encoder = command.Encoder;
        request.RangePolicy = command.RangePolicy;
        request.RangeMin = command.RangeMin;
        request.RangeMax = command.RangeMax;
        request.Width = command.Width;
        request.Height = command.Height;
        request.TargetPresentationKey = command.PresentationKey;
        request.TargetSemantic = command.TargetSemantic;
        request.GeneratedKey = command.GeneratedKey.empty()
            ? command.PropertyName
            : command.GeneratedKey;
        request.BindGeneratedTexture = command.BindGeneratedTexture;

        const SelectedMeshTextureBakeResult bake =
            ApplySelectedMeshTextureBakeCommand(
                SelectedMeshTextureBakeContext{
                    .Scene = context.Scene,
                    .AssetService = context.AssetService,
                    .CommandHistory = context.CommandHistory,
                },
                request);

        SandboxEditorCommandStatus status =
            SandboxEditorCommandStatus::Applied;
        if (!bake.Succeeded())
        {
            switch (bake.Status)
            {
            case SelectedMeshTextureBakeStatus::MissingScene:
                status = SandboxEditorCommandStatus::MissingScene;
                break;
            case SelectedMeshTextureBakeStatus::MissingAssetService:
            case SelectedMeshTextureBakeStatus::AssetLoadFailed:
                status = SandboxEditorCommandStatus::AssetImportFailed;
                break;
            case SelectedMeshTextureBakeStatus::StaleEntity:
                status = SandboxEditorCommandStatus::StaleEntity;
                break;
            case SelectedMeshTextureBakeStatus::NonMeshSelection:
            case SelectedMeshTextureBakeStatus::UnsupportedSourceDomain:
                status = SandboxEditorCommandStatus::UnsupportedGeometryDomain;
                break;
            case SelectedMeshTextureBakeStatus::CommandFailed:
                status = SandboxEditorCommandStatus::GeometryProcessingFailed;
                break;
            case SelectedMeshTextureBakeStatus::Success:
            case SelectedMeshTextureBakeStatus::Scheduled:
            case SelectedMeshTextureBakeStatus::MissingProgressiveBindings:
            case SelectedMeshTextureBakeStatus::MissingPresentation:
            case SelectedMeshTextureBakeStatus::MissingSlot:
            case SelectedMeshTextureBakeStatus::UnsupportedTargetSemantic:
            case SelectedMeshTextureBakeStatus::IncompatibleTargetSlot:
            case SelectedMeshTextureBakeStatus::InvalidResolution:
            case SelectedMeshTextureBakeStatus::InvalidRange:
            case SelectedMeshTextureBakeStatus::MissingProperty:
            case SelectedMeshTextureBakeStatus::UnsupportedPropertyType:
            case SelectedMeshTextureBakeStatus::MismatchedPropertyCount:
            case SelectedMeshTextureBakeStatus::MissingTexcoords:
            case SelectedMeshTextureBakeStatus::NonFiniteTexcoord:
            case SelectedMeshTextureBakeStatus::NonFinitePropertyValue:
            case SelectedMeshTextureBakeStatus::DegenerateAllTriangles:
            case SelectedMeshTextureBakeStatus::DegenerateUvTriangles:
            case SelectedMeshTextureBakeStatus::ZeroCoverageBake:
            case SelectedMeshTextureBakeStatus::BakeFailed:
            case SelectedMeshTextureBakeStatus::JobSubmitFailed:
            case SelectedMeshTextureBakeStatus::StaleCompletion:
                status = SandboxEditorCommandStatus::InvalidVisualizationProperty;
                break;
            }
        }

        return SandboxEditorTextureBakeCommandResult{
            .Status = status,
            .BakeStatus = bake.Status,
            .GeneratedTexture = bake.GeneratedTexture,
            .Job = bake.Job,
            .Scheduled = bake.Status == SelectedMeshTextureBakeStatus::Scheduled,
            .BoundGeneratedTexture = bake.BoundGeneratedTexture,
            .GeneratedAssetPath = bake.GeneratedAssetPath,
            .Diagnostic = bake.Diagnostic,
        };
    }

    SandboxEditorUvRegenerationCommandResult ApplySandboxEditorUvRegenerationCommand(
        const SandboxEditorContext& context,
        const SandboxEditorUvRegenerationCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return SandboxEditorUvRegenerationCommandResult{
                .Status = SandboxEditorCommandStatus::MissingScene,
                .UvStatus = Geometry::UvAtlas::UvAtlasStatus::EmptyInput,
                .Diagnostic = "Scene registry is unavailable.",
            };
        }
        if (command.Resolution == 0u || command.Padding >= command.Resolution)
        {
            return SandboxEditorUvRegenerationCommandResult{
                .Status = SandboxEditorCommandStatus::InvalidProcessingParameters,
                .UvStatus = Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                .Diagnostic = "UV regeneration requires a positive resolution and padding smaller than the atlas.",
            };
        }
        if (!command.BackendName.empty() && command.BackendName != "xatlas")
        {
            return SandboxEditorUvRegenerationCommandResult{
                .Status = SandboxEditorCommandStatus::InvalidProcessingParameters,
                .UvStatus = Geometry::UvAtlas::UvAtlasStatus::BackendUnavailable,
                .Diagnostic = "Only the promoted xatlas UV backend is available.",
            };
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return SandboxEditorUvRegenerationCommandResult{
                .Status = SandboxEditorCommandStatus::StaleEntity,
                .UvStatus = Geometry::UvAtlas::UvAtlasStatus::EmptyInput,
                .Diagnostic = "UV regeneration target entity is stale or no longer live.",
            };
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshSoupFromGeometrySourcesResult soup =
            BuildMeshSoupFromGeometrySources(view);
        if (!soup.Succeeded())
        {
            return SandboxEditorUvRegenerationCommandResult{
                .Status = soup.Status,
                .UvStatus = Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput,
                .Diagnostic = soup.Diagnostic,
            };
        }

        std::vector<glm::vec2> authoredTexcoords;
        if (view.VertexSource != nullptr)
        {
            const auto texcoords =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (texcoords && texcoords.Vector().size() == soup.Mesh.VertexCount())
                authoredTexcoords = texcoords.Vector();
        }

        Geometry::UvAtlas::UvAtlasOptions options{};
        options.PreserveValidAuthoredUvs = command.PreserveValidAuthoredUvs;
        options.ForceRegenerate = command.ForceRegenerate;
        options.Resolution = command.Resolution;
        options.Padding = command.Padding;
        options.TexelsPerUnit = command.TexelsPerUnit;
        options.BackendName = "xatlas";

        Geometry::UvAtlas::UvAtlasInput input{};
        input.Positions = soup.Mesh.Positions();
        input.Faces = soup.Mesh.Faces();
        input.AuthoredTexcoords = authoredTexcoords;
        input.VertexProperties =
            view.VertexSource != nullptr
                ? Geometry::ConstPropertySet(view.VertexSource->Properties)
                : Geometry::ConstPropertySet{};
        input.HasVertexProperties = view.VertexSource != nullptr;

        Geometry::UvAtlas::UvAtlasResult atlas =
            Geometry::UvAtlas::ResolveUvAtlas(input, options, nullptr);
        if (!atlas.Succeeded())
        {
            const bool backendFailure =
                atlas.Status == Geometry::UvAtlas::UvAtlasStatus::BackendUnavailable ||
                atlas.Status == Geometry::UvAtlas::UvAtlasStatus::BackendRejectedInput ||
                atlas.Status == Geometry::UvAtlas::UvAtlasStatus::BackendFailed;
            return SandboxEditorUvRegenerationCommandResult{
                .Status = backendFailure
                    ? SandboxEditorCommandStatus::GeometryProcessingFailed
                    : SandboxEditorCommandStatus::InvalidProcessingParameters,
                .UvStatus = atlas.Status,
                .Provenance = atlas.Provenance,
                .AtlasWidth = atlas.Diagnostics.AtlasWidth,
                .AtlasHeight = atlas.Diagnostics.AtlasHeight,
                .ChartCount = atlas.Diagnostics.ChartCount,
                .SeamSplitVertexCount = atlas.Diagnostics.OutputVertexCount >
                                                atlas.Diagnostics.InputVertexCount
                                            ? atlas.Diagnostics.OutputVertexCount -
                                                  atlas.Diagnostics.InputVertexCount
                                            : 0u,
                .Diagnostic = atlas.Diagnostics.BackendDetail.empty()
                    ? std::string{Geometry::UvAtlas::ToString(atlas.Status)}
                    : atlas.Diagnostics.BackendDetail,
            };
        }

        auto converted = Geometry::Mesh::Conversion::ToHalfedgeMesh(atlas.OutputMesh);
        if (!converted.Succeeded())
        {
            return SandboxEditorUvRegenerationCommandResult{
                .Status = SandboxEditorCommandStatus::GeometryProcessingFailed,
                .UvStatus = atlas.Status,
                .Provenance = atlas.Provenance,
                .Diagnostic = "generated UV mesh could not be converted back to halfedge topology",
            };
        }

        CopyUvOutputPropertiesToHalfedgeMesh(view, soup, atlas, converted.Mesh);
        GS::PopulateFromMesh(raw, *entity, converted.Mesh);
        Dirty::MarkVertexPositionsDirty(raw, *entity);
        Dirty::MarkVertexAttributesDirty(raw, *entity);
        Dirty::MarkEdgeTopologyDirty(raw, *entity);
        Dirty::MarkFaceTopologyDirty(raw, *entity);
        Dirty::MarkGpuDirty(raw, *entity);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty("Regenerate UVs");

        return SandboxEditorUvRegenerationCommandResult{
            .Status = SandboxEditorCommandStatus::Applied,
            .UvStatus = atlas.Status,
            .Provenance = atlas.Provenance,
            .AtlasWidth = atlas.Diagnostics.AtlasWidth,
            .AtlasHeight = atlas.Diagnostics.AtlasHeight,
            .ChartCount = atlas.Diagnostics.ChartCount,
            .SeamSplitVertexCount = atlas.Diagnostics.OutputVertexCount >
                                            atlas.Diagnostics.InputVertexCount
                                        ? atlas.Diagnostics.OutputVertexCount -
                                              atlas.Diagnostics.InputVertexCount
                                        : 0u,
            .Diagnostic = atlas.Diagnostics.BackendDetail,
        };
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

    SandboxEditorProgressivePoissonResult
    ApplySandboxEditorProgressivePoissonCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressivePoissonCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::MissingScene,
                command.Config.Channel,
                Core::ErrorCode::InvalidState,
                "Progressive Poisson sampling requires an attached scene.");
        }
        if (!IsValidProgressivePoissonConfig(command.Config))
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson sampling requires dimension 2 or 3, positive grid/max-level/hash settings, and finite radius alpha.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::StaleEntity,
                command.Config.Channel,
                Core::ErrorCode::ResourceNotFound,
                "Progressive Poisson target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::PointCloud &&
            availability.ProvenanceDomain != GS::Domain::Mesh)
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson sampling requires selected point-cloud or mesh GeometrySources.");
        }

        if (availability.ProvenanceDomain == GS::Domain::PointCloud)
        {
            if (view.VertexSource == nullptr)
            {
                return MakeProgressivePoissonResult(
                    SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                    command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires selected point-cloud vertices.");
            }

            std::optional<std::vector<glm::vec3>> positions =
                CollectKMeansPositions(view.VertexSource->Properties);
            if (!positions.has_value())
            {
                return MakeProgressivePoissonResult(
                    SandboxEditorCommandStatus::InvalidProcessingParameters,
                    command.Config.Channel,
                    Core::ErrorCode::InvalidArgument,
                    "Progressive Poisson sampling requires a non-empty finite v:position property.");
            }

            SandboxEditorProgressivePoissonResult result =
                RunProgressivePoissonAndPublish(
                    std::span<const glm::vec3>{
                        positions->data(),
                        positions->size()},
                    view.VertexSource->Properties,
                    command.Config,
                    context.Device);
            if (!result.Succeeded())
                return result;

            ApplyProgressivePoissonVisualization(
                raw,
                *entity,
                command.Config.Channel);
            Dirty::MarkVertexAttributesDirty(raw, *entity);
            if (context.CommandHistory != nullptr)
                (void)context.CommandHistory->MarkDirty(
                    "Run progressive Poisson sampling");

            AppendProgressivePoissonSuccessMessage(result);
            return result;
        }

        if (!IsValidProgressivePoissonMeshSurfaceConfig(command.Config))
        {
            return MakeProgressivePoissonResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                command.Config.Channel,
                Core::ErrorCode::InvalidArgument,
                "Progressive Poisson mesh sampling requires a positive surface sample count and finite positive minimum triangle area.");
        }

        const GS::ConstSourceView constView =
            GS::BuildConstView(raw, *entity);
        MeshDenoiseSourceResult source =
            BuildHalfedgeMeshForDenoise(constView);
        if (!source.Succeeded())
        {
            return MakeProgressivePoissonResult(
                source.Status,
                command.Config.Channel,
                source.Error,
                source.Diagnostic.empty()
                    ? "Progressive Poisson mesh sampling could not build selected mesh GeometrySources."
                    : source.Diagnostic);
        }

        SurfaceSampling::Result sampled =
            SurfaceSampling::SampleTriangleMeshSurface(
                source.Mesh,
                ToProgressivePoissonSurfaceParams(command.Config));
        SandboxEditorProgressivePoissonResult result{};
        result.Channel = command.Config.Channel;
        result.MeshSurfaceSamplingUsed = true;
        result.MeshSurfaceSampleCount =
            SaturatingUint32(sampled.Info.WrittenSampleCount);
        result.MeshSurfaceTotalFaceCount =
            SaturatingUint32(sampled.Info.TotalFaceCount);
        result.MeshSurfaceAcceptedTriangleCount =
            SaturatingUint32(sampled.Info.AcceptedTriangleCount);
        result.MeshSurfaceRejectedFaceCount = SaturatingUint32(
            sampled.Info.RejectedNonTriangleFaceCount +
            sampled.Info.RejectedDegenerateTriangleCount +
            sampled.Info.RejectedNonFiniteTriangleCount);
        result.MeshSurfaceArea = sampled.Info.TotalSurfaceArea;
        if (!sampled.Succeeded())
        {
            result.Status =
                sampled.Status == SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount
                    ? SandboxEditorCommandStatus::InvalidProcessingParameters
                    : SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error =
                sampled.Status == SurfaceSampling::SurfaceSamplingStatus::InvalidSampleCount
                    ? Core::ErrorCode::InvalidArgument
                    : Core::ErrorCode::InvalidState;
            result.Message =
                "Progressive Poisson mesh surface sampling failed with ";
            result.Message += std::string(SurfaceSampling::ToString(sampled.Status));
            result.Message += ".";
            return result;
        }

        const std::span<const glm::vec3> sampledPositions =
            sampled.Cloud.Positions();
        result = RunProgressivePoissonAndPublish(
            sampledPositions,
            sampled.Cloud.PointProperties(),
            command.Config,
            context.Device);
        result.MeshSurfaceSamplingUsed = true;
        result.MeshSurfaceSampleCount =
            SaturatingUint32(sampled.Info.WrittenSampleCount);
        result.MeshSurfaceTotalFaceCount =
            SaturatingUint32(sampled.Info.TotalFaceCount);
        result.MeshSurfaceAcceptedTriangleCount =
            SaturatingUint32(sampled.Info.AcceptedTriangleCount);
        result.MeshSurfaceRejectedFaceCount = SaturatingUint32(
            sampled.Info.RejectedNonTriangleFaceCount +
            sampled.Info.RejectedDegenerateTriangleCount +
            sampled.Info.RejectedNonFiniteTriangleCount);
        result.MeshSurfaceArea = sampled.Info.TotalSurfaceArea;
        if (!result.Succeeded())
            return result;

        const EditorCommandHistoryStatus publishStatus =
            ApplyPointCloudPointState(
                context.Scene,
                command.StableEntityId,
                sampled.Cloud);
        if (publishStatus != EditorCommandHistoryStatus::Applied)
        {
            result.Status = ToSandboxEditorCommandStatus(publishStatus);
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Progressive Poisson mesh sample publication failed.";
            return result;
        }

        if (raw.all_of<G::RenderSurface>(*entity))
            raw.remove<G::RenderSurface>(*entity);
        ApplyProgressivePoissonVisualization(
            raw,
            *entity,
            command.Config.Channel);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty(
                "Run progressive Poisson mesh sampling");

        AppendProgressivePoissonSuccessMessage(result);
        return result;
    }

    SandboxEditorProgressivePoissonConfigResult
    ApplySandboxEditorProgressivePoissonConfigCommand(
        const SandboxEditorContext& context,
        const SandboxEditorProgressivePoissonConfigCommand& command)
    {
        SandboxEditorProgressivePoissonConfigResult result{};
        if (context.EngineConfigControlState == nullptr ||
            !context.PreviewEngineConfigDocument ||
            !context.ApplyEngineConfigHotSubset ||
            !context.EngineConfigCommandsAvailable)
        {
            result.Status =
                SandboxEditorProgressivePoissonConfigStatus::MissingConfigFacade;
            result.Message =
                "Progressive Poisson config requires the engine config-control facade.";
            return result;
        }

        Core::Config::EngineConfig candidate =
            context.EngineConfigControlState->ActiveConfig;
        candidate.Sandbox.ProgressivePoisson = command.Config;
        const std::string document =
            Core::Config::SerializeEngineConfig(candidate);
        const std::string sourceId = command.SourceId.empty()
            ? std::string{"sandbox.progressive_poisson"}
            : command.SourceId;
        result.Preview =
            context.PreviewEngineConfigDocument(document, sourceId);
        if (!Core::Config::IsConfigUsable(result.Preview))
        {
            result.Status =
                SandboxEditorProgressivePoissonConfigStatus::PreviewRejected;
            result.Message =
                "Progressive Poisson config preview was rejected.";
            return result;
        }

        result.Apply = context.ApplyEngineConfigHotSubset(result.Preview);
        if (!result.Apply.Succeeded())
        {
            result.Status =
                SandboxEditorProgressivePoissonConfigStatus::ApplyRejected;
            result.Message =
                "Progressive Poisson config hot-apply was rejected.";
            return result;
        }

        result.Status =
            result.Apply.Status == RuntimeEngineConfigApplyStatus::NoChange
                ? SandboxEditorProgressivePoissonConfigStatus::NoChange
                : SandboxEditorProgressivePoissonConfigStatus::Applied;
        result.Message =
            result.Status == SandboxEditorProgressivePoissonConfigStatus::NoChange
                ? "Progressive Poisson config unchanged."
                : "Progressive Poisson config applied.";
        return result;
    }

    SandboxEditorMeshDenoiseResult ApplySandboxEditorMeshDenoiseCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshDenoiseCommand& command)
    {
        SandboxEditorMeshDenoiseResult result{
            .Status = SandboxEditorCommandStatus::NoChange,
            .DenoiseStatus = Smooth::DenoiseStatus::Success,
            .Stage = command.Stage,
            .NormalIterations = command.NormalIterations,
            .VertexIterations = command.VertexIterations,
            .SigmaSpatial = command.SigmaSpatial,
            .SigmaRange = command.SigmaRange,
            .PreserveBoundary = command.PreserveBoundary,
            .Error = Core::ErrorCode::Success,
        };

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.DenoiseStatus = Smooth::DenoiseStatus::EmptyMesh;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh denoise.";
            return result;
        }
        if (!context.MeshDenoiseKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Smoothing mesh denoiser is unavailable in this runtime configuration.";
            return result;
        }

        const bool validStage =
            std::find(kMeshDenoiseStages.begin(),
                      kMeshDenoiseStages.end(),
                      command.Stage) != kMeshDenoiseStages.end();
        if (!validStage ||
            command.NormalIterations == 0u ||
            command.VertexIterations == 0u ||
            !std::isfinite(command.SigmaSpatial) ||
            !std::isfinite(command.SigmaRange) ||
            command.SigmaSpatial < 0.0 ||
            command.SigmaRange < 0.0 ||
            !IsPositiveFinite(command.DegenerateNormalLengthEpsilon))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.DenoiseStatus = Smooth::DenoiseStatus::InvalidParams;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh denoise requires a valid stage, positive iteration counts, non-negative finite sigma values, and a positive finite degeneracy epsilon.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.DenoiseStatus = Smooth::DenoiseStatus::EmptyMesh;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh denoise target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshDenoiseSourceResult source =
            BuildHalfedgeMeshForDenoise(view);
        result.VertexSlotCount = source.BeforePositions.size();
        result.SkippedDeletedVertexCount =
            static_cast<std::size_t>(
                std::count(source.DeletedVertices.begin(),
                           source.DeletedVertices.end(),
                           true));
        result.WrittenCount =
            result.VertexSlotCount - result.SkippedDeletedVertexCount;
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.DenoiseStatus =
                source.Status ==
                        SandboxEditorCommandStatus::UnsupportedGeometryDomain
                    ? Smooth::DenoiseStatus::EmptyMesh
                    : Smooth::DenoiseStatus::InvalidParams;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        Smooth::BilateralDenoiseParams params{};
        params.NormalIterations = command.NormalIterations;
        params.VertexIterations = command.VertexIterations;
        params.SigmaSpatial = command.SigmaSpatial;
        params.SigmaRange = command.SigmaRange;
        params.PreserveBoundary = command.PreserveBoundary;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;

        const Smooth::BilateralDenoiseResult denoise =
            Smooth::DenoiseBilateral(source.Mesh, params);
        result.DenoiseStatus = denoise.Status;
        result.Error = ErrorForDenoiseStatus(denoise.Status);
        CopyMeshDenoiseCounters(denoise, result);
        result.VertexSlotCount = source.BeforePositions.size();
        result.WrittenCount =
            result.VertexSlotCount - result.SkippedDeletedVertexCount;

        if (denoise.Status != Smooth::DenoiseStatus::Success)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Message = "Geometry.Smoothing denoise failed with ";
            result.Message += std::string(Smooth::DebugName(denoise.Status));
            result.Message += ".";
            return result;
        }

        std::vector<glm::vec3> afterPositions =
            ExtractMeshPositions(source.Mesh);
        if (afterPositions.size() != source.BeforePositions.size() ||
            !AllFiniteVec3(std::span<const glm::vec3>{
                afterPositions.data(),
                afterPositions.size()}))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.DenoiseStatus = Smooth::DenoiseStatus::NonFiniteInput;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Smoothing denoise produced invalid or count-mismatched positions.";
            return result;
        }

        std::size_t movedPublishedVertices = 0u;
        for (std::size_t i = 0u; i < afterPositions.size(); ++i)
        {
            if (i < source.DeletedVertices.size() && source.DeletedVertices[i])
            {
                afterPositions[i] = source.BeforePositions[i];
                continue;
            }
            if (afterPositions[i] != source.BeforePositions[i])
                ++movedPublishedVertices;
        }
        result.MovedVertexCount = movedPublishedVertices;

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshDenoisePositions(
                context,
                command.StableEntityId,
                std::move(source.BeforePositions),
                std::move(afterPositions));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh denoise position publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshDenoiseSuccessMessage(result);
        return result;
    }

    SandboxEditorMeshCurvatureResult ApplySandboxEditorMeshCurvatureCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshCurvatureCommand& command)
    {
        SandboxEditorMeshCurvatureResult result{
            .Status = SandboxEditorCommandStatus::NoChange,
            .Output = command.Output,
            .DirectionsRequested =
                command.PublishPrincipalDirections &&
                CurvatureOutputRequestsDirections(command.Output),
            .DirectionsAvailable = context.MeshCurvatureDirectionsAvailable,
            .Error = Core::ErrorCode::Success,
        };

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh curvature.";
            return result;
        }
        if (!context.MeshCurvatureKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Curvature mesh curvature is unavailable in this runtime configuration.";
            return result;
        }

        const bool validOutput =
            std::find(kMeshCurvatureOutputs.begin(),
                      kMeshCurvatureOutputs.end(),
                      command.Output) != kMeshCurvatureOutputs.end();
        if (!validOutput)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Mesh curvature requires a valid output mode.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh curvature target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView constView = GS::BuildConstView(raw, *entity);
        MeshCurvatureSourceResult source =
            BuildHalfedgeMeshForCurvature(constView);
        result.VertexSlotCount = source.VertexSlotCount;
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        GS::MutableSourceView publishView = GS::BuildMutableView(raw, *entity);
        if (!publishView.Valid() || publishView.VertexSource == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh curvature target has no writable vertex GeometrySources.";
            return result;
        }

        MeshCurvaturePropertyState before{};
        std::string captureDiagnostic{};
        if (!CaptureMeshCurvaturePropertyState(
                publishView.VertexSource->Properties,
                result.VertexSlotCount,
                before,
                captureDiagnostic))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message = captureDiagnostic;
            return result;
        }

        Curv::CurvatureField curvature = Curv::ComputeCurvature(source.Mesh);
        if (!curvature.MeanCurvatureProperty ||
            !curvature.GaussianCurvatureProperty ||
            curvature.MeanCurvatureProperty.Vector().size() !=
                result.VertexSlotCount ||
            curvature.GaussianCurvatureProperty.Vector().size() !=
                result.VertexSlotCount)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Curvature produced missing or count-mismatched scalar properties.";
            return result;
        }

        const std::vector<double>& mean =
            curvature.MeanCurvatureProperty.Vector();
        const std::vector<double>& gaussian =
            curvature.GaussianCurvatureProperty.Vector();
        result.NonFiniteScalarCount =
            CountNonFiniteScalars(
                std::span<const double>{mean.data(), mean.size()}) +
            CountNonFiniteScalars(
                std::span<const double>{gaussian.data(), gaussian.size()});
        if (result.NonFiniteScalarCount != 0u)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Curvature produced non-finite scalar curvature values.";
            return result;
        }

        MeshCurvaturePropertyState after = before;
        after.HadMean = true;
        after.Mean = mean;
        after.HadGaussian = true;
        after.Gaussian = gaussian;
        result.ScalarPropertyCount = 2u;
        result.ScalarWrittenCount = mean.size() + gaussian.size();

        if (result.DirectionsRequested &&
            result.DirectionsAvailable)
        {
            if (!curvature.PrincipalDir1Property ||
                !curvature.PrincipalDir2Property ||
                curvature.PrincipalDir1Property.Vector().size() !=
                    result.VertexSlotCount ||
                curvature.PrincipalDir2Property.Vector().size() !=
                    result.VertexSlotCount)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced missing or count-mismatched principal-direction properties.";
                return result;
            }

            const std::vector<glm::vec3>& dir1 =
                curvature.PrincipalDir1Property.Vector();
            const std::vector<glm::vec3>& dir2 =
                curvature.PrincipalDir2Property.Vector();
            result.NonFiniteDirectionCount =
                CountNonFiniteVectors(
                    std::span<const glm::vec3>{dir1.data(), dir1.size()}) +
                CountNonFiniteVectors(
                    std::span<const glm::vec3>{dir2.data(), dir2.size()});
            if (result.NonFiniteDirectionCount != 0u)
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Curvature produced non-finite principal directions.";
                return result;
            }

            after.HadDir1 = true;
            after.Dir1 = dir1;
            after.HadDir2 = true;
            after.Dir2 = dir2;
            result.DirectionPropertyCount = 2u;
            result.DirectionWrittenCount = dir1.size() + dir2.size();
        }

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshCurvatureProperties(
                context,
                command.StableEntityId,
                std::move(before),
                std::move(after));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh curvature property publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.DirectionsPublished =
            result.DirectionPropertyCount == 2u &&
            result.DirectionWrittenCount == result.VertexSlotCount * 2u;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshCurvatureSuccessMessage(result);
        return result;
    }

    SandboxEditorMeshRemeshResult ApplySandboxEditorMeshRemeshCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshRemeshCommand& command)
    {
        SandboxEditorMeshRemeshResult result{
            .Status = SandboxEditorCommandStatus::NoChange,
            .Mode = command.Mode,
            .SizingLaw = command.SizingLaw,
            .IterationsRequested = command.Iterations,
            .TargetEdgeLength = command.TargetEdgeLength,
            .ProjectToSurface = command.ProjectToSurface,
            .Error = Core::ErrorCode::Success,
        };

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh remesh.";
            return result;
        }
        if (!ValidMeshRemeshMode(command.Mode) ||
            !ValidMeshRemeshSizingLaw(command.SizingLaw) ||
            command.Iterations == 0u ||
            !std::isfinite(command.TargetEdgeLength) ||
            command.TargetEdgeLength < 0.0 ||
            !IsPositiveFinite(command.Lambda) ||
            !std::isfinite(command.CurvatureAdaptation) ||
            command.CurvatureAdaptation < 0.0 ||
            !std::isfinite(command.MaxReferenceProjectionDistance) ||
            command.MaxReferenceProjectionDistance < 0.0 ||
            (command.ProjectToSurface && command.ReferenceProjectionK == 0u) ||
            (command.SizingLaw ==
                 SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin &&
             !IsPositiveFinite(command.ApproximationError)))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh remesh requires a valid mode, sizing law, positive iteration count, finite non-negative target length, positive lambda, and valid projection/sizing parameters.";
            return result;
        }
        if (command.Mode == SandboxEditorMeshRemeshMode::Uniform &&
            !context.MeshRemeshUniformKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Remeshing uniform remesher is unavailable in this runtime configuration.";
            return result;
        }
        if (command.Mode == SandboxEditorMeshRemeshMode::Adaptive &&
            !context.MeshRemeshAdaptiveKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.HalfedgeMesh.AdaptiveRemeshing is unavailable in this runtime configuration.";
            return result;
        }
        if (command.ProjectToSurface &&
            !context.MeshRemeshProjectToSurfaceAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Mesh remesh project-to-surface is unavailable in this runtime configuration.";
            return result;
        }
        if (command.SizingLaw ==
                SandboxEditorMeshRemeshSizingLaw::ErrorBoundedTaubin &&
            !context.MeshRemeshErrorBoundedSizingAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Mesh remesh error-bounded Taubin sizing is unavailable in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh remesh target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh remesh");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;

        std::optional<Geometry::RemeshingOperationResult> remeshResult{};
        if (command.Mode == SandboxEditorMeshRemeshMode::Uniform)
        {
            Remesh::RemeshingParams params{};
            params.TargetLength = command.TargetEdgeLength;
            params.Iterations = command.Iterations;
            params.Lambda = command.Lambda;
            params.PreserveBoundary = command.PreserveBoundary;
            params.ProjectToSurface = command.ProjectToSurface;
            params.ReferenceProjectionK = command.ReferenceProjectionK;
            params.MaxReferenceProjectionDistance =
                command.MaxReferenceProjectionDistance;
            remeshResult = Remesh::Remesh(source.Mesh, params);
        }
        else
        {
            AdaptiveRemesh::AdaptiveRemeshingParams params{};
            if (command.TargetEdgeLength > 0.0)
            {
                params.MinEdgeLength = command.TargetEdgeLength * 0.5;
                params.MaxEdgeLength = command.TargetEdgeLength * 2.0;
            }
            params.CurvatureAdaptation = command.CurvatureAdaptation;
            params.Sizing = ToAdaptiveSizingLaw(command.SizingLaw);
            params.ApproximationError = command.ApproximationError;
            params.Iterations = command.Iterations;
            params.Lambda = command.Lambda;
            params.PreserveBoundary = command.PreserveBoundary;
            params.EnableReferenceProjection = command.ProjectToSurface;
            params.ReferenceProjectionK = command.ReferenceProjectionK;
            params.MaxReferenceProjectionDistance =
                command.MaxReferenceProjectionDistance;
            remeshResult = AdaptiveRemesh::AdaptiveRemesh(source.Mesh, params);
        }

        if (!remeshResult.has_value())
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry remeshing failed for the selected mesh and parameters.";
            return result;
        }

        if (source.Mesh.HasGarbage())
            source.Mesh.GarbageCollection();
        CopyRemeshCounters(*remeshResult, result);
        result.OutputVertexCount = source.Mesh.VertexCount();
        result.OutputFaceCount = source.Mesh.FaceCount();

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Remesh mesh",
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh remesh publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshRemeshSuccessMessage(result);
        return result;
    }

    SandboxEditorMeshSubdivideResult ApplySandboxEditorMeshSubdivideCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshSubdivideCommand& command)
    {
        SandboxEditorMeshSubdivideResult result{
            .Status = SandboxEditorCommandStatus::NoChange,
            .Operator = command.Operator,
            .IterationsRequested = command.Iterations,
            .PreserveLoopFeatureEdges = command.PreserveLoopFeatureEdges,
            .Error = Core::ErrorCode::Success,
        };

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh subdivide.";
            return result;
        }
        if (!ValidMeshSubdivideOperator(command.Operator) ||
            command.Iterations == 0u ||
            (command.PreserveLoopFeatureEdges &&
             command.FeatureEdgePropertyName.empty()))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh subdivide requires a valid operator, positive iteration count, and a feature-edge property name when feature preservation is enabled.";
            return result;
        }
        if (command.Operator == SandboxEditorMeshSubdivideOperator::Loop &&
            !context.MeshSubdivideLoopKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Subdivision Loop subdivision is unavailable in this runtime configuration.";
            return result;
        }
        if (command.Operator ==
                SandboxEditorMeshSubdivideOperator::CatmullClark &&
            !context.MeshSubdivideCatmullClarkKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.CatmullClark subdivision is unavailable in this runtime configuration.";
            return result;
        }
        if (command.Operator == SandboxEditorMeshSubdivideOperator::Sqrt3 &&
            !context.MeshSubdivideSqrt3KernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.HalfedgeMesh.SubdivisionSqrt3 is unavailable in this runtime configuration.";
            return result;
        }
        if (command.PreserveLoopFeatureEdges &&
            command.Operator != SandboxEditorMeshSubdivideOperator::Loop)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Loop feature-edge preservation can only be used with the Loop subdivision operator.";
            return result;
        }
        if (command.PreserveLoopFeatureEdges &&
            !context.MeshSubdivideLoopFeatureEdgesAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Loop subdivision feature-edge preservation is unavailable in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh subdivide target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh subdivide");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;
        Geometry::HalfedgeMesh::Mesh output{};

        if (command.Operator == SandboxEditorMeshSubdivideOperator::Loop)
        {
            LoopSubdivide::SubdivisionParams params{};
            params.Iterations = command.Iterations;
            params.MaxOutputFaces = command.MaxOutputFaces;
            params.PreserveFeatureEdges = command.PreserveLoopFeatureEdges;
            params.FeatureEdgePropertyName = command.FeatureEdgePropertyName;
            const std::optional<LoopSubdivide::SubdivisionResult>
                subdivision = LoopSubdivide::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.Subdivision Loop subdivision failed for the selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }
        else if (command.Operator ==
                 SandboxEditorMeshSubdivideOperator::CatmullClark)
        {
            CatmullClark::SubdivisionParams params{};
            params.Iterations = command.Iterations;
            const std::optional<CatmullClark::SubdivisionResult>
                subdivision = CatmullClark::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.CatmullClark subdivision failed for the selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }
        else
        {
            Sqrt3Subdivide::Sqrt3Params params{};
            params.Iterations = command.Iterations;
            params.MaxOutputFaces = command.MaxOutputFaces;
            const std::optional<Sqrt3Subdivide::Sqrt3Result>
                subdivision = Sqrt3Subdivide::Subdivide(source.Mesh, output, params);
            if (!subdivision.has_value())
            {
                result.Status =
                    SandboxEditorCommandStatus::GeometryProcessingFailed;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Geometry.HalfedgeMesh.SubdivisionSqrt3 failed for the selected mesh and parameters.";
                return result;
            }
            result.IterationsPerformed =
                static_cast<std::uint32_t>(
                    subdivision->IterationsPerformed);
            result.OutputVertexCount = subdivision->FinalVertexCount;
            result.OutputFaceCount = subdivision->FinalFaceCount;
        }

        if (output.HasGarbage())
            output.GarbageCollection();
        result.OutputVertexCount = output.VertexCount();
        result.OutputFaceCount = output.FaceCount();

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Subdivide mesh",
                std::move(before),
                std::move(output));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh subdivide publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshSubdivideSuccessMessage(result);
        return result;
    }

    SandboxEditorMeshSimplifyResult ApplySandboxEditorMeshSimplifyCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshSimplifyCommand& command)
    {
        SandboxEditorMeshSimplifyResult result{
            .Status = SandboxEditorCommandStatus::NoChange,
            .Metric = command.Metric,
            .TargetFaces = command.TargetFaces,
            .MaxError = command.MaxError,
            .Error = Core::ErrorCode::Success,
        };

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message = "Scene registry is unavailable for mesh simplify.";
            return result;
        }
        const bool hasStopCriterion =
            command.TargetFaces > 0u || command.MaxError > 0.0;
        if (!ValidMeshSimplifyMetric(command.Metric) ||
            !hasStopCriterion ||
            command.NormalWeight < 0.0 ||
            command.BoundaryWeight < 0.0 ||
            command.CurvatureWeight < 0.0 ||
            command.FeatureAngleThresholdDegrees < 0.0 ||
            command.FeatureAngleThresholdDegrees > 180.0)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Mesh simplify requires a valid metric, a positive target face count or maximum error, non-negative weights, and a feature angle within [0, 180].";
            return result;
        }
        if (!context.MeshSimplifyKernelAvailable)
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidState;
            result.Message =
                "Geometry.Simplification is unavailable in this runtime configuration.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Mesh simplify target entity is stale or no longer live.";
            return result;
        }

        const GS::ConstSourceView view = GS::BuildConstView(raw, *entity);
        MeshTopologySourceResult source =
            BuildHalfedgeMeshForTopologyEdit(view, "Mesh simplify");
        if (!source.Succeeded())
        {
            result.Status = source.Status;
            result.Error = source.Error;
            result.Message = source.Diagnostic;
            return result;
        }

        result.InputVertexCount = source.Mesh.VertexCount();
        result.InputFaceCount = source.Mesh.FaceCount();
        Geometry::HalfedgeMesh::Mesh before = source.Mesh;

        Simpl::Params params{};
        params.Metric =
            command.Metric == SandboxEditorMeshSimplifyMetric::FA_QEM
                ? Simpl::Metric::FA_QEM
                : Simpl::Metric::ClassicalQEM;
        params.TargetFaces = command.TargetFaces;
        params.MaxError = command.MaxError > 0.0 ? command.MaxError : 1.0e30;
        params.PreserveBoundary = command.PreserveBoundary;
        params.FeatureAngleThresholdDegrees =
            command.FeatureAngleThresholdDegrees;
        params.NormalWeight = command.NormalWeight;
        params.BoundaryWeight = command.BoundaryWeight;
        params.CurvatureWeight = command.CurvatureWeight;
        params.PreserveSharpFeatures = command.PreserveSharpFeatures;
        params.PreserveUvSeams = command.PreserveUvSeams;

        const std::optional<Simpl::Result> simplification =
            Simpl::Simplify(source.Mesh, params);
        if (!simplification.has_value())
        {
            result.Status =
                SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Geometry.Simplification failed for the selected mesh and parameters.";
            return result;
        }

        if (source.Mesh.HasGarbage())
            source.Mesh.GarbageCollection();
        result.OutputVertexCount = source.Mesh.VertexCount();
        result.OutputFaceCount = source.Mesh.FaceCount();
        result.CollapseCount = simplification->CollapseCount;
        result.MaxCollapseError = simplification->MaxCollapseError;
        result.CollapsesRejectedTopology =
            simplification->CollapsesRejectedTopology;
        result.CollapsesRejectedQuality =
            simplification->CollapsesRejectedQuality;
        result.SharpFeatureVerticesPinned =
            simplification->SharpFeatureVerticesPinned;
        result.SeamVerticesPinned = simplification->SeamVerticesPinned;

        const SandboxEditorCommandStatus commitStatus =
            CommitMeshTopologyReplacement(
                context,
                command.StableEntityId,
                "Simplify mesh",
                std::move(before),
                std::move(source.Mesh));
        if (commitStatus != SandboxEditorCommandStatus::Applied)
        {
            result.Status = commitStatus;
            result.Error = Core::ErrorCode::Unknown;
            result.Message =
                "Mesh simplify publication failed during editor history commit.";
            return result;
        }

        result.Status = SandboxEditorCommandStatus::Applied;
        result.Error = Core::ErrorCode::Success;
        result.Message = BuildMeshSimplifySuccessMessage(result);
        return result;
    }

    SandboxEditorMeshVertexNormalsResult
    ApplySandboxEditorMeshVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorMeshVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeMeshNormalsResult(
                SandboxEditorCommandStatus::MissingScene,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for mesh vertex-normal recompute.");
        }
        if (!std::isfinite(command.DegenerateNormalLengthEpsilon) ||
            command.DegenerateNormalLengthEpsilon <= 0.0)
        {
            return MakeMeshNormalsResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                GN::RecomputeStatus::InvalidOutputProperty,
                command.Weighting,
                Core::ErrorCode::InvalidArgument,
                "Mesh vertex-normal recompute requires a positive finite degeneracy epsilon.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeMeshNormalsResult(
                SandboxEditorCommandStatus::StaleEntity,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                Core::ErrorCode::ResourceNotFound,
                "Mesh vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const MeshForVertexNormalsResult source =
            BuildHalfedgeMeshForVertexNormalRecompute(view);
        if (!source.Succeeded())
        {
            return MakeMeshNormalsResult(
                source.Status,
                GN::RecomputeStatus::EmptyMesh,
                command.Weighting,
                source.Error,
                source.Diagnostic);
        }

        Geometry::HalfedgeMesh::Mesh mesh = source.Mesh;
        GN::Params params{};
        params.Weighting = command.Weighting;
        params.OutputProperty = GN::kDefaultOutputProperty;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.SkipDeleted = true;

        const GN::Result normalResult = GN::Recompute(mesh, params);
        SandboxEditorMeshVertexNormalsResult result{
            .Status = normalResult.Status == GN::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .Weighting = normalResult.Weighting,
            .Error = normalResult.Status == GN::RecomputeStatus::Success
                ? Core::ErrorCode::Success
                : Core::ErrorCode::Unknown,
        };
        CopyMeshNormalCounters(normalResult, result);
        if (normalResult.Status != GN::RecomputeStatus::Success)
        {
            result.Message = "Geometry.HalfedgeMesh.Vertices.Normals failed with ";
            result.Message += std::string(GN::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        if (!PublishMeshVertexNormals(view, normalResult))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus = GN::RecomputeStatus::PropertyTypeConflict;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message =
                "Mesh vertex-normal publication failed because v:normal has an incompatible type or size.";
            return result;
        }

        Dirty::MarkVertexNormalsDirty(raw, *entity);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty("Recompute mesh vertex normals");
        result.Message = BuildMeshNormalsSuccessMessage(result);
        return result;
    }

    SandboxEditorGraphVertexNormalsResult
    ApplySandboxEditorGraphVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorGraphVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakeGraphNormalsResult(
                SandboxEditorCommandStatus::MissingScene,
                GraphNormals::RecomputeStatus::EmptyGraph,
                command.OrientTowardFallback,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for graph vertex-normal recompute.");
        }
        if (!IsPositiveFinite(command.DegenerateNormalLengthEpsilon) ||
            !IsPositiveFinite(command.CollinearEigenvalueRatioEpsilon))
        {
            return MakeGraphNormalsResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                GraphNormals::RecomputeStatus::InvalidOutputProperty,
                command.OrientTowardFallback,
                Core::ErrorCode::InvalidArgument,
                "Graph vertex-normal recompute requires positive finite degeneracy and collinearity epsilons.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakeGraphNormalsResult(
                SandboxEditorCommandStatus::StaleEntity,
                GraphNormals::RecomputeStatus::EmptyGraph,
                command.OrientTowardFallback,
                Core::ErrorCode::ResourceNotFound,
                "Graph vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GraphForVertexNormalsResult source =
            BuildGraphConnectivityForVertexNormalRecompute(view);
        if (!source.Succeeded())
        {
            return MakeGraphNormalsResult(
                source.Status,
                GraphNormals::RecomputeStatus::InvalidTopologyProperty,
                command.OrientTowardFallback,
                source.Error,
                source.Diagnostic);
        }

        Geometry::Vertices scratchNodes = view.NodeSource->Properties;
        GraphNormals::Params params{};
        params.PositionProperty = GS::PropertyNames::kPosition;
        params.OutputProperty = GraphNormals::kDefaultOutputProperty;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.CollinearEigenvalueRatioEpsilon =
            command.CollinearEigenvalueRatioEpsilon;
        params.SkipDeleted = true;
        params.OrientTowardFallback = command.OrientTowardFallback;

        const GraphNormals::PropertySetResult normalResult =
            GraphNormals::Recompute(
                scratchNodes,
                Geometry::ConstPropertySet(scratchNodes)
                    .Get<glm::vec3>(GS::PropertyNames::kPosition),
                Geometry::ConstPropertySet(source.Halfedges)
                    .Get<Geometry::Graph::HalfedgeConnectivity>(
                        "h:connectivity"),
                source.EdgeSlotCount,
                params,
                Geometry::ConstPropertySet(scratchNodes).Get<bool>(
                    "v:deleted"),
                Geometry::ConstPropertySet(view.EdgeSource->Properties)
                    .Get<bool>("e:deleted"));

        SandboxEditorGraphVertexNormalsResult result{
            .Status = normalResult.Status ==
                    GraphNormals::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .OrientTowardFallback = command.OrientTowardFallback,
            .Error = ErrorForGraphNormalStatus(normalResult.Status),
        };
        CopyGraphNormalCounters(normalResult.Diagnostics, result);
        if (normalResult.Status != GraphNormals::RecomputeStatus::Success)
        {
            result.Message = "Geometry.Graph.Vertex.Normals failed with ";
            result.Message +=
                std::string(GraphNormals::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        if (!normalResult.Normals.IsValid() ||
            !PublishCanonicalVec3Normals(
                view.NodeSource->Properties,
                normalResult.Normals.Vector()))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus =
                GraphNormals::RecomputeStatus::PropertyTypeConflict;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message =
                "Graph vertex-normal publication failed because v:normal has an incompatible type or size.";
            return result;
        }

        Dirty::MarkVertexNormalsDirty(raw, *entity);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty(
                "Recompute graph vertex normals");
        result.Message = BuildGraphNormalsSuccessMessage(result);
        return result;
    }

    SandboxEditorPointCloudVertexNormalsResult
    ApplySandboxEditorPointCloudVertexNormalsCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPointCloudVertexNormalsCommand& command)
    {
        if (context.Scene == nullptr)
        {
            return MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::MissingScene,
                PointNormals::RecomputeStatus::EmptyInput,
                command,
                Core::ErrorCode::InvalidState,
                "Scene registry is unavailable for point-cloud vertex-normal recompute.");
        }
        if (command.KNeighbors == 0u ||
            command.MinimumNeighbors == 0u ||
            !IsPositiveFinite(command.DegenerateNormalLengthEpsilon) ||
            !IsPositiveFinite(command.CollinearEigenvalueRatioEpsilon) ||
            (command.UseRadiusSearch &&
             (!std::isfinite(command.Radius) || command.Radius <= 0.0f)))
        {
            return MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::InvalidProcessingParameters,
                PointNormals::RecomputeStatus::InvalidOutputProperty,
                command,
                Core::ErrorCode::InvalidArgument,
                "Point-cloud vertex-normal recompute requires positive finite neighborhood settings.");
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            return MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::StaleEntity,
                PointNormals::RecomputeStatus::EmptyInput,
                command,
                Core::ErrorCode::ResourceNotFound,
                "Point-cloud vertex-normal target entity is stale or no longer live.");
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
            view.VertexSource == nullptr)
        {
            return MakePointCloudNormalsResult(
                SandboxEditorCommandStatus::UnsupportedGeometryDomain,
                PointNormals::RecomputeStatus::EmptyInput,
                command,
                Core::ErrorCode::InvalidArgument,
                "Point-cloud vertex normals require selected point-cloud GeometrySources.");
        }

        Geometry::Vertices scratchPoints = view.VertexSource->Properties;
        Geometry::PointCloud::Cloud scratchCloud{scratchPoints};
        PointNormals::Params params{};
        params.PositionProperty = GS::PropertyNames::kPosition;
        params.OutputProperty = PointNormals::kDefaultOutputProperty;
        params.KNeighbors = command.KNeighbors;
        params.MinimumNeighbors = command.MinimumNeighbors;
        params.UseRadiusSearch = command.UseRadiusSearch;
        params.Radius = command.Radius;
        params.Orientation = command.Orientation;
        params.FallbackNormal = command.FallbackNormal;
        params.DegenerateNormalLengthEpsilon =
            command.DegenerateNormalLengthEpsilon;
        params.CollinearEigenvalueRatioEpsilon =
            command.CollinearEigenvalueRatioEpsilon;
        params.SkipDeleted = true;

        const PointNormals::Result normalResult =
            PointNormals::Recompute(scratchCloud, params);

        SandboxEditorPointCloudVertexNormalsResult result{
            .Status = normalResult.Status ==
                    PointNormals::RecomputeStatus::Success
                ? SandboxEditorCommandStatus::Applied
                : SandboxEditorCommandStatus::GeometryProcessingFailed,
            .NormalStatus = normalResult.Status,
            .Backend = normalResult.Backend,
            .Orientation = command.Orientation,
            .KNeighbors = command.KNeighbors,
            .MinimumNeighbors = command.MinimumNeighbors,
            .UseRadiusSearch = command.UseRadiusSearch,
            .Radius = command.Radius,
            .Error = ErrorForPointCloudNormalStatus(normalResult.Status),
        };
        CopyPointCloudNormalCounters(normalResult.Diagnostics, result);
        if (normalResult.Status != PointNormals::RecomputeStatus::Success)
        {
            result.Message = "Geometry.PointCloud.Normals failed with ";
            result.Message +=
                std::string(PointNormals::DebugName(normalResult.Status));
            result.Message += ".";
            return result;
        }

        if (!normalResult.Normals.IsValid() ||
            !PublishCanonicalVec3Normals(
                view.VertexSource->Properties,
                normalResult.Normals.Vector()))
        {
            result.Status = SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.NormalStatus =
                PointNormals::RecomputeStatus::PropertyTypeConflict;
            result.Error = Core::ErrorCode::TypeMismatch;
            result.Message =
                "Point-cloud vertex-normal publication failed because v:normal has an incompatible type or size.";
            return result;
        }

        Dirty::MarkVertexNormalsDirty(raw, *entity);
        if (context.CommandHistory != nullptr)
            (void)context.CommandHistory->MarkDirty(
                "Recompute point-cloud vertex normals");
        result.Message = BuildPointCloudNormalsSuccessMessage(result);
        return result;
    }

    SandboxEditorPointCloudOutlierRemovalResult
    ApplySandboxEditorPointCloudOutlierRemovalCommand(
        const SandboxEditorContext& context,
        const SandboxEditorPointCloudOutlierRemovalCommand& command)
    {
        SandboxEditorPointCloudOutlierRemovalResult result{};
        result.Method = command.Method;

        if (context.Scene == nullptr)
        {
            result.Status = SandboxEditorCommandStatus::MissingScene;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires an attached scene.";
            return result;
        }

        const bool statistical =
            command.Method ==
            SandboxEditorPointCloudOutlierMethod::Statistical;
        if (statistical)
        {
            if (command.KNeighbors == 0u ||
                !std::isfinite(command.StdDevMultiplier))
            {
                result.Status =
                    SandboxEditorCommandStatus::InvalidProcessingParameters;
                result.GeometryStatus =
                    Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
                result.Error = Core::ErrorCode::InvalidArgument;
                result.Message =
                    "Statistical outlier removal requires KNeighbors > 0 and a finite std-dev multiplier.";
                return result;
            }
        }
        else if (!std::isfinite(command.SearchRadius) ||
                 command.SearchRadius <= 0.0f)
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Radius outlier removal requires a positive finite search radius.";
            return result;
        }

        entt::registry& raw = context.Scene->Raw();
        const std::optional<ECS::EntityHandle> entity =
            ResolveStableEntity(raw, command.StableEntityId);
        if (!entity.has_value())
        {
            result.Status = SandboxEditorCommandStatus::StaleEntity;
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Point-cloud outlier-removal target entity is stale or no longer live.";
            return result;
        }

        GS::MutableSourceView view = GS::BuildMutableView(raw, *entity);
        const GS::SourceAvailability availability =
            GS::BuildSourceAvailability(view);
        if (availability.ProvenanceDomain != GS::Domain::PointCloud ||
            view.VertexSource == nullptr)
        {
            result.Status =
                SandboxEditorCommandStatus::UnsupportedGeometryDomain;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires selected point-cloud GeometrySources.";
            return result;
        }

        // Snapshot the original point property set (including any deleted slots
        // and the live deletion counter) so undo restores the entity exactly.
        Geometry::Vertices originalPoints = view.VertexSource->Properties;
        std::size_t originalNumDeleted = view.VertexSource->NumDeleted;
        if (!MirrorGeometrySourcePositionsToPointCloudStorage(originalPoints))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires a count-matched v:position property.";
            return result;
        }
        Geometry::PointCloud::Cloud beforeCloud{
            originalPoints,
            originalNumDeleted};

        // Build a separate live-only working cloud: bind the deletion counter and
        // garbage-collect so the GEOM-016 operators (which iterate every slot via
        // VerticesSize()) run over the live points only, and so kept/rejected
        // indices and result counts match the live point set rather than dead
        // slots. The full property set is carried so user attributes survive.
        Geometry::Vertices workPoints = view.VertexSource->Properties;
        std::size_t workNumDeleted = view.VertexSource->NumDeleted;
        if (!MirrorGeometrySourcePositionsToPointCloudStorage(workPoints))
        {
            result.Status =
                SandboxEditorCommandStatus::InvalidProcessingParameters;
            result.GeometryStatus =
                Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message =
                "Point-cloud outlier removal requires a count-matched v:position property.";
            return result;
        }
        Geometry::PointCloud::Cloud workCloud{workPoints, workNumDeleted};
        if (workCloud.HasGarbage())
            workCloud.GarbageCollection();

        Geometry::PointCloud::OutlierRemovalResult removal{};
        if (statistical)
        {
            Geometry::PointCloud::StatisticalOutlierRemovalParams params{};
            params.KNeighbors = command.KNeighbors;
            params.StdDevMultiplier = command.StdDevMultiplier;
            removal =
                Geometry::PointCloud::RemoveStatisticalOutliers(
                    workCloud,
                    params);
        }
        else
        {
            Geometry::PointCloud::RadiusOutlierRemovalParams params{};
            params.SearchRadius = command.SearchRadius;
            params.MinNeighbors = command.MinNeighbors;
            removal =
                Geometry::PointCloud::RemoveRadiusOutliers(
                    workCloud,
                    params);
        }

        result.GeometryStatus = removal.Status;
        result.OriginalCount = removal.OriginalCount;
        result.KeptCount = removal.KeptCount;
        result.RejectedCount = removal.RejectedCount;
        result.NonFiniteCount = removal.NonFiniteCount;
        result.MeanDistance = removal.MeanDistance;
        result.StdDevDistance = removal.StdDevDistance;
        result.DistanceThreshold = removal.DistanceThreshold;

        if (removal.Status !=
            Geometry::PointCloud::OutlierRemovalStatus::Success)
        {
            result.Status =
                removal.Status ==
                        Geometry::PointCloud::OutlierRemovalStatus::InvalidParameters
                    ? SandboxEditorCommandStatus::InvalidProcessingParameters
                    : SandboxEditorCommandStatus::GeometryProcessingFailed;
            result.Error = Core::ErrorCode::InvalidArgument;
            result.Message = "Geometry.PointCloud outlier removal failed with ";
            result.Message += DebugNameForOutlierRemovalStatus(removal.Status);
            result.Message += ".";
            return result;
        }

        // Compact the full-property working cloud down to the kept points by
        // deleting the rejected slots and garbage-collecting. This preserves
        // every surviving per-point property (normals, K-Means labels,
        // visualization scalars, ...), unlike `removal.Filtered`, which only
        // carries the Cloud built-ins (position/normal/color/radius).
        Geometry::PointCloud::Cloud afterCloud = workCloud;
        for (const std::size_t rejected : removal.RejectedIndices)
            afterCloud.DeletePoint(
                Geometry::VertexHandle{static_cast<std::uint32_t>(rejected)});
        afterCloud.GarbageCollection();

        const SandboxEditorCommandStatus status =
            CommitPointCloudReplacement(
                context,
                command.StableEntityId,
                statistical
                    ? "Remove statistical point-cloud outliers"
                    : "Remove radius point-cloud outliers",
                beforeCloud,
                afterCloud);
        result.Status = status;
        if (status != SandboxEditorCommandStatus::Applied)
        {
            result.Error = Core::ErrorCode::ResourceNotFound;
            result.Message =
                "Point-cloud outlier-removal publication failed; the target entity may no longer be live.";
            return result;
        }

        result.Message = "Removed " + std::to_string(result.RejectedCount) +
                         " of " + std::to_string(result.OriginalCount) +
                         " points (kept " + std::to_string(result.KeptCount);
        if (result.NonFiniteCount > 0u)
        {
            result.Message +=
                ", non-finite " + std::to_string(result.NonFiniteCount);
        }
        result.Message += ").";
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
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr,
                       nullptr,
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
                if (m_LastMeshDenoiseResult.has_value())
                    context.LastMeshDenoiseResult =
                        &*m_LastMeshDenoiseResult;
                if (m_LastMeshCurvatureResult.has_value())
                    context.LastMeshCurvatureResult =
                        &*m_LastMeshCurvatureResult;
                if (m_LastMeshRemeshResult.has_value())
                    context.LastMeshRemeshResult =
                        &*m_LastMeshRemeshResult;
                if (m_LastMeshSubdivideResult.has_value())
                    context.LastMeshSubdivideResult =
                        &*m_LastMeshSubdivideResult;
                if (m_LastMeshSimplifyResult.has_value())
                    context.LastMeshSimplifyResult =
                        &*m_LastMeshSimplifyResult;
                if (m_LastMeshVertexNormalsResult.has_value())
                    context.LastMeshVertexNormalsResult =
                        &*m_LastMeshVertexNormalsResult;
                if (m_LastGraphVertexNormalsResult.has_value())
                    context.LastGraphVertexNormalsResult =
                        &*m_LastGraphVertexNormalsResult;
                if (m_LastPointCloudVertexNormalsResult.has_value())
                    context.LastPointCloudVertexNormalsResult =
                        &*m_LastPointCloudVertexNormalsResult;
                if (m_LastPointCloudOutlierRemovalResult.has_value())
                    context.LastPointCloudOutlierRemovalResult =
                        &*m_LastPointCloudOutlierRemovalResult;
                if (m_LastProgressivePoissonResult.has_value())
                    context.LastProgressivePoissonResult =
                        &*m_LastProgressivePoissonResult;
                const Core::Extent2D viewport =
                    context.CameraViewport.Width != 0u &&
                            context.CameraViewport.Height != 0u
                        ? context.CameraViewport
                        : Core::Extent2D{.Width = 1280u, .Height = 720u};
                const Graphics::RenderFrameInput recipeInput{
                    .Viewport = viewport,
                    .Camera = Graphics::CameraViewInput{.Valid = true},
                };
                m_RenderRecipeContext = Graphics::RenderRecipeConfigContext{
                    .Renderer = Graphics::MakeCurrentRendererDescriptor(),
                    .BaseRecipe = Graphics::MakeCurrentRendererRecipeDescriptor(),
                    .BaseViewOutput =
                        Graphics::MakeCurrentRendererViewOutputRecipe(recipeInput),
                    .BaseBindings = Graphics::MakeCurrentRendererBindingSet(),
                };
                context.RenderRecipeContext = &m_RenderRecipeContext;
                context.RenderRecipeEditorState = &m_RenderRecipeState;
                context.RenderArtifacts = &m_RenderArtifactRegistry;
                context.RenderRecipeCommandsAvailable = true;
                context.EngineConfigControlState =
                    &m_Engine->GetEngineConfigControlState();
                context.PreviewEngineConfigDocument =
                    [this](const std::string& document,
                           const std::string& sourceId)
                    {
                        return m_Engine->PreviewEngineConfigControlDocument(
                            document,
                            sourceId);
                    };
                context.ApplyEngineConfigHotSubset =
                    [this](const Core::Config::EngineConfigLoadResult& loadResult)
                    {
                        return m_Engine->ApplyEngineConfigHotSubset(
                            loadResult,
                            RuntimeConfigControlSource::Editor);
                    };
                context.EngineConfigCommandsAvailable = true;
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
                MeshDenoiseUiState denoiseState{
                    .LastResult = &m_LastMeshDenoiseResult,
                    .Stage = &m_MeshDenoiseStage,
                    .NormalIterations =
                        &m_MeshDenoiseNormalIterations,
                    .VertexIterations =
                        &m_MeshDenoiseVertexIterations,
                    .SigmaSpatial = &m_MeshDenoiseSigmaSpatial,
                    .SigmaRange = &m_MeshDenoiseSigmaRange,
                    .PreserveBoundary =
                        &m_MeshDenoisePreserveBoundary,
                };
                MeshCurvatureUiState curvatureState{
                    .LastResult = &m_LastMeshCurvatureResult,
                    .Output = &m_MeshCurvatureOutput,
                    .PublishPrincipalDirections =
                        &m_MeshCurvaturePublishDirections,
                };
                MeshRemeshUiState remeshState{
                    .LastResult = &m_LastMeshRemeshResult,
                    .Mode = &m_MeshRemeshMode,
                    .SizingLaw = &m_MeshRemeshSizingLaw,
                    .Iterations = &m_MeshRemeshIterations,
                    .TargetEdgeLength = &m_MeshRemeshTargetEdgeLength,
                    .ProjectToSurface = &m_MeshRemeshProjectToSurface,
                };
                MeshSubdivideUiState subdivideState{
                    .LastResult = &m_LastMeshSubdivideResult,
                    .Operator = &m_MeshSubdivideOperator,
                    .Iterations = &m_MeshSubdivideIterations,
                    .PreserveLoopFeatures =
                        &m_MeshSubdividePreserveLoopFeatures,
                };
                MeshSimplifyUiState simplifyState{
                    .LastResult = &m_LastMeshSimplifyResult,
                    .Metric = &m_MeshSimplifyMetric,
                    .TargetFaces = &m_MeshSimplifyTargetFaces,
                    .MaxError = &m_MeshSimplifyMaxError,
                    .PreserveBoundary = &m_MeshSimplifyPreserveBoundary,
                    .FeatureAngleThresholdDegrees =
                        &m_MeshSimplifyFeatureAngleThresholdDegrees,
                    .NormalWeight = &m_MeshSimplifyNormalWeight,
                    .BoundaryWeight = &m_MeshSimplifyBoundaryWeight,
                    .CurvatureWeight = &m_MeshSimplifyCurvatureWeight,
                    .PreserveSharpFeatures =
                        &m_MeshSimplifyPreserveSharpFeatures,
                    .PreserveUvSeams = &m_MeshSimplifyPreserveUvSeams,
                };
                MeshVertexNormalsUiState normalsState{
                    .LastResult = &m_LastMeshVertexNormalsResult,
                    .Weighting = &m_MeshVertexNormalsWeighting,
                    .FallbackNormal = &m_MeshVertexNormalsFallback,
                };
                GraphVertexNormalsUiState graphNormalsState{
                    .LastResult = &m_LastGraphVertexNormalsResult,
                    .FallbackNormal = &m_GraphVertexNormalsFallback,
                    .OrientTowardFallback =
                        &m_GraphVertexNormalsOrientTowardFallback,
                };
                PointCloudVertexNormalsUiState pointCloudNormalsState{
                    .LastResult = &m_LastPointCloudVertexNormalsResult,
                    .KNeighbors = &m_PointCloudVertexNormalsK,
                    .MinimumNeighbors =
                        &m_PointCloudVertexNormalsMinimumNeighbors,
                    .UseRadiusSearch =
                        &m_PointCloudVertexNormalsUseRadius,
                    .Radius = &m_PointCloudVertexNormalsRadius,
                    .Orientation = &m_PointCloudVertexNormalsOrientation,
                    .FallbackNormal = &m_PointCloudVertexNormalsFallback,
                };
                PointCloudOutlierRemovalUiState pointCloudOutlierState{
                    .LastResult = &m_LastPointCloudOutlierRemovalResult,
                    .Method = &m_PointCloudOutlierMethod,
                    .KNeighbors = &m_PointCloudOutlierKNeighbors,
                    .StdDevMultiplier = &m_PointCloudOutlierStdDevMultiplier,
                    .SearchRadius = &m_PointCloudOutlierSearchRadius,
                    .MinNeighbors = &m_PointCloudOutlierMinNeighbors,
                };
                ProgressivePoissonUiState progressivePoissonState{
                    .LastResult = &m_LastProgressivePoissonResult,
                    .LastConfigResult =
                        &m_LastProgressivePoissonConfigResult,
                    .Dimension = &m_ProgressivePoissonDimension,
                    .GridWidth = &m_ProgressivePoissonGridWidth,
                    .MaxLevels = &m_ProgressivePoissonMaxLevels,
                    .HashLoadFactor = &m_ProgressivePoissonHashLoadFactor,
                    .RadiusAlpha = &m_ProgressivePoissonRadiusAlpha,
                    .RandomizeGridOrigin =
                        &m_ProgressivePoissonRandomizeGridOrigin,
                    .GridOriginSeed = &m_ProgressivePoissonGridOriginSeed,
                    .ShuffleWithinLevels =
                        &m_ProgressivePoissonShuffleWithinLevels,
                    .ShuffleSeed = &m_ProgressivePoissonShuffleSeed,
                    .PrefixCount = &m_ProgressivePoissonPrefixCount,
                    .Channel = &m_ProgressivePoissonChannel,
                    .Backend = &m_ProgressivePoissonBackend,
                    .MeshSurfaceSampleCount =
                        &m_ProgressivePoissonMeshSurfaceSampleCount,
                    .MeshSurfaceSampleSeed =
                        &m_ProgressivePoissonMeshSurfaceSampleSeed,
                    .MeshSurfaceMinTriangleArea =
                        &m_ProgressivePoissonMeshSurfaceMinTriangleArea,
                    .MeshSurfaceInterpolateNormals =
                        &m_ProgressivePoissonMeshSurfaceInterpolateNormals,
                    .AutoRunOnEdit = &m_ProgressivePoissonAutoRunOnEdit,
                    .DebounceSeconds =
                        &m_ProgressivePoissonDebounceSeconds,
                    .AutoRunPending =
                        &m_ProgressivePoissonAutoRunPending,
                    .LastEditTime = &m_ProgressivePoissonLastEditTime,
                    .PendingStableEntityId =
                        &m_ProgressivePoissonPendingStableEntityId,
                };
                TextureBakeUiState textureBakeState{
                    .SourceIndex = &m_TextureBakeSourceIndex,
                    .TargetSemanticIndex = &m_TextureBakeTargetSemanticIndex,
                    .EncoderIndex = &m_TextureBakeEncoderIndex,
                    .Width = &m_TextureBakeWidth,
                    .Height = &m_TextureBakeHeight,
                    .UvResolution = &m_UvAtlasResolution,
                    .UvPadding = &m_UvAtlasPadding,
                    .UvTexelsPerUnit = &m_UvAtlasTexelsPerUnit,
                    .UvForceRegenerate = &m_UvAtlasForceRegenerate,
                    .UvPreserveAuthored = &m_UvAtlasPreserveAuthored,
                };
                DrawPanelFrame(
                    m_LastFrame,
                    &context,
                    &m_ImportPathBuffer,
                    &m_ScenePathBuffer,
                    &m_RenderRecipeDraftBuffer,
                    &m_ImportPayloadKind,
                    &m_LastImportResult,
                    &m_LastSceneFileResult,
                    &m_PanelWindowOpen,
                    &m_DomainWindowOpen,
                    &kmeansState,
                    &denoiseState,
                    &curvatureState,
                    &remeshState,
                    &subdivideState,
                    &simplifyState,
                    &normalsState,
                    &graphNormalsState,
                    &pointCloudNormalsState,
                    &pointCloudOutlierState,
                    &progressivePoissonState,
                    &textureBakeState);
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
