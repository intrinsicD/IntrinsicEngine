module;

#include <cstdint>
#include <string>
#include <utility>

module Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;

import Extrinsic.Asset.Registry;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.StableEntityLookup;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] RuntimeObjectSpaceNormalBakeBindingResult BindingFail(
            const RuntimeObjectSpaceNormalBakeBindingStatus status,
            std::string diagnostic,
            RuntimeObjectSpaceNormalBakeResult completion = {})
        {
            return RuntimeObjectSpaceNormalBakeBindingResult{
                .Status = status,
                .Completion = std::move(completion),
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] GeometryPresentationSlotRecipe* FindUniqueTargetSlot(
            GeometryPresentationRecipe& bindings,
            const RuntimeObjectSpaceNormalBakeTarget& target) noexcept
        {
            GeometryPresentationBindingRecipe* matchingPresentation = nullptr;
            for (GeometryPresentationBindingRecipe& presentation :
                 bindings.Presentations)
            {
                if (presentation.Key != target.PresentationKey)
                    continue;
                if (matchingPresentation != nullptr)
                    return nullptr;
                matchingPresentation = &presentation;
            }

            if (matchingPresentation == nullptr ||
                matchingPresentation->Kind !=
                    GeometryPresentationKind::SurfaceMaterial)
                return nullptr;

            GeometryPresentationSlotRecipe* match = nullptr;
            for (GeometryPresentationSlotRecipe& slot :
                 matchingPresentation->Slots)
            {
                if (slot.Semantic != target.Semantic)
                    continue;
                if (match != nullptr)
                    return nullptr;
                match = &slot;
            }
            return match;
        }
    }

    const char* DebugNameForRuntimeObjectSpaceNormalBakeBindingStatus(
        const RuntimeObjectSpaceNormalBakeBindingStatus status) noexcept
    {
        switch (status)
        {
        case RuntimeObjectSpaceNormalBakeBindingStatus::Bound:
            return "Bound";
        case RuntimeObjectSpaceNormalBakeBindingStatus::WaitingForGpuTexture:
            return "WaitingForGpuTexture";
        case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidContext:
            return "InvalidContext";
        case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity:
            return "InvalidStableEntity";
        case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion:
            return "InvalidCompletion";
        case RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion:
            return "StaleCompletion";
        case RuntimeObjectSpaceNormalBakeBindingStatus::StaleScene:
            return "StaleScene";
        case RuntimeObjectSpaceNormalBakeBindingStatus::StaleGeometry:
            return "StaleGeometry";
        case RuntimeObjectSpaceNormalBakeBindingStatus::StalePresentationState:
            return "StalePresentationState";
        }
        return "Unknown";
    }

    RuntimeObjectSpaceNormalBakeBindingResult TryBindReadyObjectSpaceNormalBake(
        const RuntimeObjectSpaceNormalBakeBindingContext& context,
        const RuntimeObjectSpaceNormalBakeCompletion& completion)
    {
        if (context.Queue == nullptr ||
            context.Extraction == nullptr ||
            context.GpuAssets == nullptr ||
            context.GpuWorld == nullptr ||
            context.Scene == nullptr ||
            !context.World.IsValid() ||
            context.BindingEpoch == 0u)
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::InvalidContext,
                "object-space normal bake binding context is incomplete");
        }

        const RuntimeObjectSpaceNormalBakeTarget& target =
            completion.StaleKey.Target;
        if (!IsValidRuntimeObjectSpaceNormalBakeTarget(target) ||
            completion.StaleKey.RequestGeneration == 0u ||
            completion.Identity == nullptr ||
            !completion.GeneratedTextureAsset.IsValid() ||
            completion.CacheGeneration == 0u ||
            completion.GeometryContentRevision == 0u ||
            completion.AssetSelection ==
                RuntimeObjectSpaceNormalBakeAssetSelection::None ||
            ((target.ExpectedRecipeGeneration == 0u) !=
             target.PresentationKey.empty()))
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion,
                "object-space normal bake completion is missing exact target, identity, asset provenance, cache generation, geometry revision, or coherent presentation state");
        }

        if (target.World != context.World ||
            target.BindingEpoch != context.BindingEpoch)
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::StaleScene,
                "object-space normal bake completion belongs to a replaced world binding");
        }

        if (!context.Scene->IsValid(target.Entity) ||
            StableEntityLookup::ToRenderId(target.Entity) !=
                target.StableEntityId)
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity,
                "object-space normal bake target entity is destroyed, recycled, or has a mismatched render id");
        }

        if (!context.Queue->IsLatest(completion.StaleKey))
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion,
                "object-space normal bake completion is no longer the latest target request");
        }

        const Graphics::GpuAssetState gpuState =
            context.GpuAssets->GetState(
                completion.GeneratedTextureAsset);
        if (gpuState != Graphics::GpuAssetState::Ready)
        {
            if (gpuState == Graphics::GpuAssetState::Failed ||
                gpuState == Graphics::GpuAssetState::NotRequested)
            {
                return BindingFail(
                    RuntimeObjectSpaceNormalBakeBindingStatus::
                        StaleCompletion,
                    "object-space normal bake exact texture generation is no longer resident");
            }
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::WaitingForGpuTexture,
                "object-space normal bake texture generation is not ready in GpuAssetCache");
        }

        const auto view =
            context.GpuAssets->GetView(completion.GeneratedTextureAsset);
        if (!view.has_value() ||
            view->Kind != Graphics::GpuAssetKind::Texture ||
            view->Generation != completion.CacheGeneration)
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion,
                "object-space normal bake ready texture view does not match the exact submitted generation");
        }

        const auto renderable =
            context.Extraction->FindGpuRenderableAvailability(
                target.StableEntityId);
        if (!renderable.has_value() ||
            !renderable->HasRenderable ||
            !renderable->Surface.HasGeometry)
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::StaleGeometry,
                "object-space normal bake target no longer has a resident surface geometry");
        }

        Graphics::GpuGeometryResidencyView residency{};
        if (!context.GpuWorld->TryGetGeometryResidencyView(
                renderable->Surface.Geometry,
                residency))
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::StaleGeometry,
                "object-space normal bake target geometry is no longer resident");
        }
        const RuntimeObjectSpaceNormalBakeResidencyResult residencyValidation =
            ValidateObjectSpaceNormalBakeResidency(
                *completion.Identity,
                residency,
                completion.GeometryContentRevision);
        if (!residencyValidation.Succeeded())
        {
            return BindingFail(
                RuntimeObjectSpaceNormalBakeBindingStatus::StaleGeometry,
                residencyValidation.Diagnostic);
        }

        auto* currentRecipe =
            context.Scene->Raw().try_get<GeometryPresentationRecipe>(
                target.Entity);
        auto* currentState =
            context.Scene->Raw().try_get<GeometryPresentationRuntimeState>(
                target.Entity);
        GeometryPresentationRuntimeState stagedState{};
        bool updatePresentation = false;
        if (target.ExpectedRecipeGeneration == 0u)
        {
            if (currentRecipe != nullptr || currentState != nullptr)
            {
                return BindingFail(
                    RuntimeObjectSpaceNormalBakeBindingStatus::
                        StalePresentationState,
                    "object-space normal bake target gained geometry-presentation state after scheduling");
            }
        }
        else
        {
            if (currentRecipe == nullptr || currentState == nullptr ||
                currentState->RecipeGeneration !=
                    target.ExpectedRecipeGeneration ||
                target.PresentationKey.empty())
            {
                return BindingFail(
                    RuntimeObjectSpaceNormalBakeBindingStatus::
                        StalePresentationState,
                    "object-space normal bake geometry-presentation generation changed");
            }

            GeometryPresentationRecipe recipe = *currentRecipe;
            const GeometryPresentationSlotRecipe* slot =
                FindUniqueTargetSlot(recipe, target);
            if (slot == nullptr ||
                slot->SourceKind !=
                    GeometryPresentationSourceKind::PropertyBake)
            {
                return BindingFail(
                    RuntimeObjectSpaceNormalBakeBindingStatus::
                        StalePresentationState,
                    "object-space normal bake geometry-presentation has no unique target normal slot");
            }
            stagedState = *currentState;
            GeometryPresentationSlotStatus* status =
                FindGeometryPresentationSlotStatus(
                    stagedState,
                    target.PresentationKey,
                    target.Semantic);
            if (status == nullptr ||
                status->Readiness != GeometryPresentationReadiness::Pending)
            {
                return BindingFail(
                    RuntimeObjectSpaceNormalBakeBindingStatus::
                        StalePresentationState,
                    "object-space normal bake geometry-presentation target slot is no longer pending");
            }
            status->GeneratedTexture = completion.GeneratedTextureAsset;
            status->Provenance =
                GeometryPresentationProvenance::
                    GeneratedTextureAsset;
            status->Readiness = GeometryPresentationReadiness::Ready;
            status->Diagnostic =
                "exact GPU object-space normal bake generation is ready";
            ++status->OutputGeneration;
            updatePresentation = true;
        }

        Graphics::MaterialTextureAssetBindings stagedMaterial =
            context.Extraction
                ->GetMaterialTextureAssetBindings(target.StableEntityId)
                .value_or(Graphics::MaterialTextureAssetBindings{});
        stagedMaterial.Normal = completion.GeneratedTextureAsset;
        stagedMaterial.NormalSpace =
            Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal;

        RuntimeObjectSpaceNormalBakeResult ready =
            context.Queue->Complete(
                completion.StaleKey,
                completion.GeneratedTextureAsset,
                completion.AssetSelection);
        if (!ready.Succeeded())
        {
            return BindingFail(
                ready.Status ==
                        RuntimeObjectSpaceNormalBakeStatus::StaleCompletion
                    ? RuntimeObjectSpaceNormalBakeBindingStatus::
                          StaleCompletion
                    : RuntimeObjectSpaceNormalBakeBindingStatus::
                          InvalidCompletion,
                ready.Diagnostic,
                std::move(ready));
        }

        context.Extraction->SetMaterialTextureAssetBindings(
            target.StableEntityId,
            stagedMaterial);
        if (updatePresentation)
            *currentState = std::move(stagedState);

        return RuntimeObjectSpaceNormalBakeBindingResult{
            .Status = RuntimeObjectSpaceNormalBakeBindingStatus::Bound,
            .Completion = std::move(ready),
            .BoundNormalTexture = completion.GeneratedTextureAsset,
            .Diagnostic =
                "exact object-space normal bake generation merged into material and geometry presentation status",
        };
    }
}
