#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::filesystem::path RepoRoot()
    {
        return std::filesystem::path(__FILE__)
            .parent_path()
            .parent_path()
            .parent_path()
            .parent_path();
    }

    std::vector<std::filesystem::path> CollectEcsSourceFiles()
    {
        std::vector<std::filesystem::path> files;
        const auto root = RepoRoot() / "src/ecs";
        if (!std::filesystem::exists(root))
            return files;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;
            const auto& path = entry.path();
            const auto ext = path.extension().string();
            if (ext == ".cpp" || ext == ".cppm" || ext == ".h" || ext == ".hpp" || ext == ".inl")
                files.push_back(path);
        }
        return files;
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        if (!in.good())
            return {};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

}

// ECS source modules and their implementations must not import higher layers.
// Higher layers in the engine contract (/AGENTS.md §2) include graphics,
// graphics/rhi, runtime, platform, app, and live AssetService modules. The
// ECS layer contract is `ecs -> {core, geometry}`; this test catches accidental
// upward imports before they ever reach `tools/repo/check_layering.py`.
TEST(EcsLayeringBoundaries, EcsSourcesDoNotImportHigherLayerModules)
{
    const auto files = CollectEcsSourceFiles();
    ASSERT_FALSE(files.empty()) << "No ECS source files found under src/ecs";

    static const std::vector<std::string> kForbiddenImports = {
        // Graphics / RHI layers — ECS must communicate via component data, not
        // direct graphics imports.
        "import Extrinsic.Graphics",
        "import Extrinsic.RHI",
        // Runtime / platform / app — ECS is below these in the layering map.
        "import Extrinsic.Runtime",
        "import Extrinsic.Platform",
        "import Extrinsic.App",
        // Live AssetService traffic must not enter ECS. Asset.Registry types
        // (typed handles only) are ALSO disallowed under the current ecs ->
        // {core, geometry} contract; if that contract is widened in the future
        // the entry below is the single place to update.
        "import Extrinsic.Asset.Service",
        "import Extrinsic.Asset.LoadPipeline",
        "import Extrinsic.Asset.PayloadStore",
        "import Extrinsic.Asset.EventBus",
        "import Extrinsic.Asset.PathIndex",
        "import Extrinsic.Asset.TypePool",
        "import Extrinsic.Asset.Registry",
    };

    for (const auto& path : files)
    {
        const auto content = ReadFile(path);
        SCOPED_TRACE(path.string());
        for (const auto& needle : kForbiddenImports)
        {
            EXPECT_EQ(content.find(needle), std::string::npos)
                << path.string() << " contains forbidden import '" << needle << "'";
        }
    }
}

// Canonical ECS components store CPU-only descriptors and stable IDs. They
// must not embed graphics/RHI handles, runtime sidecars, or solver-owned
// physics state. HARDEN-064 governs the future collider/rigid-body authoring
// contract; until then ECS components are forbidden from carrying runtime or
// solver state.
TEST(EcsLayeringBoundaries, EcsComponentsRejectProhibitedRuntimeAndSolverState)
{
    const auto files = CollectEcsSourceFiles();
    ASSERT_FALSE(files.empty()) << "No ECS source files found under src/ecs";

    static const std::vector<std::string> kForbiddenSymbols = {
        // Physics-world handles and broadphase / solver caches.
        "PhysicsBodyHandle",
        "PhysicsWorldHandle",
        "BroadphaseProxyHandle",
        "BroadphaseProxyId",
        "RigidBodyHandle",
        "ContactCache",
        "ContactManifoldCache",
        "IslandId",
        "SolverIndex",
        "SolverHandle",
        // GPU / RHI handles and live AssetService surface.
        "RhiTextureHandle",
        "RhiBufferHandle",
        "RHITextureHandle",
        "RHIBufferHandle",
        "BindlessHandle",
        "BindlessIndex",
        "AssetService",
    };

    for (const auto& path : files)
    {
        const auto content = ReadFile(path);
        SCOPED_TRACE(path.string());
        for (const auto& needle : kForbiddenSymbols)
        {
            EXPECT_EQ(content.find(needle), std::string::npos)
                << path.string() << " mentions forbidden symbol '" << needle << "'";
        }
    }
}

// AssetInstance::Source is the engine-wide stable asset reference on entities.
// It must remain a CPU-only stable ID — never a GPU/bindless handle and never
// a live `AssetService` pointer. The structural intent is enforced here so a
// future edit cannot silently widen the surface.
TEST(EcsLayeringBoundaries, AssetInstanceComponentRemainsCpuStableIdOnly)
{
    const auto path = RepoRoot() / "src/ecs/Components/ECS.Component.AssetInstance.cppm";
    const auto content = ReadFile(path);
    ASSERT_FALSE(content.empty()) << "Cannot read " << path.string();

    EXPECT_NE(content.find("export module Extrinsic.ECS.Components.AssetInstance"), std::string::npos);
    EXPECT_NE(content.find("AssetId"), std::string::npos);

    EXPECT_EQ(content.find("AssetService"), std::string::npos);
    EXPECT_EQ(content.find("BufferView"), std::string::npos);
    EXPECT_EQ(content.find("Bindless"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.RHI"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.Graphics"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.Runtime"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.Asset"), std::string::npos);
}

// Collider components describe CPU geometric primitives only. They must not
// embed solver-owned state (rigid-body handles, contact caches, broadphase
// proxies, island IDs, runtime sync sidecars). HARDEN-064 will define the
// authoring contract once the physics layer ownership decision (ARCH-001) lands.
TEST(EcsLayeringBoundaries, ColliderComponentRejectsRigidBodyAndSolverState)
{
    const auto path = RepoRoot() / "src/ecs/Components/ECS.Component.Collider.cppm";
    const auto content = ReadFile(path);
    ASSERT_FALSE(content.empty()) << "Cannot read " << path.string();

    // CPU geometry primitives are explicitly allowed; the existing collider
    // representation uses Geometry.Sphere as a CPU descriptor.
    EXPECT_NE(content.find("export module Extrinsic.ECS.Component.Collider"), std::string::npos);
    EXPECT_NE(content.find("import Geometry."), std::string::npos);

    EXPECT_EQ(content.find("RigidBody"), std::string::npos);
    EXPECT_EQ(content.find("PhysicsBody"), std::string::npos);
    EXPECT_EQ(content.find("Broadphase"), std::string::npos);
    EXPECT_EQ(content.find("ContactCache"), std::string::npos);
    EXPECT_EQ(content.find("IslandId"), std::string::npos);
    EXPECT_EQ(content.find("SolverIndex"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.Runtime"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.Graphics"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.RHI"), std::string::npos);
}

// Hierarchy components describe the scene-graph parent/child relationship
// only. The promoted ECS layer must keep scene topology decoupled from any
// future compound-collider topology that physics may introduce: parents do
// not implicitly become compound colliders. The test guards the promoted
// hierarchy component module against accidental physics coupling.
TEST(EcsLayeringBoundaries, SceneHierarchyDoesNotEncodeColliderTopology)
{
    const auto path = RepoRoot() / "src/ecs/Components/ECS.Component.Hierarchy.cppm";
    const auto content = ReadFile(path);
    ASSERT_FALSE(content.empty()) << "Cannot read " << path.string();

    EXPECT_NE(content.find("export module Extrinsic.ECS.Component.Hierarchy"), std::string::npos);

    EXPECT_EQ(content.find("Collider"), std::string::npos);
    EXPECT_EQ(content.find("Compound"), std::string::npos);
    EXPECT_EQ(content.find("RigidBody"), std::string::npos);
    EXPECT_EQ(content.find("PhysicsBody"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.RHI"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.Graphics"), std::string::npos);
    EXPECT_EQ(content.find("import Extrinsic.Runtime"), std::string::npos);
}
