module;
#include <array>
#include <span>

module Runtime.SystemBundles;

import ECS;
import Graphics.Systems.GPUSceneSync;
import Graphics.Systems.GraphLifecycle;
import Graphics.Systems.MeshRendererLifecycle;
import Graphics.Systems.MeshViewLifecycle;
import Graphics.Systems.PointCloudLifecycle;
import Graphics.Systems.PrimitiveBVHBuild;
import Graphics.Systems.PropertySetDirtySync;
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
                         context.MaterialRegistry,
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
                         context.MaterialRegistry,
                         context.DefaultTextureId);
    }

    constexpr std::array<Core::FeatureDescriptor, 3> kCoreSystemOrder{{
        Runtime::SystemFeatureCatalog::TransformUpdate,
        Runtime::SystemFeatureCatalog::PropertySetDirtySync,
        Runtime::SystemFeatureCatalog::PrimitiveBVHBuild,
    }};

    constexpr std::array<CoreSystemRegistrationSpec, 3> kCoreSystemRegistrations{{
        {kCoreSystemOrder[0],
         &RegisterCoreSystem<&ECS::Systems::Transform::RegisterSystem>},
        {kCoreSystemOrder[1],
         &RegisterCoreSystem<&Graphics::Systems::PropertySetDirtySync::RegisterSystem>},
        {kCoreSystemOrder[2],
         &RegisterCoreSystem<&Graphics::Systems::PrimitiveBVHBuild::RegisterSystem>},
    }};

    constexpr std::array<Core::FeatureDescriptor, 5> kGpuSystemOrder{{
        Runtime::SystemFeatureCatalog::GraphLifecycle,
        Runtime::SystemFeatureCatalog::MeshRendererLifecycle,
        Runtime::SystemFeatureCatalog::PointCloudLifecycle,
        Runtime::SystemFeatureCatalog::MeshViewLifecycle,
        Runtime::SystemFeatureCatalog::GPUSceneSync,
    }};

    constexpr std::array<GpuSystemRegistrationSpec, 5> kGpuSystemRegistrations{{
        {kGpuSystemOrder[0],
         &RegisterGeometrySyncSystem<&Graphics::Systems::GraphLifecycle::RegisterSystem>},
        {kGpuSystemOrder[1],
         &RegisterMeshSurfaceSystem<&Graphics::Systems::MeshRendererLifecycle::RegisterSystem>},
        {kGpuSystemOrder[2],
         &RegisterGeometrySyncSystem<&Graphics::Systems::PointCloudLifecycle::RegisterSystem>},
        {kGpuSystemOrder[3],
         &RegisterGeometrySyncSystem<&Graphics::Systems::MeshViewLifecycle::RegisterSystem>},
        {kGpuSystemOrder[4],
         &RegisterGpuSceneSystem<&Graphics::Systems::GPUSceneSync::RegisterSystem>},
    }};

    constexpr std::array<Core::FeatureDescriptor,
                         kCoreSystemOrder.size() + kGpuSystemOrder.size()> kVariableSystemOrder{{
        kCoreSystemOrder[0],
        kCoreSystemOrder[1],
        kCoreSystemOrder[2],
        kGpuSystemOrder[0],
        kGpuSystemOrder[1],
        kGpuSystemOrder[2],
        kGpuSystemOrder[3],
        kGpuSystemOrder[4],
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

    void VariableFrameGraphSystemBundle::Register(this const VariableFrameGraphSystemBundle&,
                                                  const CoreFrameGraphRegistrationContext& coreContext,
                                                  const GpuFrameGraphRegistrationContext* gpuContext)
    {
        CoreFrameGraphSystemBundle{}.Register(coreContext);
        if (gpuContext)
        {
            GpuFrameGraphSystemBundle{}.Register(*gpuContext);
        }
    }

    std::span<const Core::FeatureDescriptor> GetCoreFrameGraphFeatureOrder()
    {
        return kCoreSystemOrder;
    }

    std::span<const Core::FeatureDescriptor> GetGpuFrameGraphFeatureOrder()
    {
        return kGpuSystemOrder;
    }

    std::span<const Core::FeatureDescriptor> GetVariableFrameGraphFeatureOrder()
    {
        return kVariableSystemOrder;
    }
}
