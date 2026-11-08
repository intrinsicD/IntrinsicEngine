module;
#include <atomic>
#include <string_view>
#include <functional>
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
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.PointConstructionOperations;
import Geometry.Graph.Utils;
import Geometry.SurfaceReconstruction;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.System.TransformHierarchy;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Runtime.AssetWorkflowGeometryMaterialization;
import Extrinsic.Runtime.AssetWorkflowRecipePolicies;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.PointCloud;
import Geometry.HalfedgeMesh.IO;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorGeneratedEntity.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.OneShotCpuRun.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
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
        struct ConstructionWork : Detail::PointNormalCapture
        {
            PointConstructionConfig Config{};
            entt::entity Entity{entt::null};
            glm::mat4 SourceMatrix{1};
            // Derived state of one generation (the step's), cleared before and freed after it; the output stays
            // until Accept moves it into the new entity.
            std::vector<std::uint32_t> Offsets{0}, Indices{};
            std::vector<float> Field{};
            std::optional<SR::PreparedReconstruction> Prepared{};
            std::optional<Geometry::HalfedgeMesh::Mesh> Mesh{};
            std::optional<Geometry::Graph::Graph> Graph{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            // ARCH-023, Vulkan LBVH as a step run: the kNN session prepared at the start (one fixed page of query
            // rows the worker uploads per page, no frame) and the run's discard, which every page boundary and a
            // waiting page observe.
            std::shared_ptr<SpatialQuerySession> Session{};
            std::atomic<bool> Abort{false};
            // Neighbours per query, with the query itself: min(N, Hoppe and k = 1 ? 1 : k + 1), set at capture.
            std::uint32_t Width{};
            bool Abandoned{};
            EditorPointConstructionResult Result{};
        };
        bool Attached(const EditorProcessingContext& c)
        {
            return c.Scene && (!c.AttachmentActive || c.AttachmentActive());
        }
        // The world first (the run's job has no scope of its own, and this runs on every drain while it waits).
        bool Current(const EditorProcessingContext& c, const ConstructionWork& w)
        {
            if (!Detail::EditorProcessingContextWorldCurrent(c) || !Attached(c) ||
                !Detail::GeometryPropertiesCurrent(c, w.Entity, w.Inputs))
                return false;
            const auto matrix = ECS::Systems::TransformHierarchy::TryComposeWorldMatrix(c.Scene->Raw(), w.Entity);
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
        std::shared_ptr<ConstructionWork> Capture(const EditorProcessingContext& context,
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
                EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
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
            const auto matrix = ECS::Systems::TransformHierarchy::TryComposeWorldMatrix(context.Scene->Raw(), *entity);
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
            const bool captured = supplied
                ? Detail::CapturePointNormalInput(context, *entity, a, c.Positions, c.Normals,
                                                  !preview, *w, diagnostic)
                : preview ? Detail::PreparePointInput(context, *entity, a, c.Positions, *w, diagnostic)
                          : Detail::CapturePointInput(a, c.Positions, true, *w, diagnostic);
            if (!captured)
                return {};
            if (!w->ValidLbvh)
                return fail("Live positions must be finite and within the shared 1e18 "
                            "coordinate limit.");
            if (c.Backend == PointConstructionBackend::VulkanLBVH && w->HasSubnormalCoordinates)
                return fail("Vulkan construction does not support subnormal coordinate components.");
            if (supplied && (!std::isfinite(w->MaximumNormalSquaredNorm) ||
                             w->MinimumNormalSquaredNorm <= 1e-16f))
                return fail("Live supplied normals must be finite and nonzero.");
            w->Result.InputCount = w->LiveCount;
            const auto count = w->Result.InputCount;
            if (count < (c.Method == PointConstructionMethod::Hoppe ? 3u : 1u) ||
                count > (1u << 20))
                return fail("Construction requires 3..1048576 live samples for Hoppe or 1..1048576 "
                            "for graphs.");
            if (c.Method == PointConstructionMethod::KnnGraph &&
                count * std::min<std::size_t>(count, c.KNeighbors + 1) > (1u << 24))
                return fail("Graph neighborhood storage exceeds 16777216 candidate entries; reduce "
                            "k or the sample count.");
            // One width for the CPU rows and the GPU session (at most 64: k <= 63).
            w->Width = std::uint32_t(std::min<std::size_t>(
                count, c.Method == PointConstructionMethod::Hoppe && c.KNeighbors == 1 ? 1 : c.KNeighbors + 1));
            if (c.Backend != PointConstructionBackend::CpuReference && !context.SpatialIndices)
                return fail("The shared spatial cache is unavailable.");
            if (c.Backend == PointConstructionBackend::VulkanLBVH &&
                (!context.JobCommands.Available() ||
                 !context.SpatialIndices->GpuQueriesAvailable()))
                return fail("Vulkan construction requires GPU queries and editor jobs.");
            // ARCH-023: Vulkan neighbourhoods are a worker step run's pages; without the session's step runs there
            // is no frame-paced substitute.
            if (c.Backend == PointConstructionBackend::VulkanLBVH && !context.StepRuns)
                return fail("Vulkan construction requires the session's step runs.");
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
        bool Prepare(ConstructionWork& w)
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
                    return false;
                }
                r.QueryCount = w.Prepared->Dimensions.VertexCount();
                w.Field.reserve(r.QueryCount);
            }
            else
            {
                r.QueryCount = w.Points.size();
                w.Indices.reserve(r.QueryCount * w.Width);
                w.Offsets.reserve(r.QueryCount + 1);
            }
            r.CpuComputeMilliseconds +=
                std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            return true;
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
        // A page boundary: the run's discard (Abort) or the queued job's cancellation stops the step there; the
        // result belongs to a run that already ended.
        bool Stopped(ConstructionWork& w, const JobCancellation* cancellation)
        {
            if (!w.Abort.load(std::memory_order_acquire) && !(cancellation && cancellation->IsCancelled()))
                return false;
            w.Result.Status = EditorCommandStatus::StaleEntity;
            w.Result.Message = "Construction cancelled.";
            return true;
        }
        bool QueryCpu(ConstructionWork& w, const JobCancellation* cancellation)
        {
            if (w.Index && !w.Index->Cpu)
            {
                w.Result.Message = "CPU construction rows require a CPU hierarchy."; // a GPU-only capture
                return false;
            }
            const auto start = Clock::now();
            const auto priorCpu = w.Result.CpuComputeMilliseconds;
            w.Result.ActualBackend = ToString(w.Config.Backend);
            for (std::size_t first = 0; first < w.Result.QueryCount;
                 first += w.Config.GpuQueryBatchSize)
            {
                if (Stopped(w, cancellation))
                    return false;
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
                        return false;
                    }
                    for (const auto& n : row)
                        indices.push_back(n.Index);
                    offsets.push_back(indices.size());
                }
                if (!ConsumeRows(w, queries, offsets, indices))
                    return false;
            }
            w.Result.CpuComputeMilliseconds =
                priorCpu + std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            return true;
        }
        // The existing row decoder on one completed page of `queries` (the page's active prefix only): the layout
        // and full width of every row, each source slot mapped to its compact index (an unknown slot refuses the
        // page), each row re-sorted by the CPU's stable float distance/source-ID order.
        bool DecodeGpuRows(const ConstructionWork& w,
            std::span<const glm::vec3> queries,
            const SpatialNearestBatch& batch,
            std::vector<std::uint32_t>& offsets,
            std::vector<std::uint32_t>& indices,
            std::string& why)
        {
            if (batch.Capacity != w.Width || batch.Counts.size() != queries.size() ||
                batch.Neighbors.size() != queries.size() * w.Width)
            {
                why = "Vulkan construction returned an invalid neighborhood layout.";
                return false;
            }
            offsets.assign(1u, 0u);
            indices.clear();
            indices.reserve(queries.size() * w.Width);
            for (std::size_t i = 0; i < queries.size(); ++i)
            {
                if (batch.Counts[i] != w.Width)
                {
                    why = "Vulkan construction returned an incomplete neighborhood.";
                    return false;
                }
                const auto first = indices.size();
                for (unsigned j = 0; j < w.Width; ++j)
                {
                    const auto slot = batch.Neighbors[i * w.Width + j].Index;
                    const auto found = std::lower_bound(w.Slots.begin(), w.Slots.end(), slot);
                    if (found == w.Slots.end() || *found != slot)
                    {
                        why = "Vulkan construction returned an unknown source slot.";
                        return false;
                    }
                    indices.push_back(std::uint32_t(found - w.Slots.begin()));
                }
                // CPU reduction uses a stable float distance/source-ID order;
                // recompute it after GPU readback instead of relying on GPU arithmetic order.
                std::sort(indices.begin() + first,
                    indices.end(),
                    [&](auto a, auto b)
                    {
                        const auto da = queries[i] - w.Points[a], db = queries[i] - w.Points[b];
                        const auto aa = glm::dot(da, da), bb = glm::dot(db, db);
                        return aa < bb || (aa == bb && a < b);
                    });
                offsets.push_back(indices.size());
            }
            return true;
        }
        // ARCH-023, Vulkan LBVH: every page through the prepared session on the worker, without frames. A page's
        // queries are refused before any upload unless normal-or-zero within 1e18, written into the prefix of the
        // session's fixed page (the tail keeps earlier valid rows: padding the page never queries), queried as rows
        // [0, count) and decoded from that prefix. Batches count consumed pages of this step; the traffic is the
        // session's delta over the step. False with the result's reason.
        bool QueryGpu(ConstructionWork& w)
        {
            auto& r = w.Result;
            auto& session = *w.Session;
            const auto uploads = session.UploadBytes(), readbacks = session.ReadbackBytes();
            const auto settle = [&](const bool ok)
            {
                r.CpuStageUploadBytes = session.UploadBytes() - uploads;
                r.CpuStageReadbackBytes = session.ReadbackBytes() - readbacks;
                return ok;
            };
            std::vector<glm::vec3> page(session.Rows()); // zero rows are valid padding
            std::vector<std::uint32_t> offsets, indices;
            SpatialNearestBatch batch;
            for (std::size_t first = 0; first < r.QueryCount; first += session.PageQueries())
            {
                if (Stopped(w, nullptr))
                    return settle(false);
                const auto count = std::min<std::size_t>(session.PageQueries(), r.QueryCount - first);
                const auto queries = Queries(w, first, count);
                if (!std::all_of(queries.begin(), queries.end(), GpuPoint))
                {
                    r.Message = "Vulkan grid queries require normal-or-zero components within 1e18.";
                    return settle(false);
                }
                const auto started = Clock::now();
                std::copy(queries.begin(), queries.end(), page.begin());
                if (!session.UploadQueries(page, r.Message))
                    return settle(false);
                const auto status = session.Query(0u, std::uint32_t(count), 0.f, nullptr, w.Abort, batch);
                if (status == SpatialPageStatus::Abandoned) // the run's discard won while the page waited
                {
                    (void)Stopped(w, nullptr);
                    return settle(false);
                }
                if (status == SpatialPageStatus::Failed)
                {
                    r.Message = batch.Diagnostic;
                    return settle(false);
                }
                r.ActualBackend = "vulkan_lbvh";
                if (!DecodeGpuRows(w, queries, batch, offsets, indices, r.Message) ||
                    !ConsumeRows(w, queries, offsets, indices))
                    return settle(false);
                ++r.GpuQueryBatches;
                r.GpuNeighborhoodMilliseconds +=
                    std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            }
            return settle(true);
        }
        void BuildOutput(ConstructionWork& w)
        {
            const auto start = Clock::now();
            auto& r = w.Result;
            bool atlasUnavailable = false;
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
                // A valid extracted surface can exceed the atlas's texel or
                // quality budget. UV rejection must not discard that geometry.
                auto materialized = BuildRuntimeHalfedgeMeshMaterialization(payload,
                    {.UvResolution = {.FailurePolicy = RuntimeMeshUvFailurePolicy::Optional}});
                if (!materialized)
                {
                    r.Message = "Generated mesh materialization failed.";
                    return;
                }
                atlasUnavailable = !materialized->Diagnostics.ResolvedTexcoordsValid;
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
            if (atlasUnavailable)
                r.Message += " UV atlas unavailable; the surface remains usable without texture coordinates.";
        }
        EditorPointConstructionResult Publish(const EditorProcessingContext& context,
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
            const auto published = GeometryProcessingDetail::PublishEditorGeneratedEntity(
                context, {.Source = w->Entity,
                          .Name = w->Config.OutputName,
                          .Mesh = std::move(w->Mesh),
                          .Graph = std::move(w->Graph),
                          .IdentityHigh = 0x504f494e54434f4eull,
                          .Label = "Construct geometry from points"});
            r.Status = published.Status;
            r.OutputEntityId = published.OutputEntityId;
            if (!published.Message.empty())
                r.Message = published.Message;
            return r;
        }
        // The run's one step (on a worker, or inline without a job lane), from the capture: preparation, every
        // query page (CPU rows, or the session's on the device), the output (replacing an earlier generation's), and
        // the preparation's intermediates freed whatever the outcome, so a restart starts from empty rows and a run
        // waiting for Accept keeps only the output. The run's discard and a queued job's `cancellation` stop it at
        // a page boundary.
        void Compute(ConstructionWork& w, const JobCancellation* cancellation = nullptr)
        {
            if (Prepare(w) && (w.Session ? QueryGpu(w) : QueryCpu(w, cancellation)))
                BuildOutput(w);
            w.Field = std::vector<float>{};
            w.Indices = std::vector<std::uint32_t>{};
            w.Offsets = std::vector<std::uint32_t>{0u};
            w.Prepared.reset();
        }
        // ARCH-023: every backend as a one-shot worker step run (the shared helper, OneShotCpuRun.hpp) with the
        // caller's execution: a batch runs at once and accepts at its end, an interactive start honours the
        // section's start and auto_accept. Accept (Publish) creates the entity once, as one undo entry; a discard,
        // cancel or stale capture creates nothing and a restart computes again from the capture. Vulkan LBVH pages
        // its kNN rows on the device inside that step (the session prepared before the start); a discard aborts a
        // waiting page at once.
        EditorPointConstructionResult StartConstructionRun(const EditorProcessingContext& context,
            const std::shared_ptr<ConstructionWork>& w,
            const EditorJobIdentity& identity,
            const GeometryProcessingDetail::MeshSupport::QueuedJobDelivery<EditorPointConstructionResult>& delivery,
            const StepRunExecution& execution)
        {
            namespace MS = GeometryProcessingDetail::MeshSupport;
            return MS::StartOneShotCpuRun<ConstructionWork, EditorPointConstructionResult>(
                context,
                {.Label = "Point construction",
                    .Owner = "Runtime.PointConstruction",
                    .Identity = identity,
                    .Execution = execution,
                    .Abort =
                        [w]
                    {
                        w->Abort.store(true, std::memory_order_release); // a page boundary or waiting page gives up
                    },
                    .MetricNames = {"vertices", "edges", "faces", ""},
                    .Metric =
                        [w](StepRunMetric& metric)
                    {
                        metric.Values[0] = double(w->Result.OutputVertexCount);
                        metric.Values[1] = double(w->Result.OutputEdgeCount);
                        metric.Values[2] = double(w->Result.OutputFaceCount);
                    }},
                w,
                [](ConstructionWork& work)
                {
                    Compute(work);
                },
                [context](const ConstructionWork& work)
                {
                    return Current(context, work);
                },
                [context](const std::shared_ptr<ConstructionWork>& work)
                {
                    return Publish(context, work);
                },
                delivery);
        }
        // Apply's run (a batch) or a start's (`execution`, the section's): a one-shot worker step run of the
        // session's service; without that service (or without a job lane) only a CPU batch runs, as one queued job
        // (or inline). Vulkan without step runs is refused at capture.
        EditorPointConstructionResult RunPointConstruction(const EditorProcessingCommands& commands,
            const PointConstructionConfig& config,
            std::function<void(EditorPointConstructionResult)> onComplete,
            const StepRunExecution& execution)
        {
            const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
            std::string diagnostic;
            auto w = Capture(context, config, diagnostic);
            const auto report = [&](EditorCommandStatus status, std::string message)
            {
                auto r = w ? w->Result
                           : EditorPointConstructionResult{.Method = config.Method, .RequestedBackend = config.Backend};
                r.Status = status;
                r.Message = std::move(message);
                return r;
            };
            if (!w)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (w->Config.Backend != PointConstructionBackend::CpuReference)
            {
                const auto indexState = GeometryProcessingDetail::AcquirePointIndex(*context.SpatialIndices,
                    context.World,
                    w->Entity,
                    w->Config.Positions,
                    w->Slots,
                    w->Points,
                    w->GpuIndex,
                    w->Index,
                    w->Result.IndexReused,
                    diagnostic,
                    w->Config.Backend == PointConstructionBackend::CpuLBVH); // Vulkan rows build no CPU hierarchy
                if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                    return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
                if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                    return report(
                        EditorCommandStatus::StaleEntity, "Construction index does not match selected samples.");
            }
            const bool batch = execution.Start == StepRunStart::Run && execution.AutoAccept != StepRunAutoAccept::Never;
            if (!batch && (!context.StepRuns || !context.JobCommands.Available()))
                return report(EditorCommandStatus::InvalidProcessingParameters,
                    "Point construction needs the step-run service and a job lane for its run controls; none is "
                    "composed.");
            if (!context.JobCommands.Available())
            {
                Compute(*w);
                return Publish(context, w);
            }
            EditorJobIdentity identity{.EntityId = config.StableEntityId,
                .Scope = ToEditorJobScope(w->Config.Positions.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                .OutputName = std::string("construct:") + ToString(config.Method),
                .RequestedDomain = EditorJobDomainOfBackend(ToString(config.Backend))};
            namespace MS = GeometryProcessingDetail::MeshSupport;
            if (auto busy = MS::ActiveOutputJobRefusal(context, identity, "Point construction"))
                return report(
                    EditorCommandStatus::Pending, std::move(busy->Message)); // the active run keeps its callback
            if (w->Config.Backend == PointConstructionBackend::VulkanLBVH)
            {
                // The kNN session, once, on the main thread: resident index, buffers, pipelines and one page of
                // zero query rows (a graph's page holds at most its N samples) the worker replaces page by page.
                const std::vector<glm::vec3> rows(w->Config.Method == PointConstructionMethod::Hoppe
                        ? w->Config.GpuQueryBatchSize
                        : std::min<std::size_t>(w->Config.GpuQueryBatchSize, w->Points.size()));
                w->Session = context.SpatialIndices->PrepareQuerySession(w->GpuIndex,
                    {.Capacity = w->Width, .PageQueries = std::uint32_t(rows.size()), .Queries = rows, .Uploads = true},
                    diagnostic);
                if (!w->Session)
                    return report(EditorCommandStatus::GeometryProcessingFailed,
                        "Vulkan construction neighbourhoods could not be prepared: " + diagnostic);
            }
            // Once submitted, Result belongs to the running job. A submission failure reports from the delivery's
            // immutable snapshot.
            const MS::QueuedJobDelivery<EditorPointConstructionResult> delivery{context,
                std::move(onComplete),
                report(EditorCommandStatus::Pending, "Point construction queued."),
                "Point construction"};
            if (context.StepRuns)
                return StartConstructionRun(context, w, identity, delivery, execution);
            const auto validate = [context, w]
            {
                return MS::ValidateQueuedJob(w->Abandoned, Current(context, *w));
            };
            JobDesc desc{.DebugName = "Point construction",
                .Scope = context.World,
                .Work =
                    [w](const JobCancellation& cancellation)
                {
                    Compute(*w, &cancellation);
                    return JobResultEnvelope::Make(true);
                },
                .ValidateBeforeApply = validate,
                .PublishCompletion =
                    [context, w, delivery](KernelEventBus&, const JobResultEnvelope&)
                {
                    return delivery.Publish(Publish(context, w));
                },
                .FinalizeUnpublishedOnMainThread =
                    [w, delivery]
                {
                    w->Abandoned = true;
                    delivery.Finalize();
                }};
            const auto token = context.JobCommands.Submit(std::move(desc), identity);
            if (!token.IsValid())
            {
                w->Abandoned = true;
                return delivery.Rejected();
            }
            return delivery.Pending();
        }
    } // namespace
    EditorPointConstructionReadiness
    PreviewEditorPointConstructionCommand(const EditorProcessingCommands& commands,
                                          const PointConstructionConfig& config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorPointConstructionReadiness r;
        const auto w = Capture(context, config, r.Diagnostic, true);
        r.Ready = bool(w);
        if (w)
            r.Resolved = w->Config;
        return r;
    }
    EditorPointConstructionResult
    ApplyEditorPointConstructionCommand(const EditorProcessingCommands& commands,
                                        const PointConstructionConfig& config,
                                        std::function<void(EditorPointConstructionResult)> onComplete)
    {
        return RunPointConstruction(commands, config, std::move(onComplete), kOneShotBatchExecution);
    }
    EditorPointConstructionResult StartEditorPointConstructionRun(const EditorProcessingCommands& commands,
        const PointConstructionConfig& config,
        std::function<void(EditorPointConstructionResult)> onComplete)
    {
        return RunPointConstruction(commands, config, std::move(onComplete), config.Execution);
    }
    EditorPointConstructionResult
    ApplyEditorConfiguredPointConstruction(const EditorProcessingCommands& commands,
                                           std::function<void(EditorPointConstructionResult)> onComplete)
    {
        const auto config = GetEditorPointConstructionConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Point construction config is unavailable."};
        return ApplyEditorPointConstructionCommand(commands, *config, std::move(onComplete));
    }
    EditorPointConstructionResult StartEditorConfiguredPointConstruction(
        const EditorProcessingCommands& commands, std::function<void(EditorPointConstructionResult)> onComplete)
    {
        const auto config = GetEditorPointConstructionConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                .Message = "Point construction config is unavailable."};
        return StartEditorPointConstructionRun(commands, *config, std::move(onComplete));
    }

    RuntimeEngineConfigApplyResult ApplyEditorPointConstructionConfig(
        const EditorProcessingCommands& commands, const PointConstructionConfig& config,
        std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidatePointConstructionConfigSection(SerializePointConstructionConfig(config), {},
                                                   kPointConstructionConfigSectionName),
            sourceId.empty() ? std::string{kPointConstructionConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetPointConstructionConfig(candidate, config); });
    }

    std::optional<PointConstructionConfig> GetEditorPointConstructionConfig(
        const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetPointConstructionConfig(context.EngineConfigControlState->ActiveConfig);
    }
} // namespace Extrinsic::Runtime

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace Transform = ECS::Components::Transform;
        bool Attached(const EditorProcessingContext& c)
        {
            return c.Scene && (!c.AttachmentActive || c.AttachmentActive());
        }
        struct GeneratedEntity
        {
            entt::entity Entity{entt::null};
            ECS::Components::StableId Identity{};
            entt::entity Source{entt::null};
            std::string Name{};
            std::optional<Geometry::HalfedgeMesh::Mesh> Mesh{};
            std::optional<Geometry::Graph::Graph> Graph{};
            std::optional<Geometry::PointCloud::Cloud> Cloud{};
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
    } // namespace
    EditorGeneratedEntityPublication PublishEditorGeneratedEntity(
        const EditorProcessingContext& context, EditorGeneratedEntityRequest request)
    {
        EditorGeneratedEntityPublication r;
        auto generated = std::make_shared<GeneratedEntity>();
        generated->Source = request.Source;
        generated->Name = std::move(request.Name);
        generated->Mesh = std::move(request.Mesh);
        generated->Graph = std::move(request.Graph);
        generated->Cloud = std::move(request.Cloud);
        auto& raw = context.Scene->Raw();
        generated->Identity = {request.IdentityHigh, 1};
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
            else if (generated->Graph)
                GS::PopulateFromGraph(registry, entity, *generated->Graph);
            else
            {
                // Populate consumes a copy so redo after undo starts from the same cloud.
                Geometry::PointCloud::Cloud cloud = *generated->Cloud;
                GS::PopulateFromCloud(registry, entity, cloud);
            }
            const auto authored = ApplyAssetImportAuthoringRecipe(
                generated->Mesh ? Assets::AssetPayloadKind::Mesh
                : generated->Graph ? Assets::AssetPayloadKind::Graph
                                   : Assets::AssetPayloadKind::PointCloud,
                true, true, entity, scene);
            if (!authored)
            {
                scene.Destroy(entity);
                return EditorCommandHistoryStatus::CommandFailed;
            }
            const auto points =
                std::as_const(registry).get<GS::Vertices>(entity).Properties.Get<glm::vec3>(
                    GS::PropertyNames::kPosition);
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
                EditorFeatureDetail::GeometryMetadataSignatureForEntity(registry, entity);
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
                    EditorFeatureDetail::GeometryMetadataSignatureForEntity(registry, entity) ||
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
                                      ->Execute({.Label = request.Label,
                                                 .Redo = create,
                                                 .Undo = remove})
                                      .Status
                                : create();
        r.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
        if (r.Succeeded())
            r.OutputEntityId = SelectionController::ToStableEntityId(generated->Entity);
        else
            r.Message = "Generated entity publication was rejected.";
        return r;
    }
}
}
