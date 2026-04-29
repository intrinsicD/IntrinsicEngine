target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Backends.Null.cppm
        PRIVATE
        Backends.Null.cpp
        Backends.Null.Bindless.cpp
        Backends.Null.Profiler.cpp
        Backends.Null.Transfer.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicRHI
        PRIVATE
        ExtrinsicCore
)
