module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

#include "Core.Profiling.Macros.hpp"
#ifdef INTRINSIC_HAS_CUDA
#include "Runtime.PointCloudKMeans.CudaKernels.hpp"
#include <cuda.h>
#endif

module Runtime.PointCloudKMeans;

import Runtime.Engine;

import Graphics.Components;

import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.KDTree;
import Geometry.KMeans;
import Geometry.PointCloud;
import Geometry.Properties;

import ECS;

import Core.Logging;
import Core.Tasks;

#ifdef INTRINSIC_HAS_CUDA
import RHI.CudaDevice;
import RHI.CudaError;
#endif

namespace Runtime::PointCloudKMeans
{
    namespace
    {
        using TargetDomain = Runtime::PointCloudKMeans::Domain;

        struct CpuCompletionPayload
        {
            Engine* Owner = nullptr;
            entt::entity Entity = entt::null;
            TargetDomain Domain = TargetDomain::Auto;
            std::optional<Geometry::KMeans::KMeansResult> Result{};
            double DurationMs = 0.0;
        };

        struct ResolvedTarget
        {
            TargetDomain Domain = TargetDomain::Auto;
            std::size_t PointCount = 0;
            ECS::Mesh::Data* MeshData = nullptr;
            ECS::Graph::Data* GraphData = nullptr;
            ECS::PointCloud::Data* PointCloudData = nullptr;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return Domain != TargetDomain::Auto;
            }

            [[nodiscard]] bool SupportsCuda() const noexcept
            {
                return IsValid() && PointCount > 0;
            }
        };

        [[nodiscard]] glm::vec4 LabelColor(uint32_t label)
        {
            const float h = std::fmod(0.61803398875f * static_cast<float>(label), 1.0f);
            const float s = 0.65f;
            const float v = 0.95f;

            const float hh = h * 6.0f;
            const float c = v * s;
            const float x = c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
            const float m = v - c;

            glm::vec3 rgb(0.0f);
            if (hh < 1.0f) rgb = {c, x, 0.0f};
            else if (hh < 2.0f) rgb = {x, c, 0.0f};
            else if (hh < 3.0f) rgb = {0.0f, c, x};
            else if (hh < 4.0f) rgb = {0.0f, x, c};
            else if (hh < 5.0f) rgb = {x, 0.0f, c};
            else rgb = {c, 0.0f, x};
            return glm::vec4(rgb + glm::vec3(m), 1.0f);
        }

        [[nodiscard]] std::vector<glm::vec3> SnapshotMeshVertices(
            const Geometry::Halfedge::Mesh& mesh,
            std::vector<uint32_t>* handles = nullptr)
        {
            std::vector<glm::vec3> points;
            points.reserve(mesh.VertexCount());
            if (handles)
                handles->clear();

            for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            {
                const auto vh = Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(vh) || mesh.IsDeleted(vh))
                    continue;

                points.push_back(mesh.Position(vh));
                if (handles)
                    handles->push_back(static_cast<uint32_t>(i));
            }

            return points;
        }

        [[nodiscard]] std::vector<glm::vec3> SnapshotGraphVertices(
            const Geometry::Graph::Graph& graph,
            std::vector<uint32_t>* handles = nullptr)
        {
            std::vector<glm::vec3> points;
            points.reserve(graph.VertexCount());
            if (handles)
                handles->clear();

            for (std::size_t i = 0; i < graph.VerticesSize(); ++i)
            {
                const auto vh = Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)};
                if (!graph.IsValid(vh) || graph.IsDeleted(vh))
                    continue;

                points.push_back(graph.VertexPosition(vh));
                if (handles)
                    handles->push_back(static_cast<uint32_t>(i));
            }

            return points;
        }

        [[nodiscard]] ResolvedTarget ResolveTarget(
            entt::registry& reg,
            entt::entity entity,
            TargetDomain requestedDomain)
        {
            ResolvedTarget target{};
            if (!reg.valid(entity))
                return target;

            const auto tryMesh = [&]() -> bool
            {
                auto* mesh = reg.try_get<ECS::Mesh::Data>(entity);
                if (!mesh || !mesh->MeshRef)
                    return false;
                target.Domain = TargetDomain::MeshVertices;
                target.PointCount = mesh->VertexCount();
                target.MeshData = mesh;
                return true;
            };

            const auto tryGraph = [&]() -> bool
            {
                auto* graph = reg.try_get<ECS::Graph::Data>(entity);
                if (!graph || !graph->GraphRef)
                    return false;
                target.Domain = TargetDomain::GraphVertices;
                target.PointCount = graph->NodeCount();
                target.GraphData = graph;
                return true;
            };

            const auto tryPointCloud = [&]() -> bool
            {
                auto* cloud = reg.try_get<ECS::PointCloud::Data>(entity);
                if (!cloud || !cloud->CloudRef || cloud->CloudRef->IsEmpty())
                    return false;
                target.Domain = TargetDomain::PointCloudPoints;
                target.PointCount = cloud->PointCount();
                target.PointCloudData = cloud;
                return true;
            };

            switch (requestedDomain)
            {
            case TargetDomain::MeshVertices: static_cast<void>(tryMesh()); return target;
            case TargetDomain::GraphVertices: static_cast<void>(tryGraph()); return target;
            case TargetDomain::PointCloudPoints: static_cast<void>(tryPointCloud()); return target;
            case TargetDomain::Auto:
            default:
                if (tryPointCloud()) return target;
                if (tryMesh()) return target;
                if (tryGraph()) return target;
                return target;
            }
        }

        void ClearPending(ResolvedTarget& target)
        {
            switch (target.Domain)
            {
            case TargetDomain::MeshVertices:
                if (target.MeshData)
                {
                    target.MeshData->KMeansJobPending = false;
                    target.MeshData->KMeansPendingClusterCount = 0;
                }
                break;
            case TargetDomain::GraphVertices:
                if (target.GraphData)
                {
                    target.GraphData->KMeansJobPending = false;
                    target.GraphData->KMeansPendingClusterCount = 0;
                }
                break;
            case TargetDomain::PointCloudPoints:
                if (target.PointCloudData)
                {
                    target.PointCloudData->KMeansJobPending = false;
                    target.PointCloudData->KMeansPendingClusterCount = 0;
                }
                break;
            case TargetDomain::Auto:
            default:
                break;
            }
        }

        void MarkVertexAttributesDirty(entt::registry& reg, entt::entity entity)
        {
            if (reg.valid(entity))
                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);
        }

        void MarkPending(ResolvedTarget& target,
                         Geometry::KMeans::Backend backend,
                         uint32_t clusterCount,
                         uint32_t maxIterations)
        {
            switch (target.Domain)
            {
            case TargetDomain::MeshVertices:
                if (target.MeshData)
                {
                    target.MeshData->KMeansJobPending = true;
                    target.MeshData->KMeansPendingClusterCount = clusterCount;
                    target.MeshData->KMeansLastBackend = backend;
                    target.MeshData->KMeansLastIterations = maxIterations;
                    target.MeshData->KMeansLastConverged = false;
                }
                break;
            case TargetDomain::GraphVertices:
                if (target.GraphData)
                {
                    target.GraphData->KMeansJobPending = true;
                    target.GraphData->KMeansPendingClusterCount = clusterCount;
                    target.GraphData->KMeansLastBackend = backend;
                    target.GraphData->KMeansLastIterations = maxIterations;
                    target.GraphData->KMeansLastConverged = false;
                }
                break;
            case TargetDomain::PointCloudPoints:
                if (target.PointCloudData)
                {
                    target.PointCloudData->KMeansJobPending = true;
                    target.PointCloudData->KMeansPendingClusterCount = clusterCount;
                    target.PointCloudData->KMeansLastBackend = backend;
                    target.PointCloudData->KMeansLastIterations = maxIterations;
                    target.PointCloudData->KMeansLastConverged = false;
                }
                break;
            case TargetDomain::Auto:
            default:
                break;
            }
        }

        [[nodiscard]] std::vector<glm::vec3> SnapshotPoints(const ResolvedTarget& target)
        {
            switch (target.Domain)
            {
            case TargetDomain::MeshVertices:
                return (target.MeshData && target.MeshData->MeshRef)
                    ? SnapshotMeshVertices(*target.MeshData->MeshRef)
                    : std::vector<glm::vec3>{};
            case TargetDomain::GraphVertices:
                return (target.GraphData && target.GraphData->GraphRef)
                    ? SnapshotGraphVertices(*target.GraphData->GraphRef)
                    : std::vector<glm::vec3>{};
            case TargetDomain::PointCloudPoints:
                if (target.PointCloudData && target.PointCloudData->CloudRef)
                {
                    const auto positions = target.PointCloudData->CloudRef->Positions();
                    return std::vector<glm::vec3>(positions.begin(), positions.end());
                }
                return {};
            case TargetDomain::Auto:
            default:
                return {};
            }
        }

        [[nodiscard]] std::vector<glm::vec3> SnapshotExistingCentroids(entt::registry& reg, const ResolvedTarget& target)
        {
            auto snapshotFromEntity = [&](entt::entity centroidEntity) -> std::vector<glm::vec3>
            {
                if (centroidEntity == entt::null || !reg.valid(centroidEntity))
                    return {};

                const auto* centroidData = reg.try_get<ECS::PointCloud::Data>(centroidEntity);
                if (!centroidData || !centroidData->CloudRef)
                    return {};

                const auto positions = centroidData->CloudRef->Positions();
                return std::vector<glm::vec3>(positions.begin(), positions.end());
            };

            switch (target.Domain)
            {
            case TargetDomain::MeshVertices:
                return target.MeshData ? snapshotFromEntity(target.MeshData->KMeansCentroidEntity) : std::vector<glm::vec3>{};
            case TargetDomain::GraphVertices:
                return target.GraphData ? snapshotFromEntity(target.GraphData->KMeansCentroidEntity) : std::vector<glm::vec3>{};
            case TargetDomain::PointCloudPoints:
                return target.PointCloudData ? snapshotFromEntity(target.PointCloudData->KMeansCentroidEntity)
                                             : std::vector<glm::vec3>{};
            case TargetDomain::Auto:
            default:
                return {};
            }
        }

        [[nodiscard]] entt::entity GetCentroidEntity(const ResolvedTarget& target) noexcept
        {
            switch (target.Domain)
            {
            case TargetDomain::MeshVertices:
                return target.MeshData ? target.MeshData->KMeansCentroidEntity : entt::null;
            case TargetDomain::GraphVertices:
                return target.GraphData ? target.GraphData->KMeansCentroidEntity : entt::null;
            case TargetDomain::PointCloudPoints:
                return target.PointCloudData ? target.PointCloudData->KMeansCentroidEntity : entt::null;
            case TargetDomain::Auto:
            default:
                return entt::null;
            }
        }

        void SetCentroidEntity(ResolvedTarget& target, entt::entity centroidEntity) noexcept
        {
            switch (target.Domain)
            {
            case TargetDomain::MeshVertices:
                if (target.MeshData)
                    target.MeshData->KMeansCentroidEntity = centroidEntity;
                break;
            case TargetDomain::GraphVertices:
                if (target.GraphData)
                    target.GraphData->KMeansCentroidEntity = centroidEntity;
                break;
            case TargetDomain::PointCloudPoints:
                if (target.PointCloudData)
                    target.PointCloudData->KMeansCentroidEntity = centroidEntity;
                break;
            case TargetDomain::Auto:
            default:
                break;
            }
        }

        void BumpKMeansRevision(ResolvedTarget& target) noexcept
        {
            switch (target.Domain)
            {
            case TargetDomain::MeshVertices:
                if (target.MeshData)
                    ++target.MeshData->KMeansResultRevision;
                break;
            case TargetDomain::GraphVertices:
                if (target.GraphData)
                    ++target.GraphData->KMeansResultRevision;
                break;
            case TargetDomain::PointCloudPoints:
                if (target.PointCloudData)
                    ++target.PointCloudData->KMeansResultRevision;
                break;
            case TargetDomain::Auto:
            default:
                break;
            }
        }

        [[nodiscard]] std::string MakeCentroidEntityName(const entt::registry& reg, entt::entity sourceEntity)
        {
            if (const auto* name = reg.try_get<ECS::Components::NameTag::Component>(sourceEntity))
                return name->Name + " / KMeans Centroids";
            return "KMeans Centroids";
        }

        [[nodiscard]] entt::entity EnsureCentroidEntity(entt::registry& reg,
                                                        entt::entity sourceEntity,
                                                        ResolvedTarget& target)
        {
            entt::entity centroidEntity = GetCentroidEntity(target);
            if (centroidEntity != entt::null && reg.valid(centroidEntity))
                return centroidEntity;

            centroidEntity = reg.create();
            reg.emplace_or_replace<ECS::Components::NameTag::Component>(
                centroidEntity, ECS::Components::NameTag::Component{.Name = MakeCentroidEntityName(reg, sourceEntity)});
            reg.emplace_or_replace<ECS::Components::Hierarchy::Component>(centroidEntity);
            reg.emplace_or_replace<ECS::PointCloud::Data>(centroidEntity);
            ECS::Components::Hierarchy::Attach(reg, centroidEntity, sourceEntity);
            SetCentroidEntity(target, centroidEntity);
            return centroidEntity;
        }

        void SyncCentroidPointCloud(ECS::PointCloud::Data& centroidData,
                                    const Geometry::KMeans::KMeansResult& result)
        {
            if (!centroidData.CloudRef)
                centroidData.CloudRef = std::make_shared<Geometry::PointCloud::Cloud>();

            auto& cloud = *centroidData.CloudRef;
            cloud.Clear();
            cloud.EnableColors();
            cloud.EnableRadii(0.025f);

            auto colorProp = cloud.GetOrAddVertexProperty<glm::vec4>("p:color", glm::vec4(1.0f));
            auto radiusProp = cloud.GetOrAddVertexProperty<float>("p:radius", 0.025f);

            const float radius = 0.025f;
            for (uint32_t i = 0; i < static_cast<uint32_t>(result.Centroids.size()); ++i)
            {
                const auto handle = cloud.AddPoint(result.Centroids[i]);
                colorProp[handle] = LabelColor(i);
                radiusProp[handle] = radius;
            }

            centroidData.Visualization.VertexColors.PropertyName = "p:color";
            centroidData.DefaultRadius = radius;
            centroidData.SizeMultiplier = 1.25f;
            centroidData.Visible = true;
            centroidData.GpuDirty = true;
            ++centroidData.PositionRevision;
        }

        void BuildCentroidPointKDTree(entt::registry& reg, entt::entity centroidEntity)
        {
            auto* centroidData = reg.try_get<ECS::PointCloud::Data>(centroidEntity);
            if (!centroidData || !centroidData->CloudRef)
                return;

            auto& kd = reg.get_or_emplace<ECS::PointKDTree::Data>(centroidEntity);
            kd.Clear();
            const auto positions = centroidData->CloudRef->Positions();
            if (positions.empty())
                return;

            if (kd.Tree.BuildFromPoints(positions, kd.BuildParams))
            {
                kd.PointCount = static_cast<uint32_t>(positions.size());
                kd.Dirty = false;
            }
        }

        void UpdateCentroidEntity(Engine& engine,
                                  entt::entity sourceEntity,
                                  ResolvedTarget& target,
                                  const Geometry::KMeans::KMeansResult& result)
        {
            auto& reg = engine.GetScene().GetRegistry();
            const entt::entity centroidEntity = EnsureCentroidEntity(reg, sourceEntity, target);
            auto& centroidData = reg.get_or_emplace<ECS::PointCloud::Data>(centroidEntity);
            SyncCentroidPointCloud(centroidData, result);
            BuildCentroidPointKDTree(reg, centroidEntity);
            engine.GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({centroidEntity});
        }

#ifdef INTRINSIC_HAS_CUDA
        struct PendingCudaJob
        {
            entt::entity Entity = entt::null;
            Domain TargetDomain = Domain::Auto;
            RHI::CudaBufferHandle Positions{};
            RHI::CudaBufferHandle Labels{};
            RHI::CudaBufferHandle Distances{};
            RHI::CudaBufferHandle Centroids{};
            RHI::CudaBufferHandle Sums{};
            RHI::CudaBufferHandle ClusterSizes{};
            CUstream Stream = nullptr;
            CUevent StartEvent = nullptr;
            CUevent CompletionEvent = nullptr;
            uint32_t PointCount = 0;
            uint32_t ClusterCount = 0;
            uint32_t Iterations = 0;
        };

        // Thread model: main-thread only.
        //
        // ScheduleCudaJob(), PumpCompletions(), and ReleaseEntityBuffers() are
        // all invoked from the runtime's main-thread frame/update path. CPU
        // worker jobs never mutate this registry directly; they marshal
        // completions through Engine::RunOnMainThread() before touching
        // authoritative ECS/runtime state.
        class PendingCudaJobRegistry
        {
        public:
            [[nodiscard]] std::unordered_map<uint64_t, PendingCudaJob>* TryAccess(std::string_view operation)
            {
                if (m_OwningThread == std::this_thread::get_id())
                    return &m_Jobs;

                Core::Log::Error(
                    "{} must run on the owning main thread; refusing cross-thread CUDA job registry access.",
                    operation);
                return nullptr;
            }

        private:
            std::thread::id m_OwningThread = std::this_thread::get_id();
            std::unordered_map<uint64_t, PendingCudaJob> m_Jobs;
        };

        [[nodiscard]] PendingCudaJobRegistry& GetPendingCudaJobRegistry()
        {
            static PendingCudaJobRegistry registry{};
            return registry;
        }

        [[nodiscard]] uint64_t MakeEntityKey(entt::entity entity)
        {
            return static_cast<uint64_t>(static_cast<entt::id_type>(entity));
        }

        void DestroyPendingCudaJob(RHI::CudaDevice& cudaDevice, PendingCudaJob& job)
        {
            if (job.Stream)
                cudaDevice.DestroyStream(job.Stream);
            if (job.StartEvent)
                cudaDevice.DestroyEvent(job.StartEvent);
            if (job.CompletionEvent)
                cudaDevice.DestroyEvent(job.CompletionEvent);
            cudaDevice.FreeBuffer(job.Positions);
            cudaDevice.FreeBuffer(job.Labels);
            cudaDevice.FreeBuffer(job.Distances);
            cudaDevice.FreeBuffer(job.Centroids);
            cudaDevice.FreeBuffer(job.Sums);
            cudaDevice.FreeBuffer(job.ClusterSizes);
        }

        [[nodiscard]] bool ScheduleCudaJob(
            Engine& engine,
            entt::entity entity,
            ResolvedTarget target,
            const Geometry::KMeans::KMeansParams& params)
        {
            auto* cudaDevice = engine.GetCudaDevice();
            if (!cudaDevice)
                return false;

            const std::vector<glm::vec3> points = SnapshotPoints(target);
            if (points.empty())
                return false;

            PendingCudaJob job{};
            job.Entity = entity;
            job.TargetDomain = target.Domain;
            job.PointCount = static_cast<uint32_t>(points.size());
            job.ClusterCount = std::min<uint32_t>(params.ClusterCount, job.PointCount);
            job.Iterations = params.MaxIterations;

            if (job.ClusterCount == 0 || job.Iterations == 0)
                return false;

            auto cleanup = [&]()
            {
                DestroyPendingCudaJob(*cudaDevice, job);
            };

            auto stream = cudaDevice->CreateStream();
            if (!stream)
                return false;
            job.Stream = *stream;

            auto startEvent = cudaDevice->CreateEvent();
            if (!startEvent)
            {
                cleanup();
                return false;
            }
            job.StartEvent = *startEvent;

            auto completionEvent = cudaDevice->CreateEvent();
            if (!completionEvent)
            {
                cleanup();
                return false;
            }
            job.CompletionEvent = *completionEvent;

            const auto allocateBuffer = [&](RHI::CudaBufferHandle& handle, size_t bytes) -> bool
            {
                auto buffer = cudaDevice->AllocateBuffer(bytes);
                if (!buffer)
                    return false;
                handle = *buffer;
                return true;
            };

            const size_t pointBytes = points.size() * sizeof(glm::vec3);

            if (!allocateBuffer(job.Positions, pointBytes) ||
                !allocateBuffer(job.Labels, sizeof(uint32_t) * job.PointCount) ||
                !allocateBuffer(job.Distances, sizeof(float) * job.PointCount) ||
                !allocateBuffer(job.Centroids, sizeof(glm::vec3) * job.ClusterCount) ||
                !allocateBuffer(job.Sums, sizeof(glm::vec3) * job.ClusterCount) ||
                !allocateBuffer(job.ClusterSizes, sizeof(uint32_t) * job.ClusterCount))
            {
                cleanup();
                return false;
            }

            const std::vector<uint32_t> zeroLabels(job.PointCount, 0u);
            const std::vector<float> zeroDistances(job.PointCount, 0.0f);
            if (!cudaDevice->CopyHostToBufferAsync(job.Positions, points.data(), pointBytes, job.Stream) ||
                !cudaDevice->CopyHostToBufferAsync(job.Labels, zeroLabels.data(), zeroLabels.size() * sizeof(uint32_t), job.Stream) ||
                !cudaDevice->CopyHostToBufferAsync(job.Distances, zeroDistances.data(), zeroDistances.size() * sizeof(float), job.Stream))
            {
                cleanup();
                return false;
            }

            const std::vector<glm::vec3> existingCentroids =
                SnapshotExistingCentroids(engine.GetScene().GetRegistry(), target);
            const std::vector<glm::vec3> centroids = Geometry::KMeans::BuildInitialCentroids(
                points, existingCentroids, params, job.ClusterCount);
            if (!cudaDevice->CopyHostToBufferAsync(job.Centroids, centroids.data(), centroids.size() * sizeof(glm::vec3), job.Stream))
            {
                cleanup();
                return false;
            }

            if (cudaDevice->RecordEvent(job.StartEvent, job.Stream) != RHI::CudaError::Success)
            {
                cleanup();
                return false;
            }

            const bool launchOk = CudaKernels::LaunchLloyd(
                job.Stream,
                job.Positions.Ptr,
                job.PointCount,
                job.Centroids.Ptr,
                job.ClusterCount,
                job.Iterations,
                job.Labels.Ptr,
                job.Distances.Ptr,
                job.Sums.Ptr,
                job.ClusterSizes.Ptr);
            if (!launchOk)
            {
                cleanup();
                return false;
            }

            if (cudaDevice->RecordEvent(job.CompletionEvent, job.Stream) != RHI::CudaError::Success)
            {
                cleanup();
                return false;
            }

            MarkPending(target, Geometry::KMeans::Backend::CUDA, job.ClusterCount, job.Iterations);
            auto* pendingJobs = GetPendingCudaJobRegistry().TryAccess("PointCloudKMeans::ScheduleCudaJob");
            if (!pendingJobs)
            {
                cleanup();
                ClearPending(target);
                return false;
            }

            const auto [_, inserted] = pendingJobs->emplace(MakeEntityKey(entity), std::move(job));
            if (!inserted)
            {
                cleanup();
                ClearPending(target);
                Core::Log::Warn("PointCloudKMeans: duplicate CUDA job detected for entity {}.",
                                static_cast<uint32_t>(static_cast<entt::id_type>(entity)));
                return false;
            }
            return true;
        }
#endif

#ifdef INTRINSIC_HAS_CUDA
        class ScopedCudaContext
        {
        public:
            ScopedCudaContext(CUcontext context, std::string_view op)
                : m_Context(context)
                , m_Operation(op)
            {
                m_Result = cuCtxPushCurrent(m_Context);
                m_Active = (m_Result == CUDA_SUCCESS);
            }

            ~ScopedCudaContext()
            {
                if (!m_Active)
                    return;
                CUcontext popped = nullptr;
                if (const CUresult result = cuCtxPopCurrent(&popped); result != CUDA_SUCCESS)
                {
                    Core::Log::Warn("{}: cuCtxPopCurrent failed (CUresult={}).",
                                    m_Operation,
                                    static_cast<int>(result));
                }
            }

            [[nodiscard]] bool Ok() const { return m_Active; }
            [[nodiscard]] RHI::CudaError Error() const
            {
                if (m_Result == CUDA_ERROR_INVALID_CONTEXT || m_Result == CUDA_ERROR_NOT_INITIALIZED)
                    return RHI::CudaError::NotInitialized;
                return RHI::CudaError::Unknown;
            }

        private:
            CUcontext m_Context = nullptr;
            std::string_view m_Operation{};
            CUresult m_Result = CUDA_SUCCESS;
            bool m_Active = false;
        };

        [[nodiscard]] bool EnsureCudaResources(
            ECS::PointCloud::Data& pcData,
            RHI::CudaDevice& cudaDevice,
            uint32_t pointCount,
            uint32_t clusterCount)
        {
            if (!pcData.CudaStream)
            {
                auto stream = cudaDevice.CreateStream();
                if (!stream)
                    return false;
                pcData.CudaStream = *stream;
            }
            if (!pcData.CudaStartEvent)
            {
                auto event = cudaDevice.CreateEvent();
                if (!event)
                    return false;
                pcData.CudaStartEvent = *event;
            }
            if (!pcData.CudaCompletionEvent)
            {
                auto event = cudaDevice.CreateEvent();
                if (!event)
                    return false;
                pcData.CudaCompletionEvent = *event;
            }

            const auto ensureBuffer = [&](RHI::CudaBufferHandle& handle, uint32_t& capacity, uint32_t requiredCount, size_t stride) -> bool
            {
                if (capacity >= requiredCount && handle)
                    return true;
                cudaDevice.FreeBuffer(handle);
                auto buffer = cudaDevice.AllocateBuffer(static_cast<size_t>(requiredCount) * stride);
                if (!buffer)
                    return false;
                handle = *buffer;
                capacity = requiredCount;
                return true;
            };

            return ensureBuffer(pcData.CudaPositions, pcData.CudaPointCapacity, pointCount, sizeof(glm::vec3)) &&
                   ensureBuffer(pcData.CudaLabels, pcData.CudaPointCapacity, pointCount, sizeof(uint32_t)) &&
                   ensureBuffer(pcData.CudaDistances, pcData.CudaPointCapacity, pointCount, sizeof(float)) &&
                   ensureBuffer(pcData.CudaCentroids, pcData.CudaClusterCapacity, clusterCount, sizeof(glm::vec3)) &&
                   ensureBuffer(pcData.CudaSums, pcData.CudaClusterCapacity, clusterCount, sizeof(glm::vec3)) &&
                   ensureBuffer(pcData.CudaClusterSizes, pcData.CudaClusterCapacity, clusterCount, sizeof(uint32_t));
        }

        [[nodiscard]] bool ScheduleCuda(
            Engine& engine,
            entt::entity entity,
            ECS::PointCloud::Data& pcData,
            const Geometry::KMeans::KMeansParams& params)
        {
            PROFILE_SCOPE("PointCloudKMeans::ScheduleCUDA");

            auto* cudaDevice = engine.GetCudaDevice();
            if (!cudaDevice || !pcData.CloudRef)
                return false;

            const auto positions = pcData.CloudRef->Positions();
            if (positions.empty())
                return false;

            const uint32_t pointCount = static_cast<uint32_t>(positions.size());
            const uint32_t clusterCount = std::min<uint32_t>(params.ClusterCount, pointCount);
            if (clusterCount == 0 || params.MaxIterations == 0)
                return false;

            if (!EnsureCudaResources(pcData, *cudaDevice, pointCount, clusterCount))
                return false;

            if (pcData.CudaPositionRevision != pcData.PositionRevision)
            {
                auto upload = cudaDevice->CopyHostToBufferAsync(
                    pcData.CudaPositions,
                    positions.data(),
                    positions.size_bytes(),
                    pcData.CudaStream);
                if (!upload)
                    return false;
                pcData.CudaPositionRevision = pcData.PositionRevision;
            }

            auto& reg = engine.GetScene().GetRegistry();
            ResolvedTarget target = ResolveTarget(reg, entity, Domain::PointCloudPoints);
            const std::vector<glm::vec3> existingCentroids = SnapshotExistingCentroids(reg, target);
            const std::vector<glm::vec3> centroids = Geometry::KMeans::BuildInitialCentroids(
                positions, existingCentroids, params, clusterCount);
            auto uploadCentroids = cudaDevice->CopyHostToBufferAsync(
                pcData.CudaCentroids,
                centroids.data(),
                centroids.size() * sizeof(glm::vec3),
                pcData.CudaStream);
            if (!uploadCentroids)
                return false;

            if (cudaDevice->RecordEvent(pcData.CudaStartEvent, pcData.CudaStream) != RHI::CudaError::Success)
                return false;

            ScopedCudaContext context(cudaDevice->GetContext(), "PointCloudKMeans::ScheduleCUDA");
            if (!context.Ok())
                return false;

            const bool launchOk = CudaKernels::LaunchLloyd(
                pcData.CudaStream,
                pcData.CudaPositions.Ptr,
                pointCount,
                pcData.CudaCentroids.Ptr,
                clusterCount,
                params.MaxIterations,
                pcData.CudaLabels.Ptr,
                pcData.CudaDistances.Ptr,
                pcData.CudaSums.Ptr,
                pcData.CudaClusterSizes.Ptr);
            if (!launchOk)
                return false;

            if (cudaDevice->RecordEvent(pcData.CudaCompletionEvent, pcData.CudaStream) != RHI::CudaError::Success)
                return false;

            pcData.KMeansJobPending = true;
            pcData.KMeansPendingClusterCount = clusterCount;
            pcData.KMeansLastIterations = params.MaxIterations;
            pcData.KMeansLastConverged = false;
            return true;
        }
#endif
    }

    TargetInfo DescribeTarget(Engine& engine, entt::entity entity, Domain requestedDomain)
    {
        auto& reg = engine.GetScene().GetRegistry();
        const ResolvedTarget target = ResolveTarget(reg, entity, requestedDomain);

        TargetInfo info{};
        info.ResolvedDomain = target.Domain;
        info.SupportsCuda = target.SupportsCuda();

        switch (target.Domain)
        {
        case Domain::MeshVertices:
            if (target.MeshData)
            {
                info.PointCount = target.MeshData->VertexCount();
                info.JobPending = target.MeshData->KMeansJobPending;
            }
            break;
        case Domain::GraphVertices:
            if (target.GraphData)
            {
                info.PointCount = target.GraphData->NodeCount();
                info.JobPending = target.GraphData->KMeansJobPending;
            }
            break;
        case Domain::PointCloudPoints:
            if (target.PointCloudData)
            {
                info.PointCount = target.PointCloudData->PointCount();
                info.JobPending = target.PointCloudData->KMeansJobPending;
            }
            break;
        case Domain::Auto:
        default:
            break;
        }

        return info;
    }

    bool PublishResult(Engine& engine,
                       entt::entity entity,
                       const Geometry::KMeans::KMeansResult& result,
                       double durationMs,
                       Domain requestedDomain)
    {
        PROFILE_SCOPE("PointCloudKMeans::ApplyResult");

        auto& reg = engine.GetScene().GetRegistry();
        ResolvedTarget target = ResolveTarget(reg, entity, requestedDomain);
        if (!target.IsValid())
            return false;

        ClearPending(target);

        bool published = false;
        switch (target.Domain)
        {
        case Domain::MeshVertices:
            if (!target.MeshData)
                return false;
            published = PublishMeshVertexResult(*target.MeshData, result, durationMs);
            break;
        case Domain::GraphVertices:
            if (!target.GraphData)
                return false;
            published = PublishGraphVertexResult(*target.GraphData, result, durationMs);
            break;
        case Domain::PointCloudPoints:
            if (!target.PointCloudData)
                return false;
            published = PublishPointCloudPointResult(*target.PointCloudData, result, durationMs);
            break;
        case Domain::Auto:
        default:
            return false;
        }

        if (!published)
            return false;

        BumpKMeansRevision(target);
        UpdateCentroidEntity(engine, entity, target, result);
        MarkVertexAttributesDirty(reg, entity);
        engine.GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({entity});
        return true;
    }

    bool PublishMeshVertexResult(ECS::Mesh::Data& meshData,
                                 const Geometry::KMeans::KMeansResult& result,
                                 double durationMs)
    {
        if (!meshData.MeshRef)
            return false;

        auto& mesh = *meshData.MeshRef;
        std::vector<uint32_t> handles;
        const auto active = SnapshotMeshVertices(mesh, &handles);
        if (active.size() != result.Labels.size() || result.SquaredDistances.size() != result.Labels.size())
            return false;

        auto labelProp = Geometry::VertexProperty<uint32_t>(
            mesh.VertexProperties().GetOrAdd<uint32_t>("v:kmeans_label", 0u));
        auto labelFloatProp = Geometry::VertexProperty<float>(
            mesh.VertexProperties().GetOrAdd<float>("v:kmeans_label_f", 0.0f));
        auto distanceProp = Geometry::VertexProperty<float>(
            mesh.VertexProperties().GetOrAdd<float>("v:kmeans_distance", 0.0f));
        auto colorProp = Geometry::VertexProperty<glm::vec4>(
            mesh.VertexProperties().GetOrAdd<glm::vec4>("v:kmeans_color", glm::vec4(1.0f)));

        for (std::size_t i = 0; i < handles.size(); ++i)
        {
            const auto vh = Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(handles[i])};
            labelProp[vh] = result.Labels[i];
            labelFloatProp[vh] = static_cast<float>(result.Labels[i]);
            distanceProp[vh] = result.SquaredDistances[i];
            colorProp[vh] = LabelColor(result.Labels[i]);
        }

        // Use the float label property with colormap for Voronoi texel rendering.
        // The shader uses nearest-vertex selection (not interpolation) to produce
        // sharp Voronoi-like cluster boundaries on each triangle face.
        meshData.Visualization.VertexColors.PropertyName = "v:kmeans_label_f";
        meshData.Visualization.VertexColors.AutoRange = true;
        meshData.Visualization.UseNearestVertexColors = true;
        meshData.AttributesDirty = true;
        meshData.KMeansLastBackend = result.ActualBackend;
        meshData.KMeansLastIterations = result.Iterations;
        meshData.KMeansLastConverged = result.Converged;
        meshData.KMeansLastInertia = result.Inertia;
        meshData.KMeansLastMaxDistanceIndex = result.MaxDistanceIndex;
        meshData.KMeansLastDurationMs = durationMs;
        meshData.KMeansCentroids = result.Centroids;
        return true;
    }

    bool PublishGraphVertexResult(ECS::Graph::Data& graphData,
                                  const Geometry::KMeans::KMeansResult& result,
                                  double durationMs)
    {
        if (!graphData.GraphRef)
            return false;

        auto& graph = *graphData.GraphRef;
        std::vector<uint32_t> handles;
        const auto active = SnapshotGraphVertices(graph, &handles);
        if (active.size() != result.Labels.size() || result.SquaredDistances.size() != result.Labels.size())
            return false;

        auto labelProp = graph.GetOrAddVertexProperty<uint32_t>("v:kmeans_label", 0u);
        auto distanceProp = graph.GetOrAddVertexProperty<float>("v:kmeans_distance", 0.0f);
        auto colorProp = graph.GetOrAddVertexProperty<glm::vec4>("v:kmeans_color", glm::vec4(1.0f));

        for (std::size_t i = 0; i < handles.size(); ++i)
        {
            const auto vh = Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(handles[i])};
            labelProp[vh] = result.Labels[i];
            distanceProp[vh] = result.SquaredDistances[i];
            colorProp[vh] = LabelColor(result.Labels[i]);
        }

        graphData.Visualization.VertexColors.PropertyName = "v:kmeans_color";
        graphData.KMeansLastBackend = result.ActualBackend;
        graphData.KMeansLastIterations = result.Iterations;
        graphData.KMeansLastConverged = result.Converged;
        graphData.KMeansLastInertia = result.Inertia;
        graphData.KMeansLastMaxDistanceIndex = result.MaxDistanceIndex;
        graphData.KMeansLastDurationMs = durationMs;
        return true;
    }

    bool PublishPointCloudPointResult(ECS::PointCloud::Data& pointCloudData,
                                      const Geometry::KMeans::KMeansResult& result,
                                      double durationMs)
    {
        if (!pointCloudData.CloudRef)
            return false;

        auto& cloud = *pointCloudData.CloudRef;
        if (cloud.PointCount() != result.Labels.size() || result.SquaredDistances.size() != result.Labels.size())
            return false;

        auto labelProp = cloud.GetOrAddVertexProperty<uint32_t>("p:kmeans_label", 0u);
        auto distanceProp = cloud.GetOrAddVertexProperty<float>("p:kmeans_distance", 0.0f);
        auto colorProp = cloud.GetOrAddVertexProperty<glm::vec4>("p:kmeans_color", glm::vec4(1.0f));

        for (std::size_t i = 0; i < result.Labels.size(); ++i)
        {
            const auto vh = Geometry::PointCloud::Cloud::Handle(i);
            labelProp[vh] = result.Labels[i];
            distanceProp[vh] = result.SquaredDistances[i];
            colorProp[vh] = LabelColor(result.Labels[i]);
        }

        pointCloudData.Visualization.VertexColors.PropertyName = "p:kmeans_color";
        pointCloudData.KMeansLastBackend = result.ActualBackend;
        pointCloudData.KMeansLastIterations = result.Iterations;
        pointCloudData.KMeansLastConverged = result.Converged;
        pointCloudData.KMeansLastInertia = result.Inertia;
        pointCloudData.KMeansLastMaxDistanceIndex = result.MaxDistanceIndex;
        pointCloudData.KMeansLastDurationMs = durationMs;
        return true;
    }

    bool Schedule(Engine& engine,
                  entt::entity entity,
                  const Geometry::KMeans::KMeansParams& params,
                  Domain requestedDomain)
    {
        PROFILE_SCOPE("PointCloudKMeans::Schedule");

        auto& reg = engine.GetScene().GetRegistry();
        ResolvedTarget target = ResolveTarget(reg, entity, requestedDomain);
        if (!target.IsValid())
            return false;

        const auto targetInfo = DescribeTarget(engine, entity, requestedDomain);
        if (!targetInfo.IsValid() || targetInfo.JobPending)
            return false;

#ifdef INTRINSIC_HAS_CUDA
        if (params.Compute == Geometry::KMeans::Backend::CUDA)
        {
            if (target.Domain == Domain::PointCloudPoints)
            {
                if (ScheduleCuda(engine, entity, *target.PointCloudData, params))
                    return true;
            }
            else if (ScheduleCudaJob(engine, entity, target, params))
                return true;

            Core::Log::Warn("PointCloudKMeans: CUDA scheduling failed for entity {}. Falling back to CPU.",
                            static_cast<uint32_t>(static_cast<entt::id_type>(entity)));
        }
#endif

        std::vector<glm::vec3> snapshot = SnapshotPoints(target);
        std::vector<glm::vec3> initialCentroids = SnapshotExistingCentroids(reg, target);
        const Geometry::KMeans::KMeansParams cpuParams = [&]
        {
            auto copy = params;
            copy.Compute = Geometry::KMeans::Backend::CPU;
            return copy;
        }();

        if (snapshot.empty())
            return false;

        const uint32_t clusterCount = std::min<uint32_t>(cpuParams.ClusterCount,
                                                         static_cast<uint32_t>(snapshot.size()));
        MarkPending(target, Geometry::KMeans::Backend::CPU, clusterCount, cpuParams.MaxIterations);

        const Domain resolvedDomain = target.Domain;

        Core::Tasks::Scheduler::Dispatch([&engine,
                                          entity,
                                          resolvedDomain,
                                          snapshot = std::move(snapshot),
                                          initialCentroids = std::move(initialCentroids),
                                          cpuParams]() mutable
        {
            PROFILE_SCOPE("PointCloudKMeans::CPUWorker");
            const auto start = std::chrono::high_resolution_clock::now();
            Geometry::KMeans::CpuScratch scratch{};
            const auto result = Geometry::KMeans::Cluster(snapshot, initialCentroids, cpuParams, &scratch);
            const auto end = std::chrono::high_resolution_clock::now();
            const double durationMs = std::chrono::duration<double, std::milli>(end - start).count();

            auto payload = std::make_unique<CpuCompletionPayload>();
            payload->Owner = &engine;
            payload->Entity = entity;
            payload->Domain = resolvedDomain;
            payload->Result = result;
            payload->DurationMs = durationMs;

            engine.RunOnMainThread([payload = std::move(payload)]() mutable
            {
                Engine& owner = *payload->Owner;
                const entt::entity completedEntity = payload->Entity;

                if (!payload->Result)
                {
                    auto& reg = owner.GetScene().GetRegistry();
                    if (reg.valid(completedEntity))
                    {
                        ResolvedTarget target = ResolveTarget(reg, completedEntity, payload->Domain);
                        ClearPending(target);
                    }
                    return;
                }

                static_cast<void>(PublishResult(owner,
                                                completedEntity,
                                                *payload->Result,
                                                payload->DurationMs,
                                                payload->Domain));
            });
        });

        return true;
    }

    void PumpCompletions(Engine& engine)
    {
        PROFILE_SCOPE("PointCloudKMeans::PumpCompletions");

#ifdef INTRINSIC_HAS_CUDA
        auto* cudaDevice = engine.GetCudaDevice();
        if (!cudaDevice)
            return;

        auto* pendingJobs = GetPendingCudaJobRegistry().TryAccess("PointCloudKMeans::PumpCompletions");
        if (!pendingJobs)
            return;

        auto& reg = engine.GetScene().GetRegistry();

        for (auto it = pendingJobs->begin(); it != pendingJobs->end(); )
        {
            auto& job = it->second;
            if (!reg.valid(job.Entity))
            {
                DestroyPendingCudaJob(*cudaDevice, job);
                it = pendingJobs->erase(it);
                continue;
            }

            auto ready = cudaDevice->IsEventComplete(job.CompletionEvent);
            if (!ready)
            {
                ++it;
                continue;
            }
            if (!*ready)
            {
                ++it;
                continue;
            }

            std::vector<uint32_t> labels(job.PointCount, 0u);
            std::vector<float> distances(job.PointCount, 0.0f);
            std::vector<glm::vec3> centroids(job.ClusterCount, glm::vec3(0.0f));

            auto copyLabels = cudaDevice->CopyBufferToHost(labels.data(), job.Labels, labels.size() * sizeof(uint32_t));
            auto copyDistances = cudaDevice->CopyBufferToHost(distances.data(), job.Distances, distances.size() * sizeof(float));
            auto copyCentroids = cudaDevice->CopyBufferToHost(centroids.data(), job.Centroids, centroids.size() * sizeof(glm::vec3));
            if (!copyLabels || !copyDistances || !copyCentroids)
            {
                Core::Log::Warn("PointCloudKMeans: failed to download CUDA results for entity {}.",
                                static_cast<uint32_t>(static_cast<entt::id_type>(job.Entity)));
                if (reg.valid(job.Entity))
                {
                    ResolvedTarget target = ResolveTarget(reg, job.Entity, job.TargetDomain);
                    ClearPending(target);
                }
                DestroyPendingCudaJob(*cudaDevice, job);
                it = pendingJobs->erase(it);
                continue;
            }

            Geometry::KMeans::Result result{};
            result.Labels = std::move(labels);
            result.SquaredDistances = std::move(distances);
            result.Centroids = std::move(centroids);
            result.Iterations = job.Iterations;
            result.Converged = false;
            result.ActualBackend = Geometry::KMeans::Backend::CUDA;

            float inertia = 0.0f;
            float maxDistance = -1.0f;
            uint32_t maxDistanceIndex = 0;
            for (std::size_t i = 0; i < result.SquaredDistances.size(); ++i)
            {
                inertia += result.SquaredDistances[i];
                if (result.SquaredDistances[i] > maxDistance)
                {
                    maxDistance = result.SquaredDistances[i];
                    maxDistanceIndex = static_cast<uint32_t>(i);
                }
            }
            result.Inertia = inertia;
            result.MaxDistanceIndex = maxDistanceIndex;

            double durationMs = 0.0;
            if (auto elapsed = cudaDevice->GetElapsedMilliseconds(job.StartEvent, job.CompletionEvent))
                durationMs = *elapsed;

            static_cast<void>(PublishResult(engine, job.Entity, result, durationMs, job.TargetDomain));

            DestroyPendingCudaJob(*cudaDevice, job);
            it = pendingJobs->erase(it);
        }

        auto view = reg.view<ECS::PointCloud::Data>();
        for (auto [entity, pcData] : view.each())
        {
            if (!pcData.KMeansJobPending || !pcData.CudaCompletionEvent || !pcData.CloudRef)
                continue;

            auto ready = cudaDevice->IsEventComplete(pcData.CudaCompletionEvent);
            if (!ready)
                continue;
            if (!*ready)
                continue;

            const uint32_t pointCount = static_cast<uint32_t>(pcData.CloudRef->PointCount());
            const uint32_t clusterCount = pcData.KMeansPendingClusterCount;
            if (pointCount == 0 || clusterCount == 0)
            {
                pcData.KMeansJobPending = false;
                pcData.KMeansPendingClusterCount = 0;
                continue;
            }

            std::vector<uint32_t> labels(pointCount, 0u);
            std::vector<float> distances(pointCount, 0.0f);
            std::vector<glm::vec3> centroids(clusterCount, glm::vec3(0.0f));

            auto copyLabels = cudaDevice->CopyBufferToHost(labels.data(), pcData.CudaLabels, labels.size() * sizeof(uint32_t));
            auto copyDistances = cudaDevice->CopyBufferToHost(distances.data(), pcData.CudaDistances, distances.size() * sizeof(float));
            auto copyCentroids = cudaDevice->CopyBufferToHost(centroids.data(), pcData.CudaCentroids, centroids.size() * sizeof(glm::vec3));
            if (!copyLabels || !copyDistances || !copyCentroids)
            {
                Core::Log::Warn("PointCloudKMeans: failed to download CUDA results for entity {}.",
                                static_cast<uint32_t>(static_cast<entt::id_type>(entity)));
                pcData.KMeansJobPending = false;
                pcData.KMeansPendingClusterCount = 0;
                continue;
            }

            Geometry::KMeans::Result result{};
            result.Labels = std::move(labels);
            result.SquaredDistances = std::move(distances);
            result.Centroids = std::move(centroids);
            result.Iterations = pcData.KMeansLastIterations;
            result.Converged = false;
            result.ActualBackend = Geometry::KMeans::Backend::CUDA;

            float inertia = 0.0f;
            float maxDistance = -1.0f;
            uint32_t maxDistanceIndex = 0;
            for (std::size_t i = 0; i < result.SquaredDistances.size(); ++i)
            {
                inertia += result.SquaredDistances[i];
                if (result.SquaredDistances[i] > maxDistance)
                {
                    maxDistance = result.SquaredDistances[i];
                    maxDistanceIndex = static_cast<uint32_t>(i);
                }
            }
            result.Inertia = inertia;
            result.MaxDistanceIndex = maxDistanceIndex;
            result.Iterations = pcData.KMeansLastIterations;

            double durationMs = 0.0;
            if (auto elapsed = cudaDevice->GetElapsedMilliseconds(pcData.CudaStartEvent, pcData.CudaCompletionEvent))
                durationMs = *elapsed;

            static_cast<void>(PublishResult(engine, entity, result, durationMs, Domain::PointCloudPoints));
        }
#else
        (void)engine;
#endif
    }

    void ReleaseEntityBuffers(Engine& engine, entt::entity entity)
    {
#ifdef INTRINSIC_HAS_CUDA
        auto* cudaDevice = engine.GetCudaDevice();
        if (!cudaDevice)
            return;

        if (auto* pendingJobs = GetPendingCudaJobRegistry().TryAccess("PointCloudKMeans::ReleaseEntityBuffers"))
        {
            const auto jobIt = pendingJobs->find(MakeEntityKey(entity));
            if (jobIt != pendingJobs->end())
            {
                DestroyPendingCudaJob(*cudaDevice, jobIt->second);
                pendingJobs->erase(jobIt);
            }
        }

        auto& reg = engine.GetScene().GetRegistry();
        if (!reg.valid(entity))
            return;

        const auto releaseCentroidPointCloud = [&](entt::entity centroidEntity)
        {
            if (centroidEntity == entt::null || !reg.valid(centroidEntity))
                return;
            if (auto* centroidData = reg.try_get<ECS::PointCloud::Data>(centroidEntity))
                centroidData->ReleaseCudaBuffers(*cudaDevice);
        };

        if (auto* meshData = reg.try_get<ECS::Mesh::Data>(entity))
            releaseCentroidPointCloud(meshData->KMeansCentroidEntity);
        if (auto* graphData = reg.try_get<ECS::Graph::Data>(entity))
            releaseCentroidPointCloud(graphData->KMeansCentroidEntity);
        if (auto* sourcePointCloud = reg.try_get<ECS::PointCloud::Data>(entity))
            releaseCentroidPointCloud(sourcePointCloud->KMeansCentroidEntity);

        if (auto* pcData = reg.try_get<ECS::PointCloud::Data>(entity))
            pcData->ReleaseCudaBuffers(*cudaDevice);
#else
        auto& reg = engine.GetScene().GetRegistry();
        if (!reg.valid(entity))
            return;
        if (auto* pcData = reg.try_get<ECS::PointCloud::Data>(entity))
        {
            pcData->KMeansJobPending = false;
            pcData->KMeansPendingClusterCount = 0;
        }
#endif
    }
}
