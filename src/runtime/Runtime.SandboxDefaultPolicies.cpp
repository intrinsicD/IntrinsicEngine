module;

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.SandboxEditorFacades;

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
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetMeshNormals;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;
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
            Geometry::MeshIO::MeshIOResult Payload{};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::optional<RuntimeMeshMaterializationResult> Materialized{};
        };

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
            const WorldHandle world)
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
                .Width = 64u,
                .Height = 64u,
                .PaddingTexels = 4u,
                .OutputName = "generated-normal",
            };
        }

        void MarkMeshGeometryDirty(entt::registry& raw,
                                   const ECS::EntityHandle entity)
        {
            ECS::Components::DirtyTags::MarkGpuDirty(raw, entity);
            ECS::Components::DirtyTags::MarkVertexPositionsDirty(raw, entity);
            ECS::Components::DirtyTags::MarkFaceTopologyDirty(raw, entity);
            ECS::Components::DirtyTags::MarkEdgeTopologyDirty(raw, entity);
        }

        [[nodiscard]] std::vector<glm::vec3> SnapshotCurrentMeshVertexNormals(
            const entt::registry& raw,
            const ECS::EntityHandle entity)
        {
            namespace GS = ECS::Components::GeometrySources;

            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            if (!view.Valid() || view.ActiveDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr)
            {
                return {};
            }

            const auto normals =
                view.VertexSource->Properties.Get<glm::vec3>(
                    GS::PropertyNames::kNormal);
            if (!normals)
            {
                return {};
            }

            return std::vector<glm::vec3>(
                normals.Vector().begin(),
                normals.Vector().end());
        }

        [[nodiscard]] bool RestoreMeshVertexNormalsIfCompatible(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const std::vector<glm::vec3>& normals)
        {
            if (normals.empty())
            {
                return false;
            }

            namespace GS = ECS::Components::GeometrySources;
            auto* vertices = raw.try_get<GS::Vertices>(entity);
            if (vertices == nullptr)
            {
                return false;
            }

            auto target =
                vertices->Properties.Get<glm::vec3>(GS::PropertyNames::kNormal);
            if (!target || target.Vector().size() != normals.size())
            {
                return false;
            }

            target.Vector() = normals;
            return true;
        }

        // The deferred post-process only carries its outcome forward in the
        // shared state record its callbacks capture; the envelope proves the
        // worker body ran, since an empty envelope is a dropped job.
        struct DirectMeshPostProcessDone
        {
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        };

        void QueueDirectMeshPostProcess(
            JobService* jobs,
            const WorldHandle world,
            ECS::Scene::Registry& scene,
            TextureBakeService* textureBake,
            std::string meshPath,
            const Geometry::MeshIO::MeshIOResult& meshPayload,
            const ECS::EntityHandle entity)
        {
            if (jobs == nullptr ||
                entity == ECS::InvalidEntityHandle)
            {
                return;
            }

            auto state = std::make_shared<DirectMeshPostProcessState>();
            state->Path = std::move(meshPath);
            state->Payload = meshPayload;
            state->Entity = entity;

            const JobToken handle = jobs->Submit(
                JobDesc{
                    .DebugName = "Runtime.DirectMeshPostProcess." +
                        FileNameFromPath(state->Path),
                    .Scope = world,
                    .Priority = Core::Dag::TaskPriority::Low,
                    .Kind = RuntimeTaskKinds::AssetDecode,
                    .EstimatedCost = 8u,
                    .Work =
                        [state](const JobCancellation&)
                        {
                            auto materialized =
                                BuildRuntimeHalfedgeMeshMaterialization(
                                    state->Payload,
                                    RuntimeMeshMaterializationOptions{
                                        .AllowDisconnectedRenderableFallback =
                                            true,
                                    });
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
                    .PublishCompletion =
                        [
                            state,
                            &scene,
                            textureBake,
                            world
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

                            if (state->Error != Core::ErrorCode::Success ||
                                !state->Materialized.has_value())
                            {
                                Core::Log::Warn(
                                    "[Runtime] Deferred mesh post-process failed: path='{}' error={}",
                                    state->Path,
                                    Core::Error::ToString(state->Error));
                                return true;
                            }

                            if (!scene.IsValid(state->Entity))
                            {
                                return true;
                            }

                            auto& raw = scene.Raw();
                            const std::vector<glm::vec3> currentNormals =
                                SnapshotCurrentMeshVertexNormals(
                                    raw,
                                    state->Entity);
                            Geometry::HalfedgeMesh::Mesh mesh =
                                std::move(state->Materialized->Mesh);
                            ECS::Components::GeometrySources::PopulateFromMesh(
                                raw,
                                state->Entity,
                                mesh);
                            (void)RestoreMeshVertexNormalsIfCompatible(
                                raw,
                                state->Entity,
                                currentNormals);
                            MarkMeshGeometryDirty(raw, state->Entity);

                            if (textureBake != nullptr)
                            {
                                PropertyTextureBakeResult result =
                                    textureBake->Bake(
                                        BuildDirectMeshNormalBakeRequest(
                                            state->Entity,
                                            world));
                                if (result.Succeeded())
                                {
                                    const TextureBakeMutationResult consumers =
                                        textureBake->SetConsumers(
                                            TextureBakeConsumerUpdateRequest{
                                                .StableEntityId =
                                                    StableEntityLookup::
                                                        ToRenderId(
                                                            state->Entity),
                                                .OutputName =
                                                    result.OutputName,
                                                .Consumers = {
                                                    TextureBakeConsumerBinding{
                                                        .Semantic =
                                                            GeometryPresentationSlotSemantic::
                                                                Normal,
                                                        .NormalSpace =
                                                            PropertyTextureNormalSpace::
                                                                Object,
                                                    },
                                                },
                                            });
                                    if (!consumers.Succeeded())
                                    {
                                        Core::Log::Warn(
                                            "[Runtime] Direct mesh normal texture consumer binding failed: path='{}' diagnostic='{}'",
                                            state->Path,
                                            consumers.Diagnostic);
                                    }
                                }
                                else if (result.Status !=
                                         PropertyTextureBakeStatus::
                                             NonOperationalBackend)
                                {
                                    Core::Log::Warn(
                                        "[Runtime] Direct mesh normal texture bake request failed: path='{}' status={} diagnostic='{}'",
                                        state->Path,
                                        DebugNameForPropertyTextureBakeStatus(
                                            result.Status),
                                        result.Diagnostic);
                                }
                            }
                            return true;
                        },
                });

            if (handle.IsValid())
            {
                Core::Log::Info(
                    "[Runtime] Queued direct mesh post-process: path='{}'",
                    state->Path);
            }
            else
            {
                Core::Log::Warn(
                    "[Runtime] Direct mesh post-process queue submission failed: path='{}'",
                    state->Path);
            }
        }
    }

    std::array<RuntimeImportEntityAuthoringPolicyDesc, 3>
    MakeSandboxDefaultImportAuthoringPolicies()
    {
        return {
            RuntimeImportEntityAuthoringPolicyDesc{
                .DebugName = "Sandbox.DefaultMeshImportAuthoring",
                .PayloadKind = Assets::AssetPayloadKind::Mesh,
                .Apply =
                    [](const RuntimeImportEntityAuthoringPolicyContext& context,
                       RuntimeImportEntityAuthoringPolicyServices& services)
                    {
                        if (services.Scene == nullptr ||
                            context.Entity == ECS::InvalidEntityHandle ||
                            !services.Scene->IsValid(context.Entity))
                        {
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }

                        auto& raw = services.Scene->Raw();
                        raw.emplace_or_replace<
                            ECS::Components::Selection::SelectableTag>(
                            context.Entity);
                        raw.emplace_or_replace<
                            Graphics::Components::RenderSurface>(
                            context.Entity,
                            Graphics::Components::RenderSurface{
                                .Domain = Graphics::Components::RenderSurface::
                                    SourceDomain::Vertex,
                            });
                        raw.emplace_or_replace<
                            Graphics::Components::VisualizationConfig>(
                            context.Entity,
                            ImportedMeshVisualization());
                        return Core::Ok();
                    },
            },

            RuntimeImportEntityAuthoringPolicyDesc{
                .DebugName = "Sandbox.DefaultGraphImportAuthoring",
                .PayloadKind = Assets::AssetPayloadKind::Graph,
                .Apply =
                    [](const RuntimeImportEntityAuthoringPolicyContext& context,
                       RuntimeImportEntityAuthoringPolicyServices& services)
                    {
                        if (services.Scene == nullptr ||
                            context.Entity == ECS::InvalidEntityHandle ||
                            !services.Scene->IsValid(context.Entity))
                        {
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }

                        auto& raw = services.Scene->Raw();
                        raw.emplace_or_replace<
                            ECS::Components::Selection::SelectableTag>(
                            context.Entity);
                        raw.emplace_or_replace<
                            Graphics::Components::RenderEdges>(
                            context.Entity,
                            Graphics::Components::RenderEdges{
                                .Domain = Graphics::Components::RenderEdges::
                                    SourceDomain::Vertex,
                            });
                        raw.emplace_or_replace<
                            Graphics::Components::RenderPoints>(
                            context.Entity,
                            Graphics::Components::RenderPoints{});
                        raw.emplace_or_replace<
                            Graphics::Components::VisualizationConfig>(
                            context.Entity,
                            ImportedGeometryVisualization());
                        return Core::Ok();
                    },
            },

            RuntimeImportEntityAuthoringPolicyDesc{
                .DebugName = "Sandbox.DefaultPointCloudImportAuthoring",
                .PayloadKind = Assets::AssetPayloadKind::PointCloud,
                .Apply =
                    [](const RuntimeImportEntityAuthoringPolicyContext& context,
                       RuntimeImportEntityAuthoringPolicyServices& services)
                    {
                        if (services.Scene == nullptr ||
                            context.Entity == ECS::InvalidEntityHandle ||
                            !services.Scene->IsValid(context.Entity))
                        {
                            return Core::Err(Core::ErrorCode::InvalidState);
                        }

                        auto& raw = services.Scene->Raw();
                        raw.emplace_or_replace<
                            ECS::Components::Selection::SelectableTag>(
                            context.Entity);
                        raw.emplace_or_replace<
                            Graphics::Components::RenderPoints>(
                            context.Entity,
                            Graphics::Components::RenderPoints{});
                        raw.emplace_or_replace<
                            Graphics::Components::VisualizationConfig>(
                            context.Entity,
                            ImportedGeometryVisualization());
                        return Core::Ok();
                    },
            },
        };
    }

    RuntimeImportCompletedHandlerDesc
    MakeSandboxDefaultImportCompletedHandler(
        CameraControllerRegistry* const cameraControllers)
    {
        return RuntimeImportCompletedHandlerDesc{
            .DebugName = "Sandbox.DefaultImportCompletedUx",
            .PayloadKind = Assets::AssetPayloadKind::Unknown,
            .Handle =
                [cameraControllers](
                    const RuntimeImportCompletedContext& context,
                    RuntimeImportCompletedServices& services)
                {
                    if (services.Scene == nullptr)
                    {
                        return Core::Err(
                            Core::ErrorCode::InvalidState);
                    }

                    if (cameraControllers != nullptr &&
                        services.Config != nullptr)
                    {
                        FocusMainCameraOnImportTarget(
                            *cameraControllers,
                            services.Config->Camera.Controller,
                            services.Config->Camera.Enabled,
                            context.FocusTarget);
                    }

                    for (const ECS::EntityHandle entity :
                         context.CreatedEntities)
                    {
                        if (!services.Scene->IsValid(entity))
                            continue;
                        if (services.Selection != nullptr)
                        {
                            (void)services.Selection->SetSelectedEntity(
                                *services.Scene,
                                entity);
                        }
                        break;
                    }
                    return Core::Ok();
                },
        };
    }

    RuntimePostImportProcessorDesc
    MakeSandboxDefaultDirectMeshPostProcessor()
    {
        return RuntimePostImportProcessorDesc{
            .DebugName = "Sandbox.DirectMeshGeneratedNormal",
            .PayloadKind = Assets::AssetPayloadKind::Mesh,
            .Process =
                [](const RuntimePostImportProcessorContext& context,
                   RuntimePostImportProcessorServices& services)
                {
                    if (context.MeshPayload == nullptr)
                        return Core::Ok();
                    if (services.Jobs == nullptr ||
                        services.Scene == nullptr)
                    {
                        return Core::Err(
                            Core::ErrorCode::InvalidState);
                    }

                    QueueDirectMeshPostProcess(
                        services.Jobs,
                        services.World,
                        *services.Scene,
                        services.TextureBake,
                        std::string{context.Path},
                        *context.MeshPayload,
                        context.Entity);
                    return Core::Ok();
                },
        };
    }

    RuntimeInputActionDesc
    MakeSandboxDefaultFocusInputAction(
        CameraControllerRegistry& cameraControllers,
        SelectionController& selection)
    {
        return RuntimeInputActionDesc{
            .DebugName = "Sandbox.DefaultFocusCameraOnSelection",
            .Binding =
                RuntimeInputActionBinding{
                    .KeyCode = Platform::Input::Key::F,
                    .Trigger = RuntimeInputActionTrigger::KeyJustPressed,
                    .SuppressWhenImGuiCapturesKeyboard = true,
                },
            .Execute =
                [camera = &cameraControllers, selection = &selection](
                    const RuntimeInputActionContext& context,
                    RuntimeInputActionServices& services)
                {
                    if (services.Scene == nullptr ||
                        services.RenderInput == nullptr ||
                        services.Config == nullptr)
                    {
                        return Core::Err(Core::ErrorCode::InvalidState);
                    }

                    if (!services.Config->Camera.Enabled)
                        return Core::Ok();

                    if (!FocusCameraOnSelection(
                            *camera,
                            *selection,
                            *services.Scene,
                            CameraControllerSlot::Main))
                    {
                        return Core::Ok();
                    }

                    if (ICameraController* focused =
                            camera->ResolveOrNull(
                                CameraControllerSlot::Main))
                    {
                        services.RenderInput->Camera =
                            focused->GetView(context.Viewport);
                        services.RenderInput->Camera
                            .ExplicitCameraTransition =
                            camera->ConsumeCameraTransition(
                                CameraControllerSlot::Main);
                    }
                    return Core::Ok();
                },
        };
    }
}
