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

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Service;
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
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Input;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetMeshNormals;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        struct DirectMeshGeneratedTextureResult
        {
            Assets::AssetId NormalTexture{};
            std::uint64_t GeneratedTextureAssetsCreated{0u};
            std::uint64_t GeneratedTextureUploadRequests{0u};
        };

        struct DirectMeshPostProcessState
        {
            std::string Path{};
            Geometry::MeshIO::MeshIOResult Payload{};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::optional<RuntimeMeshMaterializationResult> Materialized{};
            std::optional<Assets::AssetTexture2DPayload> GeneratedNormalTexture{};
        };

        [[nodiscard]] bool IsTextureUploadDeferral(
            const Core::ErrorCode error) noexcept
        {
            return error == Core::ErrorCode::DeviceNotOperational ||
                   error == Core::ErrorCode::ResourceBusy;
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

        [[nodiscard]] std::optional<Assets::AssetTexture2DPayload>
        BakeDirectMeshGeneratedNormalTexturePayload(
            const std::string_view meshPath,
            const Geometry::HalfedgeMesh::Mesh& mesh,
            const bool hasResolvedTexcoords)
        {
            if (!hasResolvedTexcoords)
            {
                return std::nullopt;
            }

            MeshAttributeTextureBakeOptions options{};
            options.SourcePropertyName = "v:normal";
            options.Width = 64u;
            options.Height = 64u;
            options.DebugName = "generated-direct-mesh-normal-v:normal";

            MeshAttributeTextureBakeResult bake =
                BakeMeshVertexNormalTexture(mesh, options);
            if (bake.Status != MeshAttributeTextureBakeStatus::Success)
            {
                return std::nullopt;
            }

            bake.Payload.Metadata.SourcePath = std::string{meshPath};
            return std::move(bake.Payload);
        }

        [[nodiscard]] Core::Expected<DirectMeshGeneratedTextureResult>
        RegisterDirectMeshGeneratedNormalTexture(
            Assets::AssetService& assetService,
            Graphics::GpuAssetCache& gpuAssetCache,
            RenderExtractionCache& extraction,
            const std::string_view meshPath,
            const ECS::EntityHandle entity,
            const Assets::AssetTexture2DPayload& payload)
        {
            DirectMeshGeneratedTextureResult result{};
            const std::string generatedPath = BuildGeneratedTextureAssetPath(
                meshPath,
                0u,
                "normal",
                "v:normal");
            auto texture = LoadGeneratedTextureAsset(
                assetService,
                generatedPath,
                payload);
            if (!texture.has_value())
            {
                return Core::Err<DirectMeshGeneratedTextureResult>(
                    texture.error());
            }

            result.NormalTexture = *texture;
            result.GeneratedTextureAssetsCreated = 1u;

            auto upload =
                RequestTextureAssetUpload(assetService, gpuAssetCache, *texture);
            if (upload.has_value())
            {
                result.GeneratedTextureUploadRequests = 1u;
            }
            else if (!IsTextureUploadDeferral(upload.error()))
            {
                return Core::Err<DirectMeshGeneratedTextureResult>(
                    upload.error());
            }

            extraction.SetMaterialTextureAssetBindings(
                StableEntityLookup::ToRenderId(entity),
                Graphics::MaterialTextureAssetBindings{
                    .Albedo = {},
                    .Normal = *texture,
                    .MetallicRoughness = {},
                    .Emissive = {},
                    .NormalSpace =
                        Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal,
                });

            return result;
        }

        [[nodiscard]] RuntimeObjectSpaceNormalBakeRequestBuildResult
        BuildDirectMeshObjectSpaceNormalBakeRequest(
            const ECS::Scene::Registry& scene,
            const ECS::EntityHandle entity,
            const WorldHandle world,
            const std::uint64_t bindingEpoch)
        {
            namespace GS = ECS::Components::GeometrySources;

            const GS::ConstSourceView view = GS::BuildConstView(scene.Raw(), entity);
            const std::uint32_t stableId = StableEntityLookup::ToRenderId(entity);
            Graphics::ObjectSpaceNormalTextureBakeOptions bakeOptions{};
            bakeOptions.Width = 64u;
            bakeOptions.Height = 64u;
            bakeOptions.Space = Graphics::NormalTextureSpace::ObjectSpaceNormal;

            return BuildRuntimeObjectSpaceNormalBakeRequest(
                view,
                RuntimeObjectSpaceNormalBakeTarget{
                    .World = world,
                    .BindingEpoch = bindingEpoch,
                    .Entity = entity,
                    .StableEntityId = stableId,
                    .PresentationKey = {},
                    .Semantic = ProgressiveSlotSemantic::Normal,
                    .ExpectedProgressiveBindingGeneration = 0u,
                },
                bakeOptions);
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

        void QueueDirectMeshPostProcess(
            StreamingExecutor* streamingExecutor,
            const WorldHandle world,
            Assets::AssetService& assetService,
            Graphics::GpuAssetCache& gpuAssetCache,
            RenderExtractionCache& extraction,
            ECS::Scene::Registry& scene,
            RuntimeObjectSpaceNormalBakeQueue* objectSpaceNormalBakeQueue,
            const std::uint64_t objectSpaceNormalBakeBindingEpoch,
            const RHI::IDevice* objectSpaceNormalBakeDevice,
            std::string meshPath,
            const Geometry::MeshIO::MeshIOResult& meshPayload,
            const ECS::EntityHandle entity)
        {
            if (streamingExecutor == nullptr ||
                entity == ECS::InvalidEntityHandle)
            {
                return;
            }

            auto state = std::make_shared<DirectMeshPostProcessState>();
            state->Path = std::move(meshPath);
            state->Payload = meshPayload;
            state->Entity = entity;

            const StreamingTaskHandle handle = streamingExecutor->Submit(
                StreamingTaskDesc{
                    .Name = "Runtime.DirectMeshPostProcess." +
                        FileNameFromPath(state->Path),
                    .Kind = RuntimeTaskKinds::AssetDecode,
                    .Priority = Core::Dag::TaskPriority::Low,
                    .EstimatedCost = 8u,
                    .Scope = world,
                    .Execute =
                        [
                            state,
                            useObjectSpaceNormalBakeQueue =
                                objectSpaceNormalBakeQueue != nullptr]() mutable
                            -> StreamingResult
                        {
                            auto materialized =
                                BuildRuntimeHalfedgeMeshMaterialization(
                                    state->Payload,
                                    RuntimeMeshMaterializationOptions{
                                        .AllowDisconnectedRenderableFallback =
                                            true,
                                    });
                            if (!materialized.has_value())
                            {
                                state->Error = materialized.error();
                                return StreamingResult{
                                    StreamingCpuPayloadReady{.PayloadToken = 0u}};
                            }

                            if (!useObjectSpaceNormalBakeQueue)
                            {
                                state->GeneratedNormalTexture =
                                    BakeDirectMeshGeneratedNormalTexturePayload(
                                        state->Path,
                                        materialized->Mesh,
                                        materialized->Diagnostics
                                            .ResolvedTexcoordsValid);
                            }
                            state->Materialized = std::move(*materialized);
                            state->Error = Core::ErrorCode::Success;
                            return StreamingResult{
                                StreamingCpuPayloadReady{.PayloadToken = 0u}};
                        },
                    .ApplyOnMainThread =
                        [
                            state,
                            &assetService,
                            &gpuAssetCache,
                            &extraction,
                            &scene,
                            objectSpaceNormalBakeQueue,
                            world,
                            objectSpaceNormalBakeBindingEpoch,
                            objectSpaceNormalBakeDevice](
                                StreamingResult&& result) mutable
                        {
                            if (!result.has_value() ||
                                state->Error != Core::ErrorCode::Success ||
                                !state->Materialized.has_value())
                            {
                                Core::Log::Warn(
                                    "[Runtime] Deferred mesh post-process failed: path='{}' error={}",
                                    state->Path,
                                    Core::Error::ToString(
                                        result.has_value()
                                            ? state->Error
                                            : result.error()));
                                return;
                            }

                            if (!scene.IsValid(state->Entity))
                            {
                                return;
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

                            if (objectSpaceNormalBakeQueue != nullptr)
                            {
                                RuntimeObjectSpaceNormalBakeRequestBuildResult
                                    request =
                                    BuildDirectMeshObjectSpaceNormalBakeRequest(
                                        scene,
                                        state->Entity,
                                        world,
                                        objectSpaceNormalBakeBindingEpoch);
                                if (!request.Succeeded())
                                {
                                    Core::Log::Warn(
                                        "[Runtime] Direct mesh object-space normal bake request is invalid: path='{}' diagnostic='{}'",
                                        state->Path,
                                        request.Diagnostic);
                                    return;
                                }

                                const RuntimeObjectSpaceNormalBakeResult queued =
                                    objectSpaceNormalBakeQueue->Schedule(
                                        *request.Request,
                                        objectSpaceNormalBakeDevice != nullptr &&
                                            objectSpaceNormalBakeDevice->
                                                IsOperational());
                                if (queued.Status !=
                                        RuntimeObjectSpaceNormalBakeStatus::Queued &&
                                    queued.Status !=
                                        RuntimeObjectSpaceNormalBakeStatus::
                                            NonOperationalBackend)
                                {
                                    Core::Log::Warn(
                                        "[Runtime] Direct mesh object-space normal bake request failed: path='{}' status={} diagnostic='{}'",
                                        state->Path,
                                        DebugNameForRuntimeObjectSpaceNormalBakeStatus(
                                            queued.Status),
                                        queued.Diagnostic);
                                }
                                return;
                            }

                            if (state->GeneratedNormalTexture.has_value())
                            {
                                auto generated =
                                    RegisterDirectMeshGeneratedNormalTexture(
                                        assetService,
                                        gpuAssetCache,
                                        extraction,
                                        state->Path,
                                        state->Entity,
                                        *state->GeneratedNormalTexture);
                                if (!generated.has_value())
                                {
                                    Core::Log::Warn(
                                        "[Runtime] Deferred generated normal texture registration failed: path='{}' error={}",
                                        state->Path,
                                        Core::Error::ToString(generated.error()));
                                }
                            }
                        },
                });

            if (handle.IsValid())
            {
                streamingExecutor->PumpBackground(1u);
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
                    if (services.Streaming == nullptr ||
                        services.AssetService == nullptr ||
                        services.GpuAssetCache == nullptr ||
                        services.RenderExtraction == nullptr ||
                        services.Scene == nullptr)
                    {
                        return Core::Err(
                            Core::ErrorCode::InvalidState);
                    }

                    QueueDirectMeshPostProcess(
                        services.Streaming,
                        services.World,
                        *services.AssetService,
                        *services.GpuAssetCache,
                        *services.RenderExtraction,
                        *services.Scene,
                        services.ObjectSpaceNormalBakeQueue,
                        services.ObjectSpaceNormalBakeBindingEpoch,
                        services.ObjectSpaceNormalBakeDevice,
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
