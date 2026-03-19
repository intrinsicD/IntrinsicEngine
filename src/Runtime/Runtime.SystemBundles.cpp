module;
#include <array>

module Runtime.SystemBundles;

import ECS;
import Graphics;
import Runtime.SystemFeatureCatalog;

namespace
{
    using CoreBundleRegisterFn = void(*)(const Runtime::CoreFrameGraphRegistrationContext&);
    using GpuBundleRegisterFn = void(*)(const Runtime::GpuFrameGraphRegistrationContext&);

    struct CoreSystemRegistrationSpec
    {
        Core::FeatureDescriptor Feature;
        CoreBundleRegisterFn Register;
    };

    struct GpuSystemRegistrationSpec
    {
        Core::FeatureDescriptor Feature;
        GpuBundleRegisterFn Register;
    };

    template <typename RegistrationSpec, typename Context>
    void RegisterEnabledSystems(Core::FeatureRegistry& features,
                                const auto& registrations,
                                const Context& context)
    {
        for (const RegistrationSpec& registration : registrations)
        {
            if (!features.IsEnabled(registration.Feature))
            {
                continue;
            }

            registration.Register(context);
        }
    }

    template <auto RegisterSystemFn>
    void RegisterCoreSystem(const Runtime::CoreFrameGraphRegistrationContext& context)
    {
        RegisterSystemFn(context.Graph, context.Registry);
    }

    template <auto RegisterSystemFn>
    void RegisterGeometrySyncSystem(const Runtime::GpuFrameGraphRegistrationContext& context)
    {
        RegisterSystemFn(context.Core.Graph,
                         context.Core.Registry,
                         context.GpuScene,
                         context.GeometryStorage,
                         context.Device,
                         context.TransferManager,
                         context.Dispatcher);
    }

    template <auto RegisterSystemFn>
    void RegisterMeshSurfaceSystem(const Runtime::GpuFrameGraphRegistrationContext& context)
    {
        RegisterSystemFn(context.Core.Graph,
                         context.Core.Registry,
                         context.GpuScene,
                         context.AssetManager,
                         context.MaterialSystem,
                         context.GeometryStorage,
                         context.DefaultTextureId);
    }

    template <auto RegisterSystemFn>
    void RegisterGpuSceneSystem(const Runtime::GpuFrameGraphRegistrationContext& context)
    {
        RegisterSystemFn(context.Core.Graph,
                         context.Core.Registry,
                         context.GpuScene,
                         context.AssetManager,
                         context.MaterialSystem,
                         context.DefaultTextureId);
    }

    constexpr std::array<CoreSystemRegistrationSpec, 3> kCoreSystemRegistrations{{
        {Runtime::SystemFeatureCatalog::TransformUpdate,
         &RegisterCoreSystem<&ECS::Systems::Transform::RegisterSystem>},
        {Runtime::SystemFeatureCatalog::PropertySetDirtySync,
         &RegisterCoreSystem<&Graphics::Systems::PropertySetDirtySync::RegisterSystem>},
        {Runtime::SystemFeatureCatalog::PrimitiveBVHSync,
         &RegisterCoreSystem<&Graphics::Systems::PrimitiveBVHSync::RegisterSystem>},
    }};

    constexpr std::array<GpuSystemRegistrationSpec, 5> kGpuSystemRegistrations{{
        {Runtime::SystemFeatureCatalog::GraphGeometrySync,
         &RegisterGeometrySyncSystem<&Graphics::Systems::GraphGeometrySync::RegisterSystem>},
        {Runtime::SystemFeatureCatalog::MeshRendererLifecycle,
         &RegisterMeshSurfaceSystem<&Graphics::Systems::MeshRendererLifecycle::RegisterSystem>},
        {Runtime::SystemFeatureCatalog::PointCloudGeometrySync,
         &RegisterGeometrySyncSystem<&Graphics::Systems::PointCloudGeometrySync::RegisterSystem>},
        {Runtime::SystemFeatureCatalog::MeshViewLifecycle,
         &RegisterGeometrySyncSystem<&Graphics::Systems::MeshViewLifecycle::RegisterSystem>},
        {Runtime::SystemFeatureCatalog::GPUSceneSync,
         &RegisterGpuSceneSystem<&Graphics::Systems::GPUSceneSync::RegisterSystem>},
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
