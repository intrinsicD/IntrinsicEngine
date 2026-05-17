# Fixture: ExtrinsicCore links only third-party or sibling-core targets.
add_library(FixtureCore INTERFACE)
target_link_libraries(FixtureCore INTERFACE ExtrinsicCore)
