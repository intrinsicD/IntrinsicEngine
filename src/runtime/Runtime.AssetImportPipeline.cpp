module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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

module Extrinsic.Runtime.AssetImportPipeline;

import Extrinsic.Asset.GeometryPayload;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Runtime.AssetMeshNormals;
import Extrinsic.Runtime.AssetImportPolicies;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.AssetModelTextureIO;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Graph;
import Geometry.Graph.IO;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.PointCloud;
import Geometry.PointCloud.IO;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::uint64_t Delta(
            const std::uint64_t after,
            const std::uint64_t before) noexcept
        {
            return after >= before ? after - before : 0u;
        }

        [[nodiscard]] Core::Result DrainAssetImportEvents(
            Assets::AssetService& service,
            const Assets::AssetId asset)
        {
            return service.CompleteCpuLoadAndFlushEvent(asset);
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

        struct DecodedMeshImport
        {
            std::shared_ptr<const Geometry::MeshIO::MeshIOResult> Payload{};
        };

        struct DecodedGraphImport
        {
            std::shared_ptr<const Geometry::GraphIO::GraphIOResult> Payload{};
        };

        struct DecodedPointCloudImport
        {
            std::shared_ptr<const Geometry::PointCloudIO::PointCloudIOResult> Payload{};
        };

        using DecodedGeometryImportPayload =
            std::variant<DecodedMeshImport, DecodedGraphImport, DecodedPointCloudImport>;

        struct DecodedGeometryImport
        {
            std::string Path{};
            Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
            DecodedGeometryImportPayload Payload{};
        };

        struct QueuedGeometryImportState
        {
            RuntimeAssetIngestHandle IngestHandle{};
            RuntimeAssetImportRequest Request{};
            AssetImportRecipe Recipe{};
            std::shared_ptr<AssetImportStageTrace> StageTrace{};
            std::optional<DecodedGeometryImport> Decoded{};
            Core::ErrorCode Error{Core::ErrorCode::Unknown};
        };

        using DecodedModelTextureImportPayload =
            std::variant<Assets::AssetModelScenePayload,
                         Assets::AssetTexture2DPayload>;

        struct DecodedModelTextureImport
        {
            std::string Path{};
            Assets::AssetPayloadKind PayloadKind{
                Assets::AssetPayloadKind::Unknown};
            DecodedModelTextureImportPayload Payload{};
        };

        struct DroppedModelTextureImportState
        {
            RuntimeAssetIngestHandle IngestHandle{};
            RuntimeAssetImportRequest Request{};
            AssetImportRecipe Recipe{};
            std::shared_ptr<AssetImportStageTrace> StageTrace{};
            std::optional<DecodedModelTextureImport> Decoded{};
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
            std::optional<CameraFocusTarget> FocusTarget{};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        };

        [[nodiscard]] RuntimeAssetIngestRequest MakeRuntimeAssetIngestRequest(
            const RuntimeAssetImportRequest& request,
            const RuntimeAssetIngestSource source,
            const Assets::AssetId existingAsset = {})
        {
            return RuntimeAssetIngestRequest{
                .Source = source,
                .Path = request.Path,
                .PayloadKind = request.PayloadKind,
                .ExistingAsset = existingAsset,
            };
        }

        [[nodiscard]] RuntimeAssetIngestResult ToRuntimeAssetIngestResult(
            const RuntimeAssetImportResult& result) noexcept
        {
            return RuntimeAssetIngestResult{
                .PayloadKind = result.PayloadKind,
                .Asset = result.Asset,
                .PrimitiveEntitiesCreated =
                    static_cast<std::uint32_t>(result.PrimitiveEntitiesCreated),
                .EmbeddedTextureAssetsCreated =
                    static_cast<std::uint32_t>(result.EmbeddedTextureAssetsCreated),
                .TextureUploadRequests =
                    static_cast<std::uint32_t>(result.TextureUploadRequests),
                .MaterializedModelScene = result.MaterializedModelScene,
                .RequestedTextureUpload = result.RequestedTextureUpload,
            };
        }

        [[nodiscard]] RuntimeAssetIngestDiagnostic DiagnosticForImportError(
            const Core::ErrorCode error) noexcept
        {
            switch (error)
            {
            case Core::ErrorCode::FileNotFound:
                return RuntimeAssetIngestDiagnostic::MissingFile;
            case Core::ErrorCode::AssetUnsupportedFormat:
                return RuntimeAssetIngestDiagnostic::UnsupportedExtension;
            case Core::ErrorCode::AssetLoaderMissing:
                return RuntimeAssetIngestDiagnostic::CallbackFailed;
            case Core::ErrorCode::AssetDecodeFailed:
                return RuntimeAssetIngestDiagnostic::DecodeFailed;
            case Core::ErrorCode::AssetInvalidData:
                return RuntimeAssetIngestDiagnostic::MaterializationFailed;
            case Core::ErrorCode::InvalidPath:
                return RuntimeAssetIngestDiagnostic::MissingPath;
            case Core::ErrorCode::ResourceBusy:
                return RuntimeAssetIngestDiagnostic::DuplicateActiveRequest;
            default:
                break;
            }
            return RuntimeAssetIngestDiagnostic::DecodeFailed;
        }

        [[nodiscard]] RuntimeAssetIngestDiagnostic DiagnosticForImportError(
            const Core::ErrorCode error,
            const RuntimeAssetIngestSource source) noexcept
        {
            if (source == RuntimeAssetIngestSource::Reimport &&
                (error == Core::ErrorCode::ResourceNotFound ||
                 error == Core::ErrorCode::TypeMismatch ||
                 error == Core::ErrorCode::AssetTypeMismatch ||
                 error == Core::ErrorCode::InvalidArgument))
            {
                return RuntimeAssetIngestDiagnostic::InvalidReimportTarget;
            }
            return DiagnosticForImportError(error);
        }

        void AppendImportStage(
            AssetImportStageTrace& trace,
            const AssetImportStage stage,
            const Core::ErrorCode error = Core::ErrorCode::Success,
            RuntimeAssetIngestDiagnostic diagnostic =
                RuntimeAssetIngestDiagnostic::None)
        {
            if (trace.Terminal)
                return;
            if (error != Core::ErrorCode::Success &&
                diagnostic == RuntimeAssetIngestDiagnostic::None)
            {
                diagnostic = DiagnosticForImportError(error);
            }
            const Core::Result appended = AppendAssetImportStageResult(
                trace,
                AssetImportStageResult{
                    .Identity = trace.Identity,
                    .Stage = stage,
                    .Error = error,
                    .Diagnostic = diagnostic,
                });
            if (!appended.has_value())
            {
                Core::Log::Error(
                    "[Runtime] Asset import stage trace rejected stage={} error={}",
                    static_cast<std::uint32_t>(stage),
                    Core::Error::ToString(appended.error()));
            }
        }

        void AppendSuccessfulApplyStages(AssetImportStageTrace& trace)
        {
            AppendImportStage(trace, AssetImportStage::CpuMaterialize);
            AppendImportStage(trace, AssetImportStage::EcsAuthor);
            AppendImportStage(trace, AssetImportStage::Postprocess);
            AppendImportStage(trace, AssetImportStage::GpuResidency);
            AppendImportStage(trace, AssetImportStage::Complete);
        }

        void AppendNextFailedStage(
            AssetImportStageTrace& trace,
            const Core::ErrorCode error,
            const RuntimeAssetIngestDiagnostic diagnostic)
        {
            constexpr std::array<AssetImportStage, 7> orderedStages{
                AssetImportStage::Route,
                AssetImportStage::Decode,
                AssetImportStage::CpuMaterialize,
                AssetImportStage::EcsAuthor,
                AssetImportStage::Postprocess,
                AssetImportStage::GpuResidency,
                AssetImportStage::Complete,
            };
            if (trace.Terminal || trace.Results.size() >= orderedStages.size())
                return;
            const AssetImportStage next = orderedStages[trace.Results.size()];
            AppendImportStage(trace, next, error, diagnostic);
        }

        [[nodiscard]] Core::ErrorCode ErrorFromIngestTransition(
            const RuntimeAssetIngestTransition& transition) noexcept
        {
            if (transition.Error != Core::ErrorCode::Success)
                return transition.Error;
            switch (transition.Diagnostic)
            {
            case RuntimeAssetIngestDiagnostic::None:
                return Core::ErrorCode::Success;
            case RuntimeAssetIngestDiagnostic::MissingPath:
                return Core::ErrorCode::InvalidPath;
            case RuntimeAssetIngestDiagnostic::MissingFile:
                return Core::ErrorCode::FileNotFound;
            case RuntimeAssetIngestDiagnostic::DuplicateActiveRequest:
                return Core::ErrorCode::ResourceBusy;
            case RuntimeAssetIngestDiagnostic::UnknownHandle:
                return Core::ErrorCode::ResourceNotFound;
            default:
                return Core::ErrorCode::InvalidState;
            }
        }

        // A decode job is cancellable until its result is queued for the
        // main-thread drain. `AwaitingGate` is the `JobService` equivalent of
        // the retired executor's `WaitingForMainThreadApply`: the worker is
        // done and the next drain decides the outcome, so cancelling there is
        // reserved for shutdown. `AwaitingApply` is parked work whose readiness
        // gate has not opened, which is still cancellable.
        [[nodiscard]] bool JobStateCanCancel(const JobState state) noexcept
        {
            switch (state)
            {
            case JobState::AwaitingDependencies:
            case JobState::Queued:
            case JobState::Running:
            case JobState::AwaitingApply:
                return true;
            case JobState::Invalid:
            case JobState::AwaitingGate:
            case JobState::Published:
            case JobState::Dropped:
            case JobState::Cancelled:
            case JobState::Rejected:
            case JobState::StaleDiscarded:
                return false;
            }
            return false;
        }

        // A queued import carries its outcome in the shared state record the
        // job's callbacks capture, so the result envelope only has to prove
        // that the decode body ran — an empty envelope is how `JobService`
        // reports a dropped job. It names the ingest record it belongs to so
        // the completion publisher can reject a foreign payload rather than
        // apply it against the wrong request.
        struct QueuedImportDecodeDone
        {
            RuntimeAssetIngestHandle Ingest{};
        };

        [[nodiscard]] bool QueueStageCanUseAsyncCancellation(
            const RuntimeAssetImportQueueStage stage) noexcept
        {
            switch (stage)
            {
            case RuntimeAssetImportQueueStage::DecodeQueued:
            case RuntimeAssetImportQueueStage::Decoding:
                return true;
            case RuntimeAssetImportQueueStage::Queued:
            case RuntimeAssetImportQueueStage::Routing:
            case RuntimeAssetImportQueueStage::MainThreadApply:
            case RuntimeAssetImportQueueStage::GpuUpload:
            case RuntimeAssetImportQueueStage::Complete:
            case RuntimeAssetImportQueueStage::Failed:
            case RuntimeAssetImportQueueStage::Cancelled:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool CreatesOrChangesScene(
            const RuntimeAssetImportResult& result) noexcept
        {
            return result.PrimitiveEntitiesCreated > 0u ||
                   result.MaterializedModelScene;
        }

        [[nodiscard]] bool RequestsGpuUpload(
            const RuntimeAssetImportResult& result) noexcept
        {
            return result.RequestedTextureUpload ||
                   result.TextureUploadRequests > 0u;
        }

        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
        FinalizeModelSceneImport(
            AssetModelSceneHandoff& handoff,
            ECS::Scene::Registry& scene,
            const ImportAuthoringRecipe& authoring,
            const AssetImportCompletionRecipe& completion,
            SelectionController* selection,
            CameraControllerRegistry* cameraControllers,
            const Core::Config::EngineConfig* config,
            RuntimeAssetImportResult result)
        {
            const AssetModelSceneHandoffRecord* record =
                handoff.FindRecord(result.Asset);
            if (record == nullptr || record->Primitives.empty())
            {
                return Core::Err<RuntimeAssetImportResult>(
                    Core::ErrorCode::InvalidState);
            }

            std::vector<ECS::EntityHandle> primitiveEntities{};
            primitiveEntities.reserve(record->Primitives.size());
            for (const AssetModelScenePrimitiveRecord& primitive :
                 record->Primitives)
            {
                primitiveEntities.push_back(primitive.Entity);
            }

            for (const ECS::EntityHandle primitiveEntity : primitiveEntities)
            {
                if (Core::Result authored = ApplyAssetImportAuthoringRecipe(
                        Assets::AssetPayloadKind::Mesh,
                        authoring.AuthorRenderableComponents,
                        authoring.AuthorSelectableIdentity,
                        primitiveEntity,
                        scene);
                    !authored.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        authored.error());
                }
            }

            if (Core::Result completed = ApplyAssetImportCompletionRecipe(
                    completion.SelectFirstCreatedEntity,
                    completion.FocusCameraOnCreatedGeometry,
                    std::span<const ECS::EntityHandle>{primitiveEntities},
                    ComputeFocusTargetForEntities(scene, primitiveEntities),
                    scene,
                    selection,
                    config,
                    cameraControllers);
                !completed.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(completed.error());
            }
            return result;
        }

        [[nodiscard]] bool IsModelTextureImportPayload(
            const Assets::AssetPayloadKind payloadKind) noexcept
        {
            return payloadKind == Assets::AssetPayloadKind::ModelScene ||
                   payloadKind == Assets::AssetPayloadKind::Texture2D;
        }

        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
        MaterializeDecodedModelSceneImport(
            Assets::AssetService& assetService,
            AssetModelSceneHandoff& handoff,
            ECS::Scene::Registry& scene,
            const AssetImportRecipe& recipe,
            SelectionController* selection,
            CameraControllerRegistry* cameraControllers,
            const Core::Config::EngineConfig* config,
            const RuntimeAssetImportRequest& request,
            const Assets::AssetId existingAsset,
            Assets::AssetModelScenePayload decoded)
        {
            auto payload =
                std::make_shared<Assets::AssetModelScenePayload>(
                    std::move(decoded));
            const AssetModelSceneHandoffDiagnostics before =
                handoff.GetDiagnostics();
            if (existingAsset.IsValid())
            {
                Core::Result reloaded =
                    assetService.Reload<Assets::AssetModelScenePayload>(
                        existingAsset,
                        [payload](std::string_view,
                                  Assets::AssetId)
                            -> Core::Expected<Assets::AssetModelScenePayload>
                        {
                            return *payload;
                        });
                if (!reloaded.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        reloaded.error());
                }

                if (Core::Result drained =
                        DrainAssetImportEvents(assetService, existingAsset);
                    !drained.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        drained.error());
                }
                const AssetModelSceneHandoffDiagnostics after =
                    handoff.GetDiagnostics();
                if (after.ModelSceneMaterializeFailures >
                        before.ModelSceneMaterializeFailures &&
                    after.LastFailedAsset == existingAsset)
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        NormalizeImportError(after.LastError));
                }

                RuntimeAssetImportResult result{
                    .Asset = existingAsset,
                    .PayloadKind = Assets::AssetPayloadKind::ModelScene,
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
                return FinalizeModelSceneImport(
                    handoff,
                    scene,
                    recipe.Authoring,
                    recipe.Completion,
                    selection,
                    cameraControllers,
                    config,
                    std::move(result));
            }

            auto asset = assetService.Load<Assets::AssetModelScenePayload>(
                request.Path,
                [payload](std::string_view,
                          Assets::AssetId)
                    -> Core::Expected<Assets::AssetModelScenePayload>
                {
                    return *payload;
                });
            if (!asset.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(asset.error());
            }

            if (Core::Result drained =
                    DrainAssetImportEvents(assetService, *asset);
                !drained.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(drained.error());
            }
            if (handoff.FindRecord(*asset) == nullptr)
            {
                if (Core::Result materialized =
                        handoff.MaterializeReadyModelScene(*asset);
                    !materialized.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        materialized.error());
                }
            }

            const AssetModelSceneHandoffDiagnostics after =
                handoff.GetDiagnostics();
            if (after.ModelSceneMaterializeFailures >
                    before.ModelSceneMaterializeFailures &&
                after.LastFailedAsset == *asset)
            {
                return Core::Err<RuntimeAssetImportResult>(
                    NormalizeImportError(after.LastError));
            }

            RuntimeAssetImportResult result{
                .Asset = *asset,
                .PayloadKind = Assets::AssetPayloadKind::ModelScene,
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
            return FinalizeModelSceneImport(
                handoff,
                scene,
                recipe.Authoring,
                recipe.Completion,
                selection,
                cameraControllers,
                config,
                std::move(result));
        }

        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
        MaterializeDecodedTextureImport(
            Assets::AssetService& assetService,
            Graphics::GpuAssetCache& gpuAssetCache,
            AssetModelTextureHandoff& handoff,
            const RuntimeAssetImportRequest& request,
            const Assets::AssetId existingAsset,
            Assets::AssetTexture2DPayload decoded)
        {
            auto payload =
                std::make_shared<Assets::AssetTexture2DPayload>(
                    std::move(decoded));
            const AssetModelTextureHandoffDiagnostics before =
                handoff.GetDiagnostics();
            if (existingAsset.IsValid())
            {
                Core::Result reloaded =
                    assetService.Reload<Assets::AssetTexture2DPayload>(
                        existingAsset,
                        [payload](std::string_view,
                                  Assets::AssetId)
                            -> Core::Expected<Assets::AssetTexture2DPayload>
                        {
                            return *payload;
                        });
                if (!reloaded.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        reloaded.error());
                }

                if (Core::Result drained =
                        DrainAssetImportEvents(assetService, existingAsset);
                    !drained.has_value())
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        drained.error());
                }
                const AssetModelTextureHandoffDiagnostics after =
                    handoff.GetDiagnostics();
                if (after.TextureUploadFailures > before.TextureUploadFailures &&
                    after.LastFailedAsset == existingAsset)
                {
                    return Core::Err<RuntimeAssetImportResult>(
                        NormalizeImportError(after.LastError));
                }

                return RuntimeAssetImportResult{
                    .Asset = existingAsset,
                    .PayloadKind = Assets::AssetPayloadKind::Texture2D,
                    .TextureUploadRequests =
                        Delta(after.TextureUploadRequests,
                              before.TextureUploadRequests),
                    .RequestedTextureUpload =
                        after.TextureUploadRequests > before.TextureUploadRequests,
                };
            }

            auto asset = assetService.Load<Assets::AssetTexture2DPayload>(
                request.Path,
                [payload](std::string_view,
                          Assets::AssetId)
                    -> Core::Expected<Assets::AssetTexture2DPayload>
                {
                    return *payload;
                });
            if (!asset.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(asset.error());
            }

            if (Core::Result drained =
                    DrainAssetImportEvents(assetService, *asset);
                !drained.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(drained.error());
            }
            AssetModelTextureHandoffDiagnostics after =
                handoff.GetDiagnostics();
            const bool uploadWasAlreadyHandled =
                after.TextureUploadRequests > before.TextureUploadRequests ||
                after.TextureUploadDeferrals > before.TextureUploadDeferrals ||
                after.TextureUploadFailures > before.TextureUploadFailures;

            if (!uploadWasAlreadyHandled &&
                gpuAssetCache.GetState(*asset) ==
                    Graphics::GpuAssetState::NotRequested)
            {
                if (Core::Result uploaded = handoff.UploadReadyTexture(*asset);
                    !uploaded.has_value())
                {
                    if (!IsTextureUploadDeferral(uploaded.error()))
                    {
                        return Core::Err<RuntimeAssetImportResult>(
                            uploaded.error());
                    }
                }
            }

            after = handoff.GetDiagnostics();
            if (after.TextureUploadFailures > before.TextureUploadFailures &&
                after.LastFailedAsset == *asset)
            {
                return Core::Err<RuntimeAssetImportResult>(
                    NormalizeImportError(after.LastError));
            }

            return RuntimeAssetImportResult{
                .Asset = *asset,
                .PayloadKind = Assets::AssetPayloadKind::Texture2D,
                .TextureUploadRequests =
                    Delta(after.TextureUploadRequests,
                          before.TextureUploadRequests),
                .RequestedTextureUpload =
                    after.TextureUploadRequests > before.TextureUploadRequests,
            };
        }

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

        [[nodiscard]] std::optional<CameraFocusTarget> ToOptionalCameraFocusTarget(
            const std::optional<GeometryImportBounds>& bounds) noexcept
        {
            if (!bounds.has_value() || !bounds->Valid)
                return std::nullopt;
            return ToCameraFocusTarget(*bounds);
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

            switch (route->PayloadKind)
            {
            case Assets::AssetPayloadKind::Mesh:
            {
                Core::Expected<Geometry::MeshIO::MeshIOResult> meshPayload =
                    Core::Err<Geometry::MeshIO::MeshIOResult>(
                        Core::ErrorCode::AssetUnsupportedFormat);
                switch (route->Format)
                {
                case Assets::AssetFileFormat::OBJ:
                    meshPayload = Geometry::MeshIO::LoadOBJ(request.Path);
                    break;
                case Assets::AssetFileFormat::OFF:
                    meshPayload = Geometry::MeshIO::LoadOFF(request.Path);
                    break;
                case Assets::AssetFileFormat::STL:
                    meshPayload = Geometry::MeshIO::LoadSTL(request.Path);
                    break;
                case Assets::AssetFileFormat::PLY:
                    meshPayload = Geometry::MeshIO::LoadPLY(request.Path);
                    break;
                default:
                    break;
                }
                if (!meshPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        meshPayload.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedMeshImport{
                        .Payload = std::make_shared<Geometry::MeshIO::MeshIOResult>(
                            std::move(*meshPayload)),
                    },
                };
            }
            case Assets::AssetPayloadKind::Graph:
            {
                Core::Expected<Geometry::GraphIO::GraphIOResult> graphPayload =
                    Core::Err<Geometry::GraphIO::GraphIOResult>(
                        Core::ErrorCode::AssetUnsupportedFormat);
                switch (route->Format)
                {
                case Assets::AssetFileFormat::TGF:
                    graphPayload = Geometry::GraphIO::LoadTGF(request.Path);
                    break;
                case Assets::AssetFileFormat::EdgeList:
                    graphPayload = Geometry::GraphIO::LoadEdgeList(request.Path);
                    break;
                default:
                    break;
                }
                if (!graphPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        graphPayload.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedGraphImport{
                        .Payload = std::make_shared<
                            Geometry::GraphIO::GraphIOResult>(
                                std::move(*graphPayload)),
                    },
                };
            }
            case Assets::AssetPayloadKind::PointCloud:
            {
                Core::Expected<Geometry::PointCloudIO::PointCloudIOResult>
                    cloudPayload =
                        Core::Err<Geometry::PointCloudIO::PointCloudIOResult>(
                            Core::ErrorCode::AssetUnsupportedFormat);
                switch (route->Format)
                {
                case Assets::AssetFileFormat::XYZ:
                case Assets::AssetFileFormat::PTS:
                case Assets::AssetFileFormat::XYZRGB:
                    cloudPayload = Geometry::PointCloudIO::LoadXYZ(request.Path);
                    break;
                case Assets::AssetFileFormat::PCD:
                    cloudPayload = Geometry::PointCloudIO::LoadPCD(request.Path);
                    break;
                case Assets::AssetFileFormat::PLY:
                    cloudPayload = Geometry::PointCloudIO::LoadPLY(request.Path);
                    break;
                default:
                    break;
                }
                if (!cloudPayload.has_value())
                {
                    return Core::Err<DecodedGeometryImport>(
                        cloudPayload.error());
                }

                return DecodedGeometryImport{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                    .Payload = DecodedPointCloudImport{
                        .Payload = std::make_shared<
                            Geometry::PointCloudIO::PointCloudIOResult>(
                                std::move(*cloudPayload)),
                    },
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
            JobService* jobs,
            WorldRegistry* worlds,
            const WorldHandle world,
            std::function<bool()> bindingValid,
            TextureBakeService* textureBake,
            const AssetImportRecipe& recipe,
            const DecodedGeometryImport& decoded)
        {
            return std::visit(
                [&](const auto& payload) -> Core::Expected<MaterializedGeometryImport>
                {
                    using PayloadT = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<PayloadT, DecodedMeshImport>)
                    {
                        if (payload.Payload == nullptr)
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                Core::ErrorCode::AssetInvalidData);
                        }
                        const auto meshPayload = payload.Payload;
                        auto asset =
                            assetService.Load<Geometry::MeshIO::MeshIOResult>(
                                decoded.Path,
                                [meshPayload](std::string_view,
                                              Assets::AssetId)
                                    -> Core::Expected<Geometry::MeshIO::MeshIOResult>
                                {
                                    if (meshPayload == nullptr)
                                    {
                                        return Core::Err<Geometry::MeshIO::MeshIOResult>(
                                            Core::ErrorCode::AssetInvalidData);
                                    }
                                    return *meshPayload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        if (Core::Result drained =
                                DrainAssetImportEvents(assetService, *asset);
                            !drained.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                drained.error());
                        }

                        auto rawMesh = BuildRuntimeHalfedgeMeshGeometryOnly(
                            *meshPayload,
                            RuntimeMeshGeometryOnlyOptions{
                                .AllowDisconnectedRenderableFallback = true,
                            });
                        if (!rawMesh.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                rawMesh.error());
                        }

                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        if (Core::Result authored =
                                ApplyAssetImportAuthoringRecipe(
                                    decoded.PayloadKind,
                                    recipe.Authoring.AuthorRenderableComponents,
                                    recipe.Authoring.AuthorSelectableIdentity,
                                    entity,
                                    scene);
                            !authored.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                authored.error());
                        }
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromHalfedgeMesh(*rawMesh);
                        if (bounds.has_value())
                        {
                            AttachGeometryBounds(raw, entity, *bounds);
                        }
                        ECS::Components::GeometrySources::PopulateFromMesh(
                            raw,
                            entity,
                            *rawMesh);
                        if (recipe.Postprocess ==
                            AssetImportPostprocessPolicy::PrepareRenderableGeometry)
                        {
                            QueueAssetImportDirectMeshPostprocess(
                                jobs,
                                worlds,
                                world,
                                bindingValid,
                                scene,
                                textureBake,
                                decoded.Path,
                                *meshPayload,
                                entity);
                        }

                        return MaterializedGeometryImport{
                            .Result = RuntimeAssetImportResult{
                                .Asset = *asset,
                                .PayloadKind = decoded.PayloadKind,
                                .PrimitiveEntitiesCreated = 1u,
                            },
                            .FocusTarget = ToOptionalCameraFocusTarget(bounds),
                            .Entity = entity,
                        };
                    }
                    else if constexpr (std::is_same_v<PayloadT, DecodedGraphImport>)
                    {
                        if (payload.Payload == nullptr)
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                Core::ErrorCode::AssetInvalidData);
                        }
                        const auto graphPayload = payload.Payload;
                        auto asset =
                            assetService.Load<Geometry::GraphIO::GraphIOResult>(
                                decoded.Path,
                                [graphPayload](std::string_view,
                                               Assets::AssetId)
                                    -> Core::Expected<Geometry::GraphIO::GraphIOResult>
                                {
                                    if (graphPayload == nullptr)
                                    {
                                        return Core::Err<Geometry::GraphIO::GraphIOResult>(
                                            Core::ErrorCode::AssetInvalidData);
                                    }
                                    return *graphPayload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        if (Core::Result drained =
                                DrainAssetImportEvents(assetService, *asset);
                            !drained.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                drained.error());
                        }

                        Geometry::Graph::Graph graph = graphPayload->Graph;
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromGraph(graph);
                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        if (Core::Result authored =
                                ApplyAssetImportAuthoringRecipe(
                                    decoded.PayloadKind,
                                    recipe.Authoring.AuthorRenderableComponents,
                                    recipe.Authoring.AuthorSelectableIdentity,
                                    entity,
                                    scene);
                            !authored.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                authored.error());
                        }
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
                            .FocusTarget = ToOptionalCameraFocusTarget(bounds),
                            .Entity = entity,
                        };
                    }
                    else
                    {
                        if (payload.Payload == nullptr)
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                Core::ErrorCode::AssetInvalidData);
                        }
                        const auto cloudPayload = payload.Payload;
                        auto asset =
                            assetService.Load<Geometry::PointCloudIO::PointCloudIOResult>(
                                decoded.Path,
                                [cloudPayload](std::string_view,
                                               Assets::AssetId)
                                    -> Core::Expected<Geometry::PointCloudIO::PointCloudIOResult>
                                {
                                    if (cloudPayload == nullptr)
                                    {
                                        return Core::Err<Geometry::PointCloudIO::PointCloudIOResult>(
                                            Core::ErrorCode::AssetInvalidData);
                                    }
                                    return *cloudPayload;
                                });
                        if (!asset.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                asset.error());
                        }
                        if (Core::Result drained =
                                DrainAssetImportEvents(assetService, *asset);
                            !drained.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                drained.error());
                        }

                        Geometry::PointCloud::Cloud cloud = cloudPayload->Cloud;
                        const std::optional<GeometryImportBounds> bounds =
                            BoundsFromCloud(cloud);
                        const ECS::EntityHandle entity =
                            ECS::Scene::CreateDefault(
                                scene,
                                GeometryEntityName(decoded.Path, decoded.PayloadKind));
                        auto& raw = scene.Raw();
                        if (Core::Result authored =
                                ApplyAssetImportAuthoringRecipe(
                                    decoded.PayloadKind,
                                    recipe.Authoring.AuthorRenderableComponents,
                                    recipe.Authoring.AuthorSelectableIdentity,
                                    entity,
                                    scene);
                            !authored.has_value())
                        {
                            return Core::Err<MaterializedGeometryImport>(
                                authored.error());
                        }
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
                            .FocusTarget = ToOptionalCameraFocusTarget(bounds),
                            .Entity = entity,
                        };
                    }
                },
                decoded.Payload);
        }

        [[nodiscard]] Core::Expected<Assets::AssetPayloadKind>
        PayloadKindForExistingAsset(
            Assets::AssetService& assetService,
            const Assets::AssetId asset)
        {
            if (!asset.IsValid() || !assetService.IsAlive(asset))
            {
                return Core::Err<Assets::AssetPayloadKind>(
                    Core::ErrorCode::ResourceNotFound);
            }

            const auto meta = assetService.GetMeta(asset);
            if (!meta.has_value())
            {
                return Core::Err<Assets::AssetPayloadKind>(meta.error());
            }

            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Geometry::MeshIO::MeshIOResult>())
            {
                return Assets::AssetPayloadKind::Mesh;
            }
            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Geometry::GraphIO::GraphIOResult>())
            {
                return Assets::AssetPayloadKind::Graph;
            }
            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Geometry::PointCloudIO::PointCloudIOResult>())
            {
                return Assets::AssetPayloadKind::PointCloud;
            }
            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Assets::AssetModelScenePayload>())
            {
                return Assets::AssetPayloadKind::ModelScene;
            }
            if (meta->typeId == Assets::AssetService::TypeIdOf<
                    Assets::AssetTexture2DPayload>())
            {
                return Assets::AssetPayloadKind::Texture2D;
            }

            return Core::Err<Assets::AssetPayloadKind>(
                Core::ErrorCode::AssetTypeMismatch);
        }

        [[nodiscard]] Core::Expected<RuntimeAssetImportResult>
        ReloadDecodedGeometryImport(
            Assets::AssetService& assetService,
            const Assets::AssetId existingAsset,
            const DecodedGeometryImport& decoded)
        {
            auto reloadResult = std::visit(
                [&](const auto& payload) -> Core::Result
                {
                    using PayloadT = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<PayloadT, DecodedMeshImport>)
                    {
                        if (payload.Payload == nullptr)
                        {
                            return Core::Err(Core::ErrorCode::AssetInvalidData);
                        }
                        const auto storedPayload = payload.Payload;
                        return assetService.Reload<
                            Geometry::MeshIO::MeshIOResult>(
                            existingAsset,
                            [storedPayload](std::string_view,
                                            Assets::AssetId)
                                -> Core::Expected<Geometry::MeshIO::MeshIOResult>
                            {
                                if (storedPayload == nullptr)
                                {
                                    return Core::Err<Geometry::MeshIO::MeshIOResult>(
                                        Core::ErrorCode::AssetInvalidData);
                                }
                                return *storedPayload;
                            });
                    }
                    else if constexpr (std::is_same_v<PayloadT, DecodedGraphImport>)
                    {
                        if (payload.Payload == nullptr)
                        {
                            return Core::Err(Core::ErrorCode::AssetInvalidData);
                        }
                        const auto storedPayload = payload.Payload;
                        return assetService.Reload<
                            Geometry::GraphIO::GraphIOResult>(
                            existingAsset,
                            [storedPayload](std::string_view,
                                            Assets::AssetId)
                                -> Core::Expected<Geometry::GraphIO::GraphIOResult>
                            {
                                if (storedPayload == nullptr)
                                {
                                    return Core::Err<Geometry::GraphIO::GraphIOResult>(
                                        Core::ErrorCode::AssetInvalidData);
                                }
                                return *storedPayload;
                            });
                    }
                    else
                    {
                        if (payload.Payload == nullptr)
                        {
                            return Core::Err(Core::ErrorCode::AssetInvalidData);
                        }
                        const auto storedPayload = payload.Payload;
                        return assetService.Reload<
                            Geometry::PointCloudIO::PointCloudIOResult>(
                            existingAsset,
                            [storedPayload](std::string_view,
                                            Assets::AssetId)
                                -> Core::Expected<Geometry::PointCloudIO::PointCloudIOResult>
                            {
                                if (storedPayload == nullptr)
                                {
                                    return Core::Err<Geometry::PointCloudIO::PointCloudIOResult>(
                                        Core::ErrorCode::AssetInvalidData);
                                }
                                return *storedPayload;
                            });
                    }
                },
                decoded.Payload);

            if (!reloadResult.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(
                    reloadResult.error());
            }

            if (Core::Result drained =
                    DrainAssetImportEvents(assetService, existingAsset);
                !drained.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(drained.error());
            }
            return RuntimeAssetImportResult{
                .Asset = existingAsset,
                .PayloadKind = decoded.PayloadKind,
            };
        }
    }

    Core::Result ValidateAssetImportRecipe(
        const AssetImportRecipe& recipe) noexcept
    {
        if (recipe.Path.empty() ||
            recipe.PayloadKind == Assets::AssetPayloadKind::Unknown)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        const bool isReimport =
            recipe.Source == RuntimeAssetIngestSource::Reimport;
        if (isReimport != recipe.ExistingAsset.IsValid())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        if (recipe.PayloadKind != Assets::AssetPayloadKind::Texture2D &&
            (!recipe.Authoring.AuthorRenderableComponents ||
             !recipe.Authoring.AuthorSelectableIdentity))
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }
        return Core::Ok();
    }

    Core::Result AppendAssetImportStageResult(
        AssetImportStageTrace& trace,
        AssetImportStageResult result)
    {
        constexpr std::array<AssetImportStage, 7> orderedStages{
            AssetImportStage::Route,
            AssetImportStage::Decode,
            AssetImportStage::CpuMaterialize,
            AssetImportStage::EcsAuthor,
            AssetImportStage::Postprocess,
            AssetImportStage::GpuResidency,
            AssetImportStage::Complete,
        };

        if (trace.Terminal ||
            trace.Results.size() >= orderedStages.size() ||
            result.Identity != trace.Identity ||
            result.Stage != orderedStages[trace.Results.size()])
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        if (result.Succeeded())
        {
            if (result.Diagnostic != RuntimeAssetIngestDiagnostic::None)
            {
                return Core::Err(Core::ErrorCode::InvalidArgument);
            }
        }
        else if (result.Diagnostic == RuntimeAssetIngestDiagnostic::None)
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        const bool terminal =
            !result.Succeeded() || result.Stage == AssetImportStage::Complete;
        trace.Results.push_back(std::move(result));
        trace.Terminal = terminal;
        return Core::Ok();
    }

    AssetImportPipeline::AssetImportPipeline(
        AssetImportPipelineDependencies dependencies)
    {
        SetDependencies(dependencies);
    }

    void AssetImportPipeline::SetDependencies(
        AssetImportPipelineDependencies dependencies) noexcept
    {
        // Active-scene handoffs are replaced on every world switch. The world
        // handle and scene address can both become equal again after an
        // away-and-back switch, so pointer identity alone cannot prove that a
        // queued active-only operation still owns the binding it captured.
        ++m_TargetBindingEpoch;
        if (m_TargetBindingEpoch == 0u)
            m_TargetBindingEpoch = 1u;
        m_Initialized = BorrowedBool{dependencies.Initialized};
        m_Config = BorrowedSubsystem<const Core::Config::EngineConfig>{dependencies.Config};
        m_Jobs = BorrowedSubsystem<JobService>{dependencies.Jobs};
        m_WorldRegistry = BorrowedSubsystem<WorldRegistry>{dependencies.Worlds};
        m_World = dependencies.World;
        m_BindingValid = std::move(dependencies.BindingValid);
        m_AssetService = BorrowedSubsystem<Assets::AssetService>{dependencies.AssetService};
        m_GpuAssetCache = BorrowedSubsystem<Graphics::GpuAssetCache>{dependencies.GpuAssetCache};
        m_AssetModelTextureHandoff =
            BorrowedSubsystem<AssetModelTextureHandoff>{dependencies.ModelTextureHandoff};
        m_AssetModelSceneHandoff =
            BorrowedSubsystem<AssetModelSceneHandoff>{dependencies.ModelSceneHandoff};
        m_RenderExtraction =
            BorrowedSubsystem<RenderExtractionCache>{dependencies.RenderExtraction};
        m_Scene = BorrowedSubsystem<ECS::Scene::Registry>{dependencies.Scene};
        m_SelectionController =
            BorrowedSubsystem<SelectionController>{dependencies.Selection};
        m_CameraControllers =
            BorrowedSubsystem<CameraControllerRegistry>{
                dependencies.CameraControllers};
        m_EditorCommandHistory =
            BorrowedSubsystem<EditorCommandHistory>{dependencies.CommandHistory};
        m_TextureBake =
            BorrowedSubsystem<TextureBakeService>{
                dependencies.TextureBake};
    }

    bool AssetImportPipeline::IsCurrentSubmissionTarget(
        const WorldHandle world,
        const ECS::Scene::Registry* const scene,
        const std::uint64_t bindingEpoch) const noexcept
    {
        return bindingEpoch == m_TargetBindingEpoch &&
            world.IsValid() &&
            scene != nullptr &&
            m_WorldRegistry != nullptr &&
            m_World == world &&
            m_Scene.get() == scene &&
            m_WorldRegistry->ActiveWorld() == world &&
            m_WorldRegistry->Get(world) == scene;
    }

    Core::Expected<RuntimeAssetImportResult> AssetImportPipeline::ImportAssetFromPath(
        RuntimeAssetImportRequest request)
    {
        auto result = ImportAssetFromPathWithIngest(
            std::move(request),
            RuntimeAssetIngestSource::ManualImport,
            {});
        if (result.has_value() && CreatesOrChangesScene(*result))
        {
            (void)m_EditorCommandHistory->MarkDirty("Import Asset");
        }
        return result;
    }

    Core::Expected<RuntimeQueuedAssetImport> AssetImportPipeline::QueueModelTextureImport(
        RuntimeAssetImportRequest request)
    {
        return QueueAssetImport(AssetImportRecipe{
            .Path = std::move(request.Path),
            .PayloadKind = request.PayloadKind,
        });
    }

    Core::Expected<RuntimeQueuedAssetImport> AssetImportPipeline::QueueGeometryImport(
        RuntimeAssetImportRequest request)
    {
        return QueueAssetImport(AssetImportRecipe{
            .Path = std::move(request.Path),
            .PayloadKind = request.PayloadKind,
        });
    }

    Core::Expected<RuntimeQueuedAssetImport> AssetImportPipeline::QueueAssetImport(
        AssetImportRecipe recipe)
    {
        auto route = Assets::ResolveAssetImportRoute(
            recipe.Path,
            Assets::AssetRouteOperation::Import,
            Assets::AssetImportHint{.PayloadKind = recipe.PayloadKind});
        if (!route.has_value())
            return Core::Err<RuntimeQueuedAssetImport>(route.error());
        recipe.PayloadKind = route->PayloadKind;
        if (Core::Result valid = ValidateAssetImportRecipe(recipe);
            !valid.has_value())
        {
            return Core::Err<RuntimeQueuedAssetImport>(valid.error());
        }

        if (Assets::IsGeometryPayloadKind(route->PayloadKind))
        {
            return QueueGeometryImportWithIngest(
                std::move(recipe),
                {route->PayloadKind});
        }
        if (IsModelTextureImportPayload(route->PayloadKind))
            return QueueModelTextureImportWithIngest(std::move(recipe));
        return Core::Err<RuntimeQueuedAssetImport>(
            Core::ErrorCode::AssetTypeMismatch);
    }

    Core::Expected<RuntimeAssetImportResult> AssetImportPipeline::ReimportAsset(
        RuntimeAssetReimportRequest request)
    {
        RuntimeAssetImportRequest importRequest{
            .PayloadKind = request.PayloadKind,
        };

        if (m_AssetService && request.Asset.IsValid() &&
            m_AssetService->IsAlive(request.Asset))
        {
            auto path = m_AssetService->GetPath(request.Asset);
            if (path.has_value())
            {
                importRequest.Path = std::move(*path);
            }
            else
            {
                importRequest.Path = "<invalid-reimport-target>";
            }
            if (importRequest.PayloadKind == Assets::AssetPayloadKind::Unknown)
            {
                auto payloadKind =
                    PayloadKindForExistingAsset(*m_AssetService, request.Asset);
                if (payloadKind.has_value())
                {
                    importRequest.PayloadKind = *payloadKind;
                }
            }
        }
        else if (request.Asset.IsValid())
        {
            importRequest.Path = "<invalid-reimport-target>";
        }

        auto result = ImportAssetFromPathWithIngest(
            std::move(importRequest),
            RuntimeAssetIngestSource::Reimport,
            request.Asset);
        if (result.has_value() && CreatesOrChangesScene(*result))
        {
            (void)m_EditorCommandHistory->MarkDirty("Reimport Asset");
        }
        return result;
    }

    const std::optional<RuntimeAssetImportEvent>& AssetImportPipeline::GetLastAssetImportEvent()
        const noexcept
    {
        return m_LastAssetImportEvent;
    }

    std::vector<RuntimeAssetIngestRecord>
    AssetImportPipeline::GetAssetIngestRecordsForTest() const
    {
        return m_AssetIngestStateMachine.SnapshotAll();
    }

    void AssetImportPipeline::SetModelTextureImportIOBackendFactoryForTest(
        RuntimeIOBackendFactory factory)
    {
        m_ModelTextureImportIOBackendFactoryForTest = std::move(factory);
    }

    void AssetImportPipeline::SetQueuedGeometryImportBeforeDecodeHookForTest(
        std::function<void(const RuntimeAssetImportRequest&)> hook)
    {
        m_QueuedGeometryImportBeforeDecodeHookForTest = std::move(hook);
    }

    RuntimeAssetImportQueueSnapshot AssetImportPipeline::GetAssetImportQueueSnapshot() const
    {
        RuntimeAssetImportQueueSnapshot snapshot =
            m_AssetIngestStateMachine.SnapshotQueue();

        // The retired executor offered a batched `GetStates(handles)` that this
        // read collected work for; `JobService` exposes only the single-token
        // query, and a queue snapshot for the import UI holds a handful of
        // entries, so the two-pass collection collapses into one loop rather
        // than earning a new batch API on the service.
        for (RuntimeAssetImportQueueEntry& entry : snapshot.Entries)
        {
            if (entry.TerminalStatus != RuntimeAssetImportQueueTerminalStatus::None)
            {
                entry.CanCancel = false;
                entry.CancelDisabledReason =
                    "Import has already reached a terminal state.";
                continue;
            }

            const auto taskIt = std::find_if(
                m_AssetImportJobs.begin(),
                m_AssetImportJobs.end(),
                [&entry](const RuntimeAssetImportJobRecord& task)
                {
                    return task.Ingest == entry.Operation;
                });

            if (taskIt == m_AssetImportJobs.end() ||
                !taskIt->Job.IsValid() ||
                !m_Jobs)
            {
                entry.CanCancel = false;
                entry.CancelDisabledReason =
                    "Import is running synchronously or has no cancellable decode job.";
                continue;
            }

            entry.CanCancel =
                QueueStageCanUseAsyncCancellation(entry.Stage) &&
                JobStateCanCancel(m_Jobs->GetState(taskIt->Job));
            if (!entry.CanCancel)
            {
                entry.CancelDisabledReason =
                    "Import can no longer be cancelled before main-thread apply.";
            }
            else
            {
                entry.CancelDisabledReason.clear();
            }
        }

        return snapshot;
    }

    std::size_t AssetImportPipeline::ClearCompletedAssetImports()
    {
        return m_AssetIngestStateMachine.ClearCompletedQueueEntries();
    }

    Core::Result AssetImportPipeline::CancelAssetImport(
        const RuntimeAssetIngestHandle operation)
    {
        return CancelAssetImportImpl(operation, false);
    }

    void AssetImportPipeline::CancelActiveAssetImportsForShutdown()
    {
        for (const RuntimeAssetImportJobRecord& task :
             m_AssetImportJobs)
        {
            const std::optional<RuntimeAssetIngestRecord> record =
                m_AssetIngestStateMachine.Snapshot(task.Ingest);
            if (!record.has_value() || IsTerminal(record->Phase))
                continue;
            (void)CancelAssetImportImpl(task.Ingest, true);
        }
    }

    Core::Result AssetImportPipeline::CancelAssetImportImpl(
        const RuntimeAssetIngestHandle operation,
        const bool allowWaitingForMainThreadApply)
    {
        const std::optional<RuntimeAssetIngestRecord> record =
            m_AssetIngestStateMachine.Snapshot(operation);
        if (!record.has_value())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }
        if (IsTerminal(record->Phase))
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto taskIt = std::find_if(
            m_AssetImportJobs.begin(),
            m_AssetImportJobs.end(),
            [operation](const RuntimeAssetImportJobRecord& task)
            {
                return task.Ingest == operation;
            });
        if (taskIt == m_AssetImportJobs.end() ||
            !taskIt->Job.IsValid() ||
            !m_Jobs)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        const JobState state = m_Jobs->GetState(taskIt->Job);
        if (!JobStateCanCancel(state) &&
            !(allowWaitingForMainThreadApply &&
              state == JobState::AwaitingGate))
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        (void)m_Jobs->Cancel(taskIt->Job);
        RuntimeAssetIngestTransition cancelled =
            m_AssetIngestStateMachine.Cancel(operation);
        const bool cancelledRecord =
            cancelled.Mutated &&
            cancelled.Diagnostic == RuntimeAssetIngestDiagnostic::Cancelled;
        if (!cancelledRecord)
        {
            return Core::Err(ErrorFromIngestTransition(cancelled));
        }

        if (taskIt->StageTrace != nullptr)
        {
            AppendNextFailedStage(
                *taskIt->StageTrace,
                Core::ErrorCode::InvalidState,
                cancelled.Diagnostic);
        }
        RecordAssetImportEvent(
            RuntimeAssetImportRequest{
                .Path = record->Request.Path,
                .PayloadKind = record->Request.PayloadKind,
            },
            Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
            cancelled.Diagnostic,
            taskIt->StageTrace.get());
        return Core::Ok();
    }

    void AssetImportPipeline::FinalizeUnpublishedImport(
        const RuntimeAssetIngestHandle operation,
        RuntimeAssetImportRequest request,
        AssetImportStageTrace* const stageTrace)
    {
        const std::optional<RuntimeAssetIngestRecord> record =
            m_AssetIngestStateMachine.Snapshot(operation);
        if (!record.has_value() || IsTerminal(record->Phase))
            return;

        RuntimeAssetIngestTransition cancelled =
            m_AssetIngestStateMachine.Cancel(operation);
        if (!cancelled.Mutated ||
            cancelled.Diagnostic != RuntimeAssetIngestDiagnostic::Cancelled)
        {
            return;
        }

        if (stageTrace != nullptr)
        {
            AppendNextFailedStage(
                *stageTrace,
                Core::ErrorCode::InvalidState,
                cancelled.Diagnostic);
        }
        RecordAssetImportEvent(
            request,
            Core::Err<RuntimeAssetImportResult>(
                Core::ErrorCode::InvalidState),
            cancelled.Diagnostic,
            stageTrace);
    }

    void AssetImportPipeline::ImportDroppedFilePaths(std::span<const std::string> paths)
    {
        for (const std::string& path : paths)
        {
            if (path.empty())
            {
                Core::Log::Warn(
                    "[Runtime] Dropped file path ignored: empty path.");
                continue;
            }

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
                    Core::Log::Info(
                        "[Runtime] Dropped ambiguous geometry file routed to deferred import: path='{}' candidate_count={}",
                        path,
                        geometryPayloads.size());
                    (void)QueueGeometryImportWithIngest(
                        AssetImportRecipe{
                            .Path = path,
                            .PayloadKind = geometryPayloads.front(),
                            .Source = RuntimeAssetIngestSource::DroppedFile,
                        },
                        std::move(geometryPayloads));
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
                Core::Log::Info(
                    "[Runtime] Dropped geometry file routed to deferred import: path='{}' payload={}",
                    path,
                    Assets::DebugNameForAssetPayloadKind(route->PayloadKind));
                (void)QueueGeometryImportWithIngest(
                    AssetImportRecipe{
                        .Path = path,
                        .PayloadKind = route->PayloadKind,
                        .Source = RuntimeAssetIngestSource::DroppedFile,
                    },
                    {route->PayloadKind});
                continue;
            }
            if (route.has_value() &&
                (route->PayloadKind == Assets::AssetPayloadKind::ModelScene ||
                 route->PayloadKind == Assets::AssetPayloadKind::Texture2D))
            {
                Core::Log::Info(
                    "[Runtime] Dropped model/texture file routed to deferred import: path='{}' payload={}",
                    path,
                    Assets::DebugNameForAssetPayloadKind(route->PayloadKind));
                QueueDroppedModelTextureImport(path, route->PayloadKind);
                continue;
            }

            Core::Log::Info(
                "[Runtime] Dropped file routed to synchronous import: path='{}' route_status={} route_error={}",
                path,
                Assets::DebugNameForAssetRouteStatus(diagnostic.Status),
                Core::Error::ToString(diagnostic.Error));
            auto result = ImportAssetFromPathWithIngest(
                RuntimeAssetImportRequest{
                    .Path = path,
                    .PayloadKind = Assets::AssetPayloadKind::Unknown,
                },
                RuntimeAssetIngestSource::DroppedFile,
                {});
            if (result.has_value() && CreatesOrChangesScene(*result))
            {
                (void)m_EditorCommandHistory->MarkDirty("Import Asset");
            }
        }
    }

    Core::Expected<RuntimeQueuedAssetImport>
    AssetImportPipeline::QueueGeometryImportWithIngest(
        AssetImportRecipe recipe,
        std::vector<Assets::AssetPayloadKind> payloadKinds)
    {
        const RuntimeAssetIngestSource source = recipe.Source;
        RuntimeAssetImportRequest request{
            .Path = recipe.Path,
            .PayloadKind = recipe.PayloadKind,
        };
        if (!payloadKinds.empty())
            request.PayloadKind = payloadKinds.front();
        RuntimeAssetIngestTransition submit =
            m_AssetIngestStateMachine.Submit(
                MakeRuntimeAssetIngestRequest(
                    request,
                    source));
        if (!submit.Succeeded())
        {
            Core::Log::Warn(
                "[Runtime] Geometry import rejected by ingest state machine: source={} path='{}' payload={} diagnostic={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                DebugNameForRuntimeAssetIngestDiagnostic(submit.Diagnostic),
                Core::Error::ToString(ErrorFromIngestTransition(submit)));
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(submit)),
                submit.Diagnostic);
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(submit));
        }

        const WorldHandle submissionWorld = m_World;
        ECS::Scene::Registry* const submissionScene = m_Scene.get();
        const std::uint64_t submissionBindingEpoch = m_TargetBindingEpoch;
        auto stageTrace = std::make_shared<AssetImportStageTrace>(
            AssetImportStageTrace{
                .Identity = AssetImportExecutionIdentity{
                    .Request = submit.Handle,
                    .World = submissionWorld,
                    .BindingGeneration = submissionBindingEpoch,
                    .CancellationGeneration = m_Jobs
                        ? m_Jobs->WorldGeneration(submissionWorld)
                        : 0u,
                },
            });
        if (!m_Initialized ||
            !m_Jobs ||
            !m_AssetService ||
            !m_GpuAssetCache ||
            !m_RenderExtraction ||
            !m_EditorCommandHistory ||
            !IsCurrentSubmissionTarget(
                submissionWorld,
                submissionScene,
                submissionBindingEpoch) ||
            request.Path.empty() ||
            payloadKinds.empty())
        {
            RuntimeAssetIngestTransition failed =
                m_AssetIngestStateMachine.FailCallback(
                    submit.Handle,
                    Core::ErrorCode::InvalidState);
            Core::Log::Warn(
                "[Runtime] Geometry import rejected before queueing: source={} path='{}' payload={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                Core::Error::ToString(Core::ErrorCode::InvalidState));
            AppendNextFailedStage(
                *stageTrace,
                Core::ErrorCode::InvalidState,
                failed.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
                failed.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                Core::ErrorCode::InvalidState);
        }

        const Assets::AssetRouteDiagnostic routeDiagnostic =
            Assets::DiagnoseAssetImportRoute(
                request.Path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        RuntimeAssetIngestTransition routeResolved =
            m_AssetIngestStateMachine.ResolveRoute(
                submit.Handle,
                routeDiagnostic);
        if (!routeResolved.Succeeded())
        {
            AppendImportStage(
                *stageTrace,
                AssetImportStage::Route,
                ErrorFromIngestTransition(routeResolved),
                routeResolved.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(routeResolved)),
                routeResolved.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(routeResolved));
        }
        AppendImportStage(*stageTrace, AssetImportStage::Route);

        RuntimeAssetIngestTransition decodeQueued =
            m_AssetIngestStateMachine.QueueDecode(submit.Handle);
        if (!decodeQueued.Succeeded())
        {
            AppendImportStage(
                *stageTrace,
                AssetImportStage::Decode,
                ErrorFromIngestTransition(decodeQueued),
                decodeQueued.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decodeQueued)),
                decodeQueued.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(decodeQueued));
        }

        RuntimeAssetIngestTransition decoding =
            m_AssetIngestStateMachine.MarkDecoding(submit.Handle);
        if (!decoding.Succeeded())
        {
            AppendImportStage(
                *stageTrace,
                AssetImportStage::Decode,
                ErrorFromIngestTransition(decoding),
                decoding.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decoding)),
                decoding.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(decoding));
        }

        auto state = std::make_shared<QueuedGeometryImportState>();
        state->IngestHandle = submit.Handle;
        state->Request = request;
        state->Recipe = std::move(recipe);
        state->StageTrace = stageTrace;
        const std::size_t candidateCount = payloadKinds.size();
        auto beforeDecodeHook =
            m_QueuedGeometryImportBeforeDecodeHookForTest;

        const JobToken handle = m_Jobs->Submit(
            JobDesc{
                .DebugName = "Runtime.ImportGeometry." +
                    FileNameFromPath(request.Path),
                .Scope = submissionWorld,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::AssetDecode,
                .EstimatedCost = 4u,
                .Work = [
                    state,
                    path = request.Path,
                    beforeDecodeHook = std::move(beforeDecodeHook),
                    payloadKinds = std::move(payloadKinds)](
                        const JobCancellation&) mutable
                {
                    if (beforeDecodeHook)
                        beforeDecodeHook(state->Request);
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
                            break;
                        }
                        lastError = decoded.error();
                    }

                    if (!state->Decoded.has_value())
                        state->Error = lastError;
                    return JobResultEnvelope::Make<QueuedImportDecodeDone>(
                        QueuedImportDecodeDone{
                            .Ingest = state->IngestHandle,
                        });
                },
                .PublishCompletion = [
                    this,
                    state,
                    submissionWorld,
                    submissionScene,
                    submissionBindingEpoch](
                        KernelEventBus&,
                        const JobResultEnvelope& envelope) -> bool
                {
                    const QueuedImportDecodeDone* const done =
                        envelope.TryGet<QueuedImportDecodeDone>();
                    if (done == nullptr || done->Ingest != state->IngestHandle)
                        return false;

                    Core::Expected<RuntimeAssetImportResult> result =
                        Core::Err<RuntimeAssetImportResult>(state->Error);
                    RuntimeAssetIngestDiagnostic eventDiagnostic =
                        DiagnosticForImportError(result.error());

                    if (!state->Decoded.has_value())
                    {
                        RuntimeAssetIngestTransition failed =
                            result.error() == Core::ErrorCode::FileNotFound
                                ? m_AssetIngestStateMachine.MarkMissingFile(
                                      state->IngestHandle)
                                : m_AssetIngestStateMachine.FailDecode(
                                      state->IngestHandle,
                                      state->IngestHandle.Generation,
                                      result.error());
                        eventDiagnostic = failed.Diagnostic;
                        AppendImportStage(
                            *state->StageTrace,
                            AssetImportStage::Decode,
                            result.error(),
                            eventDiagnostic);
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            eventDiagnostic,
                            state->StageTrace.get());
                        return true;
                    }

                    AppendImportStage(
                        *state->StageTrace,
                        AssetImportStage::Decode);

                    RuntimeAssetIngestTransition decodeComplete =
                        m_AssetIngestStateMachine.CompleteDecode(
                            state->IngestHandle,
                            state->IngestHandle.Generation);
                    if (!decodeComplete.Succeeded())
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            ErrorFromIngestTransition(decodeComplete));
                        AppendNextFailedStage(
                            *state->StageTrace,
                            result.error(),
                            decodeComplete.Diagnostic);
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            decodeComplete.Diagnostic,
                            state->StageTrace.get());
                        return true;
                    }

                    RuntimeAssetIngestTransition applying =
                        m_AssetIngestStateMachine.BeginApply(state->IngestHandle);
                    if (!applying.Succeeded())
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            ErrorFromIngestTransition(applying));
                        AppendNextFailedStage(
                            *state->StageTrace,
                            result.error(),
                            applying.Diagnostic);
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            applying.Diagnostic,
                            state->StageTrace.get());
                        return true;
                    }

                    if (!IsCurrentSubmissionTarget(
                            submissionWorld,
                            submissionScene,
                            submissionBindingEpoch))
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            Core::ErrorCode::InvalidState);
                        const RuntimeAssetIngestTransition failed =
                            m_AssetIngestStateMachine.FailApply(
                                state->IngestHandle,
                                state->IngestHandle.Generation,
                                Core::ErrorCode::InvalidState);
                        AppendNextFailedStage(
                            *state->StageTrace,
                            result.error(),
                            failed.Diagnostic);
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            failed.Diagnostic,
                            state->StageTrace.get());
                        return true;
                    }

                    auto materialized = MaterializeDecodedGeometryImport(
                        *m_AssetService,
                        *submissionScene,
                        m_Jobs.get(),
                        m_WorldRegistry.get(),
                        submissionWorld,
                        m_BindingValid,
                        m_TextureBake.get(),
                        state->Recipe,
                        *state->Decoded);
                    if (materialized.has_value())
                    {
                        const ECS::EntityHandle createdEntity =
                            materialized->Entity;
                        result = materialized->Result;
                        if (Core::Result completed =
                                ApplyAssetImportCompletionRecipe(
                                    state->Recipe.Completion
                                        .SelectFirstCreatedEntity,
                                    state->Recipe.Completion
                                        .FocusCameraOnCreatedGeometry,
                                    std::span<const ECS::EntityHandle>(
                                        &createdEntity,
                                        1u),
                                    materialized->FocusTarget,
                                    *submissionScene,
                                    m_SelectionController.get(),
                                    m_Config.get(),
                                    m_CameraControllers.get());
                            !completed.has_value())
                        {
                            result = Core::Err<RuntimeAssetImportResult>(
                                completed.error());
                        }
                        if (result.has_value() && RequestsGpuUpload(*result))
                        {
                            RuntimeAssetIngestTransition upload =
                                m_AssetIngestStateMachine.BeginGpuUpload(
                                    state->IngestHandle);
                            eventDiagnostic = upload.Diagnostic;
                            if (!upload.Succeeded())
                            {
                                result = Core::Err<RuntimeAssetImportResult>(
                                    ErrorFromIngestTransition(upload));
                            }
                        }
                        RuntimeAssetIngestTransition complete =
                            result.has_value()
                                ? m_AssetIngestStateMachine.CompleteApply(
                                      state->IngestHandle,
                                      state->IngestHandle.Generation,
                                      ToRuntimeAssetIngestResult(*result))
                                : RuntimeAssetIngestTransition{};
                        if (result.has_value())
                        {
                            eventDiagnostic = complete.Diagnostic;
                            if (!complete.Succeeded())
                            {
                                result = Core::Err<RuntimeAssetImportResult>(
                                    ErrorFromIngestTransition(complete));
                            }
                            else if (CreatesOrChangesScene(*result))
                            {
                                (void)m_EditorCommandHistory->MarkDirty("Import Asset");
                            }
                        }
                        if (result.has_value())
                        {
                            AppendSuccessfulApplyStages(*state->StageTrace);
                        }
                        else
                        {
                            AppendNextFailedStage(
                                *state->StageTrace,
                                result.error(),
                                eventDiagnostic);
                        }
                    }
                    else
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            materialized.error());
                        RuntimeAssetIngestTransition failed =
                            m_AssetIngestStateMachine.FailApply(
                                state->IngestHandle,
                                state->IngestHandle.Generation,
                                materialized.error());
                        eventDiagnostic = failed.Diagnostic;
                        AppendImportStage(
                            *state->StageTrace,
                            AssetImportStage::CpuMaterialize,
                            materialized.error(),
                            eventDiagnostic);
                    }
                    RecordAssetImportEvent(
                        state->Request,
                        result,
                        eventDiagnostic,
                        state->StageTrace.get());
                    return true;
                },
                .FinalizeUnpublishedOnMainThread = [
                    this,
                    operation = state->IngestHandle,
                    cancelledRequest = request,
                    stageTrace = state->StageTrace]() mutable
                {
                    FinalizeUnpublishedImport(
                        operation,
                        std::move(cancelledRequest),
                        stageTrace.get());
                },
            });

        if (!handle.IsValid())
        {
            RuntimeAssetImportRequest request{
                .Path = std::move(state->Request.Path),
                .PayloadKind = state->Request.PayloadKind,
            };
            Core::Log::Warn(
                "[Runtime] Geometry import queue submission failed: source={} path='{}' payload={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                Core::Error::ToString(Core::ErrorCode::InvalidState));
            RuntimeAssetIngestTransition failed =
                m_AssetIngestStateMachine.FailCallback(
                    state->IngestHandle,
                    Core::ErrorCode::InvalidState);
            AppendNextFailedStage(
                *stageTrace,
                Core::ErrorCode::InvalidState,
                failed.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
                failed.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                Core::ErrorCode::InvalidState);
        }

        m_AssetImportJobs.push_back(RuntimeAssetImportJobRecord{
            .Ingest = state->IngestHandle,
            .Job = handle,
            .StageTrace = stageTrace,
        });

        if (source == RuntimeAssetIngestSource::DroppedFile)
        {
            Core::Log::Info(
                "[Runtime] Queued dropped geometry import: path='{}' payload={} candidate_count={}",
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                candidateCount);
        }
        else
        {
            Core::Log::Info(
                "[Runtime] Queued geometry import: source={} path='{}' payload={} candidate_count={}",
                DebugNameForRuntimeAssetIngestSource(source),
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                candidateCount);
        }

        return RuntimeQueuedAssetImport{
            .Operation = state->IngestHandle,
            .PayloadKind = request.PayloadKind,
        };
    }

    Core::Expected<RuntimeQueuedAssetImport>
    AssetImportPipeline::QueueModelTextureImportWithIngest(
        AssetImportRecipe recipe)
    {
        const RuntimeAssetIngestSource source = recipe.Source;
        const Assets::AssetId existingAsset = recipe.ExistingAsset;
        RuntimeAssetImportRequest request{
            .Path = recipe.Path,
            .PayloadKind = recipe.PayloadKind,
        };
        auto route = Assets::ResolveAssetImportRoute(
            request.Path,
            Assets::AssetRouteOperation::Import,
            Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        if (!route.has_value())
        {
            return Core::Err<RuntimeQueuedAssetImport>(route.error());
        }
        if (!IsModelTextureImportPayload(route->PayloadKind))
        {
            return Core::Err<RuntimeQueuedAssetImport>(
                Core::ErrorCode::AssetTypeMismatch);
        }
        request.PayloadKind = route->PayloadKind;

        const Assets::AssetRouteDiagnostic routeDiagnostic =
            Assets::DiagnoseAssetImportRoute(
                request.Path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        RuntimeAssetIngestTransition submit =
            m_AssetIngestStateMachine.Submit(
                MakeRuntimeAssetIngestRequest(
                    request,
                    source,
                    existingAsset));
        if (!submit.Succeeded())
        {
            Core::Log::Warn(
                "[Runtime] Model/texture import rejected by ingest state machine: source={} path='{}' payload={} diagnostic={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                DebugNameForRuntimeAssetIngestDiagnostic(submit.Diagnostic),
                Core::Error::ToString(ErrorFromIngestTransition(submit)));
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(submit)),
                submit.Diagnostic);
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(submit));
        }

        const WorldHandle submissionWorld = m_World;
        ECS::Scene::Registry* const submissionScene = m_Scene.get();
        const std::uint64_t submissionBindingEpoch = m_TargetBindingEpoch;
        auto stageTrace = std::make_shared<AssetImportStageTrace>(
            AssetImportStageTrace{
                .Identity = AssetImportExecutionIdentity{
                    .Request = submit.Handle,
                    .World = submissionWorld,
                    .BindingGeneration = submissionBindingEpoch,
                    .CancellationGeneration = m_Jobs
                        ? m_Jobs->WorldGeneration(submissionWorld)
                        : 0u,
                },
            });
        if (!m_Initialized ||
            !m_Jobs ||
            !m_AssetService ||
            !m_GpuAssetCache ||
            !m_AssetModelTextureHandoff ||
            !m_AssetModelSceneHandoff ||
            !IsCurrentSubmissionTarget(
                submissionWorld,
                submissionScene,
                submissionBindingEpoch) ||
            request.Path.empty() ||
            !IsModelTextureImportPayload(request.PayloadKind))
        {
            RuntimeAssetIngestTransition failed =
                m_AssetIngestStateMachine.FailCallback(
                    submit.Handle,
                    Core::ErrorCode::InvalidState);
            Core::Log::Warn(
                "[Runtime] Model/texture import rejected before queueing: source={} path='{}' payload={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                request.Path,
                Assets::DebugNameForAssetPayloadKind(request.PayloadKind),
                Core::Error::ToString(Core::ErrorCode::InvalidState));
            AppendNextFailedStage(
                *stageTrace,
                Core::ErrorCode::InvalidState,
                failed.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
                failed.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                Core::ErrorCode::InvalidState);
        }

        RuntimeAssetIngestTransition routeResolved =
            m_AssetIngestStateMachine.ResolveRoute(
                submit.Handle,
                routeDiagnostic);
        if (!routeResolved.Succeeded())
        {
            AppendImportStage(
                *stageTrace,
                AssetImportStage::Route,
                ErrorFromIngestTransition(routeResolved),
                routeResolved.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(routeResolved)),
                routeResolved.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(routeResolved));
        }
        AppendImportStage(*stageTrace, AssetImportStage::Route);

        RuntimeAssetIngestTransition decodeQueued =
            m_AssetIngestStateMachine.QueueDecode(submit.Handle);
        if (!decodeQueued.Succeeded())
        {
            AppendImportStage(
                *stageTrace,
                AssetImportStage::Decode,
                ErrorFromIngestTransition(decodeQueued),
                decodeQueued.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decodeQueued)),
                decodeQueued.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(decodeQueued));
        }

        RuntimeAssetIngestTransition decoding =
            m_AssetIngestStateMachine.MarkDecoding(submit.Handle);
        if (!decoding.Succeeded())
        {
            AppendImportStage(
                *stageTrace,
                AssetImportStage::Decode,
                ErrorFromIngestTransition(decoding),
                decoding.Diagnostic);
            RecordAssetImportEvent(
                request,
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decoding)),
                decoding.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                ErrorFromIngestTransition(decoding));
        }

        auto state = std::make_shared<DroppedModelTextureImportState>();
        state->IngestHandle = submit.Handle;
        state->Request = request;
        state->Recipe = std::move(recipe);
        state->StageTrace = stageTrace;
        RuntimeIOBackendFactory ioBackendFactory =
            m_ModelTextureImportIOBackendFactoryForTest;

        const JobToken handle = m_Jobs->Submit(
            JobDesc{
                .DebugName = "Runtime.ImportModelTexture." +
                    FileNameFromPath(request.Path),
                .Scope = submissionWorld,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Kind = RuntimeTaskKinds::AssetDecode,
                .EstimatedCost = 4u,
                .Work = [
                    state,
                    ioBackendFactory = std::move(ioBackendFactory),
                    path = request.Path,
                    payloadKind = request.PayloadKind](
                        const JobCancellation&) mutable
                {
                    const auto decodeDone = [&state]
                    {
                        return JobResultEnvelope::Make<QueuedImportDecodeDone>(
                            QueuedImportDecodeDone{
                                .Ingest = state->IngestHandle,
                            });
                    };

                    std::unique_ptr<Core::IO::IIOBackend> backend =
                        ioBackendFactory
                            ? ioBackendFactory()
                            : std::make_unique<Core::IO::FileIOBackend>();
                    if (!backend)
                    {
                        state->Error = Core::ErrorCode::InvalidState;
                        return decodeDone();
                    }

                    if (payloadKind == Assets::AssetPayloadKind::ModelScene)
                    {
                        auto decoded = DecodeModelSceneAsset(path, *backend);
                        if (!decoded.has_value())
                        {
                            state->Error = decoded.error();
                            return decodeDone();
                        }

                        state->Decoded = DecodedModelTextureImport{
                            .Path = path,
                            .PayloadKind = payloadKind,
                            .Payload = std::move(*decoded),
                        };
                    }
                    else
                    {
                        auto decoded = DecodeTextureAsset(path, *backend);
                        if (!decoded.has_value())
                        {
                            state->Error = decoded.error();
                            return decodeDone();
                        }

                        state->Decoded = DecodedModelTextureImport{
                            .Path = path,
                            .PayloadKind = payloadKind,
                            .Payload = std::move(*decoded),
                        };
                    }

                    state->Error = Core::ErrorCode::Success;
                    return decodeDone();
                },
                .PublishCompletion = [
                    this,
                    state,
                    existingAsset,
                    submissionWorld,
                    submissionScene,
                    submissionBindingEpoch](
                        KernelEventBus&,
                        const JobResultEnvelope& envelope) -> bool
                {
                    const QueuedImportDecodeDone* const done =
                        envelope.TryGet<QueuedImportDecodeDone>();
                    if (done == nullptr || done->Ingest != state->IngestHandle)
                        return false;

                    Core::Expected<RuntimeAssetImportResult> result =
                        Core::Err<RuntimeAssetImportResult>(state->Error);
                    RuntimeAssetIngestDiagnostic eventDiagnostic =
                        DiagnosticForImportError(result.error());

                    if (!state->Decoded.has_value())
                    {
                        RuntimeAssetIngestTransition failed{};
                        if (result.error() == Core::ErrorCode::FileNotFound)
                        {
                            failed = m_AssetIngestStateMachine.MarkMissingFile(
                                state->IngestHandle);
                        }
                        else if (result.error() == Core::ErrorCode::AssetLoaderMissing)
                        {
                            failed = m_AssetIngestStateMachine.FailCallback(
                                state->IngestHandle,
                                result.error());
                        }
                        else
                        {
                            failed = m_AssetIngestStateMachine.FailDecode(
                                state->IngestHandle,
                                state->IngestHandle.Generation,
                                result.error());
                        }
                        eventDiagnostic = failed.Diagnostic;
                        AppendImportStage(
                            *state->StageTrace,
                            AssetImportStage::Decode,
                            result.error(),
                            eventDiagnostic);
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            eventDiagnostic,
                            state->StageTrace.get());
                        return true;
                    }

                    AppendImportStage(
                        *state->StageTrace,
                        AssetImportStage::Decode);

                    RuntimeAssetIngestTransition decodeComplete =
                        m_AssetIngestStateMachine.CompleteDecode(
                            state->IngestHandle,
                            state->IngestHandle.Generation);
                    if (!decodeComplete.Succeeded())
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            ErrorFromIngestTransition(decodeComplete));
                        AppendNextFailedStage(
                            *state->StageTrace,
                            result.error(),
                            decodeComplete.Diagnostic);
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            decodeComplete.Diagnostic,
                            state->StageTrace.get());
                        return true;
                    }

                    RuntimeAssetIngestTransition applying =
                        m_AssetIngestStateMachine.BeginApply(state->IngestHandle);
                    if (!applying.Succeeded())
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            ErrorFromIngestTransition(applying));
                        AppendNextFailedStage(
                            *state->StageTrace,
                            result.error(),
                            applying.Diagnostic);
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            applying.Diagnostic,
                            state->StageTrace.get());
                        return true;
                    }

                    if (!IsCurrentSubmissionTarget(
                            submissionWorld,
                            submissionScene,
                            submissionBindingEpoch))
                    {
                        result = Core::Err<RuntimeAssetImportResult>(
                            Core::ErrorCode::InvalidState);
                        const RuntimeAssetIngestTransition failed =
                            m_AssetIngestStateMachine.FailApply(
                                state->IngestHandle,
                                state->IngestHandle.Generation,
                                Core::ErrorCode::InvalidState);
                        AppendNextFailedStage(
                            *state->StageTrace,
                            result.error(),
                            failed.Diagnostic);
                        RecordAssetImportEvent(
                            state->Request,
                            result,
                            failed.Diagnostic,
                            state->StageTrace.get());
                        return true;
                    }

                    if (state->Decoded->PayloadKind ==
                        Assets::AssetPayloadKind::ModelScene)
                    {
                        result = MaterializeDecodedModelSceneImport(
                            *m_AssetService,
                            *m_AssetModelSceneHandoff,
                            *submissionScene,
                            state->Recipe,
                            m_SelectionController.get(),
                            m_CameraControllers.get(),
                            m_Config.get(),
                            state->Request,
                            existingAsset,
                            std::move(std::get<Assets::AssetModelScenePayload>(
                                state->Decoded->Payload)));
                    }
                    else
                    {
                        result = MaterializeDecodedTextureImport(
                            *m_AssetService,
                            *m_GpuAssetCache,
                            *m_AssetModelTextureHandoff,
                            state->Request,
                            existingAsset,
                            std::move(std::get<Assets::AssetTexture2DPayload>(
                                state->Decoded->Payload)));
                    }

                    if (result.has_value())
                    {
                        if (RequestsGpuUpload(*result))
                        {
                            RuntimeAssetIngestTransition upload =
                                m_AssetIngestStateMachine.BeginGpuUpload(
                                    state->IngestHandle);
                            eventDiagnostic = upload.Diagnostic;
                            if (!upload.Succeeded())
                            {
                                result = Core::Err<RuntimeAssetImportResult>(
                                    ErrorFromIngestTransition(upload));
                            }
                        }

                        RuntimeAssetIngestTransition complete =
                            result.has_value()
                                ? m_AssetIngestStateMachine.CompleteApply(
                                      state->IngestHandle,
                                      state->IngestHandle.Generation,
                                      ToRuntimeAssetIngestResult(*result))
                                : RuntimeAssetIngestTransition{};
                        if (result.has_value())
                        {
                            eventDiagnostic = complete.Diagnostic;
                            if (!complete.Succeeded())
                            {
                                result = Core::Err<RuntimeAssetImportResult>(
                                    ErrorFromIngestTransition(complete));
                            }
                            else if (CreatesOrChangesScene(*result))
                            {
                                (void)m_EditorCommandHistory->MarkDirty("Import Asset");
                            }
                        }
                        if (result.has_value())
                        {
                            AppendSuccessfulApplyStages(*state->StageTrace);
                        }
                        else
                        {
                            AppendNextFailedStage(
                                *state->StageTrace,
                                result.error(),
                                eventDiagnostic);
                        }
                    }
                    else
                    {
                        RuntimeAssetIngestTransition failed =
                            m_AssetIngestStateMachine.FailApply(
                                state->IngestHandle,
                                state->IngestHandle.Generation,
                                result.error());
                        eventDiagnostic = failed.Diagnostic;
                        AppendImportStage(
                            *state->StageTrace,
                            AssetImportStage::CpuMaterialize,
                            result.error(),
                            eventDiagnostic);
                    }

                    RecordAssetImportEvent(
                        state->Request,
                        result,
                        eventDiagnostic,
                        state->StageTrace.get());
                    return true;
                },

                .FinalizeUnpublishedOnMainThread = [
                    this,
                    operation = state->IngestHandle,
                    cancelledRequest = request,
                    stageTrace = state->StageTrace]() mutable
                {
                    FinalizeUnpublishedImport(
                        operation,
                        std::move(cancelledRequest),
                        stageTrace.get());
                },
            });

        if (!handle.IsValid())
        {
            RuntimeAssetImportRequest queuedRequest{
                .Path = state->Request.Path,
                .PayloadKind = state->Request.PayloadKind,
            };
            Core::Log::Warn(
                "[Runtime] Model/texture import queue submission failed: source={} path='{}' payload={} error={}",
                DebugNameForRuntimeAssetIngestSource(source),
                queuedRequest.Path,
                Assets::DebugNameForAssetPayloadKind(queuedRequest.PayloadKind),
                Core::Error::ToString(Core::ErrorCode::InvalidState));
            RuntimeAssetIngestTransition failed =
                m_AssetIngestStateMachine.FailCallback(
                    state->IngestHandle,
                    Core::ErrorCode::InvalidState);
            AppendNextFailedStage(
                *stageTrace,
                Core::ErrorCode::InvalidState,
                failed.Diagnostic);
            RecordAssetImportEvent(
                queuedRequest,
                Core::Err<RuntimeAssetImportResult>(Core::ErrorCode::InvalidState),
                failed.Diagnostic,
                stageTrace.get());
            return Core::Err<RuntimeQueuedAssetImport>(
                Core::ErrorCode::InvalidState);
        }

        m_AssetImportJobs.push_back(RuntimeAssetImportJobRecord{
            .Ingest = state->IngestHandle,
            .Job = handle,
            .StageTrace = stageTrace,
        });

        Core::Log::Info(
            "[Runtime] Queued model/texture import: source={} path='{}' payload={}",
            DebugNameForRuntimeAssetIngestSource(source),
            state->Request.Path,
            Assets::DebugNameForAssetPayloadKind(state->Request.PayloadKind));

        return RuntimeQueuedAssetImport{
            .Operation = state->IngestHandle,
            .PayloadKind = state->Request.PayloadKind,
        };
    }

    void AssetImportPipeline::QueueDroppedModelTextureImport(
        std::string path,
        const Assets::AssetPayloadKind payloadKind)
    {
        (void)QueueModelTextureImportWithIngest(
            AssetImportRecipe{
                .Path = std::move(path),
                .PayloadKind = payloadKind,
                .Source = RuntimeAssetIngestSource::DroppedFile,
            });
    }

    Core::Expected<RuntimeAssetImportResult>
    AssetImportPipeline::ImportAssetFromPathWithIngest(
        RuntimeAssetImportRequest request,
        const RuntimeAssetIngestSource source,
        const Assets::AssetId existingAsset)
    {
        RuntimeAssetIngestTransition submit =
            m_AssetIngestStateMachine.Submit(
                MakeRuntimeAssetIngestRequest(request, source, existingAsset));
        if (!submit.Succeeded())
        {
            Core::Expected<RuntimeAssetImportResult> result =
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(submit));
            RecordAssetImportEvent(request, result, submit.Diagnostic);
            return result;
        }

        if (source == RuntimeAssetIngestSource::Reimport)
        {
            Core::ErrorCode targetError = Core::ErrorCode::Success;
            if (!existingAsset.IsValid())
            {
                targetError = Core::ErrorCode::InvalidArgument;
            }
            else if (!m_AssetService || !m_AssetService->IsAlive(existingAsset))
            {
                targetError = Core::ErrorCode::ResourceNotFound;
            }
            else if (request.Path == "<invalid-reimport-target>")
            {
                targetError = Core::ErrorCode::ResourceNotFound;
            }
            else if (request.PayloadKind == Assets::AssetPayloadKind::Unknown)
            {
                targetError = Core::ErrorCode::AssetTypeMismatch;
            }

            if (targetError != Core::ErrorCode::Success)
            {
                RuntimeAssetIngestTransition failed =
                    m_AssetIngestStateMachine.MarkInvalidReimportTarget(
                        submit.Handle,
                        targetError);
                Core::Expected<RuntimeAssetImportResult> result =
                    Core::Err<RuntimeAssetImportResult>(targetError);
                RecordAssetImportEvent(request, result, failed.Diagnostic);
                return result;
            }
        }

        const Assets::AssetRouteDiagnostic routeDiagnostic =
            Assets::DiagnoseAssetImportRoute(
                request.Path,
                Assets::AssetRouteOperation::Import,
                Assets::AssetImportHint{.PayloadKind = request.PayloadKind});
        RuntimeAssetIngestTransition routeResolved =
            m_AssetIngestStateMachine.ResolveRoute(
                submit.Handle,
                routeDiagnostic);
        if (!routeResolved.Succeeded())
        {
            Core::Expected<RuntimeAssetImportResult> result =
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(routeResolved));
            RecordAssetImportEvent(
                request,
                result,
                routeResolved.Diagnostic);
            return result;
        }

        RuntimeAssetIngestTransition decodeQueued =
            m_AssetIngestStateMachine.QueueDecode(submit.Handle);
        if (!decodeQueued.Succeeded())
        {
            Core::Expected<RuntimeAssetImportResult> result =
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decodeQueued));
            RecordAssetImportEvent(
                request,
                result,
                decodeQueued.Diagnostic);
            return result;
        }

        RuntimeAssetIngestTransition decoding =
            m_AssetIngestStateMachine.MarkDecoding(submit.Handle);
        if (!decoding.Succeeded())
        {
            Core::Expected<RuntimeAssetImportResult> result =
                Core::Err<RuntimeAssetImportResult>(
                    ErrorFromIngestTransition(decoding));
            RecordAssetImportEvent(request, result, decoding.Diagnostic);
            return result;
        }

        Core::Expected<RuntimeAssetImportResult> result =
            ImportAssetFromPathImpl(request, existingAsset);
        RuntimeAssetIngestDiagnostic eventDiagnostic =
            result.has_value()
                ? RuntimeAssetIngestDiagnostic::None
                : DiagnosticForImportError(result.error(), source);
        if (!result.has_value())
        {
            RuntimeAssetIngestTransition failed{};
            switch (eventDiagnostic)
            {
            case RuntimeAssetIngestDiagnostic::MissingFile:
                failed = m_AssetIngestStateMachine.MarkMissingFile(submit.Handle);
                break;
            case RuntimeAssetIngestDiagnostic::InvalidReimportTarget:
                failed = m_AssetIngestStateMachine.MarkInvalidReimportTarget(
                    submit.Handle,
                    result.error());
                break;
            case RuntimeAssetIngestDiagnostic::CallbackFailed:
                failed = m_AssetIngestStateMachine.FailCallback(
                    submit.Handle,
                    result.error());
                break;
            case RuntimeAssetIngestDiagnostic::MaterializationFailed:
            {
                RuntimeAssetIngestTransition decodeComplete =
                    m_AssetIngestStateMachine.CompleteDecode(
                        submit.Handle,
                        submit.Handle.Generation);
                if (!decodeComplete.Succeeded())
                {
                    failed = decodeComplete;
                    break;
                }
                RuntimeAssetIngestTransition applying =
                    m_AssetIngestStateMachine.BeginApply(submit.Handle);
                if (!applying.Succeeded())
                {
                    failed = applying;
                    break;
                }
                failed = m_AssetIngestStateMachine.FailApply(
                    submit.Handle,
                    submit.Handle.Generation,
                    result.error());
                break;
            }
            default:
                failed = m_AssetIngestStateMachine.FailDecode(
                    submit.Handle,
                    submit.Handle.Generation,
                    result.error());
                break;
            }
            eventDiagnostic = failed.Diagnostic;
            RecordAssetImportEvent(request, result, eventDiagnostic);
            return result;
        }

        RuntimeAssetIngestTransition decodeComplete =
            m_AssetIngestStateMachine.CompleteDecode(
                submit.Handle,
                submit.Handle.Generation);
        if (!decodeComplete.Succeeded())
        {
            result = Core::Err<RuntimeAssetImportResult>(
                ErrorFromIngestTransition(decodeComplete));
            RecordAssetImportEvent(
                request,
                result,
                decodeComplete.Diagnostic);
            return result;
        }

        RuntimeAssetIngestTransition applying =
            m_AssetIngestStateMachine.BeginApply(submit.Handle);
        if (!applying.Succeeded())
        {
            result = Core::Err<RuntimeAssetImportResult>(
                ErrorFromIngestTransition(applying));
            RecordAssetImportEvent(request, result, applying.Diagnostic);
            return result;
        }

        RuntimeAssetIngestTransition complete =
            RequestsGpuUpload(*result)
                ? m_AssetIngestStateMachine.BeginGpuUpload(submit.Handle)
                : RuntimeAssetIngestTransition{};
        if (RequestsGpuUpload(*result) && !complete.Succeeded())
        {
            result = Core::Err<RuntimeAssetImportResult>(
                ErrorFromIngestTransition(complete));
            RecordAssetImportEvent(request, result, complete.Diagnostic);
            return result;
        }
        complete = m_AssetIngestStateMachine.CompleteApply(
            submit.Handle,
            submit.Handle.Generation,
            ToRuntimeAssetIngestResult(*result));
        eventDiagnostic = complete.Diagnostic;
        if (!complete.Succeeded())
        {
            result = Core::Err<RuntimeAssetImportResult>(
                ErrorFromIngestTransition(complete));
        }
        RecordAssetImportEvent(request, result, eventDiagnostic);
        return result;
    }

    void AssetImportPipeline::RecordAssetImportEvent(
        const RuntimeAssetImportRequest& request,
        const Core::Expected<RuntimeAssetImportResult>& result,
        const RuntimeAssetIngestDiagnostic ingestDiagnostic,
        const AssetImportStageTrace* const stageTrace)
    {
        RuntimeAssetImportEvent event{};
        event.Sequence = ++m_AssetImportEventSequence;
        event.Path = request.Path;
        event.RequestedPayloadKind = request.PayloadKind;
        event.Error = result.has_value()
            ? Core::ErrorCode::Success
            : result.error();
        event.IngestDiagnostic = ingestDiagnostic;
        if (stageTrace != nullptr)
            event.StageTrace = *stageTrace;
        if (result.has_value())
        {
            event.Result = *result;
            Core::Log::Info(
                "[Runtime] Asset import succeeded: path='{}' "
                "requested_payload={} "
                "result_payload={} ingest_diagnostic={} primitive_entities={} "
                "embedded_textures={} texture_upload_requests={} "
                "materialized_model_scene={} requested_texture_upload={}",
                event.Path,
                Assets::DebugNameForAssetPayloadKind(event.RequestedPayloadKind),
                Assets::DebugNameForAssetPayloadKind(result->PayloadKind),
                DebugNameForRuntimeAssetIngestDiagnostic(event.IngestDiagnostic),
                result->PrimitiveEntitiesCreated,
                result->EmbeddedTextureAssetsCreated,
                result->TextureUploadRequests,
                result->MaterializedModelScene,
                result->RequestedTextureUpload);
        }
        else
        {
            Core::Log::Warn(
                "[Runtime] Asset import failed: path='{}' requested_payload={} ingest_diagnostic={} error={}",
                event.Path,
                Assets::DebugNameForAssetPayloadKind(event.RequestedPayloadKind),
                DebugNameForRuntimeAssetIngestDiagnostic(event.IngestDiagnostic),
                Core::Error::ToString(event.Error));
        }
        m_LastAssetImportEvent = std::move(event);
    }

    Core::Expected<RuntimeAssetImportResult> AssetImportPipeline::ImportAssetFromPathImpl(
        RuntimeAssetImportRequest request,
        const Assets::AssetId existingAsset)
    {
        if (!m_Initialized ||
            !m_AssetService ||
            !m_GpuAssetCache ||
            !m_AssetModelTextureHandoff ||
            !m_AssetModelSceneHandoff ||
            !m_Scene ||
            !IsCurrentSubmissionTarget(
                m_World,
                m_Scene.get(),
                m_TargetBindingEpoch))
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
            const AssetImportRecipe recipe{
                .Path = request.Path,
                .PayloadKind = route->PayloadKind,
                .ExistingAsset = existingAsset,
            };
            auto decoded = DecodeGeometryImport(
                RuntimeAssetImportRequest{
                    .Path = request.Path,
                    .PayloadKind = route->PayloadKind,
                });
            if (!decoded.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(decoded.error());
            }

            if (existingAsset.IsValid())
            {
                return ReloadDecodedGeometryImport(
                    *m_AssetService,
                    existingAsset,
                    *decoded);
            }

            auto materialized = MaterializeDecodedGeometryImport(
                *m_AssetService,
                *m_Scene,
                m_Jobs.get(),
                m_WorldRegistry.get(),
                m_World,
                m_BindingValid,
                m_TextureBake.get(),
                recipe,
                *decoded);
            if (!materialized.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(
                    materialized.error());
            }
            const ECS::EntityHandle createdEntity = materialized->Entity;
            if (Core::Result completed = ApplyAssetImportCompletionRecipe(
                    recipe.Completion.SelectFirstCreatedEntity,
                    recipe.Completion.FocusCameraOnCreatedGeometry,
                    std::span<const ECS::EntityHandle>(&createdEntity, 1u),
                    materialized->FocusTarget,
                    *m_Scene,
                    m_SelectionController.get(),
                    m_Config.get(),
                    m_CameraControllers.get());
                !completed.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(completed.error());
            }
            return materialized->Result;
        }
        if (route->PayloadKind != Assets::AssetPayloadKind::ModelScene &&
            route->PayloadKind != Assets::AssetPayloadKind::Texture2D)
        {
            return Core::Err<RuntimeAssetImportResult>(
                Core::ErrorCode::AssetUnsupportedFormat);
        }

        Core::IO::FileIOBackend backend;
        if (route->PayloadKind == Assets::AssetPayloadKind::ModelScene)
        {
            const AssetImportRecipe recipe{
                .Path = request.Path,
                .PayloadKind = route->PayloadKind,
                .ExistingAsset = existingAsset,
            };
            auto decoded = DecodeModelSceneAsset(request.Path, backend);
            if (!decoded.has_value())
            {
                return Core::Err<RuntimeAssetImportResult>(decoded.error());
            }

            return MaterializeDecodedModelSceneImport(
                *m_AssetService,
                *m_AssetModelSceneHandoff,
                *m_Scene,
                recipe,
                m_SelectionController.get(),
                m_CameraControllers.get(),
                m_Config.get(),
                request,
                existingAsset,
                std::move(*decoded));
        }

        auto decoded = DecodeTextureAsset(request.Path, backend);
        if (!decoded.has_value())
        {
            return Core::Err<RuntimeAssetImportResult>(decoded.error());
        }

        return MaterializeDecodedTextureImport(
            *m_AssetService,
            *m_GpuAssetCache,
            *m_AssetModelTextureHandoff,
            request,
            existingAsset,
            std::move(*decoded));
    }
}
