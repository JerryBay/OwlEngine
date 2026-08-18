find_package(SDL3 CONFIG REQUIRED)

add_library(OwlPlatform STATIC)

target_sources(OwlPlatform PRIVATE
    src/sdl/Platform.cpp
    src/sdl/Window.cpp
)

target_include_directories(OwlPlatform PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(OwlPlatform
    PRIVATE
        OwlFoundation
        SDL3::SDL3
)

owl_enable_warnings(OwlPlatform)
