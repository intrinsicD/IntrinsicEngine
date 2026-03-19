module;
#include <array>

module Runtime.SystemBundles;

import Core.Hash;
import ECS;
import Graphics;

using namespace Core::Hash;

namespace
{
    using CoreBundleRegisterFn = void(*)(const Runtime::CoreFrameGraphRegistrationContext&);
    using GpuBundleRegisterFn = void(*)(const Runtime::GpuFrameGraphRegistrationContext&);

    struct CoreSystemRegistrationSpec
    {
        Core::Hash::StringID FeatureId;
        CoreBundleRegisterFn Register;
    };

    struct GpuSystemRegistrationSpec
    {
        Core::Hash::StringID FeatureId;
        GpuBundleRegisterFn Register;
    };

    template <typename RegistrationSpec, typename Context>
    void RegisterEnabledSystems(Core::FeatureRegistry& features,
                                const auto& registrations,
                                const Context& context)
    {
        for (const RegistrationSpec& registration : registrations)
        {
            if (!features.IsEnabled(registration.FeatureId))
            {
                continue;
            }

            registration.Register(context);
        }
    }

    void RegisterTransformSystem(const Runtime::CoreFrameGraphRegistrationContext& context)
    {
        ECS::Systems::Transform::RegisterSystem(context.Graph, context.Registry);
    }

    void RegisterPropertySetDirtySyncSystem(const Runtime::CoreFrameGraphRegistrationContext& context)
    {
        Graphics::Systems::PropertySetDirtySync::RegisterSystem(context.Graph, context.Registry);
    }

    void RegisterPrimitiveBVHSyncSystem(const Runtime::CoreFrameGraphRegistrationContext& context)
    {
        Graphics::Systems::PrimitiveBVHSync::RegisterSystem(context.Graph, context.Registry);
    }

    constexpr std::array<CoreSystemRegistrationSpec, 3> kCoreSystemRegistrations{{
        {"TransformUpdate"_id, &RegisterTransformSystem},
        {"PropertySetDirtySync"_id, &RegisterPropertySetDirtySyncSystem},
        {"PrimitiveBVHSync"_id, &RegisterPrimitiveBVHSyncSystem},
    }};

    void RegisterGraphGeometrySyncSystem(const Runtime::GpuFrameGraphRegistrationContext& context)
    {
        Graphics::Systems::GraphGeometrySync::RegisterSystem(
            context.Core.Graph,
            context.Core.Registry,
            context.GpuScene,
            context.GeometryStorage,
            context.Device,
            context.TransferManager,
            context.Dispatcher);
    }

    void RegisterMeshRendererLifecycleSystem(const Runtime::GpuFrameGraphRegistrationContext& context)
    {
        Graphics::Systems::MeshRendererLifecycle::RegisterSystem(
            context.Core.Graph,
            context.Core.Registry,
            context.GpuScene,
            context.AssetManager,
            context.MaterialSystem,
            context.GeometryStorage,
            context.DefaultTextureId);
    }

    void RegisterPointCloudGeometrySyncSystem(const Runtime::GpuFrameGraphRegistrationContext& context)
    {
        Graphics::Systems::PointCloudGeometrySync::RegisterSystem(
            context.Core.Graph,
            context.Core.Registry,
            context.GpuScene,
            context.GeometryStorage,
            context.Device,
            context.TransferManager,
            context.Dispatcher);
    }

    void RegisterMeshViewLifecycleSystem(const Runtime::GpuFrameGraphRegistrationContext& context)
    {
        Graphics::Systems::MeshViewLifecycle::RegisterSystem(
            context.Core.Graph,
            context.Core.Registry,
            context.GpuScene,
            context.GeometryStorage,
            context.Device,
            context.TransferManager,
            context.Dispatcher);
    }

    void RegisterGpuSceneSyncSystem(const Runtime::GpuFrameGraphRegistrationContext& context)
    {
        Graphics::Systems::GPUSceneSync::RegisterSystem(
            context.Core.Graph,
            context.Core.Registry,
            context.GpuScene,
            context.AssetManager,
            context.MaterialSystem,
            context.DefaultTextureId);
    }

    constexpr std::array<GpuSystemRegistrationSpec, 5> kGpuSystemRegistrations{{
        {"GraphGeometrySync"_id, &RegisterGraphGeometrySyncSystem},
        {"MeshRendererLifecycle"_id, &RegisterMeshRendererLifecycleSystem},
        {"PointCloudGeometrySync"_id, &RegisterPointCloudGeometrySyncSystem},
        {"MeshViewLifecycle"_id, &RegisterMeshViewLifecycleSystem},
        {"GPUSceneSync"_id, &RegisterGpuSceneSyncSystem},
    }};
}

namespace Runtime
{
    void CoreFrameGraphSystemBundle::Register(this const CoreFrameGraphSystemBundle&,
                                              const CoreFrameGraphRegistrationContext& context)
    {
        RegisterEnabledSystems<CoreSystemRegistrationSpec>(context.Features,
                                                           kCoreSystemRegistrations,
                                                           context);
    }

    void GpuFrameGraphSystemBundle::Register(this const GpuFrameGraphSystemBundle&,
                                             const GpuFrameGraphRegistrationContext& context)
    {
        RegisterEnabledSystems<GpuSystemRegistrationSpec>(context.Core.Features,
                                                          kGpuSystemRegistrations,
                                                          context);
    }
}
