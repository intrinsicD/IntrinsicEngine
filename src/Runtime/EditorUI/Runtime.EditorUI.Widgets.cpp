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

module Runtime.EditorUI;

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Runtime.PointCloudKMeans;
import ECS;
import Graphics;
import Geometry;

namespace Runtime::EditorUI
{

namespace
{
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
        return domain == Runtime::PointCloudKMeans::Domain::PointCloudPoints
            ? "p:kmeans_color"
            : "v:kmeans_color";
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
        auto& reg = engine.GetScene().GetRegistry();
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

        auto aabbs = Geometry::Convert(collider->CollisionRef->Positions);
        collider->CollisionRef->LocalAABB = Geometry::Union(aabbs);

        std::vector<Geometry::AABB> primitiveBounds;
        primitiveBounds.reserve(collider->CollisionRef->Indices.size() / 3);
        for (size_t i = 0; i + 2 < collider->CollisionRef->Indices.size(); i += 3)
        {
            const uint32_t i0 = collider->CollisionRef->Indices[i];
            const uint32_t i1 = collider->CollisionRef->Indices[i + 1];
            const uint32_t i2 = collider->CollisionRef->Indices[i + 2];
            auto aabb = Geometry::AABB{
                collider->CollisionRef->Positions[i0], collider->CollisionRef->Positions[i0]
            };
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i1]);
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i2]);
            primitiveBounds.push_back(aabb);
        }
        static_cast<void>(collider->CollisionRef->LocalOctree.Build(
            primitiveBounds, Geometry::Octree::SplitPolicy{}, 16, 8));
        RebuildCollisionVertexLookup(*collider->CollisionRef);

        Graphics::GeometryUploadRequest uploadReq;
        uploadReq.Positions = collider->CollisionRef->Positions;
        uploadReq.Indices = collider->CollisionRef->Indices;
        uploadReq.Normals = newNormals;
        uploadReq.Aux = collider->CollisionRef->Aux;
        uploadReq.Topology = Graphics::PrimitiveTopology::Triangles;
        uploadReq.UploadMode = Graphics::GeometryUploadMode::Staged;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            engine.GetDeviceShared(), engine.GetGraphicsBackend().GetTransferManager(), uploadReq,
            &engine.GetGeometryStorage());

        auto oldHandle = sc->Geometry;
        sc->Geometry = engine.GetGeometryStorage().Add(std::move(gpuData));

        if (oldHandle.IsValid())
            engine.GetGeometryStorage().Remove(oldHandle, engine.GetDevice().GetGlobalFrameNumber());

        sc->GpuSlot = ECS::kInvalidGpuSlot;
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

        engine.GetScene().GetDispatcher().enqueue<ECS::Events::GeometryModified>({entity});
        return true;
    }

    [[nodiscard]] bool HasSurfaceInput(Runtime::Engine& engine, entt::entity entity) noexcept
    {
        const auto& reg = engine.GetScene().GetRegistry();
        return HasAnyDomain(GetGeometryProcessingCapabilities(reg, entity).Domains, GeometryProcessingDomain::SurfaceMesh);
    }
}

// =========================================================================
// ColorSourceWidget
// =========================================================================
bool ColorSourceWidget(const char* label, Graphics::ColorSource& src,
                       const Geometry::PropertySet* ps, const char* suffix)
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
                       const Geometry::PropertySet* ps, const char* suffix)
{
    bool changed = false;
    char idBuf[128];

    ImGui::SeparatorText("Vector Fields");

    // Available vec3 properties.
    std::vector<Graphics::PropertyInfo> vecProps;
    if (ps)
        vecProps = Graphics::EnumerateVectorProperties(*ps);

    // Add new vector field.
    snprintf(idBuf, sizeof(idBuf), "Add Vector Field##%s", suffix);
    if (!vecProps.empty() && ImGui::BeginCombo(idBuf, "Add..."))
    {
        for (const auto& p : vecProps)
        {
            if (ImGui::Selectable(p.Name.c_str()))
            {
                Graphics::VectorFieldEntry entry;
                entry.PropertyName = p.Name;
                config.VectorFields.push_back(std::move(entry));
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    // List existing vector fields.
    for (size_t i = 0; i < config.VectorFields.size();)
    {
        auto& vf = config.VectorFields[i];
        ImGui::PushID(static_cast<int>(i));

        ImGui::Text("%s", vf.PropertyName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
        {
            config.VectorFields.erase(config.VectorFields.begin() + static_cast<ptrdiff_t>(i));
            changed = true;
            ImGui::PopID();
            continue;
        }

        ImGui::DragFloat("Scale", &vf.Scale, 0.01f, 0.001f, 100.0f);
        ImGui::SliderFloat("Width", &vf.EdgeWidth, 0.5f, 5.0f);
        ColorEdit4("Color", vf.Color);
        ImGui::Checkbox("Overlay", &vf.Overlay);

        // Per-vector color property selector.
        if (ps)
        {
            auto colorableProps = Graphics::EnumerateColorableProperties(*ps);
            const char* colorPreview = vf.ColorPropertyName.empty()
                                           ? "(Uniform)"
                                           : vf.ColorPropertyName.c_str();
            if (ImGui::BeginCombo("Arrow Color", colorPreview))
            {
                if (ImGui::Selectable("(Uniform)", vf.ColorPropertyName.empty()))
                {
                    vf.ColorPropertyName.clear();
                    changed = true;
                }
                for (const auto& cp : colorableProps)
                {
                    if (ImGui::Selectable(cp.Name.c_str(), vf.ColorPropertyName == cp.Name))
                    {
                        vf.ColorPropertyName = cp.Name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }

            // Per-vector length property selector.
            auto scalarProps = Graphics::EnumerateScalarProperties(*ps);
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

    draw(GeometryProcessingDomain::SurfaceMesh);
    draw(GeometryProcessingDomain::MeshVertices);
    draw(GeometryProcessingDomain::GraphVertices);
    draw(GeometryProcessingDomain::PointCloudPoints);
}

bool DrawKMeansWidget(Runtime::Engine& engine,
                     entt::entity entity,
                     KMeansWidgetState& state)
{
    auto& reg = engine.GetScene().GetRegistry();
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
        ImGui::TextDisabled("Backend: CPU (CUDA path currently uses authoritative point-cloud buffers only)");
    }

    bool dispatched = false;
    const bool canRun = targetInfo.IsValid() && !targetInfo.JobPending;
    if (!canRun)
        ImGui::BeginDisabled();

    if (ImGui::Button("Run K-Means##GeometryProcessing"))
    {
        Geometry::KMeans::Params params{};
        params.ClusterCount = static_cast<uint32_t>(std::max(state.ClusterCount, 1));
        params.MaxIterations = static_cast<uint32_t>(std::max(state.MaxIterations, 1));
        params.Seed = static_cast<uint32_t>(std::max(state.Seed, 0));
        params.Init = static_cast<Geometry::KMeans::Initialization>(state.Initialization);
        params.Compute = static_cast<Geometry::KMeans::Backend>(state.Backend);
        dispatched = Runtime::PointCloudKMeans::Schedule(engine, entity, params, selectedDomain);
    }

    if (!canRun)
        ImGui::EndDisabled();

    if (targetInfo.SupportsCuda)
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

    return dispatched;
}

bool DrawRemeshingWidget(Runtime::Engine& engine,
                        entt::entity entity,
                        RemeshingWidgetState& state)
{
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? GeometryProcessingDomain::SurfaceMesh
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Remeshing requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    ImGui::TextDisabled("Input Domain: %s", GeometryDomainLabel(GeometryProcessingDomain::SurfaceMesh));
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
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? GeometryProcessingDomain::SurfaceMesh
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

    ImGui::TextDisabled("Input Domain: %s", GeometryDomainLabel(GeometryProcessingDomain::SurfaceMesh));
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
        Geometry::Simplification::SimplificationParams params;
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
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? GeometryProcessingDomain::SurfaceMesh
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Smoothing requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    ImGui::TextDisabled("Input Domain: %s", GeometryDomainLabel(GeometryProcessingDomain::SurfaceMesh));
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
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? GeometryProcessingDomain::SurfaceMesh
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Subdivision requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    ImGui::TextDisabled("Input Domain: %s", GeometryDomainLabel(GeometryProcessingDomain::SurfaceMesh));
    ImGui::DragInt("Iterations", &state.Iterations, 1.0f, 1, 5);

    bool changed = false;
    if (ImGui::Button("Run Loop Subdivision"))
    {
        const auto ui = state;
        changed = ApplySurfaceMeshOperator(engine, entity, [ui](Geometry::Halfedge::Mesh& mesh)
        {
            Geometry::Halfedge::Mesh out;
            Geometry::Subdivision::SubdivisionParams params;
            params.Iterations = ui.Iterations;
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
    DrawDomainBadges(HasSurfaceInput(engine, entity) ? GeometryProcessingDomain::SurfaceMesh
                                                     : GeometryProcessingDomain::None);
    if (!HasSurfaceInput(engine, entity))
    {
        ImGui::TextDisabled("Repair requires a selected surface mesh with collider-backed authority.");
        return false;
    }

    ImGui::TextDisabled("Input Domain: %s", GeometryDomainLabel(GeometryProcessingDomain::SurfaceMesh));
    if (!ImGui::Button("Run Mesh Repair"))
        return false;

    return ApplySurfaceMeshOperator(engine, entity, [](Geometry::Halfedge::Mesh& mesh)
    {
        static_cast<void>(Geometry::MeshRepair::Repair(mesh));
    });
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

} // namespace Runtime::EditorUI
