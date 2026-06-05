# Fixture: physics links only core and geometry.
add_library(FixturePhysics INTERFACE)
target_link_libraries(FixturePhysics
    INTERFACE
    ExtrinsicCore
    ExtrinsicGeometry
)
