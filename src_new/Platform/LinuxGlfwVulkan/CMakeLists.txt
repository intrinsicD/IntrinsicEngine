target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        PRIVATE
        Platform.Window.cpp
)

target_include_directories(${target_name} PUBLIC .)