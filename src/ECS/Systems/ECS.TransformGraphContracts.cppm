module;

export module ECS:TransformGraphContracts;

import :ComponentForwardDecls;
import Core.FrameGraph;
import Core.Hash;
import Core.SystemFeatureCatalog;

export namespace ECS::Systems::Transform::Contracts
{
    using Core::Hash::operator""_id;

    inline constexpr auto PassName = Runtime::SystemFeatureCatalog::PassNames::TransformUpdate;

    inline void DeclareTransformPass(Core::FrameGraphBuilder& builder)
    {
        builder.Read<Components::Transform::Component>();
        builder.Read<Components::Hierarchy::Component>();
        builder.Write<Components::Transform::WorldMatrix>();
        builder.Write<Components::Transform::IsDirtyTag>();
        builder.Write<Components::Transform::WorldUpdatedTag>();
        builder.Signal("TransformUpdate"_id);
    }
}
