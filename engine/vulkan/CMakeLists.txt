find_package(Vulkan REQUIRED)

add_library(OwlVulkan STATIC)

target_sources(OwlVulkan PRIVATE
    include/owl/vulkan/VulkanTriangle.h
    src/VulkanDevice.cpp
    src/VulkanDevice.h
    src/VulkanDeviceSelection.cpp
    src/VulkanDeviceSelection.h
    src/VulkanFrame.cpp
    src/VulkanFrame.h
    src/VulkanInstance.cpp
    src/VulkanInstance.h
    src/VulkanPresentationSupport.h
    src/VulkanSurface.cpp
    src/VulkanSurface.h
    src/VulkanSwapchain.cpp
    src/VulkanSwapchain.h
    src/VulkanTriangle.cpp
    src/VulkanTrianglePipeline.cpp
    src/VulkanTrianglePipeline.h
)

target_include_directories(OwlVulkan
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/platform/src/sdl"
)

target_link_libraries(OwlVulkan PRIVATE
    OwlFoundation
    OwlPlatform
    Vulkan::Vulkan
)

target_compile_definitions(OwlVulkan PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:OWL_ENABLE_VULKAN_VALIDATION=1>
)

owl_enable_warnings(OwlVulkan)
