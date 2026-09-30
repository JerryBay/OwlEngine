find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(OwlVulkan STATIC)

target_sources(OwlVulkan PRIVATE
    include/owl/vulkan/VulkanTriangle.h
    src/VulkanAllocator.cpp
    src/VulkanAllocator.h
    src/VulkanMemory.cpp
    src/VulkanMemory.h
    src/VulkanDevice.cpp
    src/VulkanDevice.h
    src/VulkanDeviceSelection.cpp
    src/VulkanDeviceSelection.h
    src/VulkanBuffer.cpp
    src/VulkanBuffer.h
    src/VulkanBufferUpload.cpp
    src/VulkanBufferUpload.h
    src/VulkanFrame.cpp
    src/VulkanFrame.h
    src/VulkanInstance.cpp
    src/VulkanInstance.h
    src/VulkanImage.cpp
    src/VulkanImage.h
    src/VulkanImageUpload.cpp
    src/VulkanImageUpload.h
    src/VulkanImageView.cpp
    src/VulkanImageView.h
    src/VulkanMipChain.cpp
    src/VulkanMipChain.h
    src/VulkanSampler.cpp
    src/VulkanSampler.h
    src/VulkanShaderBinary.cpp
    src/VulkanShaderBinary.h
    src/VulkanPresentationSupport.h
    src/VulkanSurface.cpp
    src/VulkanSurface.h
    src/VulkanSwapchain.cpp
    src/VulkanSwapchain.h
    src/VulkanTriangle.cpp
    src/VulkanTrianglePipeline.cpp
    src/VulkanTrianglePipeline.h
    src/VulkanTexturedQuadPipeline.cpp
    src/VulkanTexturedQuadPipeline.h
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
    GPUOpen::VulkanMemoryAllocator
)

target_compile_definitions(OwlVulkan PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:OWL_ENABLE_VULKAN_VALIDATION=1>
)

owl_enable_warnings(OwlVulkan)
