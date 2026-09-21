// Narrow property-domain fixtures for runtime method contract tests.
// Topology construction and property lookup compile in PointDomainFixture.cpp.
#pragma once

#include <entt/entity/fwd.hpp>

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.GeometryProperty.Types;
import Geometry.Properties;

namespace Intrinsic::Tests
{
    // Callers select one of the eight canonical point domains.
    entt::entity MakePointDomainSource(Extrinsic::ECS::Scene::Registry& scene,
                                     Extrinsic::Runtime::GeometryElementDomain domain);

    // The entity must already contain the requested domain.
    Geometry::PropertySet& PointDomainProperties(Extrinsic::ECS::Scene::Registry& scene,
                                                entt::entity entity,
                                                Extrinsic::Runtime::GeometryElementDomain domain);
}
