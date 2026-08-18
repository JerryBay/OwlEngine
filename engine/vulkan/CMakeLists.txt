find_package(Vulkan REQUIRED)

add_library(OwlVulkan STATIC)

target_sources(OwlVulkan PRIVATE
    src/VulkanTriangle.cpp
)

target_include_directories(OwlVulkan PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(OwlVulkan PRIVATE
    OwlFoundation
    OwlPlatform
    Vulkan::Vulkan
)

owl_enable_warnings(OwlVulkan)
