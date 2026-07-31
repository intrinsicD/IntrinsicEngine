module;

#include <functional>
#include <optional>
#include <span>
#include <string>

export module Extrinsic.Runtime.AssetImportPolicies;

import Extrinsic.Asset.Registry;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.HalfedgeMesh.IO;

export namespace Extrinsic::Runtime
{
    struct AssetImportMeshEnrichmentState
    {
        JobToken Job{};
        JobState Status{JobState::Invalid};
        std::string Diagnostic{};
    };

    [[nodiscard]] Core::Result ApplyAssetImportAuthoringRecipe(
        Assets::AssetPayloadKind payloadKind,
        bool authorRenderableComponents,
        bool authorSelectableIdentity,
        ECS::EntityHandle entity,
        ECS::Scene::Registry& scene);

    [[nodiscard]] Core::Result ApplyAssetImportCompletionRecipe(
        bool selectFirstCreatedEntity,
        bool focusCameraOnCreatedGeometry,
        std::span<const ECS::EntityHandle> createdEntities,
        const std::optional<CameraFocusTarget>& focusTarget,
        ECS::Scene::Registry& scene,
        SelectionController* selection,
        const Core::Config::EngineConfig* config,
        CameraControllerRegistry* cameraControllers);

    void QueueAssetImportDirectMeshPostprocess(
        JobService* jobs,
        WorldRegistry* worlds,
        WorldHandle world,
        std::function<bool()> bindingValid,
        ECS::Scene::Registry& scene,
        TextureBakeService* textureBake,
        std::string path,
        const Geometry::MeshIO::MeshIOResult& payload,
        ECS::EntityHandle entity);
}
