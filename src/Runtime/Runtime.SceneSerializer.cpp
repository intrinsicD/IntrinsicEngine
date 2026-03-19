module;
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <span>
#include <algorithm>
#include <cctype>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

module Runtime.SceneSerializer;

import Core.Logging;
import Core.Error;
import Core.IOBackend;
import Core.Assets;
import Core.Tasks;
import Graphics;
import ECS;
import Runtime.Engine;
import Runtime.SceneManager;
import Runtime.AssetPipeline;
import Runtime.RenderOrchestrator;
import Runtime.GraphicsBackend;
import Geometry;

using json = nlohmann::json;

namespace
{
    constexpr uint32_t kSchemaVersion = 1;

    // -------------------------------------------------------------------------
    // JSON helpers for glm types
    // -------------------------------------------------------------------------

    json Vec3ToJson(const glm::vec3& v)
    {
        return json::array({v.x, v.y, v.z});
    }

    json Vec4ToJson(const glm::vec4& v)
    {
        return json::array({v.x, v.y, v.z, v.w});
    }

    json QuatToJson(const glm::quat& q)
    {
        return json::array({q.w, q.x, q.y, q.z});
    }

    glm::vec3 JsonToVec3(const json& j)
    {
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    }

    glm::vec4 JsonToVec4(const json& j)
    {
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    }

    glm::quat JsonToQuat(const json& j)
    {
        // Stored as [w, x, y, z]
        return glm::quat(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
    }

    // -------------------------------------------------------------------------
    // Entity serialization
    // -------------------------------------------------------------------------

    json SerializeEntity(const entt::registry& reg, entt::entity entity, uint32_t entityIndex,
                         const std::unordered_map<entt::entity, uint32_t>& entityToIndex)
    {
        json j;
        j["id"] = entityIndex;

        // Name
        if (const auto* name = reg.try_get<ECS::Components::NameTag::Component>(entity))
            j["name"] = name->Name;

        // Transform
        if (const auto* transform = reg.try_get<ECS::Components::Transform::Component>(entity))
        {
            j["transform"] = json{
                {"position", Vec3ToJson(transform->Position)},
                {"rotation", QuatToJson(transform->Rotation)},
                {"scale",    Vec3ToJson(transform->Scale)}
            };
        }

        // Hierarchy (parent index, -1 for root)
        if (const auto* hier = reg.try_get<ECS::Components::Hierarchy::Component>(entity))
        {
            if (hier->Parent != entt::null)
            {
                auto it = entityToIndex.find(hier->Parent);
                if (it != entityToIndex.end())
                    j["parentId"] = it->second;
            }
        }

        // Asset source path
        if (const auto* src = reg.try_get<ECS::Components::AssetSourceRef::Component>(entity))
        {
            if (!src->SourcePath.empty())
                j["assetSource"] = src->SourcePath;
        }

        // Components section — only present data
        json components = json::object();

        // Surface (mesh) component
        if (const auto* sc = reg.try_get<ECS::Surface::Component>(entity))
        {
            json surf;
            surf["visible"] = sc->Visible;
            surf["showPerFaceColors"] = sc->ShowPerFaceColors;
            components["surface"] = surf;
        }

        // Line component (wireframe display)
        if (const auto* lc = reg.try_get<ECS::Line::Component>(entity))
        {
            json line;
            line["color"] = Vec4ToJson(lc->Color);
            line["width"] = lc->Width;
            line["overlay"] = lc->Overlay;
            line["showPerEdgeColors"] = lc->ShowPerEdgeColors;
            components["line"] = line;
        }

        // Point component (vertex display)
        if (const auto* pc = reg.try_get<ECS::Point::Component>(entity))
        {
            json point;
            point["color"] = Vec4ToJson(pc->Color);
            point["size"] = pc->Size;
            point["sizeMultiplier"] = pc->SizeMultiplier;
            point["mode"] = static_cast<uint32_t>(pc->Mode);
            components["point"] = point;
        }

        // PointCloud data
        if (const auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity))
        {
            json cloud;
            cloud["renderMode"] = static_cast<uint32_t>(pcd->RenderMode);
            cloud["defaultRadius"] = pcd->DefaultRadius;
            cloud["sizeMultiplier"] = pcd->SizeMultiplier;
            cloud["defaultColor"] = Vec4ToJson(pcd->DefaultColor);
            cloud["visible"] = pcd->Visible;
            components["pointCloud"] = cloud;
        }

        // Graph data
        if (const auto* gd = reg.try_get<ECS::Graph::Data>(entity))
        {
            json graph;
            graph["nodeRenderMode"] = static_cast<uint32_t>(gd->NodeRenderMode);
            graph["defaultNodeRadius"] = gd->DefaultNodeRadius;
            graph["nodeSizeMultiplier"] = gd->NodeSizeMultiplier;
            graph["defaultNodeColor"] = Vec4ToJson(gd->DefaultNodeColor);
            graph["defaultEdgeColor"] = Vec4ToJson(gd->DefaultEdgeColor);
            graph["edgeWidth"] = gd->EdgeWidth;
            graph["edgesOverlay"] = gd->EdgesOverlay;
            graph["visible"] = gd->Visible;
            graph["staticGeometry"] = gd->StaticGeometry;
            components["graph"] = graph;
        }

        // MeshEdgeView presence (wireframe enabled)
        if (reg.all_of<ECS::MeshEdgeView::Component>(entity))
            components["meshEdgeView"] = true;

        // MeshVertexView presence (vertex points enabled)
        if (reg.all_of<ECS::MeshVertexView::Component>(entity))
            components["meshVertexView"] = true;

        if (!components.empty())
            j["components"] = components;

        return j;
    }

    // -------------------------------------------------------------------------
    // Re-import an asset file into the engine (synchronous for scene load).
    // -------------------------------------------------------------------------

    struct ReimportedAsset
    {
        Core::Assets::AssetHandle ModelHandle{};
        Core::Assets::AssetHandle MaterialHandle{};
        bool Success = false;
    };

    ReimportedAsset ReimportAsset(Runtime::Engine& engine, const std::string& sourcePath)
    {
        ReimportedAsset result;

        auto imported = engine.GetAssetIngestService().ImportModelSync(sourcePath, "scene");
        if (!imported)
        {
            Core::Log::Error("Scene load: failed to import asset: {}", sourcePath);
            return result;
        }

        result.ModelHandle = imported->ModelHandle;
        result.MaterialHandle = imported->MaterialHandle;
        result.Success = true;
        return result;
    }

    // -------------------------------------------------------------------------
    // Apply component settings from JSON to a loaded entity
    // -------------------------------------------------------------------------

    void ApplyComponentSettings(entt::registry& reg, entt::entity entity, const json& components)
    {
        if (components.contains("surface"))
        {
            if (auto* sc = reg.try_get<ECS::Surface::Component>(entity))
            {
                const auto& surf = components["surface"];
                if (surf.contains("visible"))
                    sc->Visible = surf["visible"].get<bool>();
                if (surf.contains("showPerFaceColors"))
                    sc->ShowPerFaceColors = surf["showPerFaceColors"].get<bool>();
            }
        }

        if (components.contains("pointCloud"))
        {
            if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity))
            {
                const auto& cloud = components["pointCloud"];
                if (cloud.contains("renderMode"))
                    pcd->RenderMode = static_cast<Geometry::PointCloud::RenderMode>(cloud["renderMode"].get<uint32_t>());
                if (cloud.contains("defaultRadius"))
                    pcd->DefaultRadius = cloud["defaultRadius"].get<float>();
                if (cloud.contains("sizeMultiplier"))
                    pcd->SizeMultiplier = cloud["sizeMultiplier"].get<float>();
                if (cloud.contains("defaultColor"))
                    pcd->DefaultColor = JsonToVec4(cloud["defaultColor"]);
                if (cloud.contains("visible"))
                    pcd->Visible = cloud["visible"].get<bool>();
            }
        }

        if (components.contains("graph"))
        {
            if (auto* gd = reg.try_get<ECS::Graph::Data>(entity))
            {
                const auto& graph = components["graph"];
                if (graph.contains("nodeRenderMode"))
                    gd->NodeRenderMode = static_cast<Geometry::PointCloud::RenderMode>(graph["nodeRenderMode"].get<uint32_t>());
                if (graph.contains("defaultNodeRadius"))
                    gd->DefaultNodeRadius = graph["defaultNodeRadius"].get<float>();
                if (graph.contains("nodeSizeMultiplier"))
                    gd->NodeSizeMultiplier = graph["nodeSizeMultiplier"].get<float>();
                if (graph.contains("defaultNodeColor"))
                    gd->DefaultNodeColor = JsonToVec4(graph["defaultNodeColor"]);
                if (graph.contains("defaultEdgeColor"))
                    gd->DefaultEdgeColor = JsonToVec4(graph["defaultEdgeColor"]);
                if (graph.contains("edgeWidth"))
                    gd->EdgeWidth = graph["edgeWidth"].get<float>();
                if (graph.contains("edgesOverlay"))
                    gd->EdgesOverlay = graph["edgesOverlay"].get<bool>();
                if (graph.contains("visible"))
                    gd->Visible = graph["visible"].get<bool>();
                if (graph.contains("staticGeometry"))
                    gd->StaticGeometry = graph["staticGeometry"].get<bool>();
            }
        }

        // Wireframe/vertex view display toggles.
        // These are presence-based components managed by lifecycle systems.
        // We record if they were enabled at save time; the lifecycle systems
        // will create the actual GPU geometry.
        if (components.contains("meshEdgeView"))
        {
            if (reg.all_of<ECS::Surface::Component>(entity) && !reg.all_of<ECS::Line::Component>(entity))
            {
                reg.emplace<ECS::Line::Component>(entity);
            }
        }

        if (components.contains("meshVertexView"))
        {
            if (reg.all_of<ECS::Surface::Component>(entity) && !reg.all_of<ECS::Point::Component>(entity))
            {
                reg.emplace<ECS::Point::Component>(entity);
            }
        }

        // Line component appearance settings
        if (components.contains("line"))
        {
            if (auto* lc = reg.try_get<ECS::Line::Component>(entity))
            {
                const auto& line = components["line"];
                if (line.contains("color"))
                    lc->Color = JsonToVec4(line["color"]);
                if (line.contains("width"))
                    lc->Width = line["width"].get<float>();
                if (line.contains("overlay"))
                    lc->Overlay = line["overlay"].get<bool>();
                if (line.contains("showPerEdgeColors"))
                    lc->ShowPerEdgeColors = line["showPerEdgeColors"].get<bool>();
            }
        }

        // Point component appearance settings
        if (components.contains("point"))
        {
            if (auto* pc = reg.try_get<ECS::Point::Component>(entity))
            {
                const auto& point = components["point"];
                if (point.contains("color"))
                    pc->Color = JsonToVec4(point["color"]);
                if (point.contains("size"))
                    pc->Size = point["size"].get<float>();
                if (point.contains("sizeMultiplier"))
                    pc->SizeMultiplier = point["sizeMultiplier"].get<float>();
                if (point.contains("mode"))
                    pc->Mode = static_cast<Geometry::PointCloud::RenderMode>(point["mode"].get<uint32_t>());
            }
        }
    }
}

namespace Runtime
{
    std::expected<void, SceneError> SaveScene(
        const Engine& engine,
        const std::string& path,
        Core::IO::IIOBackend& backend)
    {
        const auto& reg = engine.GetScene().GetRegistry();

        // Build entity → index mapping for hierarchy references
        std::unordered_map<entt::entity, uint32_t> entityToIndex;
        std::vector<entt::entity> entities;

        // Collect all live entities (skip camera/internal entities by checking for NameTag)
        const auto view = reg.view<ECS::Components::NameTag::Component>();
        for (auto entity : view)
        {
            entityToIndex[entity] = static_cast<uint32_t>(entities.size());
            entities.push_back(entity);
        }

        // Build JSON document
        json doc;
        doc["version"] = kSchemaVersion;

        json entityArray = json::array();
        for (uint32_t i = 0; i < entities.size(); ++i)
        {
            entityArray.push_back(SerializeEntity(reg, entities[i], i, entityToIndex));
        }
        doc["entities"] = entityArray;

        // Serialize to string
        std::string content = doc.dump(2);

        // Write via IO backend
        Core::IO::IORequest request;
        request.Path = path;
        auto bytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(content.data()),
            content.size());

        auto writeResult = backend.Write(request, bytes);
        if (!writeResult)
        {
            Core::Log::Error("Scene save failed: could not write to {}", path);
            return std::unexpected(SceneError::WriteError);
        }

        Core::Log::Info("Scene saved: {} ({} entities)", path, entities.size());
        return {};
    }

    std::expected<LoadDiagnostics, SceneError> LoadScene(
        Engine& engine,
        const std::string& path,
        Core::IO::IIOBackend& backend)
    {
        LoadDiagnostics diag;

        // 1. Read the file
        Core::IO::IORequest request;
        request.Path = path;

        auto readResult = backend.Read(request);
        if (!readResult)
        {
            Core::Log::Error("Scene load failed: could not read {}", path);
            return std::unexpected(SceneError::ReadError);
        }

        // 2. Parse JSON
        json doc;
        {
            std::string content(
                reinterpret_cast<const char*>(readResult->Data.data()),
                readResult->Data.size());

            auto parseResult = json::parse(content, nullptr, false);
            if (parseResult.is_discarded())
            {
                Core::Log::Error("Scene load failed: invalid JSON in {}", path);
                return std::unexpected(SceneError::ParseError);
            }
            doc = std::move(parseResult);
        }

        // 3. Validate schema
        if (!doc.contains("version") || !doc.contains("entities"))
        {
            Core::Log::Error("Scene load failed: missing version or entities in {}", path);
            return std::unexpected(SceneError::InvalidSchema);
        }

        uint32_t version = doc["version"].get<uint32_t>();
        if (version > kSchemaVersion)
        {
            Core::Log::Error("Scene load failed: unsupported schema version {} (max {})", version, kSchemaVersion);
            return std::unexpected(SceneError::InvalidSchema);
        }

        // 4. Clear the current scene
        engine.GetSelection().ClearSelection(engine.GetScene());
        engine.GetSceneManager().Clear();

        // 5. Import unique assets first (group entities by source path)
        const auto& entityArray = doc["entities"];

        // Collect unique asset sources and pre-import them
        std::unordered_map<std::string, ReimportedAsset> assetCache;
        for (const auto& ej : entityArray)
        {
            if (ej.contains("assetSource"))
            {
                std::string src = ej["assetSource"].get<std::string>();
                if (!src.empty() && !assetCache.contains(src))
                {
                    auto imported = ReimportAsset(engine, src);
                    if (imported.Success)
                        ++diag.AssetsLoaded;
                    else
                        ++diag.AssetsFailed;
                    assetCache[src] = imported;
                }
            }
        }

        // 6. Recreate entities
        // Map from saved index to new entity
        std::unordered_map<uint32_t, entt::entity> indexToEntity;

        // First pass: create entities with names and transforms
        for (const auto& ej : entityArray)
        {
            uint32_t savedId = ej["id"].get<uint32_t>();
            std::string name = ej.value("name", "Unnamed Entity");

            entt::entity entity = engine.GetScene().CreateEntity(name);
            indexToEntity[savedId] = entity;

            // Apply transform
            if (ej.contains("transform"))
            {
                auto& t = engine.GetScene().GetRegistry().get<ECS::Components::Transform::Component>(entity);
                const auto& tj = ej["transform"];
                if (tj.contains("position"))
                    t.Position = JsonToVec3(tj["position"]);
                if (tj.contains("rotation"))
                    t.Rotation = JsonToQuat(tj["rotation"]);
                if (tj.contains("scale"))
                    t.Scale = JsonToVec3(tj["scale"]);

                engine.GetScene().GetRegistry().emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(entity);
            }

            // Store asset source ref
            if (ej.contains("assetSource"))
            {
                std::string src = ej["assetSource"].get<std::string>();
                if (!src.empty())
                {
                    engine.GetScene().GetRegistry().emplace<ECS::Components::AssetSourceRef::Component>(entity, src);
                }
            }

            // Make selectable
            engine.GetScene().GetRegistry().emplace<ECS::Components::Selection::SelectableTag>(entity);
            static uint32_t s_PickId = 1000000u; // Start high to avoid conflicts
            engine.GetScene().GetRegistry().emplace<ECS::Components::Selection::PickID>(entity, s_PickId++);

            // If this entity has an asset source, spawn the model's geometry onto it
            if (ej.contains("assetSource"))
            {
                std::string src = ej["assetSource"].get<std::string>();
                auto it = assetCache.find(src);
                if (it != assetCache.end() && it->second.Success)
                {
                    // Resolve the model to get mesh segment handles
                    auto* model = engine.GetAssetManager().TryGet<Graphics::Model>(it->second.ModelHandle);
                    if (model && !model->Meshes.empty())
                    {
                        // For simplicity, attach the first mesh segment's geometry.
                        // Multi-mesh models are handled by SpawnModel which creates children,
                        // but for scene reload we attach directly.
                        const auto& seg = model->Meshes[0];
                        const auto handle = seg->Handle;
                        const auto* geo = engine.GetGeometryStorage().GetIfValid(handle);

                        if (geo && geo->GetTopology() == Graphics::PrimitiveTopology::Points)
                        {
                            auto& pcd = engine.GetScene().GetRegistry().emplace_or_replace<ECS::PointCloud::Data>(entity);
                            pcd.GpuGeometry = handle;
                            pcd.GpuDirty = false;
                            pcd.CloudRef.reset();
                            pcd.HasGpuNormals = (geo->GetLayout().NormalsSize > 0);
                            pcd.GpuPointCount = static_cast<uint32_t>(
                                geo->GetLayout().PositionsSize / sizeof(glm::vec3));
                        }
                        else
                        {
                            auto& sc = engine.GetScene().GetRegistry().emplace_or_replace<ECS::Surface::Component>(entity);
                            sc.Geometry = handle;
                            sc.Material = it->second.MaterialHandle;

                            if (seg->CollisionGeometry)
                            {
                                auto& col = engine.GetScene().GetRegistry().emplace_or_replace<ECS::MeshCollider::Component>(entity);
                                col.CollisionRef = seg->CollisionGeometry;
                                col.WorldOBB.Center = col.CollisionRef->LocalAABB.GetCenter();

                                auto& primitiveBvh = engine.GetScene().GetRegistry().emplace_or_replace<ECS::PrimitiveBVH::Data>(entity);
                                primitiveBvh.Source = ECS::PrimitiveBVH::SourceKind::MeshTriangles;
                                primitiveBvh.Dirty = true;
                            }
                        }
                    }
                }
            }

            ++diag.EntitiesLoaded;
        }

        // Second pass: restore hierarchy
        for (const auto& ej : entityArray)
        {
            if (ej.contains("parentId"))
            {
                uint32_t savedId = ej["id"].get<uint32_t>();
                uint32_t parentSavedId = ej["parentId"].get<uint32_t>();

                auto childIt = indexToEntity.find(savedId);
                auto parentIt = indexToEntity.find(parentSavedId);
                if (childIt != indexToEntity.end() && parentIt != indexToEntity.end())
                {
                    ECS::Components::Hierarchy::Attach(
                        engine.GetScene().GetRegistry(),
                        childIt->second,
                        parentIt->second);
                }
            }
        }

        // Third pass: apply component display settings
        for (const auto& ej : entityArray)
        {
            if (ej.contains("components"))
            {
                uint32_t savedId = ej["id"].get<uint32_t>();
                auto it = indexToEntity.find(savedId);
                if (it != indexToEntity.end())
                {
                    ApplyComponentSettings(engine.GetScene().GetRegistry(), it->second, ej["components"]);
                }
            }
        }

        Core::Log::Info("Scene loaded: {} ({} entities, {} assets imported, {} assets failed)",
                        path, diag.EntitiesLoaded, diag.AssetsLoaded, diag.AssetsFailed);

        return diag;
    }
}
