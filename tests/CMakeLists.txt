find_package(Vulkan REQUIRED)

set(OWL_FOUNDATION_TEST_SOURCES
    foundation/AssertTests.cpp
    foundation/BuildInfoTests.cpp
    foundation/CommandLineTests.cpp
    foundation/LogTests.cpp
)

set(OWL_PLATFORM_TEST_SOURCES
    platform/PlatformTests.cpp
    platform/WindowTests.cpp
)

set(OWL_VULKAN_TEST_SOURCES
    vulkan/DeviceSelectionTests.cpp
    vulkan/DeviceTests.cpp
    vulkan/FrameTests.cpp
    vulkan/SwapchainTests.cpp
    vulkan/VulkanBootstrapTests.cpp
)

add_executable(OwlUnitTests)

target_sources(OwlUnitTests PRIVATE
    ${OWL_FOUNDATION_TEST_SOURCES}
    ${OWL_PLATFORM_TEST_SOURCES}
    ${OWL_VULKAN_TEST_SOURCES}
)

source_group("foundation" FILES ${OWL_FOUNDATION_TEST_SOURCES})
source_group("platform" FILES ${OWL_PLATFORM_TEST_SOURCES})
source_group("vulkan" FILES ${OWL_VULKAN_TEST_SOURCES})

target_include_directories(OwlUnitTests PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/vulkan/src"
    "${PROJECT_SOURCE_DIR}/engine/platform/src/sdl"
)

target_link_libraries(OwlUnitTests PRIVATE
    Catch2::Catch2WithMain
    OwlFoundation
    OwlPlatform
    OwlVulkan
    Vulkan::Vulkan
)

owl_enable_warnings(OwlUnitTests)
# Keep discovered executable paths separate for each Visual Studio configuration.
catch_discover_tests(OwlUnitTests DISCOVERY_MODE PRE_TEST)
