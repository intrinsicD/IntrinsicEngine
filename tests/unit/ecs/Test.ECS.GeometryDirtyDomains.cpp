#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
namespace DirtyTags = Extrinsic::ECS::Components::DirtyTags;

TEST(ECSGeometryDirtyDomains, MarkVertexPositionsDirtyStampsOnlyThatTag)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    DirtyTags::MarkVertexPositionsDirty(raw, entity);

    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexAttributes>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyEdgeTopology>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyFaceTopology>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::GpuDirty>(entity));
}

TEST(ECSGeometryDirtyDomains, MarkVertexAttributesDirtyStampsOnlyThatTag)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    DirtyTags::MarkVertexAttributesDirty(raw, entity);

    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyVertexAttributes>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyEdgeTopology>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyFaceTopology>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::GpuDirty>(entity));
}

TEST(ECSGeometryDirtyDomains, MarkEdgeTopologyDirtyStampsOnlyThatTag)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    DirtyTags::MarkEdgeTopologyDirty(raw, entity);

    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexAttributes>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyEdgeTopology>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyFaceTopology>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::GpuDirty>(entity));
}

TEST(ECSGeometryDirtyDomains, MarkFaceTopologyDirtyStampsOnlyThatTag)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    DirtyTags::MarkFaceTopologyDirty(raw, entity);

    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexAttributes>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyEdgeTopology>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyFaceTopology>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::GpuDirty>(entity));
}

TEST(ECSGeometryDirtyDomains, MarkGpuDirtyStampsOnlyThatTag)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    DirtyTags::MarkGpuDirty(raw, entity);

    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexAttributes>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyEdgeTopology>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyFaceTopology>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::GpuDirty>(entity));
}

TEST(ECSGeometryDirtyDomains, StampingHelpersAreIdempotent)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    DirtyTags::MarkVertexPositionsDirty(raw, entity);
    DirtyTags::MarkVertexPositionsDirty(raw, entity);
    DirtyTags::MarkGpuDirty(raw, entity);
    DirtyTags::MarkGpuDirty(raw, entity);

    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::GpuDirty>(entity));
}

TEST(ECSGeometryDirtyDomains, DirtyDomainsCoexistIndependentlyOnSameEntity)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    DirtyTags::MarkVertexPositionsDirty(raw, entity);
    DirtyTags::MarkVertexAttributesDirty(raw, entity);
    DirtyTags::MarkEdgeTopologyDirty(raw, entity);
    DirtyTags::MarkFaceTopologyDirty(raw, entity);
    DirtyTags::MarkGpuDirty(raw, entity);

    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyVertexAttributes>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyEdgeTopology>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyFaceTopology>(entity));
    EXPECT_TRUE(raw.all_of<DirtyTags::GpuDirty>(entity));
}

TEST(ECSGeometryDirtyDomains, StampingOneEntityDoesNotAffectOthers)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle source = scene.Create();
    const EntityHandle bystander = scene.Create();

    DirtyTags::MarkVertexPositionsDirty(raw, source);
    DirtyTags::MarkGpuDirty(raw, source);

    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyVertexPositions>(source));
    EXPECT_TRUE(raw.all_of<DirtyTags::GpuDirty>(source));
    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexPositions>(bystander));
    EXPECT_FALSE(raw.all_of<DirtyTags::GpuDirty>(bystander));
}

TEST(ECSGeometryDirtyDomains, ConsumerCanDrainTagsAfterStamping)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    DirtyTags::MarkVertexPositionsDirty(raw, entity);
    DirtyTags::MarkGpuDirty(raw, entity);

    ASSERT_TRUE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    ASSERT_TRUE(raw.all_of<DirtyTags::GpuDirty>(entity));

    // Mirror the runtime-side drain: remove tags after observation. ECS
    // helpers never own this step; the drain pattern matches
    // `Runtime.RenderExtraction::ExtractAndSubmit` for `DirtyTransform`.
    raw.remove<DirtyTags::DirtyVertexPositions>(entity);
    raw.remove<DirtyTags::GpuDirty>(entity);

    EXPECT_FALSE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
    EXPECT_FALSE(raw.all_of<DirtyTags::GpuDirty>(entity));

    // Re-stamping after a drain is supported and idempotent.
    DirtyTags::MarkVertexPositionsDirty(raw, entity);
    EXPECT_TRUE(raw.all_of<DirtyTags::DirtyVertexPositions>(entity));
}
