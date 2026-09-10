module;
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.GeometryProcessingOperations;
import Geometry.Graph.Utils;
import Geometry.SurfaceReconstruction;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Runtime.AssetWorkflowGeometryMaterialization;
import Extrinsic.Runtime.AssetWorkflowRecipePolicies;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
#include "Editor/Operations/Runtime.GeometryProcessingOperations.Internal.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace SR = Geometry::SurfaceReconstruction;
        namespace Detail = GeometryProcessingDetail;
        namespace Transform = ECS::Components::Transform;
        using D = GeometryElementDomain;
        using Clock = std::chrono::steady_clock;
        struct ConstructionWork
        {
            PointConstructionConfig Config{};
            entt::entity Entity{entt::null};
            glm::mat4 SourceMatrix{1};
            std::vector<Detail::PointPropertyWatch> Inputs{};
            std::vector<glm::vec3> Points{}, Normals{}, Queries{};
            std::vector<std::uint32_t> Slots{}, Offsets{0}, Indices{};
            std::vector<float> Field{};
            std::optional<SR::PreparedReconstruction> Prepared{};
            std::optional<Geometry::HalfedgeMesh::Mesh> Mesh{};
            std::optional<Geometry::Graph::Graph> Graph{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            std::shared_ptr<SpatialNearestBatch> Batch{};
            std::size_t NextQuery{};
            std::uint32_t Width{};
            bool PreparedOk{}, QueriesComplete{}, Abandoned{};
            Clock::time_point GpuStarted{};
            std::optional<EditorPointConstructionResult> MainFailure{};
            EditorPointConstructionResult Result{};
        };
        bool Attached(const EditorGeometryProcessingContext& c)
        {
            return c.Scene && (!c.AttachmentActive || c.AttachmentActive());
        }
        std::optional<glm::mat4> WorldMatrix(const entt::registry& raw, entt::entity entity)
        {
            glm::mat4 matrix{1};
            std::vector<entt::entity> visited;
            while (entity != entt::null)
            {
                if (!raw.valid(entity) || visited.size() >= 4096 ||
                    std::ranges::find(visited, entity) != visited.end())
                    return {};
                visited.push_back(entity);
                if (const auto* t = raw.try_get<Transform::Component>(entity))
                    matrix = Transform::GetMatrix(*t) * matrix;
                const auto* hierarchy = raw.try_get<ECS::Components::Hierarchy::Component>(entity);
                entity = hierarchy ? hierarchy->Parent : entt::null;
            }
            for (unsigned i = 0; i < 4; ++i)
                for (unsigned j = 0; j < 4; ++j)
                    if (!std::isfinite(matrix[i][j]))
                        return {};
            return matrix;
        }
        bool Current(const EditorGeometryProcessingContext& c, const ConstructionWork& w)
        {
            if (!Attached(c) || !Detail::GeometryPropertiesCurrent(c, w.Entity, w.Inputs))
                return false;
            const auto matrix = WorldMatrix(c.Scene->Raw(), w.Entity);
            return matrix && *matrix == w.SourceMatrix;
        }
        bool GpuPoint(glm::vec3 p)
        {
            if (!Geometry::PointLBVH::ValidPoint(p))
                return false;
            for (unsigned i = 0; i < 3; ++i)
            {
                const auto magnitude = std::bit_cast<std::uint32_t>(p[i]) & 0x7fffffffu;
                if (magnitude && magnitude < 0x00800000u)
                    return false;
            }
            return true;
        }
        std::shared_ptr<ConstructionWork> Capture(const EditorGeometryProcessingContext& context,
                                                  PointConstructionConfig c,
                                                  std::string& diagnostic, bool preview = false)
        {
            const auto fail = [&](std::string why) -> std::shared_ptr<ConstructionWork>
            {
                diagnostic = std::move(why);
                return {};
            };
            const auto validation = ValidatePointConstructionConfigSection(
                SerializePointConstructionConfig(c), {}, kPointConstructionConfigSectionName);
            if (!validation.Usable())
                return fail(validation.Diagnostics.front().Message);
            if (!Attached(context))
                return fail("Scene is unavailable.");
            const auto entity =
                Detail::ResolveEditorStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity)
                return fail("Construction source is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (c.Positions.Domain == D::Unknown)
                c.Positions.Domain = Detail::PrimaryPointDomain(a);
            if (c.Normals.Domain == D::Unknown)
                c.Normals.Domain = c.Positions.Domain;
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!props || !ResolveGeometryProperty(a, c.Positions, props->Size(), false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved domain.");
            if (props->Size() > std::numeric_limits<std::uint32_t>::max())
                return fail("Input exceeds the supported slot range.");
            const bool supplied = c.Method == PointConstructionMethod::Hoppe && !c.EstimateNormals;
            if (supplied &&
                (c.Normals.Domain != c.Positions.Domain ||
                 !ResolveGeometryProperty(a, c.Normals, props->Size(), false).Resolved()))
                return fail("Hoppe reconstruction requires count-matched normals on the position "
                            "domain, or CPU normal estimation.");
            const auto matrix = WorldMatrix(context.Scene->Raw(), *entity);
            if (!matrix || !std::isfinite(glm::determinant(glm::mat3(*matrix))) ||
                glm::determinant(glm::mat3(*matrix)) == 0)
                return fail(
                    "The source hierarchy must have a finite, nonsingular world transform.");
            auto w = std::make_shared<ConstructionWork>();
            w->Config = c;
            w->Entity = *entity;
            w->SourceMatrix = *matrix;
            w->Result.Method = c.Method;
            w->Result.RequestedBackend = c.Backend;
            w->Inputs.push_back(
                Detail::ObserveGeometryProperty(a, c.Positions.Domain, c.Positions.Name));
            if (supplied)
                w->Inputs.push_back(
                    Detail::ObserveGeometryProperty(a, c.Normals.Domain, c.Normals.Name));
            auto deletionDomain = c.Positions.Domain;
            const char* deletionName = "v:deleted";
            std::size_t divisor = 1;
            if (deletionDomain == D::MeshFace)
                deletionName = "f:deleted";
            if (deletionDomain == D::MeshEdge || deletionDomain == D::GraphEdge)
                deletionName = "e:deleted";
            if (deletionDomain == D::MeshHalfedge || deletionDomain == D::GraphHalfedge)
            {
                deletionDomain = deletionDomain == D::MeshHalfedge ? D::MeshEdge : D::GraphEdge;
                deletionName = "e:deleted";
                divisor = 2;
            }
            const auto* deletionProps = ResolveGeometryPropertySet(a, deletionDomain);
            if (!deletionProps || props->Size() % divisor ||
                deletionProps->Size() != props->Size() / divisor)
                return fail("Invalid deletion domain/cardinality.");
            const auto deleted = deletionProps->Get<bool>(deletionName);
            if (deletionProps->Exists(deletionName) &&
                (!deleted || deleted.Size() != deletionProps->Size()))
                return fail("Deletion mask must be count-matched bool.");
            w->Inputs.push_back(Detail::ObserveGeometryProperty(a, deletionDomain, deletionName));
            const auto points = props->Get<glm::vec3>(c.Positions.Name),
                       normals = props->Get<glm::vec3>(c.Normals.Name);
            for (std::uint32_t i = 0; i < props->Size(); ++i)
            {
                if (deleted && deleted[i / divisor])
                    continue;
                const auto point = points[i];
                if (!Geometry::PointLBVH::ValidPoint(point))
                    return fail("Live positions must be finite and within the shared 1e18 "
                                "coordinate limit.");
                if (c.Backend == PointConstructionBackend::VulkanLBVH && !GpuPoint(point))
                    return fail(
                        "Vulkan construction does not support subnormal coordinate components.");
                if (supplied && (!Detail::FinitePosition(normals[i]) ||
                                 !std::isfinite(glm::dot(normals[i], normals[i])) ||
                                 glm::dot(normals[i], normals[i]) <= 1e-16f))
                    return fail("Live supplied normals must be finite and nonzero.");
                ++w->Result.InputCount;
                if (!preview)
                {
                    w->Points.push_back(point);
                    w->Slots.push_back(i);
                    if (supplied)
                        w->Normals.push_back(normals[i]);
                }
            }
            const auto count = w->Result.InputCount;
            if (count < (c.Method == PointConstructionMethod::Hoppe ? 3u : 1u) ||
                count > (1u << 20))
                return fail("Construction requires 3..1048576 live samples for Hoppe or 1..1048576 "
                            "for graphs.");
            if (c.Method == PointConstructionMethod::KnnGraph &&
                count * std::min<std::size_t>(count, c.KNeighbors + 1) > (1u << 24))
                return fail("Graph neighborhood storage exceeds 16777216 candidate entries; reduce "
                            "k or the sample count.");
            if (c.Backend != PointConstructionBackend::CpuReference && !context.SpatialIndices)
                return fail("The shared spatial cache is unavailable.");
            if (c.Backend == PointConstructionBackend::VulkanLBVH &&
                (!context.JobCommands.Available() ||
                 !context.SpatialIndices->GpuQueriesAvailable()))
                return fail("Vulkan construction requires framed GPU queries and editor jobs.");
            return w;
        }
        SR::ReconstructionParams ReconstructionParams(const PointConstructionConfig& c)
        {
            SR::ReconstructionParams p;
            p.Resolution = c.Resolution;
            p.MaxGridVertices = c.MaxGridVertices;
            p.KNeighbors = c.KNeighbors;
            p.EstimateNormals = c.EstimateNormals;
            p.NormalKNeighbors = c.NormalKNeighbors;
            p.BoundingBoxPadding = c.BoundingBoxPadding;
            p.NormalAgreementPower = c.NormalAgreementPower;
            p.KernelSigmaScale = c.KernelSigmaScale;
            return p;
        }
        void Prepare(ConstructionWork& w)
        {
            const auto start = Clock::now();
            auto& r = w.Result;
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            if (w.Config.Method == PointConstructionMethod::Hoppe)
            {
                w.Prepared = SR::Prepare(w.Points, w.Normals, ReconstructionParams(w.Config));
                if (!w.Prepared || w.Prepared->Points != w.Points)
                {
                    r.Message =
                        "Reconstruction preparation failed: check normal estimation, nondegenerate "
                        "extent and grid budget. Every live sample must survive preparation.";
                    return;
                }
                r.QueryCount = w.Prepared->Dimensions.VertexCount();
                w.Field.reserve(r.QueryCount);
                w.Width = std::min<std::size_t>(
                    w.Points.size(), w.Config.KNeighbors == 1 ? 1 : w.Config.KNeighbors + 1);
            }
            else
            {
                r.QueryCount = w.Points.size();
                w.Width = std::min<std::size_t>(w.Points.size(), w.Config.KNeighbors + 1);
                w.Indices.reserve(r.QueryCount * w.Width);
                w.Offsets.reserve(r.QueryCount + 1);
            }
            w.PreparedOk = true;
            r.CpuComputeMilliseconds +=
                std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }
        std::vector<glm::vec3> Queries(const ConstructionWork& w, std::size_t first,
                                       std::size_t count)
        {
            if (w.Prepared)
                return SR::GridQueries(*w.Prepared, first, count);
            return {w.Points.begin() + first, w.Points.begin() + first + count};
        }
        bool ConsumeRows(ConstructionWork& w, std::span<const glm::vec3> queries,
                         std::span<const std::uint32_t> offsets,
                         std::span<const std::uint32_t> indices)
        {
            const auto start = Clock::now();
            if (w.Prepared)
            {
                const auto field =
                    SR::EvaluateSignedDistances(w.Points, w.Prepared->Normals, queries,
                                                {offsets, indices}, ReconstructionParams(w.Config));
                if (!field)
                {
                    w.Result.Message =
                        "The reconstruction field rejected incomplete or invalid neighborhoods.";
                    return false;
                }
                w.Field.insert(w.Field.end(), field->begin(), field->end());
            }
            else
            {
                const auto base = w.Indices.size();
                w.Indices.insert(w.Indices.end(), indices.begin(), indices.end());
                for (std::size_t i = 1; i < offsets.size(); ++i)
                    w.Offsets.push_back(std::uint32_t(base + offsets[i]));
            }
            w.Result.CpuComputeMilliseconds +=
                std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            return true;
        }
        void QueryCpu(ConstructionWork& w, const JobCancellation* cancellation = nullptr)
        {
            if (!w.PreparedOk)
                return;
            const auto start = Clock::now();
            const auto priorCpu = w.Result.CpuComputeMilliseconds;
            w.Result.ActualBackend = ToString(w.Config.Backend);
            for (std::size_t first = 0; first < w.Result.QueryCount;
                 first += w.Config.GpuQueryBatchSize)
            {
                if (cancellation && cancellation->IsCancelled())
                {
                    w.Result.Message = "Construction cancelled.";
                    return;
                }
                const auto queries = Queries(
                    w, first,
                    std::min<std::size_t>(w.Config.GpuQueryBatchSize, w.Result.QueryCount - first));
                std::vector<std::uint32_t> offsets{0}, indices;
                indices.reserve(queries.size() * w.Width);
                for (auto q : queries)
                {
                    const auto row =
                        w.Index ? w.Index->Index.KNearest(q, w.Width)
                                : Geometry::PointLBVH::KNearestReference(w.Points, q, w.Width);
                    if (row.size() != w.Width)
                    {
                        w.Result.Message = "CPU construction received incomplete neighbors.";
                        return;
                    }
                    for (const auto& n : row)
                        indices.push_back(n.Index);
                    offsets.push_back(indices.size());
                }
                if (!ConsumeRows(w, queries, offsets, indices))
                    return;
            }
            w.QueriesComplete = true;
            w.Result.ActualBackend = ToString(w.Config.Backend);
            w.Result.CpuComputeMilliseconds =
                priorCpu + std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }
        bool AdvanceGpu(const EditorGeometryProcessingContext& context, ConstructionWork& w)
        {
            const auto fail = [&](std::string message)
            {
                auto r = w.Result;
                r.Status = EditorCommandStatus::GeometryProcessingFailed;
                r.Message = std::move(message);
                w.MainFailure = std::move(r);
                w.Batch.reset();
                return true;
            };
            if (w.Abandoned || !Current(context, w))
                return fail("Construction source changed or the job was cancelled.");
            if (w.Batch)
            {
                if (w.Batch->State == SpatialQueryState::Failed)
                    return fail(w.Batch->Diagnostic);
                if (w.Batch->State != SpatialQueryState::Ready)
                    return false;
                w.Result.ActualBackend = "vulkan_lbvh";
                if (w.Batch->Capacity != w.Width || w.Batch->Counts.size() != w.Queries.size() ||
                    w.Batch->Neighbors.size() != w.Queries.size() * w.Width)
                    return fail("Vulkan construction returned an invalid neighborhood layout.");
                std::vector<std::uint32_t> offsets{0}, indices;
                indices.reserve(w.Queries.size() * w.Width);
                for (std::size_t i = 0; i < w.Queries.size(); ++i)
                {
                    if (w.Batch->Counts[i] != w.Width)
                        return fail("Vulkan construction returned an incomplete neighborhood.");
                    const auto first = indices.size();
                    for (unsigned j = 0; j < w.Width; ++j)
                    {
                        const auto slot = w.Batch->Neighbors[i * w.Width + j].Index;
                        const auto found = std::lower_bound(w.Slots.begin(), w.Slots.end(), slot);
                        if (found == w.Slots.end() || *found != slot)
                            return fail("Vulkan construction returned an unknown source slot.");
                        indices.push_back(std::uint32_t(found - w.Slots.begin()));
                    }
                    // CPU reduction uses a stable float distance/source-ID order;
                    // recompute it after GPU readback instead of relying on GPU arithmetic order.
                    std::sort(indices.begin() + first, indices.end(),
                              [&](auto a, auto b)
                              {
                                  const auto da = w.Queries[i] - w.Points[a],
                                             db = w.Queries[i] - w.Points[b];
                                  const auto aa = glm::dot(da, da), bb = glm::dot(db, db);
                                  return aa < bb || (aa == bb && a < b);
                              });
                    offsets.push_back(indices.size());
                }
                if (!ConsumeRows(w, w.Queries, offsets, indices))
                    return fail(w.Result.Message);
                w.NextQuery += w.Queries.size();
                ++w.Result.GpuQueryBatches;
                w.Result.GpuNeighborhoodMilliseconds +=
                    std::chrono::duration<double, std::milli>(Clock::now() - w.GpuStarted).count();
            }
            if (w.NextQuery == w.Result.QueryCount)
            {
                w.QueriesComplete = true;
                w.Batch.reset();
                return true;
            }
            w.Queries = Queries(w, w.NextQuery,
                                std::min<std::size_t>(w.Config.GpuQueryBatchSize,
                                                      w.Result.QueryCount - w.NextQuery));
            if (!std::all_of(w.Queries.begin(), w.Queries.end(), GpuPoint))
                return fail("Vulkan grid queries require normal-or-zero components within 1e18.");
            if (w.Batch && w.Batch->Counts.size() != w.Queries.size())
                w.Batch.reset();
            w.GpuStarted = Clock::now();
            w.Batch = context.SpatialIndices->QueueGpuKNearest(w.GpuIndex, w.Queries, w.Width, {},
                                                               std::move(w.Batch));
            if (!w.Batch || w.Batch->State == SpatialQueryState::Failed)
                return fail(w.Batch ? w.Batch->Diagnostic : "Vulkan query submission rejected.");
            return false;
        }
        void BuildOutput(ConstructionWork& w)
        {
            if (!w.QueriesComplete)
                return;
            const auto start = Clock::now();
            auto& r = w.Result;
            if (w.Prepared)
            {
                auto extracted = SR::Extract(*w.Prepared, w.Field);
                if (!extracted || !extracted->OutputFaceCount)
                {
                    r.Message = "The sampled field produced no usable surface.";
                    return;
                }
                Geometry::MeshIO::MeshIOResult payload;
                payload.Vertices.Resize(extracted->OutputVertexCount);
                auto positions = payload.Vertices.Add<glm::vec3>("v:point");
                const auto source = std::as_const(extracted->OutputMesh).Positions();
                for (std::size_t i = 0; i < source.size(); ++i)
                {
                    positions[i] = glm::vec3(w.SourceMatrix * glm::vec4(source[i], 1));
                    if (!Geometry::PointLBVH::ValidPoint(positions[i]))
                    {
                        r.Message = "Generated world-space positions exceed the supported bounds.";
                        return;
                    }
                }
                payload.Faces.Resize(extracted->OutputFaceCount);
                auto faces = payload.Faces.Add<std::vector<std::uint32_t>>("f:vertices");
                const bool reflected = glm::determinant(glm::mat3(w.SourceMatrix)) < 0;
                for (auto f : extracted->OutputMesh.LiveFaces())
                {
                    for (auto v : extracted->OutputMesh.VerticesAroundFace(f))
                        faces[f.Index].push_back(v.Index);
                    if (reflected)
                        std::reverse(faces[f.Index].begin(), faces[f.Index].end());
                }
                auto materialized = BuildRuntimeHalfedgeMeshMaterialization(payload);
                if (!materialized)
                {
                    r.Message = "Generated mesh normal/UV materialization failed.";
                    return;
                }
                w.Mesh = std::move(materialized->Mesh);
                r.OutputVertexCount = w.Mesh->VertexCount();
                r.OutputEdgeCount = w.Mesh->EdgeCount();
                r.OutputFaceCount = w.Mesh->FaceCount();
            }
            else
            {
                Geometry::Graph::KNNBuildParams p;
                p.K = w.Config.KNeighbors;
                p.MinDistanceEpsilon = w.Config.MinDistanceEpsilon;
                p.Connectivity = w.Config.Mutual ? Geometry::Graph::KNNConnectivity::Mutual
                                                 : Geometry::Graph::KNNConnectivity::Union;
                w.Graph.emplace();
                const auto built = Geometry::Graph::BuildKNNGraphFromNeighbors(
                    *w.Graph, w.Points, {w.Offsets, w.Indices}, p);
                if (!built)
                {
                    w.Graph.reset();
                    r.Message = "Graph construction rejected the neighborhoods.";
                    return;
                }
                for (auto v : w.Graph->LiveVertices())
                {
                    const auto point =
                        glm::vec3(w.SourceMatrix * glm::vec4(w.Graph->VertexPosition(v), 1));
                    w.Graph->SetVertexPosition(v, point);
                    if (!Geometry::PointLBVH::ValidPoint(point))
                    {
                        w.Graph.reset();
                        r.Message = "Generated world-space positions exceed the supported bounds.";
                        return;
                    }
                }
                r.OutputVertexCount = w.Graph->VertexCount();
                r.OutputEdgeCount = w.Graph->EdgeCount();
            }
            r.CpuComputeMilliseconds +=
                std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            r.Status = EditorCommandStatus::Applied;
            r.Message = std::string(ToString(w.Config.Method)) + " constructed with " +
                        r.ActualBackend + " queries and CPU geometry extraction.";
        }
        struct GeneratedEntity
        {
            entt::entity Entity{entt::null};
            ECS::Components::StableId Identity{};
            entt::entity Source{entt::null};
            std::string Name{};
            std::optional<Geometry::HalfedgeMesh::Mesh> Mesh{};
            std::optional<Geometry::Graph::Graph> Graph{};
            std::uint64_t Metadata{};
            std::array<Geometry::PropertyRevision, 4> Revisions{};
        };
        std::array<Geometry::PropertyRevision, 4> GeometryRevisions(const entt::registry& raw,
                                                                    entt::entity entity)
        {
            const auto v = GS::BuildConstView(raw, entity);
            return {v.VertexSource ? v.VertexSource->Properties.Revision() : 0,
                    v.EdgeSource ? v.EdgeSource->Properties.Revision() : 0,
                    v.HalfedgeSource ? v.HalfedgeSource->Properties.Revision() : 0,
                    v.FaceSource ? v.FaceSource->Properties.Revision() : 0};
        }
        EditorPointConstructionResult Publish(const EditorGeometryProcessingContext& context,
                                              const std::shared_ptr<ConstructionWork>& w)
        {
            auto& r = w->Result;
            if (w->Abandoned || !Current(context, *w))
            {
                r.Status = EditorCommandStatus::StaleEntity;
                r.Message = "Construction source changed before publication.";
                return r;
            }
            if (!r.Succeeded())
                return r;
            auto generated = std::make_shared<GeneratedEntity>();
            generated->Source = w->Entity;
            generated->Name = w->Config.OutputName;
            generated->Mesh = std::move(w->Mesh);
            generated->Graph = std::move(w->Graph);
            auto& raw = context.Scene->Raw();
            generated->Identity = {0x504f494e54434f4eull, 1};
            for (auto e : raw.view<ECS::Components::StableId>())
            {
                const auto id = raw.get<ECS::Components::StableId>(e);
                if (id.High == generated->Identity.High && id.Low >= generated->Identity.Low)
                {
                    if (id.Low == std::numeric_limits<std::uint64_t>::max())
                    {
                        r.Status = EditorCommandStatus::GeometryProcessingFailed;
                        r.Message = "Generated identity range exhausted.";
                        return r;
                    }
                    generated->Identity.Low = id.Low + 1;
                }
            }
            const auto create = [context, generated]
            {
                if (!Attached(context))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto& scene = *context.Scene;
                auto& registry = scene.Raw();
                if (registry.valid(generated->Entity))
                    return EditorCommandHistoryStatus::StaleEntity;
                for (auto e : registry.view<ECS::Components::StableId>())
                    if (registry.get<ECS::Components::StableId>(e) == generated->Identity)
                        return EditorCommandHistoryStatus::StaleEntity;
                const auto entity = ECS::Scene::CreateDefault(scene, generated->Name);
                registry.emplace<ECS::Components::StableId>(entity, generated->Identity);
                if (generated->Mesh)
                    GS::PopulateFromMesh(registry, entity, *generated->Mesh);
                else
                    GS::PopulateFromGraph(registry, entity, *generated->Graph);
                const auto authored = ApplyAssetImportAuthoringRecipe(
                    generated->Mesh ? Assets::AssetPayloadKind::Mesh
                                    : Assets::AssetPayloadKind::Graph,
                    true, true, entity, scene);
                if (!authored)
                {
                    scene.Destroy(entity);
                    return EditorCommandHistoryStatus::CommandFailed;
                }
                const auto points =
                    std::as_const(registry).get<GS::Vertices>(entity).Properties.Get<glm::vec3>(
                        "v:position");
                glm::vec3 minimum = points[0], maximum = points[0];
                for (auto p : points.Vector())
                {
                    minimum = glm::min(minimum, p);
                    maximum = glm::max(maximum, p);
                }
                ECS::Components::Culling::Local::Bounds local{};
                local.LocalBoundingAABB.Min = minimum;
                local.LocalBoundingAABB.Max = maximum;
                local.LocalBoundingSphere.Center = (minimum + maximum) * 0.5f;
                local.LocalBoundingSphere.Radius = glm::length(maximum - minimum) * 0.5f;
                registry.emplace<ECS::Components::Culling::Local::Bounds>(entity, local);
                ECS::Components::Culling::World::Bounds world{};
                world.WorldBoundingOBB.Center = local.LocalBoundingSphere.Center;
                world.WorldBoundingOBB.Extents = (maximum - minimum) * 0.5f;
                world.WorldBoundingSphere = local.LocalBoundingSphere;
                registry.emplace<ECS::Components::Culling::World::Bounds>(entity, world);
                generated->Entity = entity;
                generated->Metadata =
                    Detail::EditorGeometryMetadataSignatureForEntity(registry, entity);
                generated->Revisions = GeometryRevisions(registry, entity);
                if (context.Selection)
                    (void)context.Selection->SetSelectedEntity(scene, entity);
                if (context.InvalidateWorkspaceSnapshotCache)
                    context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto remove = [context, generated]
            {
                if (!Attached(context))
                    return EditorCommandHistoryStatus::StaleEntity;
                const auto& registry = std::as_const(context.Scene->Raw());
                const auto entity = generated->Entity;
                if (!registry.valid(entity))
                    return EditorCommandHistoryStatus::StaleEntity;
                const auto* id = registry.try_get<ECS::Components::StableId>(entity);
                const auto* name = registry.try_get<ECS::Components::MetaData>(entity);
                const auto* hierarchy =
                    registry.try_get<ECS::Components::Hierarchy::Component>(entity);
                const auto* transform = registry.try_get<Transform::Component>(entity);
                if (!id || *id != generated->Identity || !name ||
                    name->EntityName != generated->Name || !hierarchy ||
                    hierarchy->Parent != entt::null || hierarchy->FirstChild != entt::null ||
                    hierarchy->ChildCount || !transform ||
                    Transform::GetMatrix(*transform) != glm::mat4(1) ||
                    generated->Metadata !=
                        Detail::EditorGeometryMetadataSignatureForEntity(registry, entity) ||
                    generated->Revisions != GeometryRevisions(registry, entity))
                    return EditorCommandHistoryStatus::StaleEntity;
                if (context.Selection)
                {
                    const auto selected = context.Selection->SelectedStableIds();
                    if (selected.size() == 1 &&
                        selected.front() == SelectionController::ToStableEntityId(entity))
                    {
                        if (registry.valid(generated->Source))
                            (void)context.Selection->SetSelectedEntity(*context.Scene,
                                                                       generated->Source);
                        else
                            context.Selection->ClearSelection(*context.Scene);
                    }
                }
                context.Scene->Destroy(entity);
                generated->Entity = entt::null;
                if (context.InvalidateWorkspaceSnapshotCache)
                    context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status = context.CommandHistory
                                    ? context.CommandHistory
                                          ->Execute({.Label = "Construct geometry from points",
                                                     .Redo = create,
                                                     .Undo = remove})
                                          .Status
                                    : create();
            r.Status = Detail::ToEditorMethodCommandStatus(status);
            if (r.Succeeded())
                r.OutputEntityId = SelectionController::ToStableEntityId(generated->Entity);
            else
                r.Message = "Generated entity publication was rejected.";
            return r;
        }
    } // namespace
    EditorPointConstructionReadiness
    PreviewEditorPointConstructionCommand(const EditorGeometryProcessingContext& context,
                                          const PointConstructionConfig& config)
    {
        EditorPointConstructionReadiness r;
        const auto w = Capture(context, config, r.Diagnostic, true);
        r.Ready = bool(w);
        if (w)
            r.Resolved = w->Config;
        return r;
    }
    GeometryPropertyCatalogSnapshot
    GetEditorPointConstructionInputCatalog(const EditorGeometryProcessingContext& context,
                                           std::uint32_t id)
    {
        return GetEditorKeypointAnalysisInputCatalog(context, id);
    }
    EditorPointConstructionResult
    ApplyEditorPointConstructionCommand(const EditorGeometryProcessingContext& context,
                                        const PointConstructionConfig& config)
    {
        std::string diagnostic;
        auto w = Capture(context, config, diagnostic);
        const auto report = [&](EditorCommandStatus status, std::string message)
        {
            auto r = w ? w->Result
                       : EditorPointConstructionResult{.Method = config.Method,
                                                       .RequestedBackend = config.Backend};
            r.Status = status;
            r.Message = std::move(message);
            return r;
        };
        if (!w)
            return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
        if (w->Config.Backend != PointConstructionBackend::CpuReference)
        {
            const auto acquired =
                context.SpatialIndices->Acquire(context.World, w->Entity, w->Config.Positions);
            if (!acquired.Ready())
                return report(EditorCommandStatus::InvalidProcessingParameters,
                              acquired.Diagnostic);
            w->GpuIndex = acquired.Handle;
            w->Index = context.SpatialIndices->Snapshot(acquired.Handle);
            w->Result.IndexReused = acquired.Reused;
            if (!w->Index || w->Index->Slots != w->Slots ||
                w->Index->Index.Points().size() != w->Points.size() ||
                !std::equal(w->Points.begin(), w->Points.end(), w->Index->Index.Points().begin()))
                return report(EditorCommandStatus::StaleEntity,
                              "Construction index does not match selected samples.");
        }
        if (!context.JobCommands.Available())
        {
            Prepare(*w);
            QueryCpu(*w);
            BuildOutput(*w);
            return Publish(context, w);
        }
        const EditorJobIdentity identity{
            .EntityId = config.StableEntityId,
            .Scope = ToEditorJobScope(w->Config.Positions.Domain),
            .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
            .OutputName = std::string("construct:") + ToString(config.Method)};
        if (context.JobCommands.FindActive)
            if (auto active = context.JobCommands.FindActive(identity);
                active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,
                              "A construction job for this source/method is already active.");
        const auto pending = report(EditorCommandStatus::Pending, "Point construction queued.");
        auto delivered = std::make_shared<bool>(false);
        auto sink = context.MethodResultSinks.PointConstruction;
        const auto rejected = [pending](std::string message)
        {
            auto r = pending;
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            r.Message = std::move(message);
            return r;
        };
        JobDesc final{.DebugName = "Point construction",
                      .Scope = context.World,
                      .Kind = RuntimeTaskKinds::GeometryProcess,
                      .Work =
                          [w](const JobCancellation& cancellation)
                      {
                          if (w->Config.Backend != PointConstructionBackend::VulkanLBVH)
                          {
                              Prepare(*w);
                              QueryCpu(*w, &cancellation);
                          }
                          BuildOutput(*w);
                          return JobResultEnvelope::Make(true);
                      },
                      .ValidateBeforeApply =
                          [context, w]
                      {
                          return !w->Abandoned && Current(context, *w)
                                     ? JobApplyValidation::Current
                                     : JobApplyValidation::StaleGeneration;
                      },
                      .PublishCompletion =
                          [context, w, sink, delivered](KernelEventBus&, const JobResultEnvelope&)
                      {
                          const auto r = Publish(context, w);
                          *delivered = true;
                          if (sink)
                              sink(r);
                          return r.Succeeded();
                      },
                      .FinalizeUnpublishedOnMainThread =
                          [w, sink, delivered, pending]() mutable
                      {
                          w->Abandoned = true;
                          if (*delivered)
                              return;
                          *delivered = true;
                          auto r = pending;
                          if (w->MainFailure)
                              r = *w->MainFailure;
                          else
                          {
                              r.Status = EditorCommandStatus::StaleEntity;
                              r.Message = "Construction cancelled or stale; no entity created.";
                          }
                          if (sink)
                              sink(std::move(r));
                      }};
        if (w->Config.Backend == PointConstructionBackend::VulkanLBVH)
        {
            JobDesc prepare{.DebugName = "Prepare point construction",
                            .Scope = context.World,
                            .Kind = RuntimeTaskKinds::GeometryProcess,
                            .Work =
                                [w](const JobCancellation&)
                            {
                                Prepare(*w);
                                return JobResultEnvelope::Make(true);
                            },
                            .ValidateBeforeApply =
                                [context, w]
                            {
                                return !w->Abandoned && Current(context, *w)
                                           ? JobApplyValidation::Current
                                           : JobApplyValidation::StaleGeneration;
                            },
                            .PublishCompletion =
                                [w](KernelEventBus&, const JobResultEnvelope&)
                            {
                                if (!w->PreparedOk)
                                    w->MainFailure = w->Result;
                                return w->PreparedOk;
                            },
                            .FinalizeUnpublishedOnMainThread = [w] { w->Abandoned = true; }};
            const auto prepared = context.JobCommands.Submit(std::move(prepare), identity);
            if (!prepared.IsValid())
                return rejected("Construction preparation submission rejected.");
            JobDesc gpu{.DebugName = "Construction neighbors (Vulkan)",
                        .Scope = context.World,
                        .Kind = RuntimeTaskKinds::GeometryProcess,
                        .Work = [](const JobCancellation&)
                        { return JobResultEnvelope::Make(true); },
                        .IsReadyToApply = [context, w] { return AdvanceGpu(context, *w); },
                        .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&)
                        { return w->QueriesComplete; },
                        .FinalizeUnpublishedOnMainThread = [w] { w->Abandoned = true; }};
            gpu.DependsOn.push_back({prepared, "Prepare query locations before Vulkan dispatch"});
            const auto support = context.JobCommands.Submit(std::move(gpu), identity);
            if (!support.IsValid())
            {
                w->Abandoned = true;
                return rejected("Construction GPU submission rejected.");
            }
            final.DependsOn.push_back(
                {support, "Complete Vulkan neighborhoods before geometry extraction"});
        }
        const auto token = context.JobCommands.Submit(std::move(final), identity);
        if (!token.IsValid())
        {
            w->Abandoned = true;
            return rejected("Construction output submission rejected.");
        }
        return pending;
    }
    EditorPointConstructionResult
    ApplyEditorConfiguredPointConstruction(const EditorGeometryProcessingContext& context)
    {
        const auto config = GetEditorPointConstructionConfig(context);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Point construction config is unavailable."};
        return ApplyEditorPointConstructionCommand(context, *config);
    }
} // namespace Extrinsic::Runtime
