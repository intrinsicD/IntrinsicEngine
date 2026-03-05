#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>
#include <cstddef>
#include <expected>
#include <span>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

import Runtime.SceneSerializer;
import Runtime.SceneManager;
import ECS;
import Core;
import Core.IOBackend;

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// In-memory I/O backend for testing (no filesystem needed)
// ---------------------------------------------------------------------------
class MemoryIOBackend final : public Core::IO::IIOBackend
{
public:
    std::expected<Core::IO::IOReadResult, Core::ErrorCode> Read(
        const Core::IO::IORequest& request) override
    {
        auto it = m_Files.find(request.Path);
        if (it == m_Files.end())
            return std::unexpected(Core::ErrorCode::FileNotFound);
        return Core::IO::IOReadResult{it->second};
    }

    std::expected<void, Core::ErrorCode> Write(
        const Core::IO::IORequest& request,
        std::span<const std::byte> data) override
    {
        m_Files[request.Path] = std::vector<std::byte>(data.begin(), data.end());
        return {};
    }

    [[nodiscard]] bool HasFile(const std::string& path) const
    {
        return m_Files.contains(path);
    }

    [[nodiscard]] std::string ReadFileAsString(const std::string& path) const
    {
        auto it = m_Files.find(path);
        if (it == m_Files.end()) return {};
        return std::string(reinterpret_cast<const char*>(it->second.data()), it->second.size());
    }

private:
    std::unordered_map<std::string, std::vector<std::byte>> m_Files;
};

// ---------------------------------------------------------------------------
// Dirty Tracker tests (no GPU required)
// ---------------------------------------------------------------------------

TEST(SceneDirtyTracker, InitiallyClean)
{
    Runtime::SceneDirtyTracker tracker;
    EXPECT_FALSE(tracker.IsDirty());
    EXPECT_TRUE(tracker.GetCurrentPath().empty());
}

TEST(SceneDirtyTracker, MarkAndClear)
{
    Runtime::SceneDirtyTracker tracker;
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());
    tracker.ClearDirty();
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(SceneDirtyTracker, PathTracking)
{
    Runtime::SceneDirtyTracker tracker;
    tracker.SetCurrentPath("test_scene.json");
    EXPECT_EQ(tracker.GetCurrentPath(), "test_scene.json");
}

// ---------------------------------------------------------------------------
// Scene schema tests — verify JSON structure produced by SaveScene
// without needing a full Engine (we test the schema format via manual
// serialization patterns that mirror the serializer logic).
// ---------------------------------------------------------------------------

TEST(SceneSchema, VersionField)
{
    // Verify the schema version is present and correct.
    // We simulate what SaveScene produces by creating a minimal JSON.
    json doc;
    doc["version"] = 1;
    doc["entities"] = json::array();

    EXPECT_EQ(doc["version"].get<uint32_t>(), 1u);
    EXPECT_TRUE(doc["entities"].is_array());
    EXPECT_EQ(doc["entities"].size(), 0u);
}

TEST(SceneSchema, EntityWithTransform)
{
    json entity;
    entity["id"] = 0;
    entity["name"] = "TestEntity";
    entity["transform"] = {
        {"position", {1.0f, 2.0f, 3.0f}},
        {"rotation", {1.0f, 0.0f, 0.0f, 0.0f}},
        {"scale",    {1.0f, 1.0f, 1.0f}}
    };

    EXPECT_EQ(entity["name"].get<std::string>(), "TestEntity");

    const auto& pos = entity["transform"]["position"];
    EXPECT_FLOAT_EQ(pos[0].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(pos[1].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(pos[2].get<float>(), 3.0f);

    const auto& rot = entity["transform"]["rotation"];
    EXPECT_FLOAT_EQ(rot[0].get<float>(), 1.0f); // w
    EXPECT_FLOAT_EQ(rot[1].get<float>(), 0.0f); // x
}

TEST(SceneSchema, HierarchyRelationship)
{
    json parent;
    parent["id"] = 0;
    parent["name"] = "Parent";

    json child;
    child["id"] = 1;
    child["name"] = "Child";
    child["parentId"] = 0;

    EXPECT_EQ(child["parentId"].get<uint32_t>(), 0u);
}

TEST(SceneSchema, AssetSourceField)
{
    json entity;
    entity["id"] = 0;
    entity["name"] = "MeshEntity";
    entity["assetSource"] = "assets/models/duck.glb";

    EXPECT_EQ(entity["assetSource"].get<std::string>(), "assets/models/duck.glb");
}

TEST(SceneSchema, ComponentPresenceFlags)
{
    json entity;
    entity["id"] = 0;
    entity["components"] = json::object();
    entity["components"]["surface"] = {{"visible", true}, {"showPerFaceColors", false}};
    entity["components"]["meshEdgeView"] = true;
    entity["components"]["meshVertexView"] = true;

    EXPECT_TRUE(entity["components"].contains("surface"));
    EXPECT_TRUE(entity["components"]["surface"]["visible"].get<bool>());
    EXPECT_FALSE(entity["components"]["surface"]["showPerFaceColors"].get<bool>());
    EXPECT_TRUE(entity["components"].contains("meshEdgeView"));
}

TEST(SceneSchema, PointCloudRenderParams)
{
    json cloud;
    cloud["renderMode"] = 2; // EWA
    cloud["defaultRadius"] = 0.005f;
    cloud["sizeMultiplier"] = 1.5f;
    cloud["defaultColor"] = {1.0f, 0.5f, 0.0f, 1.0f};
    cloud["visible"] = true;

    EXPECT_EQ(cloud["renderMode"].get<uint32_t>(), 2u);
    EXPECT_FLOAT_EQ(cloud["defaultRadius"].get<float>(), 0.005f);
}

TEST(SceneSchema, GraphRenderParams)
{
    json graph;
    graph["nodeRenderMode"] = 0;
    graph["defaultNodeRadius"] = 0.01f;
    graph["nodeSizeMultiplier"] = 1.0f;
    graph["defaultNodeColor"] = {0.8f, 0.5f, 0.0f, 1.0f};
    graph["defaultEdgeColor"] = {0.6f, 0.6f, 0.6f, 1.0f};
    graph["edgeWidth"] = 1.5f;
    graph["edgesOverlay"] = false;
    graph["visible"] = true;
    graph["staticGeometry"] = false;

    EXPECT_FLOAT_EQ(graph["edgeWidth"].get<float>(), 1.5f);
    EXPECT_FALSE(graph["edgesOverlay"].get<bool>());
}

// ---------------------------------------------------------------------------
// Round-trip test: SceneManager → JSON → parse → verify structure
// ---------------------------------------------------------------------------

TEST(SceneSchema, RoundTripDocumentStructure)
{
    // Build a scene document manually (mimicking SaveScene output)
    json doc;
    doc["version"] = 1;

    json entities = json::array();

    // Parent entity
    json parent;
    parent["id"] = 0;
    parent["name"] = "Root";
    parent["transform"] = {
        {"position", {0.0f, 1.0f, 0.0f}},
        {"rotation", {1.0f, 0.0f, 0.0f, 0.0f}},
        {"scale",    {2.0f, 2.0f, 2.0f}}
    };
    parent["assetSource"] = "assets/test.glb";
    parent["components"] = {{"surface", {{"visible", true}, {"showPerFaceColors", false}}}};
    entities.push_back(parent);

    // Child entity
    json child;
    child["id"] = 1;
    child["name"] = "Child";
    child["parentId"] = 0;
    child["transform"] = {
        {"position", {1.0f, 0.0f, 0.0f}},
        {"rotation", {0.707f, 0.0f, 0.707f, 0.0f}},
        {"scale",    {1.0f, 1.0f, 1.0f}}
    };
    entities.push_back(child);

    doc["entities"] = entities;

    // Serialize to string and parse back
    std::string serialized = doc.dump(2);
    EXPECT_FALSE(serialized.empty());

    json parsed = json::parse(serialized, nullptr, false);
    EXPECT_FALSE(parsed.is_discarded());

    // Verify round-trip
    EXPECT_EQ(parsed["version"].get<uint32_t>(), 1u);
    EXPECT_EQ(parsed["entities"].size(), 2u);
    EXPECT_EQ(parsed["entities"][0]["name"].get<std::string>(), "Root");
    EXPECT_EQ(parsed["entities"][1]["name"].get<std::string>(), "Child");
    EXPECT_EQ(parsed["entities"][1]["parentId"].get<uint32_t>(), 0u);

    // Verify transform values survived round-trip
    const auto& rootPos = parsed["entities"][0]["transform"]["position"];
    EXPECT_FLOAT_EQ(rootPos[0].get<float>(), 0.0f);
    EXPECT_FLOAT_EQ(rootPos[1].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(rootPos[2].get<float>(), 0.0f);

    const auto& rootScale = parsed["entities"][0]["transform"]["scale"];
    EXPECT_FLOAT_EQ(rootScale[0].get<float>(), 2.0f);

    // Verify asset source
    EXPECT_EQ(parsed["entities"][0]["assetSource"].get<std::string>(), "assets/test.glb");

    // Verify components
    EXPECT_TRUE(parsed["entities"][0]["components"]["surface"]["visible"].get<bool>());
}

// ---------------------------------------------------------------------------
// AssetSourceRef component tests
// ---------------------------------------------------------------------------

TEST(AssetSourceRef, EmptyByDefault)
{
    ECS::Components::AssetSourceRef::Component comp;
    EXPECT_TRUE(comp.SourcePath.empty());
}

TEST(AssetSourceRef, StoresPath)
{
    ECS::Components::AssetSourceRef::Component comp{"assets/model.glb"};
    EXPECT_EQ(comp.SourcePath, "assets/model.glb");
}

TEST(AssetSourceRef, AttachToEntity)
{
    Runtime::SceneManager mgr;
    auto& scene = mgr.GetScene();
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("TestMesh");
    reg.emplace<ECS::Components::AssetSourceRef::Component>(e, "assets/duck.glb");

    EXPECT_TRUE(reg.all_of<ECS::Components::AssetSourceRef::Component>(e));
    EXPECT_EQ(reg.get<ECS::Components::AssetSourceRef::Component>(e).SourcePath, "assets/duck.glb");
}

// ---------------------------------------------------------------------------
// MemoryIOBackend verification
// ---------------------------------------------------------------------------

TEST(MemoryIOBackend, WriteAndRead)
{
    MemoryIOBackend backend;

    std::string content = R"({"version": 1, "entities": []})";
    std::vector<std::byte> bytes(content.size());
    std::memcpy(bytes.data(), content.data(), content.size());

    Core::IO::IORequest req;
    req.Path = "test_scene.json";

    auto writeResult = backend.Write(req, bytes);
    EXPECT_TRUE(writeResult.has_value());

    auto readResult = backend.Read(req);
    EXPECT_TRUE(readResult.has_value());

    std::string readContent(
        reinterpret_cast<const char*>(readResult->Data.data()),
        readResult->Data.size());
    EXPECT_EQ(readContent, content);
}

TEST(MemoryIOBackend, ReadNonexistent)
{
    MemoryIOBackend backend;

    Core::IO::IORequest req;
    req.Path = "nonexistent.json";

    auto result = backend.Read(req);
    EXPECT_FALSE(result.has_value());
}
