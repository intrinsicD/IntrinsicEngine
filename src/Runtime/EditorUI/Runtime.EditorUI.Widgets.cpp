// Runtime.EditorUI.Widgets — Reusable ImGui widgets for PropertySet-driven
// color source selection and vector field overlay management. Plus editor
// utility functions (matrix/vector comparison, depth ramp, AABB transform).

module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <vector>
#include <glm/glm.hpp>
#include <imgui.h>
#include <entt/entity/fwd.hpp>
#include <entt/entity/entity.hpp>
#include <entt/signal/fwd.hpp>

module Runtime.EditorUI;

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Runtime.PointCloudKMeans;
import Runtime.Selection;

import ECS;

import Graphics.Components;
import Graphics.GPUScene;
import Graphics.Colormap;
import Graphics.GpuColor;
import Graphics.Geometry;
import Graphics.OctreeDebugDraw;
import Graphics.PropertyEnumerator;
import Graphics.VisualizationConfig;
import Graphics.OverlayEntityFactory;

import Geometry.MeshUtils;
import Geometry.Octree;
import Geometry.DEC;
import Geometry.Remeshing;
import Geometry.AdaptiveRemeshing;
import Geometry.Smoothing;
import Geometry.CatmullClark;
import Geometry.MeshRepair;
import Geometry.MeshAnalysis;
import Geometry.MeshQuality;
import Geometry.NormalEstimation;
import Geometry.ShortestPath;
import Geometry.ConvexHullBuilder;
import Geometry.SurfaceReconstruction;
import Geometry.VectorHeatMethod;

import Core.Logging;

import Graphics.LifecycleUtils;

using namespace Graphics::LifecycleUtils;

namespace Runtime::EditorUI
{

namespace
{
    constexpr GeometryProcessingDomain kMeshTopologyDomains =
        GeometryProcessingDomain::MeshVertices |
        GeometryProcessingDomain::MeshEdges |
        GeometryProcessingDomain::MeshHalfedges |
        GeometryProcessingDomain::MeshFaces;

    constexpr GeometryProcessingDomain kGraphTopologyDomains =
        GeometryProcessingDomain::GraphVertices |
        GeometryProcessingDomain::GraphEdges |
        GeometryProcessingDomain::GraphHalfedges;

    [[nodiscard]] constexpr GeometryProcessingDomain ToUiDomain(Runtime::PointCloudKMeans::Domain domain) noexcept
    {
        switch (domain)
        {
        case Runtime::PointCloudKMeans::Domain::MeshVertices: return GeometryProcessingDomain::MeshVertices;
        case Runtime::PointCloudKMeans::Domain::GraphVertices: return GeometryProcessingDomain::GraphVertices;
        case Runtime::PointCloudKMeans::Domain::PointCloudPoints: return GeometryProcessingDomain::PointCloudPoints;
        case Runtime::PointCloudKMeans::Domain::Auto:
        default: return GeometryProcessingDomain::None;
        }
    }

    [[nodiscard]] constexpr const char* KMeansResultProperty(Runtime::PointCloudKMeans::Domain domain) noexcept
    {
        if (domain == Runtime::PointCloudKMeans::Domain::PointCloudPoints)
            return "p:kmeans_color";
        if (domain == Runtime::PointCloudKMeans::Domain::MeshVertices)
            return "v:kmeans_label_f";  // Scalar label → colormap → Voronoi texel rendering
        return "v:kmeans_color";
    }

    struct KMeansStatus
    {
        bool JobPending = false;
        Geometry::KMeans::Backend LastBackend = Geometry::KMeans::Backend::CPU;
        uint32_t LastIterations = 0;
        bool LastConverged = false;
        float LastInertia = 0.0f;
        uint32_t LastMaxDistanceIndex = 0;
        double LastDurationMs = 0.0;
    };

    [[nodiscard]] std::optional<KMeansStatus> ReadKMeansStatus(const entt::registry& reg,
                                                               entt::entity entity,
                                                               Runtime::PointCloudKMeans::Domain domain)
    {
        switch (domain)
        {
        case Runtime::PointCloudKMeans::Domain::MeshVertices:
            if (const auto* mesh = reg.try_get<ECS::Mesh::Data>(entity))
            {
                return KMeansStatus{mesh->KMeansJobPending,
                                    mesh->KMeansLastBackend,
                                    mesh->KMeansLastIterations,
                                    mesh->KMeansLastConverged,
                                    mesh->KMeansLastInertia,
                                    mesh->KMeansLastMaxDistanceIndex,
                                    mesh->KMeansLastDurationMs};
            }
            break;
        case Runtime::PointCloudKMeans::Domain::GraphVertices:
            if (const auto* graph = reg.try_get<ECS::Graph::Data>(entity))
            {
                return KMeansStatus{graph->KMeansJobPending,
                                    graph->KMeansLastBackend,
                                    graph->KMeansLastIterations,
                                    graph->KMeansLastConverged,
                                    graph->KMeansLastInertia,
                                    graph->KMeansLastMaxDistanceIndex,
                                    graph->KMeansLastDurationMs};
            }
            break;
        case Runtime::PointCloudKMeans::Domain::PointCloudPoints:
            if (const auto* pointCloud = reg.try_get<ECS::PointCloud::Data>(entity))
            {
                return KMeansStatus{pointCloud->KMeansJobPending,
                                    pointCloud->KMeansLastBackend,
                                    pointCloud->KMeansLastIterations,
                                    pointCloud->KMeansLastConverged,
                                    pointCloud->KMeansLastInertia,
                                    pointCloud->KMeansLastMaxDistanceIndex,
                                    pointCloud->KMeansLastDurationMs};
            }
            break;
        case Runtime::PointCloudKMeans::Domain::Auto:
        default:
            break;
        }

        return std::nullopt;
    }

    struct KMeansDomainUiData
    {
        std::array<Runtime::PointCloudKMeans::Domain, 3> Domains{};
        std::array<Runtime::PointCloudKMeans::TargetInfo, 3> Targets{};
        std::array<std::string, 3> Labels{};
        std::array<const char*, 3> LabelPointers{};
        int Count = 0;
    };

    [[nodiscard]] KMeansDomainUiData BuildKMeansDomainUiData(Runtime::Engine& engine,
                                                             const entt::registry& reg,
                                                             entt::entity entity)
    {
        KMeansDomainUiData data{};
        const auto domains = GetAvailableKMeansDomains(reg, entity);
        for (const auto domain : domains)
        {
            const int index = data.Count++;
            const auto target = Runtime::PointCloudKMeans::DescribeTarget(engine, entity, domain);
            data.Domains[index] = domain;
            data.Targets[index] = target;
            data.Labels[index] = std::string(GeometryDomainLabel(ToUiDomain(domain)))
                               + " (" + std::to_string(target.PointCount) + ')';
            data.LabelPointers[index] = data.Labels[index].c_str();
        }
        return data;
    }

    [[nodiscard]] int FindKMeansDomainIndex(const KMeansDomainUiData& data,
                                            Runtime::PointCloudKMeans::Domain selectedDomain) noexcept
    {
        for (int i = 0; i < data.Count; ++i)
        {
            if (data.Domains[i] == selectedDomain)
                return i;
        }
        return data.Count > 0 ? 0 : -1;
    }

    void RebuildCollisionVertexLookup(Graphics::GeometryCollisionData& collision)
    {
        collision.LocalVertexLookupPoints.clear();
        collision.LocalVertexLookupIndices.clear();
        collision.LocalVertexKdTree = {};

        if (collision.SourceMesh)
        {
            collision.LocalVertexLookupPoints.reserve(collision.SourceMesh->VertexCount());
            collision.LocalVertexLookupIndices.reserve(collision.SourceMesh->VertexCount());
            for (std::size_t i = 0; i < collision.SourceMesh->VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
                if (!collision.SourceMesh->IsValid(vh) || collision.SourceMesh->IsDeleted(vh))
                    continue;

                collision.LocalVertexLookupPoints.push_back(collision.SourceMesh->Position(vh));
                collision.LocalVertexLookupIndices.push_back(static_cast<uint32_t>(vh.Index));
            }
        }
        else
        {
            collision.LocalVertexLookupPoints = collision.Positions;
            collision.LocalVertexLookupIndices.reserve(collision.Positions.size());
            for (uint32_t i = 0; i < collision.Positions.size(); ++i)
                collision.LocalVertexLookupIndices.push_back(i);
        }

        if (!collision.LocalVertexLookupPoints.empty())
            static_cast<void>(collision.LocalVertexKdTree.BuildFromPoints(collision.LocalVertexLookupPoints));
    }

    [[nodiscard]] bool ApplySurfaceMeshOperator(Runtime::Engine& engine,
                                                entt::entity entity,
                                                const std::function<void(Geometry::Halfedge::Mesh&)>& op)
    {
        auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
        auto* collider = reg.try_get<ECS::MeshCollider::Component>(entity);
        auto* sc = reg.try_get<ECS::Surface::Component>(entity);
        auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
        if (!collider || !collider->CollisionRef || !sc)
            return false;

        Geometry::Halfedge::Mesh mesh;
        if (meshData && meshData->MeshRef)
        {
            mesh = *meshData->MeshRef;
        }
        else if (collider->CollisionRef->SourceMesh)
        {
            mesh = *collider->CollisionRef->SourceMesh;
        }
        else
        {
            Geometry::MeshUtils::TriangleSoupBuildParams buildParams;
            buildParams.WeldVertices = true;
            buildParams.WeldEpsilon = 1e-6f;

            auto built = Geometry::MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(
                collider->CollisionRef->Positions,
                collider->CollisionRef->Indices,
                collider->CollisionRef->Aux,
                buildParams);
            if (!built)
                return false;
            mesh = std::move(*built);
        }

        if (mesh.VertexProperties().Get<glm::vec2>("v:texcoord"))
        {
            Geometry::Halfedge::Mesh::VertexAttributeTransfer uvTransfer;
            uvTransfer.Name = "v:texcoord";
            uvTransfer.Rule = Geometry::Halfedge::Mesh::VertexAttributeTransfer::Policy::Average;
            mesh.SetVertexAttributeTransferRules(std::span<const Geometry::Halfedge::Mesh::VertexAttributeTransfer>(&uvTransfer, 1));
        }
        else
        {
            mesh.ClearVertexAttributeTransferRules();
        }

        op(mesh);
        mesh.GarbageCollection();

        std::vector<glm::vec3> newPos;
        std::vector<uint32_t> newIdx;
        std::vector<glm::vec4> newAux;
        Geometry::MeshUtils::ExtractIndexedTriangles(mesh, newPos, newIdx, &newAux);

        collider->CollisionRef->Positions = std::move(newPos);
        collider->CollisionRef->Aux = std::move(newAux);
        collider->CollisionRef->Indices = std::move(newIdx);
        collider->CollisionRef->SourceMesh = std::make_shared<Geometry::Halfedge::Mesh>(mesh);

        std::vector<glm::vec3> newNormals(collider->CollisionRef->Positions.size(), glm::vec3(0, 1, 0));
        Geometry::MeshUtils::CalculateNormals(collider->CollisionRef->Positions, collider->CollisionRef->Indices,
                                              newNormals);

        if (!collider->CollisionRef->Positions.empty())
        {
            auto aabbs = Geometry::ToAABB(collider->CollisionRef->Positions);
            collider->CollisionRef->LocalAABB = Geometry::Union(aabbs);
        }
        else
        {
            collider->CollisionRef->LocalAABB = Geometry::AABB{};
        }

        collider->CollisionRef->LocalOctree = Geometry::Octree{};
        std::vector<Geometry::AABB> primitiveBounds;
        primitiveBounds.reserve(collider->CollisionRef->Indices.size() / 3);
        for (size_t i = 0; i + 2 < collider->CollisionRef->Indices.size(); i += 3)
        {
            const uint32_t i0 = collider->CollisionRef->Indices[i];
            const uint32_t i1 = collider->CollisionRef->Indices[i + 1];
            const uint32_t i2 = collider->CollisionRef->Indices[i + 2];
            if (i0 >= collider->CollisionRef->Positions.size()
                || i1 >= collider->CollisionRef->Positions.size()
                || i2 >= collider->CollisionRef->Positions.size())
            {
                continue;
            }
            auto aabb = Geometry::AABB{
                collider->CollisionRef->Positions[i0], collider->CollisionRef->Positions[i0]
            };
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i1]);
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i2]);
            primitiveBounds.push_back(aabb);
        }
        if (!primitiveBounds.empty())
        {
            static_cast<void>(collider->CollisionRef->LocalOctree.Build(
                primitiveBounds, Geometry::Octree::SplitPolicy{}, 16, 8));
        }
        RebuildCollisionVertexLookup(*collider->CollisionRef);

        Graphics::GeometryUploadRequest uploadReq;
        uploadReq.Positions = collider->CollisionRef->Positions;
        uploadReq.Indices = collider->CollisionRef->Indices;
        uploadReq.Normals = newNormals;
        uploadReq.Aux = collider->CollisionRef->Aux;
        uploadReq.Topology = Graphics::PrimitiveTopology::Triangles;
        uploadReq.UploadMode = Graphics::GeometryUploadMode::Staged;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            engine.GetGraphicsBackend().GetDeviceShared(), engine.GetGraphicsBackend().GetTransferManager(), uploadReq,
            &engine.GetRenderOrchestrator().GetGeometryStorage());

        auto oldHandle = sc->Geometry;
        sc->Geometry = engine.GetRenderOrchestrator().GetGeometryStorage().Add(std::move(gpuData));

        if (oldHandle.IsValid())
            engine.GetRenderOrchestrator().GetGeometryStorage().Remove(oldHandle, engine.GetGraphicsBackend().GetDevice().GetGlobalFrameNumber());

        ReleaseGpuSlot(engine.GetRenderOrchestrator().GetGPUScene(), *sc);
        reg.emplace_or_replace<ECS::Components::Transform::WorldUpdatedTag>(entity);

        if (auto* ev = reg.try_get<ECS::MeshEdgeView::Component>(entity))
            ev->Dirty = true;
        if (auto* pv = reg.try_get<ECS::MeshVertexView::Component>(entity))
            pv->Dirty = true;
        if (auto* bvh = reg.try_get<ECS::PrimitiveBVH::Data>(entity))
            bvh->Dirty = true;

        auto& md = reg.emplace_or_replace<ECS::Mesh::Data>(entity);
        md.MeshRef = collider->CollisionRef->SourceMesh;
        md.AttributesDirty = true;

        engine.GetSceneManager().GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({entity});
        return true;
    }

    [[nodiscard]] bool HasSurfaceInput(Runtime::Engine& engine, entt::entity entity) noexcept
    {
        const auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
        return GetGeometryProcessingCapabilities(reg, entity).HasEditableSurfaceMesh;
    }

    [[nodiscard]] const char* PropertyTypeLabel(const Geometry::ConstPropertySet& ps,
                                                const std::string& name)
    {
        if (ps.Get<bool>(name)) return "bool";
        if (ps.Get<int>(name)) return "int";
        if (ps.Get<uint32_t>(name)) return "uint32";
        if (ps.Get<float>(name)) return "float";
        if (ps.Get<double>(name)) return "double";
        if (ps.Get<glm::vec2>(name)) return "vec2";
        if (ps.Get<glm::vec3>(name)) return "vec3";
        if (ps.Get<glm::vec4>(name)) return "vec4";
        return "unsupported";
    }

    [[nodiscard]] std::string FormatValue(bool value)
    {
        return value ? "true" : "false";
    }

    [[nodiscard]] std::string FormatValue(int value)
    {
        return std::to_string(value);
    }

    [[nodiscard]] std::string FormatValue(uint32_t value)
    {
        return std::to_string(value);
    }

    [[nodiscard]] std::string FormatValue(float value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
        return std::string(buffer);
    }

    [[nodiscard]] std::string FormatValue(double value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.6g", value);
        return std::string(buffer);
    }

    [[nodiscard]] std::string FormatValue(const glm::vec2& value)
    {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "(%.6g, %.6g)",
                      static_cast<double>(value.x),
                      static_cast<double>(value.y));
        return std::string(buffer);
    }

    [[nodiscard]] std::string FormatValue(const glm::vec3& value)
    {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "(%.6g, %.6g, %.6g)",
                      static_cast<double>(value.x),
                      static_cast<double>(value.y),
                      static_cast<double>(value.z));
        return std::string(buffer);
    }

    [[nodiscard]] std::string FormatValue(const glm::vec4& value)
    {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "(%.6g, %.6g, %.6g, %.6g)",
                      static_cast<double>(value.x),
                      static_cast<double>(value.y),
                      static_cast<double>(value.z),
                      static_cast<double>(value.w));
        return std::string(buffer);
    }

    template <class T>
    bool DrawTypedPropertyPreview(const Geometry::ConstPropertySet& ps,
                                  const std::string& name,
                                  const PropertySetBrowserState& state)
    {
        const auto property = ps.Get<T>(name);
        if (!property)
            return false;

        const int totalRows = static_cast<int>(property.Vector().size());
        const int previewRows = state.ShowAllRows
            ? totalRows
            : std::min(totalRows, std::max(state.PreviewRows, 1));

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders
                                    | ImGuiTableFlags_RowBg
                                    | ImGuiTableFlags_SizingStretchProp
                                    | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##property_preview", state.ShowIndices ? 2 : 1, flags, ImVec2(0.0f, 180.0f)))
        {
            if (state.ShowIndices)
                ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (int row = 0; row < previewRows; ++row)
            {
                ImGui::TableNextRow();
                if (state.ShowIndices)
                {
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", row);
                    ImGui::TableSetColumnIndex(1);
                }
                else
                {
                    ImGui::TableSetColumnIndex(0);
                }

                const std::string formatted = FormatValue(property[static_cast<std::size_t>(row)]);
                ImGui::TextUnformatted(formatted.c_str());
            }

            ImGui::EndTable();
        }

        if (!state.ShowAllRows && previewRows < totalRows)
            ImGui::TextDisabled("Showing %d / %d rows.", previewRows, totalRows);

        return true;
    }

    [[nodiscard]] double WeightedDot(std::span<const double> a,
                                     std::span<const double> b,
                                     const Geometry::DEC::DiagonalMatrix& mass,
                                     std::span<const uint32_t> active)
    {
        double result = 0.0;
        for (const uint32_t idx : active)
            result += mass.Diagonal[idx] * a[idx] * b[idx];
        return result;
    }

    [[nodiscard]] double WeightedNorm(std::span<const double> x,
                                      const Geometry::DEC::DiagonalMatrix& mass,
                                      std::span<const uint32_t> active)
    {
        return std::sqrt(std::max(0.0, WeightedDot(x, x, mass, active)));
    }

    void RemoveWeightedMean(std::vector<double>& x,
                            const Geometry::DEC::DiagonalMatrix& mass,
                            std::span<const uint32_t> active)
    {
        double weightSum = 0.0;
        double weightedSum = 0.0;
        for (const uint32_t idx : active)
        {
            const double w = mass.Diagonal[idx];
            weightSum += w;
            weightedSum += w * x[idx];
        }
        if (weightSum <= 1.0e-20)
            return;

        const double mean = weightedSum / weightSum;
        for (const uint32_t idx : active)
            x[idx] -= mean;
    }

    void OrthogonalizeWeighted(std::vector<double>& x,
                               std::span<const double> basis,
                               const Geometry::DEC::DiagonalMatrix& mass,
                               std::span<const uint32_t> active)
    {
        const double denom = WeightedDot(basis, basis, mass, active);
        if (std::abs(denom) <= 1.0e-20)
            return;

        const double coeff = WeightedDot(x, basis, mass, active) / denom;
        for (const uint32_t idx : active)
            x[idx] -= coeff * basis[idx];
    }

    [[nodiscard]] double NormalizeWeighted(std::vector<double>& x,
                                           const Geometry::DEC::DiagonalMatrix& mass,
                                           std::span<const uint32_t> active)
    {
        const double norm = WeightedNorm(x, mass, active);
        if (norm <= 1.0e-20)
            return 0.0;

        const double invNorm = 1.0 / norm;
        for (const uint32_t idx : active)
            x[idx] *= invNorm;
        return norm;
    }

    [[nodiscard]] double MaxActiveDifference(std::span<const double> a,
                                             std::span<const double> b,
                                             std::span<const uint32_t> active)
    {
        double maxDiff = 0.0;
        for (const uint32_t idx : active)
            maxDiff = std::max(maxDiff, std::abs(a[idx] - b[idx]));
        return maxDiff;
    }

    [[nodiscard]] double MaxActiveAbs(std::span<const double> x,
                                      std::span<const uint32_t> active)
    {
        double maxAbs = 0.0;
        for (const uint32_t idx : active)
            maxAbs = std::max(maxAbs, std::abs(x[idx]));
        return maxAbs;
    }

    struct MeshSpectralComputationResult
    {
        std::size_t ActiveVertices = 0;
        std::uint32_t IterationsPerformed = 0;
        bool Converged = false;
        std::array<double, 2> Eigenvalues{0.0, 0.0};
        std::array<double, 2> Residuals{0.0, 0.0};
        std::array<std::vector<double>, 2> Modes{};
    };

    [[nodiscard]] std::optional<MeshSpectralComputationResult> ComputeMeshSpectralModes(
        const Geometry::Halfedge::Mesh& mesh,
        int requestedModeCount,
        int maxIterations,
        float shift,
        float solverTolerance)
    {
        const std::size_t n = mesh.VerticesSize();
        if (n == 0)
            return std::nullopt;

        std::vector<uint32_t> active;
        active.reserve(mesh.VertexCount());
        for (std::size_t i = 0; i < n; ++i)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                continue;
            active.push_back(static_cast<uint32_t>(i));
        }
        if (active.size() < 2)
            return std::nullopt;

        const int modeCount = std::clamp(requestedModeCount, 1, 2);
        const auto ops = Geometry::DEC::BuildOperators(mesh);
        if (ops.Hodge0.Size != n || ops.Laplacian.Rows != n || ops.Laplacian.Cols != n)
            return std::nullopt;

        std::array<std::vector<double>, 2> basis{
            std::vector<double>(n, 0.0),
            std::vector<double>(n, 0.0)
        };
        std::array<std::vector<double>, 2> nextBasis{
            std::vector<double>(n, 0.0),
            std::vector<double>(n, 0.0)
        };
        std::array<std::vector<double>, 2> rhs{
            std::vector<double>(n, 0.0),
            std::vector<double>(n, 0.0)
        };

        for (std::size_t rank = 0; rank < active.size(); ++rank)
        {
            const auto t = static_cast<double>(rank + 1u);
            const uint32_t idx = active[rank];
            basis[0][idx] = std::sin(0.73 * t) + 0.17 * std::cos(1.11 * t);
            basis[1][idx] = std::cos(0.61 * t) - 0.21 * std::sin(1.37 * t);
        }

        RemoveWeightedMean(basis[0], ops.Hodge0, active);
        if (NormalizeWeighted(basis[0], ops.Hodge0, active) == 0.0)
            return std::nullopt;

        if (modeCount > 1)
        {
            RemoveWeightedMean(basis[1], ops.Hodge0, active);
            OrthogonalizeWeighted(basis[1], basis[0], ops.Hodge0, active);
            if (NormalizeWeighted(basis[1], ops.Hodge0, active) == 0.0)
                return std::nullopt;
        }

        Geometry::DEC::CGParams cgParams;
        cgParams.MaxIterations = static_cast<std::size_t>(std::max(maxIterations * 8, 32));
        cgParams.Tolerance = std::max(static_cast<double>(solverTolerance), 1.0e-10);

        MeshSpectralComputationResult result;
        result.ActiveVertices = active.size();
        result.Converged = false;

        const double shiftValue = std::max(static_cast<double>(shift), 1.0e-6);
        const double uiTolerance = std::max(static_cast<double>(solverTolerance), 1.0e-8);

        for (int iteration = 0; iteration < std::max(maxIterations, 1); ++iteration)
        {
            double maxDelta = 0.0;
            bool cgConverged = true;

            for (int col = 0; col < modeCount; ++col)
            {
                ops.Hodge0.Multiply(basis[col], rhs[col]);
                std::fill(nextBasis[col].begin(), nextBasis[col].end(), 0.0);

                const auto cg = Geometry::DEC::SolveCGShifted(
                    ops.Hodge0, 1.0,
                    ops.Laplacian, shiftValue,
                    rhs[col], nextBasis[col], cgParams);
                result.IterationsPerformed = std::max(result.IterationsPerformed,
                                                      static_cast<std::uint32_t>(cg.Iterations));
                cgConverged = cgConverged && cg.Converged;

                RemoveWeightedMean(nextBasis[col], ops.Hodge0, active);
                for (int prev = 0; prev < col; ++prev)
                    OrthogonalizeWeighted(nextBasis[col], nextBasis[prev], ops.Hodge0, active);

                if (NormalizeWeighted(nextBasis[col], ops.Hodge0, active) == 0.0)
                    return std::nullopt;

                maxDelta = std::max(maxDelta, MaxActiveDifference(nextBasis[col], basis[col], active));
            }

            basis = nextBasis;
            result.Converged = maxDelta <= uiTolerance;
            if (result.Converged && cgConverged)
                break;
        }

        for (int col = 0; col < modeCount; ++col)
        {
            std::vector<double> laplace(n, 0.0);
            std::vector<double> massTimes(n, 0.0);
            std::vector<double> residual(n, 0.0);
            ops.Laplacian.Multiply(basis[col], laplace);
            ops.Hodge0.Multiply(basis[col], massTimes);

            const double denom = WeightedDot(basis[col], basis[col], ops.Hodge0, active);
            const double lambda = denom > 1.0e-20
                ? WeightedDot(basis[col], laplace, ops.Hodge0, active) / denom
                : 0.0;
            result.Eigenvalues[col] = lambda;

            double residualNorm = 0.0;
            double rhsNorm = 0.0;
            for (const uint32_t idx : active)
            {
                residual[idx] = laplace[idx] - lambda * massTimes[idx];
                residualNorm += residual[idx] * residual[idx];
                rhsNorm += massTimes[idx] * massTimes[idx];
            }
            result.Residuals[col] = std::sqrt(residualNorm / std::max(rhsNorm, 1.0e-20));
            result.Modes[col] = basis[col];
        }

        return result;
    }

    void PublishMeshModeProperty(Geometry::Halfedge::Mesh& mesh,
                                 std::string_view propertyName,
                                 std::span<const double> mode,
                                 bool normalize)
    {
        auto property = mesh.VertexProperties().GetOrAdd<float>(std::string(propertyName), 0.0f);
        std::fill(property.Vector().begin(), property.Vector().end(), 0.0f);

        std::vector<uint32_t> active;
        active.reserve(mesh.VertexCount());
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsValid(v) || mesh.IsDeleted(v))
                continue;
            active.push_back(static_cast<uint32_t>(i));
        }

        const double maxAbs = normalize ? MaxActiveAbs(mode, active) : 1.0;
        const double scale = maxAbs > 1.0e-20 ? 1.0 / maxAbs : 1.0;
        for (const uint32_t idx : active)
            property[idx] = static_cast<float>(mode[idx] * scale);
    }

    [[nodiscard]] std::vector<glm::vec2> ExtractGraphEmbeddingXY(const Geometry::Graph::Graph& graph)
    {
        std::vector<glm::vec2> positions(graph.VerticesSize(), glm::vec2(0.0f));
        for (std::size_t i = 0; i < graph.VerticesSize(); ++i)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!v.IsValid() || graph.IsDeleted(v))
                continue;
            const glm::vec3 p = graph.VertexPosition(v);
            positions[i] = glm::vec2(p.x, p.y);
        }
        return positions;
    }

    void PublishGraphSpectralProperties(Geometry::Graph::Graph& graph,
                                        std::span<const glm::vec2> positions,
                                        std::string_view uProperty,
                                        std::string_view vProperty,
                                        std::string_view radiusProperty)
    {
        auto u = graph.GetOrAddVertexProperty<float>(std::string(uProperty), 0.0f);
        auto v = graph.GetOrAddVertexProperty<float>(std::string(vProperty), 0.0f);
        auto radius = graph.GetOrAddVertexProperty<float>(std::string(radiusProperty), 0.0f);

        std::fill(u.Vector().begin(), u.Vector().end(), 0.0f);
        std::fill(v.Vector().begin(), v.Vector().end(), 0.0f);
        std::fill(radius.Vector().begin(), radius.Vector().end(), 0.0f);

        for (std::size_t i = 0; i < graph.VerticesSize(); ++i)
        {
            const Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
            if (!graph.IsValid(vh) || graph.IsDeleted(vh))
                continue;
            u[vh] = positions[i].x;
            v[vh] = positions[i].y;
            radius[vh] = glm::length(positions[i]);
        }
    }

    template <typename T>
    void BuildHistogram(std::span<const T> samples,
                        int binCount,
                        std::vector<float>& outHistogram)
    {
        outHistogram.assign(static_cast<std::size_t>(std::max(binCount, 1)), 0.0f);
        if (samples.empty())
            return;

        auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
        const double minValue = static_cast<double>(*minIt);
        const double maxValue = static_cast<double>(*maxIt);
        if (!std::isfinite(minValue) || !std::isfinite(maxValue))
            return;

        const double range = std::max(maxValue - minValue, 1.0e-12);
        const double invRange = 1.0 / range;

        for (const auto value : samples)
        {
            const double normalized = (static_cast<double>(value) - minValue) * invRange;
            const double clamped = std::clamp(normalized, 0.0, 1.0);
            const std::size_t index = std::min(
                outHistogram.size() - 1,
                static_cast<std::size_t>(clamped * static_cast<double>(outHistogram.size() - 1)));
            outHistogram[index] += 1.0f;
        }

        const float invCount = 1.0f / static_cast<float>(samples.size());
        for (auto& bin : outHistogram)
            bin *= invCount;
    }
}

// =========================================================================
// ColorSourceWidget
// =========================================================================
bool ColorSourceWidget(const char* label, Graphics::ColorSource& src,
                       const Geometry::ConstPropertySet* ps, const char* suffix)
{
    bool changed = false;
    char idBuf[128];

    ImGui::SeparatorText(label);

    // Property selector combo.
    if (ps)
    {
        auto props = Graphics::EnumerateColorableProperties(*ps);

        snprintf(idBuf, sizeof(idBuf), "Property##%s", suffix);
        const char* currentName = src.PropertyName.empty() ? "(none)" : src.PropertyName.c_str();
        if (ImGui::BeginCombo(idBuf, currentName))
        {
            if (ImGui::Selectable("(none)", src.PropertyName.empty()))
            {
                src.PropertyName.clear();
                changed = true;
            }
            for (const auto& p : props)
            {
                const char* typeLabel = "";
                switch (p.Type)
                {
                case Graphics::PropertyDataType::Scalar: typeLabel = " [float]";
                    break;
                case Graphics::PropertyDataType::Vec3: typeLabel = " [vec3]";
                    break;
                case Graphics::PropertyDataType::Vec4: typeLabel = " [vec4]";
                    break;
                }
                char itemLabel[256];
                snprintf(itemLabel, sizeof(itemLabel), "%s%s", p.Name.c_str(), typeLabel);
                if (ImGui::Selectable(itemLabel, src.PropertyName == p.Name))
                {
                    src.PropertyName = p.Name;
                    src.AutoRange = true;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    if (src.PropertyName.empty())
        return changed;

    // Colormap selector.
    snprintf(idBuf, sizeof(idBuf), "Colormap##%s", suffix);
    int mapIdx = static_cast<int>(src.Map);
    const char* mapNames[] = {"Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};
    if (ImGui::Combo(idBuf, &mapIdx, mapNames, 6))
    {
        src.Map = static_cast<Graphics::Colormap::Type>(mapIdx);
        changed = true;
    }

    // Auto-range checkbox.
    snprintf(idBuf, sizeof(idBuf), "Auto Range##%s", suffix);
    if (ImGui::Checkbox(idBuf, &src.AutoRange))
        changed = true;

    // Range sliders (disabled when auto-range is on).
    if (!src.AutoRange)
    {
        snprintf(idBuf, sizeof(idBuf), "Range Min##%s", suffix);
        if (ImGui::DragFloat(idBuf, &src.RangeMin, 0.01f))
            changed = true;
        snprintf(idBuf, sizeof(idBuf), "Range Max##%s", suffix);
        if (ImGui::DragFloat(idBuf, &src.RangeMax, 0.01f))
            changed = true;
    }
    else
    {
        ImGui::Text("Range: [%.4f, %.4f]", src.RangeMin, src.RangeMax);
    }

    // Bins slider.
    snprintf(idBuf, sizeof(idBuf), "Bins##%s", suffix);
    int bins = static_cast<int>(src.Bins);
    if (ImGui::SliderInt(idBuf, &bins, 0, 32, bins == 0 ? "Continuous" : "%d"))
    {
        src.Bins = static_cast<uint32_t>(std::max(0, bins));
        changed = true;
    }

    return changed;
}

// =========================================================================
// VectorFieldWidget
// =========================================================================
bool VectorFieldWidget(Graphics::VisualizationConfig& config,
                       const Geometry::ConstPropertySet* vertexPs,
                       const Geometry::ConstPropertySet* edgePs,
                       const Geometry::ConstPropertySet* facePs,
                       const char* suffix,
                       entt::registry* registry)
{
    bool changed = false;
    char idBuf[128];


    auto domainProps = [&](Graphics::VectorFieldDomain domain) -> const Geometry::ConstPropertySet*
    {
        switch (domain)
        {
        case Graphics::VectorFieldDomain::Vertex: return vertexPs;
        case Graphics::VectorFieldDomain::Edge:   return edgePs;
        case Graphics::VectorFieldDomain::Face:   return facePs;
        }
        return vertexPs;
    };

    auto domainVecProps = [&](const Geometry::ConstPropertySet* props)
    {
        std::vector<Graphics::PropertyInfo> result;
        if (props)
            result = Graphics::EnumerateVectorProperties(*props);
        return result;
    };

    ImGui::SeparatorText("Vector Fields");

    struct DomainSection
    {
        Graphics::VectorFieldDomain Domain;
        const Geometry::ConstPropertySet* Props;
        const char* Label;
        std::vector<Graphics::PropertyInfo> VectorProps;
    };

    const DomainSection sections[] = {
        {Graphics::VectorFieldDomain::Vertex, vertexPs, "Vertex", domainVecProps(vertexPs)},
        {Graphics::VectorFieldDomain::Edge,   edgePs,   "Edge",   domainVecProps(edgePs)},
        {Graphics::VectorFieldDomain::Face,   facePs,   "Face",   domainVecProps(facePs)},
    };

    for (const auto& section : sections)
    {
        if (!section.Props)
            continue;

        ImGui::PushID(section.Label);
        ImGui::SeparatorText(section.Label);

        snprintf(idBuf, sizeof(idBuf), "Add Vector Field##%s-%s", suffix, section.Label);
        if (!section.VectorProps.empty() && ImGui::BeginCombo(idBuf, "Add..."))
        {
            for (const auto& p : section.VectorProps)
            {
                std::string label = std::string(section.Label) + ": " + p.Name;
                if (ImGui::Selectable(label.c_str()))
                {
                    Graphics::VectorFieldEntry entry;
                    entry.Domain = section.Domain;
                    entry.VectorPropertyName = p.Name;
                    config.VectorFields.push_back(std::move(entry));
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        const bool hasEntries = std::ranges::any_of(config.VectorFields, [&](const Graphics::VectorFieldEntry& vf)
        {
            return vf.Domain == section.Domain;
        });

        if (!hasEntries)
        {
            ImGui::TextDisabled("No vector fields attached to this domain.");
            ImGui::PopID();
            continue;
        }

        for (size_t i = 0; i < config.VectorFields.size();)
        {
            auto& vf = config.VectorFields[i];
            if (vf.Domain != section.Domain)
            {
                ++i;
                continue;
            }

            ImGui::PushID(static_cast<int>(i));
            ImGui::Separator();
            ImGui::Text("%s", vf.VectorPropertyName.empty() ? "(inactive)" : vf.VectorPropertyName.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                if (registry)
                    Graphics::OverlayEntityFactory::DestroyOverlay(*registry, vf.ChildEntity);
                config.VectorFields.erase(config.VectorFields.begin() + static_cast<ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                continue;
            }

            // Domain is fixed at creation time and shown as read-only label.
            // Changing the domain would invalidate PropertySet references.
            static constexpr const char* kDomainNames[] = {"Vertex", "Edge", "Face"};
            const auto domainIdx = static_cast<size_t>(vf.Domain);
            ImGui::TextDisabled("Domain: %s", domainIdx < std::size(kDomainNames) ? kDomainNames[domainIdx] : "?");

            auto* props = domainProps(vf.Domain);
            const auto vectorProps = domainVecProps(props);

            if (props && !vectorProps.empty())
            {
                const char* basePreview = vf.BasePropertyName.empty()
                    ? "(Canonical Positions)"
                    : vf.BasePropertyName.c_str();
                if (ImGui::BeginCombo("Base Points", basePreview))
                {
                    if (ImGui::Selectable("(Canonical Positions)", vf.BasePropertyName.empty()))
                    {
                        vf.BasePropertyName.clear();
                        changed = true;
                    }
                    for (const auto& p : vectorProps)
                    {
                        if (ImGui::Selectable(p.Name.c_str(), vf.BasePropertyName == p.Name))
                        {
                            vf.BasePropertyName = p.Name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                const char* vectorPreview = vf.VectorPropertyName.empty()
                    ? "(None)"
                    : vf.VectorPropertyName.c_str();
                if (ImGui::BeginCombo("Vector Source", vectorPreview))
                {
                    if (ImGui::Selectable("(None)", vf.VectorPropertyName.empty()))
                    {
                        vf.VectorPropertyName.clear();
                        changed = true;
                    }
                    for (const auto& p : vectorProps)
                    {
                        if (ImGui::Selectable(p.Name.c_str(), vf.VectorPropertyName == p.Name))
                        {
                            vf.VectorPropertyName = p.Name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::TextDisabled("No vec3 properties available for %s.", section.Label);
            }

            if (ColorSourceWidget("Arrow Color Source", vf.ArrowColor, props, "ArrowColor"))
                changed = true;

            if (ImGui::DragFloat("Scale", &vf.Scale, 0.01f, 0.001f, 100.0f))
                changed = true;
            if (ImGui::SliderFloat("Width", &vf.EdgeWidth, 0.5f, 5.0f))
                changed = true;
            if (ColorEdit4("Uniform Color", vf.Color))
                changed = true;
            if (ImGui::Checkbox("Overlay", &vf.Overlay))
                changed = true;

            if (props)
            {
                auto scalarProps = Graphics::EnumerateScalarProperties(*props);
                const char* lenPreview = vf.LengthPropertyName.empty()
                    ? "(Uniform)"
                    : vf.LengthPropertyName.c_str();
                if (ImGui::BeginCombo("Arrow Length", lenPreview))
                {
                    if (ImGui::Selectable("(Uniform)", vf.LengthPropertyName.empty()))
                    {
                        vf.LengthPropertyName.clear();
                        changed = true;
                    }
                    for (const auto& sp : scalarProps)
                    {
                        if (ImGui::Selectable(sp.Name.c_str(), vf.LengthPropertyName == sp.Name))
                        {
                            vf.LengthPropertyName = sp.Name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::PopID();
            ++i;
        }

        ImGui::PopID();
    }

    return changed;
}

// =========================================================================
// Reusable micro-widgets
// =========================================================================

bool PointRenderModeCombo(const char* label,
                          Geometry::PointCloud::RenderMode& mode)
{
    static constexpr const char* kModeNames[] = {"Flat Disc", "Surfel", "EWA Splatting", "Sphere"};
    int idx = static_cast<int>(mode);
    if (idx < 0 || idx > 3) idx = 0;
    if (ImGui::Combo(label, &idx, kModeNames, 4))
    {
        mode = static_cast<Geometry::PointCloud::RenderMode>(idx);
        return true;
    }
    return false;
}

bool ColorEdit4(const char* label, glm::vec4& color)
{
    float c[4] = {color.r, color.g, color.b, color.a};
    if (ImGui::ColorEdit4(label, c))
    {
        color = glm::vec4(c[0], c[1], c[2], c[3]);
        return true;
    }
    return false;
}

bool DrawPropertySetBrowserWidget(const char* label,
                                  const Geometry::ConstPropertySet* ps,
                                  PropertySetBrowserState& state,
                                  const char* suffix)
{
    if (label && label[0] != '\0')
        ImGui::SeparatorText(label);

    if (!ps)
    {
        ImGui::TextDisabled("No property set available.");
        return false;
    }

    const auto names = ps->Properties();
    if (names.empty())
    {
        ImGui::TextDisabled("No properties.");
        return false;
    }

    state.SelectedProperty = std::clamp(state.SelectedProperty, 0, static_cast<int>(names.size()) - 1);
    const char* preview = names[static_cast<std::size_t>(state.SelectedProperty)].c_str();

    char comboLabel[128];
    std::snprintf(comboLabel, sizeof(comboLabel), "Property##%s", suffix ? suffix : "PropertySetBrowser");
    bool changed = false;
    if (ImGui::BeginCombo(comboLabel, preview))
    {
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
        {
            const bool selected = (state.SelectedProperty == i);
            if (ImGui::Selectable(names[static_cast<std::size_t>(i)].c_str(), selected))
            {
                state.SelectedProperty = i;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::DragInt("Preview Rows", &state.PreviewRows, 1.0f, 1, 4096);
    ImGui::Checkbox("Show Indices", &state.ShowIndices);
    ImGui::SameLine();
    ImGui::Checkbox("Show All Rows", &state.ShowAllRows);

    const std::string& propertyName = names[static_cast<std::size_t>(state.SelectedProperty)];
    ImGui::Text("Property Count: %zu", names.size());
    ImGui::Text("Value Count: %zu", ps->Size());
    ImGui::TextDisabled("Type: %s", PropertyTypeLabel(*ps, propertyName));

    if (DrawTypedPropertyPreview<bool>(*ps, propertyName, state)) return changed;
    if (DrawTypedPropertyPreview<int>(*ps, propertyName, state)) return changed;
    if (DrawTypedPropertyPreview<uint32_t>(*ps, propertyName, state)) return changed;
    if (DrawTypedPropertyPreview<float>(*ps, propertyName, state)) return changed;
    if (DrawTypedPropertyPreview<double>(*ps, propertyName, state)) return changed;
    if (DrawTypedPropertyPreview<glm::vec2>(*ps, propertyName, state)) return changed;
    if (DrawTypedPropertyPreview<glm::vec3>(*ps, propertyName, state)) return changed;
    if (DrawTypedPropertyPreview<glm::vec4>(*ps, propertyName, state)) return changed;

    ImGui::TextDisabled("Unsupported preview type for '%s'.", propertyName.c_str());
    return changed;
}

void DrawDomainBadges(GeometryProcessingDomain domains)
{
    bool first = true;
    const auto draw = [&](GeometryProcessingDomain domain)
    {
        if (!HasAnyDomain(domains, domain))
            return;
        if (!first)
            ImGui::SameLine();
        ImGui::TextDisabled("[%s]", GeometryDomainLabel(domain));
        first = false;
    };

    draw(GeometryProcessingDomain::MeshVertices);
    draw(GeometryProcessingDomain::MeshEdges);
    draw(GeometryProcessingDomain::MeshHalfedges);
    draw(GeometryProcessingDomain::MeshFaces);
    draw(GeometryProcessingDomain::GraphVertices);
    draw(GeometryProcessingDomain::GraphEdges);
    draw(GeometryProcessingDomain::GraphHalfedges);
    draw(GeometryProcessingDomain::PointCloudPoints);
}

bool DrawKMeansWidget(Runtime::Engine& engine,
                     entt::entity entity,
                     KMeansWidgetState& state)
{
    auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
    const auto domains = GetGeometryProcessingCapabilities(reg, entity).Domains
                       & GetSupportedDomains(GeometryProcessingAlgorithm::KMeans);
    DrawDomainBadges(domains);

    if (domains == GeometryProcessingDomain::None)
    {
        ImGui::TextDisabled("No compatible point-set domain is available on the selected entity.");
        return false;
    }

    const KMeansDomainUiData ui = BuildKMeansDomainUiData(engine, reg, entity);
    if (ui.Count <= 0)
    {
        ImGui::TextDisabled("No compatible point-set domain is available on the selected entity.");
        return false;
    }

    int selectedDomainIndex = FindKMeansDomainIndex(ui, state.SelectedDomain);
    if (selectedDomainIndex < 0)
        selectedDomainIndex = 0;

    if (ui.Count > 1)
    {
        ImGui::Combo("Source Domain##KMeans", &selectedDomainIndex, ui.LabelPointers.data(), ui.Count);
    }
    else
    {
        ImGui::TextDisabled("Source Domain: %s", ui.LabelPointers[0]);
    }

    const auto selectedDomain = ui.Domains[selectedDomainIndex];
    state.SelectedDomain = selectedDomain;
    const auto& targetInfo = ui.Targets[selectedDomainIndex];

    ImGui::Text("Points: %zu", targetInfo.PointCount);
    ImGui::TextDisabled("Published color property: %s", KMeansResultProperty(selectedDomain));

    ImGui::DragInt("Clusters##ProcKMeans", &state.ClusterCount, 1.0f, 1, 4096);
    ImGui::DragInt("Max Iterations##ProcKMeans", &state.MaxIterations, 1.0f, 1, 4096);
    ImGui::DragInt("Seed##ProcKMeans", &state.Seed, 1.0f, 0, std::numeric_limits<int>::max());

    {
        const char* initItems[] = {"Random", "Hierarchical"};
        ImGui::Combo("Initialization##ProcKMeans", &state.Initialization, initItems, IM_ARRAYSIZE(initItems));
    }

    if (targetInfo.SupportsCuda)
    {
        const char* backendItems[] = {
            "CPU",
#ifdef INTRINSIC_HAS_CUDA
            "CUDA"
#endif
        };
        state.Backend = std::clamp(state.Backend, 0, IM_ARRAYSIZE(backendItems) - 1);
        ImGui::Combo("Backend##ProcKMeans", &state.Backend, backendItems, IM_ARRAYSIZE(backendItems));
    }
    else
    {
        state.Backend = static_cast<int>(Geometry::KMeans::Backend::CPU);
        ImGui::TextDisabled("Backend: CPU (CUDA backend unavailable for this target or build)");
    }

    bool dispatched = false;
    const bool canRun = targetInfo.IsValid() && !targetInfo.JobPending;
    if (!canRun)
        ImGui::BeginDisabled();

    if (ImGui::Button("Run K-Means##GeometryProcessing"))
    {
        Geometry::KMeans::KMeansParams params{};
        params.ClusterCount = static_cast<uint32_t>(std::max(state.ClusterCount, 1));
        params.MaxIterations = static_cast<uint32_t>(std::max(state.MaxIterations, 1));
        params.Seed = static_cast<uint32_t>(std::max(state.Seed, 0));
        params.Init = static_cast<Geometry::KMeans::Initialization>(state.Initialization);
        params.Compute = static_cast<Geometry::KMeans::Backend>(state.Backend);
        dispatched = Runtime::PointCloudKMeans::Schedule(engine, entity, params, selectedDomain);
    }

    if (!canRun)
        ImGui::EndDisabled();

    if (targetInfo.SupportsCuda && selectedDomain == Runtime::PointCloudKMeans::Domain::PointCloudPoints)
    {
        ImGui::SameLine();
        if (ImGui::Button("Release Compute Buffers##GeometryProcessing"))
            Runtime::PointCloudKMeans::ReleaseEntityBuffers(engine, entity);
    }

    if (const auto stats = ReadKMeansStatus(reg, entity, selectedDomain))
    {
        ImGui::Text("Job Pending: %s", stats->JobPending ? "Yes" : "No");
        ImGui::Text("Last Backend: %s", stats->LastBackend == Geometry::KMeans::Backend::CUDA ? "CUDA" : "CPU");
        ImGui::Text("Last Iterations: %u", stats->LastIterations);
        ImGui::Text("Last Converged: %s", stats->LastConverged ? "Yes" : "No");
        ImGui::Text("Last Inertia: %.6f", stats->LastInertia);
        ImGui::Text("Last Max-Distance Index: %u", stats->LastMaxDistanceIndex);
        ImGui::Text("Last Duration: %.3f ms", stats->LastDurationMs);
    }

    // Colormap selector for mesh vertex labels (Voronoi texel rendering).
    if (selectedDomain == Runtime::PointCloudKMeans::Domain::MeshVertices)
    {
        if (auto* md = reg.try_get<ECS::Mesh::Data>(entity))
        {
            if (md->Visualization.VertexColors.PropertyName == "v:kmeans_label_f")
            {
                ImGui::SeparatorText("Label Colormap");
                const char* mapNames[] = {"Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};
                int mapIdx = static_cast<int>(md->Visualization.VertexColors.Map);
                if (ImGui::Combo("Colormap##KMeansVoronoi", &mapIdx, mapNames, 6))
                {
                    md->Visualization.VertexColors.Map = static_cast<Graphics::Colormap::Type>(mapIdx);
                    md->AttributesDirty = true;
                    reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);
                }
                int bins = static_cast<int>(md->Visualization.VertexColors.Bins);
                if (ImGui::DragInt("Bins##KMeansVoronoi", &bins, 1.0f, 0, 256))
                {
                    md->Visualization.VertexColors.Bins = static_cast<uint32_t>(std::max(bins, 0));
                    md->AttributesDirty = true;
                    reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);
                }
                ImGui::TextDisabled("Bins=0: continuous colormap. >0: quantized steps.");
            }
        }
    }

    return dispatched;
}

bool DrawMeshSpectralWidget(Runtime::Engine& engine,
                           entt::entity entity,
                           MeshSpectralWidgetState& state)
{
    auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
    auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
    auto* collider = reg.try_get<ECS::MeshCollider::Component>(entity);

    std::shared_ptr<Geometry::Halfedge::Mesh> meshRef;
    if (meshData && meshData->MeshRef)
        meshRef = meshData->MeshRef;
    else if (collider && collider->CollisionRef && collider->CollisionRef->SourceMesh)
        meshRef = collider->CollisionRef->SourceMesh;

    DrawDomainBadges(meshRef ? kMeshTopologyDomains : GeometryProcessingDomain::None);
    if (!meshRef)
    {
        ImGui::TextDisabled("Mesh spectral analysis requires an authoritative halfedge surface mesh.");
        return false;
    }

    ImGui::Text("Vertices: %zu", meshRef->VertexCount());
    ImGui::Text("Edges: %zu", meshRef->EdgeCount());
    ImGui::Text("Faces: %zu", meshRef->FaceCount());

    ImGui::DragInt("Modes", &state.ModeCount, 1.0f, 1, 2);
    ImGui::DragInt("Inverse Iterations", &state.MaxIterations, 1.0f, 1, 256);
    ImGui::DragFloat("Shift t", &state.Shift, 0.01f, 1.0e-4f, 100.0f, "%.5f", ImGuiSliderFlags_Logarithmic);
    ImGui::DragFloat("Solver Tolerance", &state.SolverTolerance, 1.0e-7f, 1.0e-8f, 1.0e-2f, "%.1e",
                     ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("Normalize Published Modes", &state.NormalizePublishedModes);

    ImGui::InputText("Mode 0 Property", state.Mode0Property, IM_ARRAYSIZE(state.Mode0Property));
    if (std::clamp(state.ModeCount, 1, 2) > 1)
        ImGui::InputText("Mode 1 Property", state.Mode1Property, IM_ARRAYSIZE(state.Mode1Property));

    ImGui::TextDisabled("Solves the generalized eigenproblem Lx = λMx via projected inverse iteration on (M + tL)^{-1}M.");

    bool changed = false;
    if (ImGui::Button("Compute Mesh Spectral Modes"))
    {
        const auto computed = ComputeMeshSpectralModes(*meshRef,
                                                       state.ModeCount,
                                                       state.MaxIterations,
                                                       state.Shift,
                                                       state.SolverTolerance);
        if (computed)
        {
            state.LastActiveVertices = computed->ActiveVertices;
            state.LastIterations = computed->IterationsPerformed;
            state.LastConverged = computed->Converged;
            state.LastEigenvalue0 = computed->Eigenvalues[0];
            state.LastEigenvalue1 = computed->Eigenvalues[1];
            state.LastResidual0 = computed->Residuals[0];
            state.LastResidual1 = computed->Residuals[1];

            PublishMeshModeProperty(*meshRef, state.Mode0Property, computed->Modes[0], state.NormalizePublishedModes);
            if (std::clamp(state.ModeCount, 1, 2) > 1)
                PublishMeshModeProperty(*meshRef, state.Mode1Property, computed->Modes[1], state.NormalizePublishedModes);

            auto& writableMeshData = reg.get_or_emplace<ECS::Mesh::Data>(entity);
            writableMeshData.MeshRef = meshRef;
            writableMeshData.AttributesDirty = true;
            reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);
            engine.GetSceneManager().GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({entity});
            changed = true;
        }
        else
        {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                               "Failed: mesh is degenerate or the shifted solve did not produce a valid mode basis.");
        }
    }

    if (state.LastActiveVertices > 0)
    {
        ImGui::SeparatorText("Diagnostics");
        ImGui::Text("Active Vertices: %zu", state.LastActiveVertices);
        ImGui::Text("Iterations: %u", state.LastIterations);
        ImGui::Text("Converged: %s", state.LastConverged ? "Yes" : "No");
        ImGui::Text("λ0: %.6g   residual: %.6g",
                    state.LastEigenvalue0,
                    state.LastResidual0);
        if (std::clamp(state.ModeCount, 1, 2) > 1)
        {
            ImGui::Text("λ1: %.6g   residual: %.6g",
                        state.LastEigenvalue1,
                        state.LastResidual1);
        }
        ImGui::TextDisabled("Visualize the published scalar fields via the mesh Vertex Color Source selector.");
    }

    return changed;
}

bool DrawGraphSpectralWidget(Runtime::Engine& engine,
                            entt::entity entity,
                            GraphSpectralWidgetState& state)
{
    auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
    auto* graphData = reg.try_get<ECS::Graph::Data>(entity);
    if (!graphData || !graphData->GraphRef)
    {
        DrawDomainBadges(GeometryProcessingDomain::None);
        ImGui::TextDisabled("Graph spectral layout requires an authoritative graph entity.");
        return false;
    }

    auto& graph = *graphData->GraphRef;
    DrawDomainBadges(kGraphTopologyDomains);
    ImGui::Text("Nodes: %zu", graph.VertexCount());
    ImGui::Text("Edges: %zu", graph.EdgeCount());

    static constexpr const char* kVariants[] = {"Combinatorial", "Normalized Symmetric"};
    state.Variant = std::clamp(state.Variant, 0, 1);
    ImGui::Combo("Laplacian Variant", &state.Variant, kVariants, IM_ARRAYSIZE(kVariants));
    ImGui::DragInt("Iterations##GraphSpectral", &state.MaxIterations, 1.0f, 1, 4096);
    ImGui::DragFloat("Step Scale##GraphSpectral", &state.StepScale, 0.01f, 0.01f, 4.0f, "%.3f");
    ImGui::DragFloat("Convergence Tol##GraphSpectral", &state.ConvergenceTolerance, 1.0e-6f,
                     1.0e-7f, 1.0e-2f, "%.1e", ImGuiSliderFlags_Logarithmic);
    ImGui::DragFloat("Min Norm Epsilon##GraphSpectral", &state.MinNormEpsilon, 1.0e-8f,
                     1.0e-9f, 1.0e-2f, "%.1e", ImGuiSliderFlags_Logarithmic);
    ImGui::DragFloat("Area Extent##GraphSpectral", &state.AreaExtent, 0.05f, 0.1f, 128.0f, "%.2f",
                     ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Published Properties");
    ImGui::InputText("U Property", state.UProperty, IM_ARRAYSIZE(state.UProperty));
    ImGui::InputText("V Property", state.VProperty, IM_ARRAYSIZE(state.VProperty));
    ImGui::InputText("Radius Property", state.RadiusProperty, IM_ARRAYSIZE(state.RadiusProperty));

    ImGui::SeparatorText("Apply Layout");
    ImGui::Checkbox("Preserve Existing Z", &state.PreserveExistingZ);
    if (!state.PreserveExistingZ)
        ImGui::DragFloat("Output Z", &state.OutputZ, 0.01f, -100.0f, 100.0f, "%.3f");

    auto runLayout = [&](bool applyPositions)
    {
        auto positions = ExtractGraphEmbeddingXY(graph);
        Geometry::Graph::SpectralLayoutParams params;
        params.MaxIterations = static_cast<std::uint32_t>(std::max(state.MaxIterations, 1));
        params.StepScale = state.StepScale;
        params.ConvergenceTolerance = state.ConvergenceTolerance;
        params.MinNormEpsilon = state.MinNormEpsilon;
        params.AreaExtent = state.AreaExtent;
        params.Variant = static_cast<Geometry::Graph::SpectralLayoutParams::LaplacianVariant>(state.Variant);

        const auto result = Geometry::Graph::ComputeSpectralLayout(graph, positions, params);
        if (!result)
            return false;

        state.LastActiveVertices = result->ActiveVertexCount;
        state.LastActiveEdges = result->ActiveEdgeCount;
        state.LastIterations = result->IterationsPerformed;
        state.LastSubspaceDelta = result->SubspaceDelta;
        state.LastConverged = result->Converged;

        PublishGraphSpectralProperties(graph, positions, state.UProperty, state.VProperty, state.RadiusProperty);
        if (const auto crossings = Geometry::Graph::CountEdgeCrossings(graph, positions))
        {
            state.LastCrossingCountValid = true;
            state.LastCrossingCount = *crossings;
        }
        else
        {
            state.LastCrossingCountValid = false;
            state.LastCrossingCount = 0;
        }

        reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);

        if (applyPositions)
        {
            for (std::size_t i = 0; i < graph.VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
                if (!graph.IsValid(vh) || graph.IsDeleted(vh))
                    continue;

                const glm::vec3 current = graph.VertexPosition(vh);
                graph.SetVertexPosition(vh, glm::vec3(positions[i].x,
                                                      positions[i].y,
                                                      state.PreserveExistingZ ? current.z : state.OutputZ));
            }
            reg.emplace_or_replace<ECS::DirtyTag::VertexPositions>(entity);
        }

        engine.GetSceneManager().GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({entity});
        return true;
    };

    bool changed = false;
    if (ImGui::Button("Compute Spectral Layout Properties"))
        changed = runLayout(false);
    ImGui::SameLine();
    if (ImGui::Button("Apply Spectral Layout To Graph"))
        changed = runLayout(true) || changed;

    if (state.LastActiveVertices > 0)
    {
        ImGui::SeparatorText("Diagnostics");
        ImGui::Text("Active Vertices: %zu", state.LastActiveVertices);
        ImGui::Text("Active Edges: %zu", state.LastActiveEdges);
        ImGui::Text("Iterations: %u", state.LastIterations);
        ImGui::Text("Converged: %s", state.LastConverged ? "Yes" : "No");
        ImGui::Text("Subspace Delta: %.6g", static_cast<double>(state.LastSubspaceDelta));
        if (state.LastCrossingCountValid)
            ImGui::Text("Edge Crossings: %zu", state.LastCrossingCount);
        else
            ImGui::TextDisabled("Edge crossings unavailable for the current embedding.");
        ImGui::TextDisabled("Use '%s' / '%s' as graph color sources or apply the embedding to update node positions.",
                            state.UProperty, state.VProperty);
    }

    return changed;
}

bool DrawRemeshingWidget(Runtime::Engine& engine,
                        entt::entity entity,
                        RemeshingWidgetState& state)
{
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? kMeshTopologyDomains
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Remeshing requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    ImGui::TextDisabled("Input Domains: Mesh Vertices / Mesh Edges / Mesh Halfedges / Mesh Faces");
    ImGui::DragFloat("Target Length", &state.TargetLength, 0.01f, 0.001f, 10.0f);
    ImGui::DragInt("Iterations", &state.Iterations, 1.0f, 1, 20);
    ImGui::Checkbox("Preserve Boundary", &state.PreserveBoundary);

    bool changed = false;
    ImGui::SeparatorText("Approaches");
    ImGui::TextDisabled("Uniform target edge length for evenly distributed tessellation.");
    if (ImGui::Button("Run Isotropic Remeshing"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::Remeshing::RemeshingParams params;
            params.TargetLength = ui.TargetLength;
            params.Iterations = ui.Iterations;
            params.PreserveBoundary = ui.PreserveBoundary;
            static_cast<void>(Geometry::Remeshing::Remesh(mesh, params));
        });
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Curvature-aware min/max edge lengths for adaptive workflows.");
    if (ImGui::Button("Run Adaptive Remeshing"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
            params.MinEdgeLength = ui.TargetLength * 0.5f;
            params.MaxEdgeLength = ui.TargetLength * 2.0f;
            params.Iterations = ui.Iterations;
            params.PreserveBoundary = ui.PreserveBoundary;
            static_cast<void>(Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params));
        }) || changed;
    }

    return changed;
}

bool DrawSimplificationWidget(Runtime::Engine& engine,
                             entt::entity entity,
                             SimplificationWidgetState& state)
{
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? kMeshTopologyDomains
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Simplification requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    static constexpr const char* kQuadricTypes[] = {"Plane", "Triangle", "Point"};
    static constexpr const char* kProbabilisticModes[] = {"Deterministic", "Isotropic", "Covariance"};
    static constexpr const char* kResidences[] = {"Vertices", "Faces", "Vertices + Faces"};
    static constexpr const char* kPlacementPolicies[] = {
        "Keep Survivor",
        "Quadric Minimizer",
        "Best of Endpoints + Minimizer"
    };

    ImGui::TextDisabled("Input Domains: Mesh Vertices / Mesh Edges / Mesh Halfedges / Mesh Faces");
    ImGui::DragInt("Target Faces", &state.TargetFaces, 10.0f, 10, 1000000);
    ImGui::Checkbox("Preserve Boundary", &state.PreserveBoundary);
    ImGui::DragFloat("Hausdorff Error", &state.HausdorffError, 0.001f, 0.0f, 10.0f, "%.4f");
    ImGui::DragFloat("Max Normal Deviation (deg)", &state.MaxNormalDeviationDeg, 1.0f, 0.0f, 180.0f, "%.1f");

    ImGui::SeparatorText("Quadrics");
    ImGui::Combo("Quadric Type", &state.QuadricType, kQuadricTypes, IM_ARRAYSIZE(kQuadricTypes));
    ImGui::Combo("Probabilistic Mode", &state.ProbabilisticMode, kProbabilisticModes, IM_ARRAYSIZE(kProbabilisticModes));
    ImGui::Combo("Quadric Residence", &state.Residence, kResidences, IM_ARRAYSIZE(kResidences));
    ImGui::Combo("Placement Policy", &state.PlacementPolicy, kPlacementPolicies, IM_ARRAYSIZE(kPlacementPolicies));
    ImGui::Checkbox("Average Vertex Quadrics", &state.AverageVertexQuadrics);
    ImGui::Checkbox("Average Face Quadrics", &state.AverageFaceQuadrics);

    const auto quadricType = static_cast<Geometry::Simplification::QuadricType>(state.QuadricType);
    const auto probabilisticMode = static_cast<Geometry::Simplification::QuadricProbabilisticMode>(state.ProbabilisticMode);

    if (quadricType == Geometry::Simplification::QuadricType::Point
        && probabilisticMode != Geometry::Simplification::QuadricProbabilisticMode::Deterministic)
    {
        ImGui::TextDisabled("Point quadrics currently use deterministic point-fit energy only.");
    }

    if (quadricType != Geometry::Simplification::QuadricType::Point
        && probabilisticMode == Geometry::Simplification::QuadricProbabilisticMode::Isotropic)
    {
        ImGui::DragFloat("Position StdDev", &state.PositionStdDev, 0.001f, 0.0f, 10.0f, "%.5f");
        if (quadricType == Geometry::Simplification::QuadricType::Plane)
            ImGui::DragFloat("Normal StdDev", &state.NormalStdDev, 0.001f, 0.0f, 10.0f, "%.5f");
    }
    else if (quadricType != Geometry::Simplification::QuadricType::Point
             && probabilisticMode == Geometry::Simplification::QuadricProbabilisticMode::Covariance)
    {
        if (quadricType == Geometry::Simplification::QuadricType::Plane)
        {
            ImGui::InputText("Face Position Covariance", state.FacePositionCovarianceProperty,
                             IM_ARRAYSIZE(state.FacePositionCovarianceProperty));
            ImGui::InputText("Face Normal Covariance", state.FaceNormalCovarianceProperty,
                             IM_ARRAYSIZE(state.FaceNormalCovarianceProperty));
        }
        else if (quadricType == Geometry::Simplification::QuadricType::Triangle)
        {
            ImGui::InputText("Vertex Position Covariance", state.VertexPositionCovarianceProperty,
                             IM_ARRAYSIZE(state.VertexPositionCovarianceProperty));
        }
        else
        {
            ImGui::TextDisabled("Point quadrics are deterministic; covariance inputs are ignored.");
        }
    }

    if (!ImGui::Button("Run QEM Simplification"))
        return false;

    const auto ui = state;
    return ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
    {
        Geometry::Simplification::Params params;
        params.TargetFaces = static_cast<std::size_t>(std::max(ui.TargetFaces, 0));
        params.PreserveBoundary = ui.PreserveBoundary;
        params.HausdorffError = ui.HausdorffError;
        params.MaxNormalDeviationDegrees = ui.MaxNormalDeviationDeg;
        params.Quadric.Type = static_cast<Geometry::Simplification::QuadricType>(ui.QuadricType);
        params.Quadric.ProbabilisticMode = static_cast<Geometry::Simplification::QuadricProbabilisticMode>(ui.ProbabilisticMode);
        params.Quadric.Residence = static_cast<Geometry::Simplification::QuadricResidence>(ui.Residence);
        params.Quadric.PlacementPolicy = static_cast<Geometry::Simplification::CollapsePlacementPolicy>(ui.PlacementPolicy);
        params.Quadric.AverageVertexQuadrics = ui.AverageVertexQuadrics;
        params.Quadric.AverageFaceQuadrics = ui.AverageFaceQuadrics;
        params.Quadric.PositionStdDev = ui.PositionStdDev;
        params.Quadric.NormalStdDev = ui.NormalStdDev;
        params.Quadric.VertexPositionCovarianceProperty = ui.VertexPositionCovarianceProperty;
        params.Quadric.FacePositionCovarianceProperty = ui.FacePositionCovarianceProperty;
        params.Quadric.FaceNormalCovarianceProperty = ui.FaceNormalCovarianceProperty;
        if (params.Quadric.Type == Geometry::Simplification::QuadricType::Point)
            params.Quadric.ProbabilisticMode = Geometry::Simplification::QuadricProbabilisticMode::Deterministic;
        static_cast<void>(Geometry::Simplification::Simplify(mesh, params));
    });
}

bool DrawSmoothingWidget(Runtime::Engine& engine,
                        entt::entity entity,
                        SmoothingWidgetState& state)
{
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? kMeshTopologyDomains
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Smoothing requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    ImGui::TextDisabled("Input Domains: Mesh Vertices / Mesh Edges / Mesh Halfedges / Mesh Faces");
    ImGui::DragInt("Iterations", &state.Iterations, 1.0f, 1, 100);
    ImGui::DragFloat("Lambda", &state.Lambda, 0.01f, 0.0f, 1.0f);
    ImGui::Checkbox("Preserve Boundary", &state.PreserveBoundary);

    bool changed = false;
    ImGui::SeparatorText("Approaches");
    if (ImGui::Button("Run Uniform Laplacian"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::Smoothing::SmoothingParams params;
            params.Iterations = ui.Iterations;
            params.Lambda = ui.Lambda;
            params.PreserveBoundary = ui.PreserveBoundary;
            static_cast<void>(Geometry::Smoothing::UniformLaplacian(mesh, params));
        });
    }
    if (ImGui::Button("Run Cotan Laplacian"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::Smoothing::SmoothingParams params;
            params.Iterations = ui.Iterations;
            params.Lambda = ui.Lambda;
            params.PreserveBoundary = ui.PreserveBoundary;
            static_cast<void>(Geometry::Smoothing::CotanLaplacian(mesh, params));
        }) || changed;
    }
    if (ImGui::Button("Run Taubin Smoothing"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::Smoothing::TaubinParams params;
            params.Iterations = ui.Iterations;
            params.Lambda = ui.Lambda;
            params.PreserveBoundary = ui.PreserveBoundary;
            static_cast<void>(Geometry::Smoothing::Taubin(mesh, params));
        }) || changed;
    }
    if (ImGui::Button("Run Implicit Smoothing"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::Smoothing::ImplicitSmoothingParams params;
            params.Iterations = ui.Iterations;
            params.Lambda = ui.Lambda;
            params.PreserveBoundary = ui.PreserveBoundary;
            static_cast<void>(Geometry::Smoothing::ImplicitLaplacian(mesh, params));
        }) || changed;
    }

    return changed;
}

bool DrawSubdivisionWidget(Runtime::Engine& engine,
                          entt::entity entity,
                          SubdivisionWidgetState& state)
{
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? kMeshTopologyDomains
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Subdivision requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    ImGui::TextDisabled("Input Domains: Mesh Vertices / Mesh Edges / Mesh Halfedges / Mesh Faces");
    ImGui::DragInt("Iterations", &state.Iterations, 1.0f, 1, 5);
    ImGui::Checkbox("Clamp output faces", &state.EnforceFaceBudget);
    if (state.EnforceFaceBudget)
    {
        ImGui::DragInt("Max Output Faces", &state.MaxOutputFaces, 1000.0f, 1024, 4000000);
    }

    bool changed = false;
    const auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
    const auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
    const std::size_t inputFaceCount = (meshData && meshData->MeshRef) ? meshData->MeshRef->FaceCount() : 0u;

    std::size_t requestedFaces = inputFaceCount;
    for (int i = 0; i < std::max(state.Iterations, 0); ++i)
    {
        if (requestedFaces > (std::numeric_limits<std::size_t>::max() / 4u))
            break;
        requestedFaces *= 4u;
    }

    if (state.EnforceFaceBudget && state.MaxOutputFaces > 0 && inputFaceCount > 0)
    {
        std::size_t clampedIterations = 0;
        std::size_t faces = inputFaceCount;
        while (clampedIterations < static_cast<std::size_t>(std::max(state.Iterations, 0)))
        {
            if (faces > (static_cast<std::size_t>(state.MaxOutputFaces) / 4u))
                break;
            faces *= 4u;
            ++clampedIterations;
        }

        ImGui::TextDisabled("Face growth estimate: %zu -> %zu (effective iterations: %zu)",
                            inputFaceCount,
                            faces,
                            clampedIterations);
    }
    else if (inputFaceCount > 0)
    {
        ImGui::TextDisabled("Face growth estimate: %zu -> %zu", inputFaceCount, requestedFaces);
    }

    if (ImGui::Button("Run Loop Subdivision"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::Halfedge::Mesh out;
            Geometry::Subdivision::SubdivisionParams params;
            params.Iterations = static_cast<std::size_t>(std::max(ui.Iterations, 1));
            if (ui.EnforceFaceBudget && ui.MaxOutputFaces > 0)
            {
                params.MaxOutputFaces = static_cast<std::size_t>(ui.MaxOutputFaces);
            }
            if (Geometry::Subdivision::Subdivide(mesh, out, params))
                mesh = std::move(out);
        });
    }
    if (ImGui::Button("Run Catmull-Clark Subdivision"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::Halfedge::Mesh out;
            Geometry::CatmullClark::SubdivisionParams params;
            params.Iterations = ui.Iterations;
            if (Geometry::CatmullClark::Subdivide(mesh, out, params))
                mesh = std::move(out);
        }) || changed;
    }

    return changed;
}

bool DrawRepairWidget(Runtime::Engine& engine,
                     entt::entity entity)
{
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? kMeshTopologyDomains
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Repair requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    ImGui::TextDisabled("Input Domains: Mesh Vertices / Mesh Edges / Mesh Halfedges / Mesh Faces");
    if (!ImGui::Button("Run Mesh Repair"))
        return false;

    return ApplySurfaceMeshOperator(engine, entity, [](Geometry::Halfedge::Mesh& mesh)
    {
        static_cast<void>(Geometry::MeshRepair::Repair(mesh));
    });
}

// =========================================================================
// Normal Estimation (Point Cloud)
// =========================================================================

bool DrawNormalEstimationWidget(Runtime::Engine& engine,
                                entt::entity entity,
                                NormalEstimationWidgetState& state)
{
    auto& reg = engine.GetSceneManager().GetScene().GetRegistry();

    auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity);
    if (!pcd || !pcd->CloudRef || pcd->CloudRef->IsEmpty())
    {
        ImGui::TextDisabled("Normal Estimation requires a point cloud with positions.");
        return false;
    }

    ImGui::TextDisabled("Estimates surface normals via PCA local plane fitting\n"
                        "with MST-based consistent orientation (Hoppe et al. 1992).");

    ImGui::SliderInt("K Neighbors", &state.KNeighbors, 3, 50);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Number of nearest neighbors for local plane fitting.\n"
                          "Larger k = smoother normals, less sensitive to features.");
    ImGui::Checkbox("Orient Normals (MST)", &state.OrientNormals);

    const bool hasNormals = pcd->CloudRef->HasNormals();
    if (hasNormals)
        ImGui::TextColored({0.4f, 0.8f, 0.4f, 1.0f}, "Cloud already has normals (%zu points).",
                           pcd->CloudRef->Normals().size());

    const bool pressed = ImGui::Button(hasNormals ? "Re-estimate Normals" : "Estimate Normals");

    // Persistent result / failure display (survives across frames).
    if (state.LastRunFailed)
        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Last estimation failed (too few points?).");
    if (state.HasResults)
    {
        ImGui::SeparatorText("Last Result");
        ImGui::Text("Estimated: %zu normals", state.EstimatedCount);
        ImGui::Text("Degenerate neighborhoods: %zu", state.DegenerateCount);
        if (state.FlippedCount > 0)
            ImGui::Text("Flipped during orientation: %zu", state.FlippedCount);
    }

    if (!pressed)
        return false;

    Geometry::NormalEstimation::EstimationParams params;
    params.KNeighbors = static_cast<std::size_t>(std::max(3, state.KNeighbors));
    params.OrientNormals = state.OrientNormals;

    auto positions = pcd->CloudRef->Positions();
    auto result = Geometry::NormalEstimation::EstimateNormals(positions, params);
    if (!result)
    {
        state.LastRunFailed = true;
        state.HasResults = false;
        return false;
    }

    state.LastRunFailed = false;
    state.HasResults = true;
    state.EstimatedCount = result->Normals.size();
    state.DegenerateCount = result->DegenerateCount;
    state.FlippedCount = result->FlippedCount;

    auto& cloud = *pcd->CloudRef;
    if (!cloud.HasNormals())
        cloud.EnableNormals();

    auto normals = cloud.Normals();
    for (std::size_t i = 0; i < result->Normals.size() && i < normals.size(); ++i)
        normals[i] = result->Normals[i];

    pcd->GpuDirty = true;

    Core::Log::Info("NormalEstimation: estimated {} normals ({} degenerate, {} flipped)",
                    result->Normals.size(), result->DegenerateCount, result->FlippedCount);

    return true;
}

bool DrawShortestPathWidget(Runtime::Engine& engine,
                            entt::entity entity,
                            ShortestPathWidgetState& state)
{
    auto& scene = engine.GetSceneManager().GetScene();
    auto& reg = scene.GetRegistry();
    auto& selection = engine.GetSelection();
    const auto& sub = selection.GetSubElementSelection();
    const auto elementMode = selection.GetConfig().ElementMode;

    const bool hasMesh = reg.try_get<ECS::Mesh::Data>(entity) != nullptr;
    const bool hasGraph = reg.try_get<ECS::Graph::Data>(entity) != nullptr;
    DrawDomainBadges((hasMesh ? GeometryProcessingDomain::MeshVertices : GeometryProcessingDomain::None)
                    | (hasGraph ? GeometryProcessingDomain::GraphVertices : GeometryProcessingDomain::None));

    if (!hasMesh && !hasGraph)
    {
        ImGui::TextDisabled("Shortest Path requires a mesh or graph entity.");
        return false;
    }

    if (elementMode != Runtime::Selection::ElementMode::Vertex || sub.Entity != entity)
    {
        ImGui::TextDisabled("Switch to Vertex selection mode and select source/target vertices.");
        return false;
    }

    std::vector<Geometry::VertexHandle> selectedVertices;
    selectedVertices.reserve(sub.SelectedVertices.size());
    for (uint32_t index : sub.SelectedVertices)
        selectedVertices.emplace_back(static_cast<Geometry::PropertyIndex>(index));

    ImGui::Text("Selected vertices: %zu", selectedVertices.size());
    if (selectedVertices.empty())
    {
        ImGui::TextDisabled("Pick at least one source vertex.");
    }
    else
    {
        state.SourceCount = std::clamp(state.SourceCount, 1, static_cast<int>(selectedVertices.size()));
        ImGui::SliderInt("Source vertices (prefix count)",
                         &state.SourceCount,
                         1,
                         static_cast<int>(selectedVertices.size()));

        const std::size_t sourceCount = static_cast<std::size_t>(std::max(1, state.SourceCount));
        const std::size_t targetCount = selectedVertices.size() > sourceCount
            ? (selectedVertices.size() - sourceCount)
            : 0;
        ImGui::TextDisabled("Sources: first %zu selected vertices", sourceCount);
        ImGui::TextDisabled("Targets: %zu (%s)", targetCount, targetCount == 0 ? "optional full tree" : "explicit");
    }

    ImGui::Checkbox("Stop when all targets settled", &state.StopWhenAllTargetsSettled);
    ImGui::SliderInt("Max settled vertices (0 = auto)", &state.MaxSettledVertices, 0, 200000);

    if (state.LastRunFailed)
        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Last shortest-path run failed.");
    if (state.HasResults)
    {
        ImGui::SeparatorText("Last Result");
        ImGui::Text("Settled vertices: %zu", state.LastSettledVertexCount);
        ImGui::Text("Relaxed edges: %zu", state.LastRelaxedEdgeCount);
        ImGui::Text("Reached goals: %zu", state.LastReachedGoalCount);
        ImGui::Text("Converged: %s", state.LastConverged ? "Yes" : "No");
        ImGui::Text("Early terminated: %s", state.LastEarlyTerminated ? "Yes" : "No");
        if (state.LastPathLengthValid)
            ImGui::Text("Path length: %.6f", state.LastPathLength);
        else
            ImGui::TextDisabled("Path length: n/a (no reachable explicit target)");
    }

    bool changed = false;

    const bool canRun = !selectedVertices.empty();
    if (!canRun)
        ImGui::BeginDisabled();
    const bool runRequested = ImGui::Button("Compute Shortest Path");
    if (!canRun)
        ImGui::EndDisabled();

    if (runRequested && canRun)
    {
        const std::size_t sourceCount = static_cast<std::size_t>(std::max(1, std::min(state.SourceCount,
            static_cast<int>(selectedVertices.size()))));
        const std::span<const Geometry::VertexHandle> sources(selectedVertices.data(), sourceCount);
        const std::span<const Geometry::VertexHandle> targets(selectedVertices.data() + sourceCount,
                                                              selectedVertices.size() - sourceCount);

        Geometry::ShortestPath::DijkstraParams params{};
        params.StopWhenAllTargetsSettled = state.StopWhenAllTargetsSettled;
        params.MaxSettledVertices = state.MaxSettledVertices > 0
            ? static_cast<std::size_t>(state.MaxSettledVertices)
            : 0;

        std::optional<Geometry::ShortestPath::ShortestPathResult> result{};
        if (auto* meshData = reg.try_get<ECS::Mesh::Data>(entity); meshData && meshData->MeshRef)
            result = Geometry::ShortestPath::Dijkstra(*meshData->MeshRef, sources, targets, params);
        else if (auto* graphData = reg.try_get<ECS::Graph::Data>(entity); graphData && graphData->GraphRef)
            result = Geometry::ShortestPath::Dijkstra(*graphData->GraphRef, sources, targets, params);

        if (!result)
        {
            state.LastRunFailed = true;
            state.HasResults = false;
        }
        else
        {
            state.LastRunFailed = false;
            state.HasResults = true;
            state.LastSettledVertexCount = result->SettledVertexCount;
            state.LastRelaxedEdgeCount = result->RelaxedEdgeCount;
            state.LastReachedGoalCount = result->ReachedGoalCount;
            state.LastConverged = result->Converged;
            state.LastEarlyTerminated = result->EarlyTerminated;
            state.LastExtractFailed = false;
            state.LastPathLengthValid = false;
            state.LastPathLength = 0.0;

            if (!targets.empty())
            {
                double minDistance = std::numeric_limits<double>::infinity();
                for (const auto target : targets)
                {
                    const auto distance = result->Distances[target];
                    if (std::isfinite(distance) && distance < minDistance)
                        minDistance = distance;
                }

                if (std::isfinite(minDistance))
                {
                    state.LastPathLengthValid = true;
                    state.LastPathLength = minDistance;
                }
            }

            if (auto* meshData = reg.try_get<ECS::Mesh::Data>(entity); meshData && meshData->MeshRef)
            {
                meshData->AttributesDirty = true;
                meshData->Visualization.VertexColors.PropertyName = "v:shortest_path_distance";
                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);
            }
            if (auto* graphData = reg.try_get<ECS::Graph::Data>(entity); graphData && graphData->GraphRef)
            {
                graphData->GpuDirty = true;
                graphData->Visualization.VertexColors.PropertyName = "v:shortest_path_distance";
                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);
            }

            changed = true;
            Core::Log::Info("ShortestPath: settled={} relaxed={} reached={} pathLength={}",
                            result->SettledVertexCount,
                            result->RelaxedEdgeCount,
                            result->ReachedGoalCount,
                            state.LastPathLengthValid ? state.LastPathLength : -1.0);
        }
    }

    if (state.HasResults)
    {
        const bool canExtract = !selectedVertices.empty();
        if (!canExtract)
            ImGui::BeginDisabled();
        if (ImGui::Button("Extract Path Graph"))
        {
            const std::size_t sourceCount = static_cast<std::size_t>(std::max(1, std::min(state.SourceCount,
                static_cast<int>(selectedVertices.size()))));
            const std::span<const Geometry::VertexHandle> sources(selectedVertices.data(), sourceCount);
            const std::span<const Geometry::VertexHandle> targets(selectedVertices.data() + sourceCount,
                                                                  selectedVertices.size() - sourceCount);

            Geometry::ShortestPath::DijkstraParams params{};
            params.StopWhenAllTargetsSettled = state.StopWhenAllTargetsSettled;
            params.MaxSettledVertices = state.MaxSettledVertices > 0
                ? static_cast<std::size_t>(state.MaxSettledVertices)
                : 0;

            std::optional<Geometry::ShortestPath::ShortestPathResult> result{};
            std::optional<Geometry::Graph::Graph> extracted{};
            if (auto* meshData = reg.try_get<ECS::Mesh::Data>(entity); meshData && meshData->MeshRef)
            {
                result = Geometry::ShortestPath::Dijkstra(*meshData->MeshRef, sources, targets, params);
                if (result) extracted = Geometry::ShortestPath::ExtractPathGraph(*meshData->MeshRef, *result, sources, targets);
            }
            else if (auto* graphData = reg.try_get<ECS::Graph::Data>(entity); graphData && graphData->GraphRef)
            {
                result = Geometry::ShortestPath::Dijkstra(*graphData->GraphRef, sources, targets, params);
                if (result) extracted = Geometry::ShortestPath::ExtractPathGraph(*graphData->GraphRef, *result, sources, targets);
            }

            if (!extracted)
            {
                state.LastExtractFailed = true;
            }
            else
            {
                entt::entity pathEntity = scene.CreateEntity("Shortest Path Graph");
                reg.emplace<ECS::DataAuthority::GraphTag>(pathEntity);
                auto& graphData = reg.emplace<ECS::Graph::Data>(pathEntity);
                graphData.GraphRef = std::make_shared<Geometry::Graph::Graph>(std::move(*extracted));
                graphData.GpuDirty = true;
                reg.emplace<ECS::Components::Selection::SelectableTag>(pathEntity);
                state.LastExtractFailed = false;
                state.LastPathVertexCount = graphData.GraphRef->VertexCount();
                state.LastPathEdgeCount = graphData.GraphRef->EdgeCount();
                changed = true;
            }
        }
        if (!canExtract)
            ImGui::EndDisabled();

        if (state.LastExtractFailed)
            ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Path extraction failed.");
        if (state.LastPathVertexCount > 0 || state.LastPathEdgeCount > 0)
            ImGui::Text("Last path graph: %zu vertices, %zu edges",
                        state.LastPathVertexCount,
                        state.LastPathEdgeCount);
    }

    return changed;
}

bool DrawMeshQualityWidget(Runtime::Engine& engine,
                           entt::entity entity,
                           MeshQualityWidgetState& state)
{
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? kMeshTopologyDomains
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Mesh Quality requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
    const auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
    if (!meshData || !meshData->MeshRef)
    {
        ImGui::TextDisabled("No editable halfedge mesh available on the selected entity.");
        return false;
    }

    ImGui::TextDisabled("Aggregate diagnostics for mesh quality (angles, aspect ratios, lengths, valence, area).");
    ImGui::SliderInt("Histogram bins", &state.HistogramBinCount, 8, 128);

    if (ImGui::Button("Run Mesh Quality Analysis"))
    {
        Geometry::MeshQuality::QualityParams params{};
        auto quality = Geometry::MeshQuality::ComputeQuality(*meshData->MeshRef, params);
        auto distributions = Geometry::MeshQuality::ComputeDistributions(*meshData->MeshRef, params);
        if (!quality)
        {
            state.LastRunFailed = true;
            state.HasResults = false;
            return false;
        }
        if (!distributions)
        {
            state.LastRunFailed = true;
            state.HasResults = false;
            return false;
        }

        state.LastRunFailed = false;
        state.HasResults = true;
        state.LastResult = *quality;

        BuildHistogram(std::span<const double>(distributions->AnglesDeg), state.HistogramBinCount, state.AngleHistogram);
        BuildHistogram(std::span<const double>(distributions->AspectRatios), state.HistogramBinCount, state.AspectRatioHistogram);
        BuildHistogram(std::span<const double>(distributions->EdgeLengths), state.HistogramBinCount, state.EdgeLengthHistogram);
        BuildHistogram(std::span<const double>(distributions->Valences), state.HistogramBinCount, state.ValenceHistogram);
        BuildHistogram(std::span<const double>(distributions->FaceAreas), state.HistogramBinCount, state.FaceAreaHistogram);
    }

    if (state.LastRunFailed)
        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Mesh quality analysis failed.");
    if (!state.HasResults)
        return false;

    const auto& q = state.LastResult;
    ImGui::SeparatorText("Aggregate Statistics");
    ImGui::Text("Angles (deg): min %.3f / max %.3f / mean %.3f", q.MinAngle, q.MaxAngle, q.MeanAngle);
    ImGui::Text("Aspect ratio: min %.3f / max %.3f / mean %.3f",
                q.MinAspectRatio, q.MaxAspectRatio, q.MeanAspectRatio);
    ImGui::Text("Edge length: min %.5f / max %.5f / mean %.5f / std %.5f",
                q.MinEdgeLength, q.MaxEdgeLength, q.MeanEdgeLength, q.StdDevEdgeLength);
    ImGui::Text("Valence: min %zu / max %zu / mean %.3f", q.MinValence, q.MaxValence, q.MeanValence);
    ImGui::Text("Face area: min %.8f / max %.8f / mean %.8f", q.MinFaceArea, q.MaxFaceArea, q.MeanFaceArea);
    ImGui::Text("Total area: %.8f", q.TotalArea);
    ImGui::Text("Volume: %.8f (%s)", q.Volume, q.IsClosed ? "closed mesh" : "open mesh");
    ImGui::Text("Degenerate faces: %zu", q.DegenerateFaceCount);
    ImGui::Text("Small/Large angles: %zu / %zu", q.SmallAngleCount, q.LargeAngleCount);
    ImGui::Text("Topology: V=%zu E=%zu F=%zu, boundary loops=%zu, Euler=%d",
                q.VertexCount, q.EdgeCount, q.FaceCount, q.BoundaryLoopCount, q.EulerCharacteristic);

    const ImVec2 histogramSize{0.0f, 80.0f};
    if (!state.AngleHistogram.empty())
        ImGui::PlotHistogram("Angle distribution", state.AngleHistogram.data(),
                             static_cast<int>(state.AngleHistogram.size()), 0, nullptr, 0.0f, 0.2f, histogramSize);
    if (!state.AspectRatioHistogram.empty())
        ImGui::PlotHistogram("Aspect ratio distribution", state.AspectRatioHistogram.data(),
                             static_cast<int>(state.AspectRatioHistogram.size()), 0, nullptr, 0.0f, 0.2f, histogramSize);
    if (!state.EdgeLengthHistogram.empty())
        ImGui::PlotHistogram("Edge length distribution", state.EdgeLengthHistogram.data(),
                             static_cast<int>(state.EdgeLengthHistogram.size()), 0, nullptr, 0.0f, 0.2f, histogramSize);
    if (!state.ValenceHistogram.empty())
        ImGui::PlotHistogram("Valence distribution", state.ValenceHistogram.data(),
                             static_cast<int>(state.ValenceHistogram.size()), 0, nullptr, 0.0f, 0.4f, histogramSize);
    if (!state.FaceAreaHistogram.empty())
        ImGui::PlotHistogram("Face area distribution", state.FaceAreaHistogram.data(),
                             static_cast<int>(state.FaceAreaHistogram.size()), 0, nullptr, 0.0f, 0.2f, histogramSize);

    return false;
}

// =========================================================================
// Utility functions
// =========================================================================

bool MatricesNearlyEqual(const glm::mat4& a, const glm::mat4& b, float eps)
{
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            if (std::abs(a[c][r] - b[c][r]) > eps)
                return false;
        }
    }
    return true;
}

bool Vec3NearlyEqual(const glm::vec3& a, const glm::vec3& b, float eps)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(eps)));
}

bool OctreeSettingsEqual(const Graphics::OctreeDebugDrawSettings& a,
                         const Graphics::OctreeDebugDrawSettings& b)
{
    return a.Enabled == b.Enabled &&
        a.Overlay == b.Overlay &&
        a.ColorByDepth == b.ColorByDepth &&
        a.MaxDepth == b.MaxDepth &&
        a.LeafOnly == b.LeafOnly &&
        a.DrawInternal == b.DrawInternal &&
        a.OccupiedOnly == b.OccupiedOnly &&
        std::abs(a.Alpha - b.Alpha) <= 1e-4f &&
        Vec3NearlyEqual(a.BaseColor, b.BaseColor);
}

glm::vec3 DepthRamp(float t)
{
    return Graphics::GpuColor::DepthRamp(t);
}

uint32_t PackWithAlpha(const glm::vec3& rgb, float alpha)
{
    return Graphics::GpuColor::PackVec3WithAlpha(rgb, alpha);
}

void TransformAABB(const glm::vec3& lo, const glm::vec3& hi, const glm::mat4& m,
                   glm::vec3& outLo, glm::vec3& outHi)
{
    const Geometry::AABB src{lo, hi};
    const Geometry::AABB result = Geometry::TransformAABB(src, m);
    outLo = result.Min;
    outHi = result.Max;
}

// =========================================================================
// Helper: Create a standalone mesh entity from a Halfedge::Mesh
// =========================================================================

namespace
{
    [[nodiscard]] entt::entity SpawnMeshEntity(Runtime::Engine& engine,
                                                const char* name,
                                                Geometry::Halfedge::Mesh mesh)
    {
        auto& scene = engine.GetSceneManager().GetScene();
        auto& reg = scene.GetRegistry();

        std::vector<glm::vec3> positions;
        std::vector<uint32_t> indices;
        std::vector<glm::vec4> aux;
        Geometry::MeshUtils::ExtractIndexedTriangles(mesh, positions, indices, &aux);

        if (positions.empty() || indices.empty())
            return entt::null;

        std::vector<glm::vec3> normals(positions.size(), glm::vec3(0, 1, 0));
        Geometry::MeshUtils::CalculateNormals(positions, indices, normals);

        entt::entity entity = scene.CreateEntity(name);
        reg.emplace<ECS::DataAuthority::MeshTag>(entity);
        reg.emplace<ECS::Components::Selection::SelectableTag>(entity);
        static uint32_t s_SpawnedMeshPickId = 1000000u;
        reg.emplace<ECS::Components::Selection::PickID>(entity, s_SpawnedMeshPickId++);

        // Build collision data.
        auto collisionRef = std::make_shared<Graphics::GeometryCollisionData>();
        collisionRef->Positions = positions;
        collisionRef->Indices = indices;
        collisionRef->Aux = aux;
        collisionRef->SourceMesh = std::make_shared<Geometry::Halfedge::Mesh>(std::move(mesh));

        if (!positions.empty())
        {
            auto aabbs = Geometry::ToAABB(positions);
            collisionRef->LocalAABB = Geometry::Union(aabbs);
        }

        std::vector<Geometry::AABB> primitiveBounds;
        primitiveBounds.reserve(indices.size() / 3);
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t i0 = indices[i];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                continue;
            auto aabb = Geometry::AABB{positions[i0], positions[i0]};
            aabb = Geometry::Union(aabb, positions[i1]);
            aabb = Geometry::Union(aabb, positions[i2]);
            primitiveBounds.push_back(aabb);
        }
        if (!primitiveBounds.empty())
        {
            static_cast<void>(collisionRef->LocalOctree.Build(
                primitiveBounds, Geometry::Octree::SplitPolicy{}, 16, 8));
        }
        RebuildCollisionVertexLookup(*collisionRef);

        auto& col = reg.emplace<ECS::MeshCollider::Component>(entity);
        col.CollisionRef = collisionRef;
        col.WorldOBB.Center = collisionRef->LocalAABB.GetCenter();

        // Upload geometry to GPU.
        Graphics::GeometryUploadRequest uploadReq;
        uploadReq.Positions = collisionRef->Positions;
        uploadReq.Indices = collisionRef->Indices;
        uploadReq.Normals = normals;
        uploadReq.Aux = collisionRef->Aux;
        uploadReq.Topology = Graphics::PrimitiveTopology::Triangles;
        uploadReq.UploadMode = Graphics::GeometryUploadMode::Staged;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            engine.GetGraphicsBackend().GetDeviceShared(),
            engine.GetGraphicsBackend().GetTransferManager(),
            uploadReq,
            &engine.GetRenderOrchestrator().GetGeometryStorage());

        auto& surface = reg.emplace<ECS::Surface::Component>(entity);
        surface.Geometry = engine.GetRenderOrchestrator().GetGeometryStorage().Add(std::move(gpuData));

        auto& meshData = reg.emplace<ECS::Mesh::Data>(entity);
        meshData.MeshRef = collisionRef->SourceMesh;
        meshData.AttributesDirty = true;

        auto& bvh = reg.emplace<ECS::PrimitiveBVH::Data>(entity);
        bvh.Source = ECS::PrimitiveBVH::SourceKind::MeshTriangles;
        bvh.Dirty = true;

        scene.GetDispatcher().enqueue<ECS::Events::EntitySpawned>({entity});
        return entity;
    }
} // anonymous namespace

// =========================================================================
// Convex Hull Widget
// =========================================================================

bool DrawConvexHullWidget(Runtime::Engine& engine,
                          entt::entity entity,
                          ConvexHullWidgetState& state)
{
    auto& reg = engine.GetSceneManager().GetScene().GetRegistry();

    const bool hasMesh = [&]() {
        if (auto* md = reg.try_get<ECS::Mesh::Data>(entity); md && md->MeshRef)
            return true;
        if (auto* col = reg.try_get<ECS::MeshCollider::Component>(entity);
            col && col->CollisionRef && !col->CollisionRef->Positions.empty())
            return true;
        return false;
    }();

    const bool hasPointCloud = [&]() {
        auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity);
        return pcd && pcd->CloudRef && !pcd->CloudRef->IsEmpty();
    }();

    DrawDomainBadges(
        (hasMesh ? GeometryProcessingDomain::MeshVertices : GeometryProcessingDomain::None)
        | (hasPointCloud ? GeometryProcessingDomain::PointCloudPoints : GeometryProcessingDomain::None));

    if (!hasMesh && !hasPointCloud)
    {
        ImGui::TextDisabled("Convex Hull requires a mesh or point cloud entity.");
        return false;
    }

    if (state.HasResults)
    {
        ImGui::SeparatorText("Last Result");
        ImGui::Text("Input points: %zu", state.InputPointCount);
        ImGui::Text("Hull vertices: %zu", state.HullVertexCount);
        ImGui::Text("Hull faces: %zu", state.HullFaceCount);
        ImGui::Text("Hull edges: %zu", state.HullEdgeCount);
        ImGui::Text("Interior points: %zu", state.InteriorPointCount);
    }

    if (state.LastRunFailed)
        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Last convex hull computation failed (degenerate input).");

    if (ImGui::Button("Compute Convex Hull"))
    {
        Geometry::ConvexHullBuilder::ConvexHullParams params{};
        params.BuildMesh = true;
        params.ComputePlanes = true;

        std::optional<Geometry::ConvexHullBuilder::ConvexHullResult> result{};

        if (auto* md = reg.try_get<ECS::Mesh::Data>(entity); md && md->MeshRef)
        {
            result = Geometry::ConvexHullBuilder::BuildFromMesh(*md->MeshRef, params);
        }
        else if (auto* col = reg.try_get<ECS::MeshCollider::Component>(entity);
                 col && col->CollisionRef && !col->CollisionRef->Positions.empty())
        {
            result = Geometry::ConvexHullBuilder::Build(col->CollisionRef->Positions, params);
        }
        else if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity); pcd && pcd->CloudRef)
        {
            result = Geometry::ConvexHullBuilder::Build(pcd->CloudRef->Positions(), params);
        }

        if (!result)
        {
            state.LastRunFailed = true;
            state.HasResults = false;
        }
        else
        {
            state.LastRunFailed = false;
            state.HasResults = true;
            state.InputPointCount = result->InputPointCount;
            state.HullVertexCount = result->HullVertexCount;
            state.HullFaceCount = result->HullFaceCount;
            state.HullEdgeCount = result->HullEdgeCount;
            state.InteriorPointCount = result->InteriorPointCount;

            entt::entity hullEntity = SpawnMeshEntity(
                engine, "Convex Hull", std::move(result->Mesh));

            if (hullEntity != entt::null)
            {
                Core::Log::Info("ConvexHull: spawned hull entity with {} vertices, {} faces",
                                state.HullVertexCount, state.HullFaceCount);
            }
            else
            {
                state.LastRunFailed = true;
                Core::Log::Warn("ConvexHull: failed to create hull mesh entity (empty geometry).");
            }
        }
    }

    return false;
}

// =========================================================================
// Surface Reconstruction Widget
// =========================================================================

bool DrawSurfaceReconstructionWidget(Runtime::Engine& engine,
                                      entt::entity entity,
                                      SurfaceReconstructionWidgetState& state)
{
    auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
    auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity);

    DrawDomainBadges(
        (pcd && pcd->CloudRef && !pcd->CloudRef->IsEmpty())
            ? GeometryProcessingDomain::PointCloudPoints
            : GeometryProcessingDomain::None);

    if (!pcd || !pcd->CloudRef || pcd->CloudRef->IsEmpty())
    {
        ImGui::TextDisabled("Surface Reconstruction requires a point cloud entity.");
        return false;
    }

    ImGui::SeparatorText("Parameters");
    ImGui::SliderInt("Grid Resolution", &state.Resolution, 16, 256);
    ImGui::SetItemTooltip("Number of grid cells along longest axis. Higher = finer detail, more memory.");
    ImGui::SliderInt("K Neighbors (SDF)", &state.KNeighbors, 1, 32);
    ImGui::SetItemTooltip("Neighbors for signed distance. k=1 is fast; k>1 smooths noisy data.");
    ImGui::SliderFloat("Bounding Box Padding", &state.BoundingBoxPadding, 0.0f, 0.5f, "%.2f");
    ImGui::Checkbox("Estimate Normals", &state.EstimateNormals);
    if (state.EstimateNormals)
    {
        ImGui::SliderInt("Normal K Neighbors", &state.NormalKNeighbors, 5, 50);
    }
    if (state.KNeighbors > 1)
    {
        ImGui::SliderFloat("Normal Agreement Power", &state.NormalAgreementPower, 0.5f, 8.0f, "%.1f");
        ImGui::SliderFloat("Kernel Sigma Scale", &state.KernelSigmaScale, 0.5f, 8.0f, "%.1f");
    }

    if (state.HasResults)
    {
        ImGui::SeparatorText("Last Result");
        ImGui::Text("Output vertices: %zu", state.OutputVertexCount);
        ImGui::Text("Output faces: %zu", state.OutputFaceCount);
        ImGui::Text("Grid: %zu x %zu x %zu", state.GridNX, state.GridNY, state.GridNZ);
    }

    if (state.LastRunFailed)
        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Last reconstruction failed.");

    if (ImGui::Button("Reconstruct Surface"))
    {
        auto positions = pcd->CloudRef->Positions();
        auto normals = pcd->CloudRef->Normals();

        Geometry::SurfaceReconstruction::ReconstructionParams params{};
        params.Resolution = static_cast<std::size_t>(std::max(8, state.Resolution));
        params.KNeighbors = static_cast<std::size_t>(std::max(1, state.KNeighbors));
        params.BoundingBoxPadding = state.BoundingBoxPadding;
        params.EstimateNormals = state.EstimateNormals;
        params.NormalKNeighbors = static_cast<std::size_t>(std::max(3, state.NormalKNeighbors));
        params.NormalAgreementPower = state.NormalAgreementPower;
        params.KernelSigmaScale = state.KernelSigmaScale;

        std::vector<glm::vec3> posVec(positions.begin(), positions.end());
        std::vector<glm::vec3> normVec(normals.begin(), normals.end());

        auto result = Geometry::SurfaceReconstruction::Reconstruct(
            posVec,
            state.EstimateNormals ? std::span<const glm::vec3>{} : std::span<const glm::vec3>(normVec),
            params);

        if (!result)
        {
            state.LastRunFailed = true;
            state.HasResults = false;
        }
        else
        {
            state.LastRunFailed = false;
            state.HasResults = true;
            state.OutputVertexCount = result->OutputVertexCount;
            state.OutputFaceCount = result->OutputFaceCount;
            state.GridNX = result->GridNX;
            state.GridNY = result->GridNY;
            state.GridNZ = result->GridNZ;

            entt::entity meshEntity = SpawnMeshEntity(
                engine, "Reconstructed Surface", std::move(result->OutputMesh));

            if (meshEntity != entt::null)
            {
                Core::Log::Info("SurfaceReconstruction: {} vertices, {} faces (grid {}x{}x{})",
                                state.OutputVertexCount, state.OutputFaceCount,
                                state.GridNX, state.GridNY, state.GridNZ);
            }
            else
            {
                state.LastRunFailed = true;
                Core::Log::Warn("SurfaceReconstruction: failed to create mesh entity (empty geometry).");
            }
        }
    }

    return false;
}

bool DrawVectorHeatWidget(Runtime::Engine& engine,
                          entt::entity entity,
                          VectorHeatWidgetState& state)
{
    auto& scene = engine.GetSceneManager().GetScene();
    auto& reg = scene.GetRegistry();
    auto& selection = engine.GetSelection();
    const auto& sub = selection.GetSubElementSelection();
    const auto elementMode = selection.GetConfig().ElementMode;

    auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
    DrawDomainBadges(meshData ? GeometryProcessingDomain::MeshVertices
                              : GeometryProcessingDomain::None);

    if (!meshData || !meshData->MeshRef)
    {
        ImGui::TextDisabled("Vector Heat Method requires a mesh entity with halfedge data.");
        return false;
    }

    if (elementMode != Runtime::Selection::ElementMode::Vertex || sub.Entity != entity)
    {
        ImGui::TextDisabled("Switch to Vertex selection mode and select source vertices.");
        return false;
    }

    std::vector<std::size_t> selectedVertices;
    selectedVertices.reserve(sub.SelectedVertices.size());
    for (uint32_t index : sub.SelectedVertices)
        selectedVertices.push_back(static_cast<std::size_t>(index));

    ImGui::Text("Selected vertices: %zu", selectedVertices.size());

    // --- Parameters ---
    ImGui::SeparatorText("Parameters");

    double timeStep = state.TimeStep;
    if (ImGui::InputDouble("Time step (0 = auto h\xc2\xb2)", &timeStep, 0.0, 0.0, "%.6g"))
        state.TimeStep = std::max(timeStep, 0.0);
    ImGui::SetItemTooltip("Heat diffusion time. 0 uses mean-edge-length squared (recommended).");

    float tol = state.SolverTolerance;
    if (ImGui::InputFloat("Solver tolerance", &tol, 0.0f, 0.0f, "%.2e"))
        state.SolverTolerance = std::clamp(tol, 1e-15f, 1e-2f);

    ImGui::SliderInt("Max solver iterations", &state.MaxSolverIterations, 100, 10000);

    // --- Results section ---
    if (state.LastRunFailed)
        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Last Vector Heat run failed.");

    if (state.HasTransportResults)
    {
        ImGui::SeparatorText("Last Result (Transport)");
        ImGui::Text("Solve iterations: %zu", state.LastTransportIterations);
        ImGui::Text("Converged: %s", state.LastConverged ? "Yes" : "No");
    }
    else if (state.HasLogMapResults)
    {
        ImGui::SeparatorText("Last Result (Log Map)");
        ImGui::Text("Vector solve iterations: %zu", state.LastVectorSolveIterations);
        ImGui::Text("Scalar solve iterations: %zu", state.LastScalarSolveIterations);
        ImGui::Text("Poisson solve iterations: %zu", state.LastPoissonSolveIterations);
        ImGui::Text("Converged: %s", state.LastConverged ? "Yes" : "No");
    }

    bool changed = false;

    // --- Transport Vectors button ---
    {
        const bool canRun = !selectedVertices.empty();
        if (!canRun) ImGui::BeginDisabled();

        if (ImGui::Button("Transport Vectors"))
        {
            auto& mesh = *meshData->MeshRef;
            // Use first outgoing halfedge direction as default source tangent
            // vector to transport from each source vertex.
            std::vector<glm::vec3> sourceVectors;
            sourceVectors.reserve(selectedVertices.size());
            for (std::size_t vi : selectedVertices)
            {
                auto vh = Geometry::VertexHandle(static_cast<Geometry::PropertyIndex>(vi));
                auto he = mesh.Halfedge(vh);
                if (he.IsValid())
                {
                    auto target = mesh.ToVertex(he);
                    glm::vec3 edge = mesh.Position(target) - mesh.Position(vh);
                    float len = glm::length(edge);
                    if (len > 1e-8f)
                        sourceVectors.push_back(edge / len);
                    else
                        sourceVectors.push_back(glm::vec3(1.0f, 0.0f, 0.0f));
                }
                else
                {
                    sourceVectors.push_back(glm::vec3(1.0f, 0.0f, 0.0f));
                }
            }

            Geometry::VectorHeatMethod::VectorHeatParams params{};
            params.TimeStep = state.TimeStep;
            params.SolverTolerance = static_cast<double>(state.SolverTolerance);
            params.MaxSolverIterations = static_cast<std::size_t>(std::max(1, state.MaxSolverIterations));

            auto result = Geometry::VectorHeatMethod::TransportVectors(
                mesh,
                std::span<const std::size_t>(selectedVertices),
                std::span<const glm::vec3>(sourceVectors),
                params);

            state.HasLogMapResults = false;
            if (!result)
            {
                state.LastRunFailed = true;
                state.HasTransportResults = false;
            }
            else
            {
                state.LastRunFailed = false;
                state.HasTransportResults = true;
                state.LastTransportIterations = result->SolveIterations;
                state.LastConverged = result->Converged;

                meshData->AttributesDirty = true;
                meshData->Visualization.VertexColors.PropertyName = "v:transported_angle";
                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);
                changed = true;

                Core::Log::Info("VectorHeat: TransportVectors iterations={} converged={}",
                                result->SolveIterations, result->Converged);
            }
        }

        if (!canRun) ImGui::EndDisabled();
    }

    ImGui::SameLine();

    // --- Compute Log Map button ---
    {
        const bool canRun = (selectedVertices.size() == 1);
        if (!canRun) ImGui::BeginDisabled();

        if (ImGui::Button("Compute Log Map"))
        {
            auto& mesh = *meshData->MeshRef;

            Geometry::VectorHeatMethod::VectorHeatParams params{};
            params.TimeStep = state.TimeStep;
            params.SolverTolerance = static_cast<double>(state.SolverTolerance);
            params.MaxSolverIterations = static_cast<std::size_t>(std::max(1, state.MaxSolverIterations));

            auto result = Geometry::VectorHeatMethod::ComputeLogMap(
                mesh, selectedVertices[0], params);

            state.HasTransportResults = false;
            if (!result)
            {
                state.LastRunFailed = true;
                state.HasLogMapResults = false;
            }
            else
            {
                state.LastRunFailed = false;
                state.HasLogMapResults = true;
                state.LastVectorSolveIterations = result->VectorSolveIterations;
                state.LastScalarSolveIterations = result->ScalarSolveIterations;
                state.LastPoissonSolveIterations = result->PoissonSolveIterations;
                state.LastConverged = result->Converged;

                meshData->AttributesDirty = true;
                meshData->Visualization.VertexColors.PropertyName = "v:geodesic_distance";
                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(entity);
                changed = true;

                Core::Log::Info("VectorHeat: ComputeLogMap vecIter={} scalarIter={} poissonIter={} converged={}",
                                result->VectorSolveIterations, result->ScalarSolveIterations,
                                result->PoissonSolveIterations, result->Converged);
            }
        }

        if (!canRun) ImGui::EndDisabled();
        if (selectedVertices.size() != 1)
            ImGui::SetItemTooltip("Log Map requires exactly 1 selected source vertex.");
    }

    return changed;
}

} // namespace Runtime::EditorUI
