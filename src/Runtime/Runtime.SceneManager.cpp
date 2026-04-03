module;
#include <cassert>
#include <string>
#include <cstdint>
#include <type_traits>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <glm/glm.hpp>

module Runtime.SceneManager;

import Core.Logging;
import Core.Assets;

import Graphics.Components;
import Graphics.Geometry;
import Graphics.GPUScene;
import Graphics.Model;
import Graphics.VectorFieldManager;
import Graphics.VisualizationConfig;

import ECS;

import Geometry.Handle;
#ifdef INTRINSIC_HAS_CUDA
import RHI.CudaDevice;
#endif

import Graphics.LifecycleUtils;

using namespace Graphics::LifecycleUtils;

namespace Runtime
{
    SceneManager::SceneManager()
    {
        Core::Log::Info("SceneManager: Initialized.");
    }

    SceneManager::~SceneManager()
    {
        DisconnectGpuHooks();
        Core::Log::Info("SceneManager: Shutdown.");
    }

    WorldSnapshot SceneManager::CreateReadonlySnapshot() const
    {
        return WorldSnapshot{
            .Scene = &m_Scene,
            .Registry = &m_Scene.GetRegistry(),
            .CommitSource = &m_CommittedTick,
            .CommittedTick = m_CommittedTick,
        };
    }

    void SceneManager::CommitFixedTick()
    {
        ++m_CommittedTick;
    }

    template <typename T>
    void SceneManager::OnGpuComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!m_GpuHookContext.GpuScene)
            return;

        auto& comp = registry.get<T>(entity);

#ifdef INTRINSIC_HAS_CUDA
        if constexpr (std::is_same_v<T, ECS::PointCloud::Data>)
        {
            if (m_GpuHookContext.CudaDevice)
                comp.ReleaseCudaBuffers(*m_GpuHookContext.CudaDevice);
        }
#endif

        if (comp.GpuSlot != ECS::kInvalidGpuSlot)
            ReleaseGpuSlot(*m_GpuHookContext.GpuScene, comp);

        // Release geometry pool entries to prevent GPU memory leaks.
        // Without this, GeometryGpuData (owning VkBuffer GPU memory) would
        // remain active in the ResourcePool until pool destruction at shutdown.
        if (m_GeometryStorage && m_GpuHookContext.Device)
        {
            const uint64_t frame = m_GpuHookContext.Device->GetGlobalFrameNumber();

            if constexpr (std::is_same_v<T, ECS::Graph::Data>)
            {
                if (comp.GpuGeometry.IsValid())
                {
                    m_GeometryStorage->Remove(comp.GpuGeometry, frame);
                    comp.GpuGeometry = {};
                }
                if (comp.GpuEdgeGeometry.IsValid())
                {
                    m_GeometryStorage->Remove(comp.GpuEdgeGeometry, frame);
                    comp.GpuEdgeGeometry = {};
                }
            }
            else if constexpr (std::is_same_v<T, ECS::PointCloud::Data>)
            {
                if (comp.GpuGeometry.IsValid())
                {
                    m_GeometryStorage->Remove(comp.GpuGeometry, frame);
                    comp.GpuGeometry = {};
                }
            }
            else if constexpr (std::is_same_v<T, ECS::MeshEdgeView::Component>
                            || std::is_same_v<T, ECS::MeshVertexView::Component>)
            {
                if (comp.Geometry.IsValid())
                {
                    m_GeometryStorage->Remove(comp.Geometry, frame);
                    comp.Geometry = {};
                }
            }
            // Note: Surface::Component geometry is owned by the model loader
            // and shared across submeshes; it is not removed here because
            // multiple entities may reference the same geometry handle.
        }
    }

    template <typename T>
    void SceneManager::OnDataComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        auto& comp = registry.get<T>(entity);
        if (!comp.Visualization.VectorFields.empty())
            Graphics::VectorFieldManager::DestroyAllVectorFields(registry, comp.Visualization);
    }

    void SceneManager::ConnectGpuHooks(Graphics::GPUScene& gpuScene,
                                       RHI::VulkanDevice& device
#ifdef INTRINSIC_HAS_CUDA
                                       , RHI::CudaDevice* cudaDevice
#endif
    )
    {
        DisconnectGpuHooks();

        m_GpuHookContext.GpuScene = &gpuScene;
        m_GpuHookContext.Device = &device;
#ifdef INTRINSIC_HAS_CUDA
        m_GpuHookContext.CudaDevice = cudaDevice;
#endif
        auto& reg = m_Scene.GetRegistry();
        reg.on_destroy<ECS::Surface::Component>().connect<&SceneManager::OnGpuComponentDestroyed<ECS::Surface::Component>>(*this);
        reg.on_destroy<ECS::MeshEdgeView::Component>().connect<&SceneManager::OnGpuComponentDestroyed<ECS::MeshEdgeView::Component>>(*this);
        reg.on_destroy<ECS::MeshVertexView::Component>().connect<&SceneManager::OnGpuComponentDestroyed<ECS::MeshVertexView::Component>>(*this);
        reg.on_destroy<ECS::Graph::Data>().connect<&SceneManager::OnGpuComponentDestroyed<ECS::Graph::Data>>(*this);
        reg.on_destroy<ECS::PointCloud::Data>().connect<&SceneManager::OnGpuComponentDestroyed<ECS::PointCloud::Data>>(*this);

        // Vector field child cleanup: destroy child Graph entities when the
        // parent data component is removed, preventing orphaned entities.
        reg.on_destroy<ECS::Mesh::Data>().connect<&SceneManager::OnDataComponentDestroyed<ECS::Mesh::Data>>(*this);
        reg.on_destroy<ECS::Graph::Data>().connect<&SceneManager::OnDataComponentDestroyed<ECS::Graph::Data>>(*this);
        reg.on_destroy<ECS::PointCloud::Data>().connect<&SceneManager::OnDataComponentDestroyed<ECS::PointCloud::Data>>(*this);
    }

    void SceneManager::DisconnectGpuHooks()
    {
        auto& reg = m_Scene.GetRegistry();
        reg.on_destroy<ECS::Surface::Component>().disconnect<&SceneManager::OnGpuComponentDestroyed<ECS::Surface::Component>>(*this);
        reg.on_destroy<ECS::MeshEdgeView::Component>().disconnect<&SceneManager::OnGpuComponentDestroyed<ECS::MeshEdgeView::Component>>(*this);
        reg.on_destroy<ECS::MeshVertexView::Component>().disconnect<&SceneManager::OnGpuComponentDestroyed<ECS::MeshVertexView::Component>>(*this);
        reg.on_destroy<ECS::Graph::Data>().disconnect<&SceneManager::OnGpuComponentDestroyed<ECS::Graph::Data>>(*this);
        reg.on_destroy<ECS::PointCloud::Data>().disconnect<&SceneManager::OnGpuComponentDestroyed<ECS::PointCloud::Data>>(*this);

        reg.on_destroy<ECS::Mesh::Data>().disconnect<&SceneManager::OnDataComponentDestroyed<ECS::Mesh::Data>>(*this);
        reg.on_destroy<ECS::Graph::Data>().disconnect<&SceneManager::OnDataComponentDestroyed<ECS::Graph::Data>>(*this);
        reg.on_destroy<ECS::PointCloud::Data>().disconnect<&SceneManager::OnDataComponentDestroyed<ECS::PointCloud::Data>>(*this);

        m_GpuHookContext.GpuScene = nullptr;
        m_GpuHookContext.Device = nullptr;
#ifdef INTRINSIC_HAS_CUDA
        m_GpuHookContext.CudaDevice = nullptr;
#endif
    }

    entt::entity SceneManager::SpawnModel(Core::Assets::AssetManager& assetManager,
                                          Core::Assets::AssetHandle modelHandle,
                                          Core::Assets::AssetHandle materialHandle,
                                          glm::vec3 position,
                                          glm::vec3 scale)
    {
        // 1. Resolve Model
        if (auto* model = assetManager.TryGet<Graphics::Model>(modelHandle))
        {
            // 2. Create Root
            std::string name = "Model";

            entt::entity root = m_Scene.CreateEntity(name);
            auto& t = m_Scene.GetRegistry().get<ECS::Components::Transform::Component>(root);
            t.Position = position;
            t.Scale = scale;

            // Assign stable pick IDs (monotonic, never reused during runtime).
            static uint32_t s_NextPickId = 1u;
            if (!m_Scene.GetRegistry().all_of<ECS::Components::Selection::PickID>(root))
            {
                m_Scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(root, s_NextPickId++);
            }

            // 3. Create Submeshes / Point Clouds
            for (size_t i = 0; i < model->Meshes.size(); i++)
            {
                entt::entity targetEntity = root;

                // If complex model, create children. If single mesh, put it on root.
                if (model->Meshes.size() > 1)
                {
                    targetEntity = m_Scene.CreateEntity(model->Meshes[i]->Name);
                    ECS::Components::Hierarchy::Attach(m_Scene.GetRegistry(), targetEntity, root);
                }

                // Route point topologies to PointCloud::Data for PointPass
                // rendering. Meshes/lines use the standard Surface path.
                const auto handle = model->Meshes[i]->Handle;
                const auto* geo = m_GeometryStorage ? m_GeometryStorage->GetIfValid(handle) : nullptr;

                if (geo && geo->GetTopology() == Graphics::PrimitiveTopology::Points)
                {
                    m_Scene.GetRegistry().emplace<ECS::DataAuthority::PointCloudTag>(targetEntity);
                    auto& pcd = m_Scene.GetRegistry().emplace<ECS::PointCloud::Data>(targetEntity);
                    pcd.GpuGeometry = handle;
                    pcd.GpuDirty = false; // Already uploaded by ModelLoader.
                    pcd.CloudRef.reset();
                    pcd.HasGpuNormals = (geo->GetLayout().NormalsSize > 0);
                    pcd.GpuPointCount = static_cast<uint32_t>(geo->GetLayout().PositionsSize / sizeof(glm::vec3));
                }
                else
                {
                    m_Scene.GetRegistry().emplace<ECS::DataAuthority::MeshTag>(targetEntity);
                    auto& sc = m_Scene.GetRegistry().emplace<ECS::Surface::Component>(targetEntity);
                    sc.Geometry = handle;
                    sc.Material = materialHandle;

                    // Add Collider
                    if (model->Meshes[i]->CollisionGeometry)
                    {
                        auto& col = m_Scene.GetRegistry().emplace<ECS::MeshCollider::Component>(targetEntity);
                        col.CollisionRef = model->Meshes[i]->CollisionGeometry;
                        col.WorldOBB.Center = col.CollisionRef->LocalAABB.GetCenter();

                        if (col.CollisionRef->SourceMesh)
                        {
                            auto& meshData = m_Scene.GetRegistry().emplace_or_replace<ECS::Mesh::Data>(targetEntity);
                            meshData.MeshRef = col.CollisionRef->SourceMesh;
                            meshData.AttributesDirty = true;
                        }

                        auto& primitiveBvh = m_Scene.GetRegistry().emplace_or_replace<ECS::PrimitiveBVH::Data>(targetEntity);
                        primitiveBvh.Source = ECS::PrimitiveBVH::SourceKind::MeshTriangles;
                        primitiveBvh.Dirty = true;
                    }
                }

                // Add Selectable Tag
                m_Scene.GetRegistry().emplace<ECS::Components::Selection::SelectableTag>(targetEntity);

                // Stable pick ID for each selectable entity.
                if (!m_Scene.GetRegistry().all_of<ECS::Components::Selection::PickID>(targetEntity))
                {
                    m_Scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(targetEntity, s_NextPickId++);
                }
            }

            m_Scene.GetDispatcher().enqueue<ECS::Events::EntitySpawned>({root});
            return root;
        }

        Core::Log::Error("Cannot spawn model: Asset not ready or invalid.");
        return entt::null;
    }

    void SceneManager::Clear()
    {
        m_Scene.GetRegistry().clear();
        m_CommittedTick = 0;
    }
}
