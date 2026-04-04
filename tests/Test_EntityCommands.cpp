#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

import Core;
import ECS;

namespace
{
    // gtest's EXPECT_EQ/NE macros trigger entt_traits<long long> instantiation
    // when comparing entt::entity with entt::null.  This helper avoids that.
    [[nodiscard]] bool IsNull(entt::entity e) { return e == entt::null; }
}

// ===========================================================================
// Entity Create/Delete command pattern tests
//
// Validates the create/delete undo/redo pattern used by
// Runtime::EditorUI::MakeCreateEntityCommand and MakeDeleteEntityCommand.
// Tested at the ECS level without a full Runtime::Engine.
// ===========================================================================

TEST(EntityCommands, CreateUndoDestroysEntity)
{
    ECS::Scene scene;
    Core::CommandHistory history;
    auto handle = std::make_shared<entt::entity>(entt::null);

    auto createCmd = Core::EditorCommand{
        .name = "Create Entity",
        .redo = [&scene, handle]()
        {
            *handle = scene.CreateEntity("TestEntity");
        },
        .undo = [&scene, handle]()
        {
            auto& reg = scene.GetRegistry();
            if (*handle != entt::null && reg.valid(*handle))
            {
                if (reg.all_of<ECS::Components::Hierarchy::Component>(*handle))
                    ECS::Components::Hierarchy::Detach(reg, *handle);
                reg.destroy(*handle);
            }
            *handle = entt::null;
        },
    };

    const std::size_t initialSize = scene.Size();
    EXPECT_TRUE(history.Execute(std::move(createCmd)));

    // Entity was created
    EXPECT_FALSE(IsNull(*handle));
    EXPECT_TRUE(scene.GetRegistry().valid(*handle));
    EXPECT_GT(scene.Size(), initialSize);

    auto* nameTag = scene.GetRegistry().try_get<ECS::Components::NameTag::Component>(*handle);
    ASSERT_NE(nameTag, nullptr);
    EXPECT_EQ(nameTag->Name, "TestEntity");

    // Undo: entity destroyed
    EXPECT_TRUE(history.Undo());
    EXPECT_TRUE(IsNull(*handle));

    // Redo: entity recreated
    EXPECT_TRUE(history.Redo());
    EXPECT_FALSE(IsNull(*handle));
    EXPECT_TRUE(scene.GetRegistry().valid(*handle));

    nameTag = scene.GetRegistry().try_get<ECS::Components::NameTag::Component>(*handle);
    ASSERT_NE(nameTag, nullptr);
    EXPECT_EQ(nameTag->Name, "TestEntity");
}

TEST(EntityCommands, DeleteUndoRestoresNameAndTransform)
{
    ECS::Scene scene;
    Core::CommandHistory history;

    entt::entity target = scene.CreateEntity("Deletable");
    auto& reg = scene.GetRegistry();
    auto& xf = reg.get<ECS::Components::Transform::Component>(target);
    xf.Position = glm::vec3(1.0f, 2.0f, 3.0f);

    const std::string expectedName = "Deletable";
    const glm::vec3 expectedPos(1.0f, 2.0f, 3.0f);

    auto handle = std::make_shared<entt::entity>(target);
    auto savedName = std::make_shared<std::string>(expectedName);
    auto savedPos = std::make_shared<glm::vec3>(expectedPos);

    auto deleteCmd = Core::EditorCommand{
        .name = "Delete Entity",
        .redo = [&scene, handle, savedName, savedPos]()
        {
            auto& r = scene.GetRegistry();
            if (*handle != entt::null && r.valid(*handle))
            {
                if (auto* nt = r.try_get<ECS::Components::NameTag::Component>(*handle))
                    *savedName = nt->Name;
                if (auto* xfr = r.try_get<ECS::Components::Transform::Component>(*handle))
                    *savedPos = xfr->Position;

                if (r.all_of<ECS::Components::Hierarchy::Component>(*handle))
                    ECS::Components::Hierarchy::Detach(r, *handle);
                r.destroy(*handle);
            }
        },
        .undo = [&scene, handle, savedName, savedPos]()
        {
            *handle = scene.CreateEntity(*savedName);
            auto& r = scene.GetRegistry();
            auto& xfr = r.get<ECS::Components::Transform::Component>(*handle);
            xfr.Position = *savedPos;
            r.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(*handle);
        },
    };

    EXPECT_TRUE(reg.valid(target));
    EXPECT_TRUE(history.Execute(std::move(deleteCmd)));
    EXPECT_FALSE(reg.valid(target));

    // Undo: entity recreated with original state
    EXPECT_TRUE(history.Undo());
    EXPECT_FALSE(IsNull(*handle));
    EXPECT_TRUE(reg.valid(*handle));

    auto* restoredName = reg.try_get<ECS::Components::NameTag::Component>(*handle);
    ASSERT_NE(restoredName, nullptr);
    EXPECT_EQ(restoredName->Name, expectedName);

    auto* restoredXf = reg.try_get<ECS::Components::Transform::Component>(*handle);
    ASSERT_NE(restoredXf, nullptr);
    EXPECT_FLOAT_EQ(restoredXf->Position.x, expectedPos.x);
    EXPECT_FLOAT_EQ(restoredXf->Position.y, expectedPos.y);
    EXPECT_FLOAT_EQ(restoredXf->Position.z, expectedPos.z);

    // Redo: delete again
    entt::entity restoredEntity = *handle;
    EXPECT_TRUE(history.Redo());
    EXPECT_FALSE(reg.valid(restoredEntity));
}

TEST(EntityCommands, DeleteChildPreservesHierarchyOnUndo)
{
    ECS::Scene scene;
    Core::CommandHistory history;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");
    ECS::Components::Hierarchy::Attach(reg, child, parent);

    auto* childH = reg.try_get<ECS::Components::Hierarchy::Component>(child);
    ASSERT_NE(childH, nullptr);
    EXPECT_TRUE(childH->Parent == parent);

    auto* parentH = reg.try_get<ECS::Components::Hierarchy::Component>(parent);
    ASSERT_NE(parentH, nullptr);
    EXPECT_TRUE(parentH->FirstChild == child);

    auto handle = std::make_shared<entt::entity>(child);
    auto savedParent = std::make_shared<entt::entity>(parent);
    auto savedName = std::make_shared<std::string>("Child");

    auto deleteCmd = Core::EditorCommand{
        .name = "Delete Child",
        .redo = [&scene, handle]()
        {
            auto& r = scene.GetRegistry();
            if (*handle != entt::null && r.valid(*handle))
            {
                if (r.all_of<ECS::Components::Hierarchy::Component>(*handle))
                    ECS::Components::Hierarchy::Detach(r, *handle);
                r.destroy(*handle);
            }
        },
        .undo = [&scene, handle, savedParent, savedName]()
        {
            *handle = scene.CreateEntity(*savedName);
            auto& r = scene.GetRegistry();
            if (*savedParent != entt::null && r.valid(*savedParent))
                ECS::Components::Hierarchy::Attach(r, *handle, *savedParent);
        },
    };

    EXPECT_TRUE(history.Execute(std::move(deleteCmd)));
    EXPECT_FALSE(reg.valid(child));

    parentH = reg.try_get<ECS::Components::Hierarchy::Component>(parent);
    ASSERT_NE(parentH, nullptr);
    EXPECT_TRUE(IsNull(parentH->FirstChild));

    // Undo: child recreated and re-attached
    EXPECT_TRUE(history.Undo());
    EXPECT_FALSE(IsNull(*handle));
    EXPECT_TRUE(reg.valid(*handle));

    childH = reg.try_get<ECS::Components::Hierarchy::Component>(*handle);
    ASSERT_NE(childH, nullptr);
    EXPECT_TRUE(childH->Parent == parent);

    parentH = reg.try_get<ECS::Components::Hierarchy::Component>(parent);
    ASSERT_NE(parentH, nullptr);
    EXPECT_TRUE(parentH->FirstChild == *handle);
}
