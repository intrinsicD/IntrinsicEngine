module;

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.AssetWorkflowRecipePolicies;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.AssetWorkflowGeometryMaterialization;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        struct DirectMeshPostProcessState
        {
            std::string Path{};
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::shared_ptr<const Geometry::MeshIO::MeshIOResult> Payload{};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
            JobToken Job{};
            WorldRegistry* Worlds{};
            WorldHandle World{};
            std::function<bool()> BindingValid{};
            std::uint64_t SubmittedSignature{0u};
            std::uint64_t SubmittedBindingSignature{0u};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::optional<RuntimeMeshMaterializationResult> Materialized{};
        };

        constexpr std::uint64_t kDirectMeshSignatureOffset =
            1469598103934665603ull;
        constexpr std::uint64_t kDirectMeshSignaturePrime =
            1099511628211ull;

        void MixDirectMeshSignatureByte(
            std::uint64_t& signature,
            const std::uint8_t value) noexcept
        {
            signature ^= value;
            signature *= kDirectMeshSignaturePrime;
        }

        void MixDirectMeshSignature(
            std::uint64_t& signature,
            std::uint64_t value) noexcept
        {
            for (std::uint32_t byte = 0u; byte < 8u; ++byte)
            {
                MixDirectMeshSignatureByte(
                    signature,
                    static_cast<std::uint8_t>(
                        (value >> (byte * 8u)) & 0xffu));
            }
        }

        void MixDirectMeshSignatureString(
            std::uint64_t& signature,
            const std::string_view value) noexcept
        {
            MixDirectMeshSignature(
                signature,
                static_cast<std::uint64_t>(value.size()));
            for (const char character : value)
            {
                MixDirectMeshSignatureByte(
                    signature,
                    static_cast<std::uint8_t>(character));
            }
        }

        [[nodiscard]] bool AppendDirectMeshPropertySetSignature(
            std::uint64_t& signature,
            const std::uint64_t domainTag,
            const Geometry::PropertySet* properties,
            const std::size_t deletedCount)
        {
            MixDirectMeshSignature(signature, domainTag);
            if (properties == nullptr)
            {
                MixDirectMeshSignature(signature, 0u);
                return true;
            }

            MixDirectMeshSignature(signature, 1u);
            MixDirectMeshSignature(
                signature,
                static_cast<std::uint64_t>(properties->Size()));
            MixDirectMeshSignature(
                signature,
                static_cast<std::uint64_t>(deletedCount));
            const std::vector<Geometry::PropertyDescriptor> descriptors =
                properties->Registry().Descriptors(false);
            MixDirectMeshSignature(
                signature,
                static_cast<std::uint64_t>(descriptors.size()));

            std::uint64_t order = 0u;
            for (const Geometry::PropertyDescriptor& descriptor : descriptors)
            {
                MixDirectMeshSignature(signature, order++);
                MixDirectMeshSignatureString(signature, descriptor.Name);
                MixDirectMeshSignature(
                    signature,
                    static_cast<std::uint64_t>(descriptor.ValueKind));
                MixDirectMeshSignature(
                    signature,
                    static_cast<std::uint64_t>(descriptor.ElementCount));

                MixDirectMeshSignature(signature, descriptor.ContentRevision);
                MixDirectMeshSignature(signature, descriptor.Id);
            }
            return true;
        }

        void AppendDirectMeshPropertyRefSignature(
            std::uint64_t& signature,
            const GeometryPropertyRef& property)
        {
            MixDirectMeshSignature(
                signature,
                static_cast<std::uint64_t>(property.Domain));
            MixDirectMeshSignatureString(signature, property.Name);
            MixDirectMeshSignature(
                signature,
                static_cast<std::uint64_t>(property.ValueKind));
        }

        std::uint64_t CaptureDirectMeshBindingSignature(const entt::registry& raw,
            ECS::EntityHandle entity)
        {
            std::uint64_t signature = kDirectMeshSignatureOffset;
            if (const auto* bindings =
                    raw.try_get<VertexChannelBindingSet>(entity))
            {
                MixDirectMeshSignature(signature, 1u);
                MixDirectMeshSignature(
                    signature,
                    bindings->BindingGeneration);
                for (const VertexChannelSourceBinding* binding :
                     {&bindings->Position, &bindings->Normal, &bindings->Texcoord})
                {
                    MixDirectMeshSignature(signature, binding->Enabled ? 1u : 0u);
                    AppendDirectMeshPropertyRefSignature(signature, binding->Property);
                }
            }
            else
            {
                MixDirectMeshSignature(signature, 0u);
            }

            return signature;
        }

        bool MatchesDirectMeshGeometry(const entt::registry& raw, ECS::EntityHandle entity,
            const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            namespace GS = ECS::Components::GeometrySources;
            const auto view = GS::BuildConstView(raw, entity);
            if (view.ActiveDomain != GS::Domain::Mesh || !view.VertexSource ||
                !view.EdgeSource || !view.HalfedgeSource || !view.FaceSource ||
                view.VertexSource->Properties.Size() != mesh.VerticesSize() ||
                view.EdgeSource->Properties.Size() != mesh.EdgesSize() ||
                view.HalfedgeSource->Properties.Size() != mesh.HalfedgesSize() ||
                view.FaceSource->Properties.Size() != mesh.FacesSize() ||
                view.VertexSource->NumDeleted || view.EdgeSource->NumDeleted || view.FaceSource->NumDeleted)
                return false;
            const auto& vertices = view.VertexSource->Properties;
            const auto& edges = view.EdgeSource->Properties;
            const auto& halfedges = view.HalfedgeSource->Properties;
            const auto& faces = view.FaceSource->Properties;
            if (vertices.Exists("v:texcoord") || halfedges.Exists("h:texcoord")) return false;
            const auto positions = vertices.Get<glm::vec3>(GS::PropertyNames::kPosition);
            const auto v0 = edges.Get<std::uint32_t>("e:v0");
            const auto v1 = edges.Get<std::uint32_t>("e:v1");
            const auto to = halfedges.Get<std::uint32_t>("h:to_vertex");
            const auto next = halfedges.Get<std::uint32_t>("h:next");
            const auto face = halfedges.Get<std::uint32_t>("h:face");
            const auto first = faces.Get<std::uint32_t>("f:halfedge");
            if (!positions || !v0 || !v1 || !to || !next || !face || !first ||
                positions.Vector().size() != mesh.VerticesSize() ||
                v0.Vector().size() != mesh.EdgesSize() || v1.Vector().size() != mesh.EdgesSize() ||
                to.Vector().size() != mesh.HalfedgesSize() || next.Vector().size() != mesh.HalfedgesSize() ||
                face.Vector().size() != mesh.HalfedgesSize() || first.Vector().size() != mesh.FacesSize()) return false;
            for (const auto* properties : {&vertices, &edges, &faces})
                for (const auto* name : {"v:deleted", "e:deleted", "f:deleted"})
                    if (const auto deleted = properties->Get<bool>(name))
                        for (const bool value : deleted.Vector())
                            if (value) return false;
            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
                if (positions.Vector()[i] != mesh.Position(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)})) return false;
            for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
            {
                const auto h = mesh.Halfedge(Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)}, 0);
                if (v0.Vector()[i] != mesh.FromVertex(h).Index || v1.Vector()[i] != mesh.ToVertex(h).Index) return false;
            }
            for (std::size_t i = 0; i < mesh.HalfedgesSize(); ++i)
            {
                const Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(i)};
                if (to.Vector()[i] != mesh.ToVertex(h).Index || next.Vector()[i] != mesh.NextHalfedge(h).Index ||
                    face.Vector()[i] != mesh.Face(h).Index) return false;
            }
            for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
                if (first.Vector()[i] != mesh.Halfedge(Geometry::FaceHandle{static_cast<Geometry::PropertyIndex>(i)}).Index) return false;
            return true;
        }

        [[nodiscard]] std::optional<std::uint64_t>
        CaptureDirectMeshGenerationSignature(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            namespace GS = ECS::Components::GeometrySources;
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            if (!view.Valid() || view.ActiveDomain != GS::Domain::Mesh)
                return std::nullopt;

            std::uint64_t signature = kDirectMeshSignatureOffset;
            MixDirectMeshSignature(
                signature,
                static_cast<std::uint64_t>(view.ActiveDomain));
            MixDirectMeshSignature(
                signature,
                view.HasMeshTopologyMarker ? 1u : 0u);
            MixDirectMeshSignature(
                signature,
                view.HasGraphTopologyMarker ? 1u : 0u);

            if (!AppendDirectMeshPropertySetSignature(
                    signature,
                    1u,
                    view.VertexSource != nullptr
                        ? &view.VertexSource->Properties
                        : nullptr,
                    view.VertexSource != nullptr
                        ? view.VertexSource->NumDeleted
                        : 0u) ||
                !AppendDirectMeshPropertySetSignature(
                    signature,
                    2u,
                    view.EdgeSource != nullptr
                        ? &view.EdgeSource->Properties
                        : nullptr,
                    view.EdgeSource != nullptr
                        ? view.EdgeSource->NumDeleted
                        : 0u) ||
                !AppendDirectMeshPropertySetSignature(
                    signature,
                    3u,
                    view.HalfedgeSource != nullptr
                        ? &view.HalfedgeSource->Properties
                        : nullptr,
                    0u) ||
                !AppendDirectMeshPropertySetSignature(
                    signature,
                    4u,
                    view.FaceSource != nullptr
                        ? &view.FaceSource->Properties
                        : nullptr,
                    view.FaceSource != nullptr
                        ? view.FaceSource->NumDeleted
                        : 0u))
            {
                return std::nullopt;
            }

            MixDirectMeshSignature(signature, CaptureDirectMeshBindingSignature(raw, entity));

            return signature;
        }

        [[nodiscard]] std::string FileNameFromPath(const std::string_view path)
        {
            if (path.empty())
            {
                return {};
            }

            const std::size_t slash = path.find_last_of("/\\");
            const std::size_t begin =
                slash == std::string_view::npos ? 0u : slash + 1u;
            if (begin >= path.size())
            {
                return {};
            }
            return std::string(path.substr(begin));
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

        [[nodiscard]] Graphics::Components::VisualizationConfig
            ImportedMeshVisualization() noexcept
        {
            Graphics::Components::VisualizationConfig visualization =
                ImportedGeometryVisualization();
            visualization.Source =
                Graphics::Components::VisualizationConfig::ColorSource::Material;
            return visualization;
        }

        void FocusMainCameraOnImportTarget(
            CameraControllerRegistry& cameraControllers,
            const Core::Config::CameraControllerKind controllerKind,
            const bool cameraEnabled,
            const std::optional<CameraFocusTarget>& target)
        {
            if (!cameraEnabled || !target.has_value())
                return;

            ICameraController* controller =
                cameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            if (controller == nullptr)
            {
                cameraControllers.Register(
                    CameraControllerSlot::Main,
                    CreateCameraController(controllerKind));
                controller =
                    cameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            }
            if (controller == nullptr)
                return;

            controller->Focus(*target);
            cameraControllers.MarkCameraTransition(CameraControllerSlot::Main);
        }

        [[nodiscard]] PropertyTextureBakeRequest
        BuildDirectMeshNormalBakeRequest(
            const ECS::EntityHandle entity,
            const WorldHandle world,
            const std::uint32_t atlasWidth,
            const std::uint32_t atlasHeight)
        {
            namespace GS = ECS::Components::GeometrySources;
            return PropertyTextureBakeRequest{
                .World = world,
                .StableEntityId =
                    StableEntityLookup::ToRenderId(entity),
                .Source = GeometryPropertyRef{
                    .Domain = GeometryElementDomain::MeshVertex,
                    .Name = std::string{GS::PropertyNames::kNormal},
                    .ValueKind = Geometry::PropertyValueKind::Vec3,
                },
                .Storage = PropertyTextureBakeStorage::EncodedRgba,
                .Encoding = PropertyTextureBakeEncoding::Normal,
                .Width = atlasWidth != 0u ? atlasWidth : 1024u,
                .Height = atlasHeight != 0u ? atlasHeight : 1024u,
                .PaddingTexels = 2u,
                .OutputName = "generated-normal",
            };
        }

        void ConfigureDirectMeshNormalPresentationTarget(
            ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const std::string_view outputName)
        {
            auto& raw = scene.Raw();
            auto& recipe =
                raw.get_or_emplace<GeometryPresentationRecipe>(entity);
            recipe.Shape = GeometryPresentationShape::Mesh;

            GeometryPresentationLaneRecipe* lane = FindGeometryPresentationLane(
                recipe, GeometryRenderLane::Surface);
            if (lane == nullptr)
            {
                recipe.Lanes.push_back(GeometryPresentationLaneRecipe{
                    .Lane = GeometryRenderLane::Surface,
                    .PresentationKey = "mesh.surface",
                });
            }
            else if (lane->PresentationKey.empty())
            {
                lane->PresentationKey = "mesh.surface";
            }

            GeometryPresentationBindingRecipe* presentation =
                FindGeometryPresentationBinding(recipe, "mesh.surface");
            if (presentation == nullptr)
            {
                recipe.Presentations.push_back(
                    GeometryPresentationBindingRecipe{
                        .Key = "mesh.surface",
                        .Kind = GeometryPresentationKind::SurfaceMaterial,
                    });
                presentation = &recipe.Presentations.back();
            }
            GeometryPresentationSlotRecipe* slot = FindGeometryPresentationSlot(
                *presentation, GeometryPresentationSlotSemantic::Normal);
            if (slot == nullptr)
            {
                presentation->Slots.push_back(GeometryPresentationSlotRecipe{
                    .Semantic = GeometryPresentationSlotSemantic::Normal,
                });
                slot = &presentation->Slots.back();
            }
            slot->SourceKind = GeometryPresentationSourceKind::PropertyBake;
            slot->Property = GeometryPropertyRef{
                .Domain = GeometryElementDomain::MeshVertex,
                .Name = std::string{ECS::Components::GeometrySources::
                                        PropertyNames::kNormal},
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            };
            slot->GeneratedOutputName = std::string{outputName};
            slot->NormalSpace = GeometryPresentationNormalSpace::Object;
            slot->GeneratedPolicy =
                GeometryGeneratedOutputPolicy::DeterministicChildAsset;
            slot->Enabled = true;

            auto& state =
                raw.get_or_emplace<GeometryPresentationRuntimeState>(entity);
            GeometryPresentationSlotStatus* status =
                FindGeometryPresentationSlotStatus(
                    state,
                    presentation->Key,
                    GeometryPresentationSlotSemantic::Normal);
            if (status == nullptr)
            {
                state.Slots.push_back(GeometryPresentationSlotStatus{
                    .PresentationKey = presentation->Key,
                    .Semantic = GeometryPresentationSlotSemantic::Normal,
                });
                status = &state.Slots.back();
            }
            status->Readiness = GeometryPresentationReadiness::Pending;
            status->Provenance =
                GeometryPresentationProvenance::PropertyBinding;
            status->Diagnostic = "GPU property texture bake pending";
            ++state.RecipeGeneration;
        }

        void MarkMeshGeometryDirty(entt::registry& raw,
                                   const ECS::EntityHandle entity)
        {
            ECS::Components::DirtyTags::MarkGpuDirty(raw, entity);
            ECS::Components::DirtyTags::MarkVertexPositionsDirty(raw, entity);
            ECS::Components::DirtyTags::MarkFaceTopologyDirty(raw, entity);
            ECS::Components::DirtyTags::MarkEdgeTopologyDirty(raw, entity);
        }

        void UpdateDirectMeshEnrichmentState(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const JobToken job,
            const JobState status,
            std::string diagnostic)
        {
            auto* enrichment =
                raw.try_get<AssetImportMeshEnrichmentState>(entity);
            if (enrichment == nullptr || enrichment->Job != job)
                return;
            enrichment->Status = status;
            enrichment->Diagnostic = std::move(diagnostic);
        }

        [[nodiscard]] const char* DescribeUvProvenance(
            const RuntimeMeshResolvedUvProvenance provenance) noexcept
        {
            switch (provenance)
            {
            case RuntimeMeshResolvedUvProvenance::AuthoredPreserved:
                return "authored";
            case RuntimeMeshResolvedUvProvenance::GeneratedAtlas:
                return "generated-atlas";
            case RuntimeMeshResolvedUvProvenance::None:
                break;
            }
            return "none";
        }

        // The enrichment diagnostic exposes what import produced, so it names
        // the preserved topology and where UV seam duplication is applied.
        [[nodiscard]] std::string DescribeDirectMeshEnrichment(
            const RuntimeMeshMaterializationDiagnostics& diagnostics)
        {
            std::string message =
                "Direct mesh enrichment applied successfully: "
                + std::to_string(diagnostics.ResolvedVertexCount)
                + " vertices, "
                + std::to_string(diagnostics.ResolvedFaceCount)
                + " faces.";
            if (diagnostics.TexcoordsOnCornerDomain)
            {
                message += " UVs are per-corner ("
                    + std::string{DescribeUvProvenance(
                        diagnostics.TexcoordProvenance)}
                    + "); GPU upload duplicates "
                    + std::to_string(diagnostics.GpuSplitVertexCount)
                    + " vertices at the seams.";
            }
            else if (diagnostics.ResolvedTexcoordsValid)
            {
                message += " UVs are per-vertex ("
                    + std::string{DescribeUvProvenance(
                        diagnostics.TexcoordProvenance)}
                    + "); no UV seams.";
            }
            else
            {
                message += " No usable texture coordinates were resolved.";
            }
            if (diagnostics.UnmappedCornerCount != 0u)
            {
                message += " "
                    + std::to_string(diagnostics.UnmappedCornerCount)
                    + " corners inherited a neighbouring UV because the atlas "
                      "dropped their face.";
            }
            return message;
        }

        // The deferred post-process only carries its outcome forward in the
        // shared state record its callbacks capture; the envelope proves the
        // worker body ran, since an empty envelope is a dropped job.
        struct DirectMeshPostProcessDone
        {
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        };

        struct DirectMeshNormalBakeRetryDone
        {
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        };

        constexpr std::uint32_t kDirectMeshNormalBakeRetryDrainLimit = 256u;

        [[nodiscard]] ECS::Scene::Registry* ResolveDirectMeshScene(
            WorldRegistry* const worlds,
            const WorldHandle world) noexcept
        {
            return worlds != nullptr && world.IsValid()
                ? worlds->Get(world)
                : nullptr;
        }

        [[nodiscard]] bool IsDirectMeshBindingCurrent(
            WorldRegistry* const worlds,
            const WorldHandle world,
            const std::function<bool()>& bindingValid)
        {
            return bindingValid &&
                bindingValid() &&
                worlds != nullptr &&
                world.IsValid() &&
                worlds->ActiveWorld() == world &&
                worlds->Get(world) != nullptr;
        }

        void DeferDirectMeshNormalBakeUntilOperational(
            JobService& jobs,
            WorldRegistry* const worlds,
            const WorldHandle world,
            std::function<bool()> bindingValid,
            TextureBakeService& textureBake,
            const ECS::EntityHandle entity,
            const std::uint32_t atlasWidth,
            const std::uint32_t atlasHeight,
            std::string sourcePath)
        {
            auto readinessChecks = std::make_shared<std::uint32_t>(0u);
            const JobToken handle = jobs.Submit(
                JobDesc{
                    .DebugName = "Runtime.DirectMeshNormalBakeRetry." +
                        FileNameFromPath(sourcePath),
                    .Scope = world,
                    .Priority = Core::Dag::TaskPriority::Low,
                    .Kind = RuntimeTaskKinds::AssetDecode,
                    .EstimatedCost = 1u,
                    .Work =
                        [entity](const JobCancellation&)
                        {
                            return JobResultEnvelope::Make<
                                DirectMeshNormalBakeRetryDone>(
                                DirectMeshNormalBakeRetryDone{
                                    .Entity = entity,
                                });
                        },
                    .IsReadyToApply =
                        [&textureBake, readinessChecks]
                        {
                            if (textureBake.Available())
                                return true;
                            ++*readinessChecks;
                            return *readinessChecks >=
                                kDirectMeshNormalBakeRetryDrainLimit;
                        },
                    .ValidateBeforeApply =
                        [worlds, world, bindingValid, entity]
                        {
                            if (!IsDirectMeshBindingCurrent(
                                    worlds, world, bindingValid))
                            {
                                return JobApplyValidation::StaleWorld;
                            }
                            const ECS::Scene::Registry* const scene =
                                ResolveDirectMeshScene(worlds, world);
                            return scene != nullptr && scene->IsValid(entity)
                                ? JobApplyValidation::Current
                                : JobApplyValidation::MissingTarget;
                        },
                    .PublishCompletion =
                        [
                            &textureBake,
                            entity,
                            world,
                            atlasWidth,
                            atlasHeight,
                            sourcePath
                        ](
                            KernelEventBus&,
                            const JobResultEnvelope& envelope) -> bool
                        {
                            const auto* const done =
                                envelope.TryGet<
                                    DirectMeshNormalBakeRetryDone>();
                            if (done == nullptr || done->Entity != entity)
                                return false;

                            const PropertyTextureBakeResult result =
                                textureBake.Bake(
                                    BuildDirectMeshNormalBakeRequest(
                                        entity,
                                        world,
                                        atlasWidth,
                                        atlasHeight));
                            if (!result.Succeeded())
                            {
                                Core::Log::Warn(
                                    "[Runtime] Deferred direct mesh normal texture bake failed: path='{}' status={} diagnostic='{}'",
                                    sourcePath,
                                    DebugNameForPropertyTextureBakeStatus(
                                        result.Status),
                                    result.Diagnostic);
                            }
                            return true;
                        },
                });

            if (!handle.IsValid())
            {
                Core::Log::Warn(
                    "[Runtime] Direct mesh normal texture bake retry submission failed: path='{}'",
                    sourcePath);
            }
        }

        void RequestDirectMeshNormalBake(JobService* jobs, WorldRegistry* worlds,
            WorldHandle world, const std::function<bool()>& bindingValid,
            ECS::Scene::Registry& scene, TextureBakeService* textureBake,
            ECS::EntityHandle entity, const std::string& path,
            std::uint32_t width = 0u, std::uint32_t height = 0u)
        {
            if (textureBake == nullptr) return;
            ConfigureDirectMeshNormalPresentationTarget(scene, entity, "generated-normal");
            const auto result = textureBake->Bake(
                BuildDirectMeshNormalBakeRequest(entity, world, width, height));
            if (result.Status == PropertyTextureBakeStatus::NonOperationalBackend)
                DeferDirectMeshNormalBakeUntilOperational(*jobs, worlds, world,
                    bindingValid, *textureBake, entity, width, height, path);
            else if (!result.Succeeded())
                Core::Log::Warn(
                    "[Runtime] Direct mesh normal texture bake request failed: path='{}' status={} diagnostic='{}'",
                    path, DebugNameForPropertyTextureBakeStatus(result.Status), result.Diagnostic);
        }

        void QueueDirectMeshPostProcess(
            JobService* jobs,
            WorldRegistry* worlds,
            const WorldHandle world,
            std::function<bool()> bindingValid,
            ECS::Scene::Registry& scene,
            TextureBakeService* textureBake,
            std::string meshPath,
            Geometry::HalfedgeMesh::Mesh mesh,
            std::shared_ptr<const Geometry::MeshIO::MeshIOResult> payload,
            const ECS::EntityHandle entity)
        {
            if (jobs == nullptr ||
                !IsDirectMeshBindingCurrent(
                    worlds, world, bindingValid) ||
                worlds->Get(world) != &scene ||
                entity == ECS::InvalidEntityHandle ||
                !scene.IsValid(entity))
            {
                return;
            }

            // Presence is authoritative: importing must never silently regenerate
            // authored coordinates, including tiled or quality-rejected UVs.
            if (mesh.VertexProperties().Exists("v:texcoord") ||
                mesh.HalfedgeProperties().Exists("h:texcoord"))
            {
                RequestDirectMeshNormalBake(jobs, worlds, world, bindingValid,
                    scene, textureBake, entity, meshPath);
                return;
            }

            auto state = std::make_shared<DirectMeshPostProcessState>();
            state->Path = std::move(meshPath);
            state->Mesh = std::move(mesh);
            state->Payload = std::move(payload);
            state->Entity = entity;
            state->Worlds = worlds;
            state->World = world;
            state->BindingValid = std::move(bindingValid);

            auto& raw = scene.Raw();
            const std::optional<std::uint64_t> submittedSignature =
                CaptureDirectMeshGenerationSignature(raw, entity);
            if (!submittedSignature.has_value())
            {
                raw.emplace_or_replace<
                    AssetImportMeshEnrichmentState>(
                    entity,
                    AssetImportMeshEnrichmentState{
                        .Status = JobState::Rejected,
                        .Diagnostic =
                            "Direct mesh enrichment was rejected because the "
                            "published mesh source could not be signed.",
                    });
                Core::Log::Warn(
                    "[Runtime] Direct mesh post-process signature capture "
                    "failed: path='{}'",
                    state->Path);
                return;
            }

            state->SubmittedSignature = *submittedSignature;
            state->SubmittedBindingSignature = CaptureDirectMeshBindingSignature(raw, entity);
            raw.emplace_or_replace<AssetImportMeshEnrichmentState>(
                entity,
                AssetImportMeshEnrichmentState{
                    .Status = JobState::Queued,
                    .Diagnostic =
                        "Direct mesh enrichment is pending; geometry actions "
                        "remain available when their inputs are ready.",
                });

            const JobToken handle = jobs->Submit(
                JobDesc{
                    .DebugName = "Runtime.DirectMeshPostProcess." +
                        FileNameFromPath(state->Path),
                    .Scope = world,
                    .Priority = Core::Dag::TaskPriority::Low,
                    .Kind = RuntimeTaskKinds::AssetDecode,
                    .EstimatedCost = 8u,
                    .CancellationGeneration = jobs->WorldGeneration(world),
                    .Work =
                        [state](const JobCancellation&)
                        {
                            auto materialized = GenerateRuntimeMeshTexcoords(std::move(state->Mesh), *state->Payload);
                            if (materialized.has_value())
                            {
                                state->Materialized = std::move(*materialized);
                                state->Error = Core::ErrorCode::Success;
                            }
                            else
                            {
                                state->Error = materialized.error();
                            }

                            return JobResultEnvelope::Make<
                                DirectMeshPostProcessDone>(
                                DirectMeshPostProcessDone{
                                    .Entity = state->Entity,
                                });
                        },
                    .ValidateBeforeApply =
                        [state]
                        {
                            if (!IsDirectMeshBindingCurrent(
                                    state->Worlds,
                                    state->World,
                                    state->BindingValid))
                            {
                                return JobApplyValidation::StaleWorld;
                            }

                            ECS::Scene::Registry* const scene =
                                ResolveDirectMeshScene(
                                    state->Worlds,
                                    state->World);
                            if (scene == nullptr ||
                                !scene->IsValid(state->Entity))
                            {
                                return JobApplyValidation::MissingTarget;
                            }

                            const auto* enrichment = scene->Raw().try_get<
                                AssetImportMeshEnrichmentState>(
                                state->Entity);
                            if (enrichment == nullptr ||
                                enrichment->Job != state->Job)
                            {
                                return JobApplyValidation::StaleGeneration;
                            }

                            if (state->Error != Core::ErrorCode::Success)
                                return JobApplyValidation::Current;

                            const std::optional<std::uint64_t>
                                currentSignature =
                                    CaptureDirectMeshGenerationSignature(
                                        scene->Raw(),
                                        state->Entity);
                            if (!currentSignature.has_value())
                                return JobApplyValidation::StaleGeneration;
                            if (*currentSignature != state->SubmittedSignature)
                            {
                                // EnTT relocation and mutable reads may advance revisions
                                // without changing geometry. Only that uncommon case needs
                                // a content comparison; UV apply never replaces other attributes.
                                if (CaptureDirectMeshBindingSignature(scene->Raw(), state->Entity) !=
                                        state->SubmittedBindingSignature || !state->Materialized ||
                                    !MatchesDirectMeshGeometry(scene->Raw(), state->Entity, state->Materialized->Mesh))
                                    return JobApplyValidation::StaleGeneration;
                            }
                            return JobApplyValidation::Current;
                        },
                    .PublishCompletion =
                        [
                            state,
                            jobs,
                            textureBake
                        ](
                                KernelEventBus&,
                                const JobResultEnvelope& envelope) -> bool
                        {
                            const DirectMeshPostProcessDone* const done =
                                envelope.TryGet<DirectMeshPostProcessDone>();
                            if (done == nullptr ||
                                done->Entity != state->Entity)
                            {
                                return false;
                            }

                            ECS::Scene::Registry* const scene =
                                ResolveDirectMeshScene(
                                    state->Worlds,
                                    state->World);
                            if (scene == nullptr)
                                return false;

                            if (state->Error != Core::ErrorCode::Success ||
                                !state->Materialized.has_value())
                            {
                                if (scene->IsValid(state->Entity))
                                {
                                    UpdateDirectMeshEnrichmentState(
                                        scene->Raw(),
                                        state->Entity,
                                        state->Job,
                                        JobState::Dropped,
                                        "Direct mesh enrichment failed while "
                                        "materializing the imported mesh.");
                                }
                                Core::Log::Warn(
                                    "[Runtime] Deferred mesh post-process failed: path='{}' error={}",
                                    state->Path,
                                    Core::Error::ToString(state->Error));
                                return true;
                            }

                            if (!scene->IsValid(state->Entity))
                            {
                                return true;
                            }

                            auto& raw = scene->Raw();
                            const RuntimeMeshMaterializationDiagnostics&
                                meshDiagnostics =
                                    state->Materialized->Diagnostics;
                            auto& mesh = state->Materialized->Mesh;
                            namespace GS = ECS::Components::GeometrySources;
                            // Publish only UVs. Topology, normals, and unrelated current
                            // properties retain their storage and revisions.
                            if (auto uv = mesh.HalfedgeProperties().Get<glm::vec2>("h:texcoord"))
                                raw.get<GS::Halfedges>(state->Entity).Properties
                                    .GetOrAdd<glm::vec2>("h:texcoord").Vector() = std::move(uv.Vector());
                            else if (auto uv = mesh.VertexProperties().Get<glm::vec2>("v:texcoord"))
                                raw.get<GS::Vertices>(state->Entity).Properties
                                    .GetOrAdd<glm::vec2>("v:texcoord").Vector() = std::move(uv.Vector());
                            // Only a generated atlas has an extent; authored
                            // UVs clear any record and use the bake default.
                            const bool generatedAtlas =
                                meshDiagnostics.TexcoordProvenance ==
                                RuntimeMeshResolvedUvProvenance::GeneratedAtlas;
                            (void)PublishMeshUvAtlasExtent(
                                raw,
                                state->Entity,
                                generatedAtlas ? meshDiagnostics.AtlasWidth : 0u,
                                generatedAtlas ? meshDiagnostics.AtlasHeight : 0u);
                            MarkMeshGeometryDirty(raw, state->Entity);
                            UpdateDirectMeshEnrichmentState(
                                raw,
                                state->Entity,
                                state->Job,
                                JobState::Published,
                                DescribeDirectMeshEnrichment(meshDiagnostics));
                            // The import preserves source topology, so any
                            // vertex duplication happens at GPU
                            // upload. Report it rather than computing it and
                            // dropping it on the floor.
                            Core::Log::Info(
                                "[Runtime] Direct mesh enrichment applied: path='{}' vertices={} (source {}) faces={} (source polygons {}) uv-domain={} uv-provenance={} atlas-backend='{}' gpu-split-vertices={}",
                                state->Path,
                                meshDiagnostics.ResolvedVertexCount,
                                meshDiagnostics.SourceVertexCount,
                                meshDiagnostics.ResolvedFaceCount,
                                meshDiagnostics.SourceFaceCount,
                                meshDiagnostics.TexcoordsOnCornerDomain
                                    ? "corner"
                                    : (meshDiagnostics.ResolvedTexcoordsValid
                                           ? "vertex"
                                           : "none"),
                                DescribeUvProvenance(
                                    meshDiagnostics.TexcoordProvenance),
                                meshDiagnostics.AtlasBackendName,
                                meshDiagnostics.GpuSplitVertexCount);

                            if (meshDiagnostics.ResolvedTexcoordsValid)
                                RequestDirectMeshNormalBake(jobs, state->Worlds, state->World,
                                    state->BindingValid, *scene, textureBake, state->Entity,
                                    state->Path, meshDiagnostics.AtlasWidth, meshDiagnostics.AtlasHeight);
                            return true;
                        },
                    .FinalizeUnpublishedOnMainThread =
                        [state, jobs]
                        {
                            ECS::Scene::Registry* const scene =
                                ResolveDirectMeshScene(
                                    state->Worlds,
                                    state->World);
                            if (scene == nullptr ||
                                !scene->IsValid(state->Entity))
                            {
                                return;
                            }

                            const JobState status = jobs->GetState(state->Job);
                            std::string diagnostic;
                            if (status == JobState::StaleDiscarded)
                            {
                                diagnostic =
                                    "Direct mesh enrichment was discarded "
                                    "because the entity, geometry, source "
                                    "properties, bindings, or world changed "
                                    "after submission.";
                            }
                            else if (status == JobState::Cancelled)
                            {
                                diagnostic =
                                    "Direct mesh enrichment was cancelled "
                                    "before it could be applied.";
                            }
                            else
                            {
                                diagnostic =
                                    "Direct mesh enrichment ended without "
                                    "applying its result.";
                            }
                            UpdateDirectMeshEnrichmentState(
                                scene->Raw(),
                                state->Entity,
                                state->Job,
                                status,
                                std::move(diagnostic));
                        },
                });

            state->Job = handle;

            if (handle.IsValid())
            {
                auto* enrichment = raw.try_get<
                    AssetImportMeshEnrichmentState>(entity);
                if (enrichment != nullptr && !enrichment->Job.IsValid())
                    enrichment->Job = handle;
                Core::Log::Info(
                    "[Runtime] Queued direct mesh post-process: path='{}'",
                    state->Path);
            }
            else
            {
                auto* enrichment = raw.try_get<
                    AssetImportMeshEnrichmentState>(entity);
                if (enrichment != nullptr && !enrichment->Job.IsValid())
                {
                    enrichment->Status = JobState::Rejected;
                    enrichment->Diagnostic =
                        "Direct mesh enrichment could not be submitted to the "
                        "runtime job lane.";
                }
                Core::Log::Warn(
                    "[Runtime] Direct mesh post-process queue submission failed: path='{}'",
                    state->Path);
            }
        }
    }

    Core::Result ApplyAssetImportAuthoringRecipe(
        const Assets::AssetPayloadKind payloadKind,
        const bool authorRenderableComponents,
        const bool authorSelectableIdentity,
        const ECS::EntityHandle entity,
        ECS::Scene::Registry& scene)
    {
        if (entity == ECS::InvalidEntityHandle || !scene.IsValid(entity))
            return Core::Err(Core::ErrorCode::InvalidState);

        auto& raw = scene.Raw();
        if (authorSelectableIdentity)
        {
            raw.emplace_or_replace<
                ECS::Components::Selection::SelectableTag>(entity);
        }
        if (!authorRenderableComponents)
            return Core::Ok();

        switch (payloadKind)
        {
        case Assets::AssetPayloadKind::Mesh:
            raw.emplace_or_replace<Graphics::Components::RenderSurface>(
                entity,
                Graphics::Components::RenderSurface{
                    .Domain = Graphics::Components::RenderSurface::
                        SourceDomain::Vertex,
                });
            raw.emplace_or_replace<Graphics::Components::VisualizationConfig>(
                entity,
                ImportedMeshVisualization());
            break;
        case Assets::AssetPayloadKind::Graph:
            raw.emplace_or_replace<Graphics::Components::RenderEdges>(
                entity,
                Graphics::Components::RenderEdges{
                    .Domain = Graphics::Components::RenderEdges::
                        SourceDomain::Vertex,
                });
            raw.emplace_or_replace<Graphics::Components::RenderPoints>(
                entity,
                Graphics::Components::RenderPoints{});
            raw.emplace_or_replace<Graphics::Components::VisualizationConfig>(
                entity,
                ImportedGeometryVisualization());
            break;
        case Assets::AssetPayloadKind::PointCloud:
            raw.emplace_or_replace<Graphics::Components::RenderPoints>(
                entity,
                Graphics::Components::RenderPoints{});
            raw.emplace_or_replace<Graphics::Components::VisualizationConfig>(
                entity,
                ImportedGeometryVisualization());
            break;
        case Assets::AssetPayloadKind::Unknown:
        case Assets::AssetPayloadKind::ModelScene:
        case Assets::AssetPayloadKind::Texture2D:
            break;
        }
        return Core::Ok();
    }

    Core::Result ApplyAssetImportCompletionRecipe(
        const bool selectFirstCreatedEntity,
        const bool focusCameraOnCreatedGeometry,
        const std::span<const ECS::EntityHandle> createdEntities,
        const std::optional<CameraFocusTarget>& focusTarget,
        ECS::Scene::Registry& scene,
        SelectionController* const selection,
        const Core::Config::EngineConfig* const config,
        CameraControllerRegistry* const cameraControllers)
    {
        if (focusCameraOnCreatedGeometry && cameraControllers != nullptr &&
            config != nullptr)
        {
            FocusMainCameraOnImportTarget(
                *cameraControllers,
                config->Camera.Controller,
                config->Camera.Enabled,
                focusTarget);
        }

        if (!selectFirstCreatedEntity)
            return Core::Ok();
        for (const ECS::EntityHandle entity : createdEntities)
        {
            if (!scene.IsValid(entity))
                continue;
            if (selection != nullptr)
                (void)selection->SetSelectedEntity(scene, entity);
            break;
        }
        return Core::Ok();
    }

    void QueueAssetImportDirectMeshPostprocess(
        JobService* const jobs,
        WorldRegistry* const worlds,
        const WorldHandle world,
        std::function<bool()> bindingValid,
        ECS::Scene::Registry& scene,
        TextureBakeService* const textureBake,
        std::string path,
        Geometry::HalfedgeMesh::Mesh mesh,
        std::shared_ptr<const Geometry::MeshIO::MeshIOResult> payload,
        const ECS::EntityHandle entity)
    {
        if (jobs == nullptr || worlds == nullptr || !bindingValid)
            return;
        QueueDirectMeshPostProcess(
            jobs,
            worlds,
            world,
            std::move(bindingValid),
            scene,
            textureBake,
            std::move(path),
            std::move(mesh),
            std::move(payload),
            entity);
    }
}
