target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.Events.cppm
)

target_include_directories(${target_name} PUBLIC .)
