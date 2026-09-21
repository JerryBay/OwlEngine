# vcpkg's native deployer skips delay imports; the PowerShell deployer includes them.
# This directory-local setting affects only the Sandbox target.
set(VCPKG_USE_LEGACY_APPLOCAL ON)
add_executable(OwlSandbox)

target_sources(OwlSandbox PRIVATE
    src/main.cpp
    src/ClearSample.cpp
    src/ClearSample.h
    src/SmokeSample.cpp
    src/SmokeSample.h
    src/TriangleSample.cpp
    src/TriangleSample.h
    src/VulkanSample.cpp
    src/VulkanSample.h
)

target_link_libraries(OwlSandbox PRIVATE
    OwlFoundation
    OwlPlatform
    OwlVulkan
)

if(MSVC)
    # Keep help and smoke usable without loading the Vulkan runtime.
    target_link_options(OwlSandbox PRIVATE /DELAYLOAD:vulkan-1.dll)
    target_link_libraries(OwlSandbox PRIVATE delayimp)
endif()

set_target_properties(OwlSandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

# Run for every requested Sandbox build, including asset-only updates and deleted outputs.
# copy_if_different leaves unchanged deployment files untouched.
add_custom_target(OwlSandboxAssets
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/assets/m1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/m1/triangle.vert.spv"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/m1/triangle.frag.spv"
        "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/assets/m1"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/m1/triangle.vert.spv"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/m1/triangle.frag.spv"
    VERBATIM
)
add_dependencies(OwlSandbox OwlSandboxAssets)

owl_enable_warnings(OwlSandbox)

if(OWL_BUILD_TESTS)
    add_test(
        NAME OwlSandbox.Help
        COMMAND $<TARGET_FILE:OwlSandbox> --help
    )
    set_tests_properties(OwlSandbox.Help PROPERTIES
        PASS_REGULAR_EXPRESSION "--sample triangle"
    )

    add_test(
        NAME OwlSandbox.ClearPlatformFailure
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=owl-test-unavailable
            ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:OwlSandbox>
            -DEXPECTED_EXIT_CODE=3
            -DPROGRAM_ARGUMENT=--sample
            -DPROGRAM_EXTRA_ARGUMENTS=clear
            -P ${PROJECT_SOURCE_DIR}/tests/cmake/ExpectExitCode.cmake
    )

    add_test(
        NAME OwlSandbox.TrianglePlatformFailure
        COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEODRIVER=owl-test-unavailable
            ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:OwlSandbox>
            -DEXPECTED_EXIT_CODE=3
            -DPROGRAM_ARGUMENT=--sample
            -DPROGRAM_EXTRA_ARGUMENTS=triangle
            -P ${PROJECT_SOURCE_DIR}/tests/cmake/ExpectExitCode.cmake
    )

    add_test(
        NAME OwlSandbox.InvalidArguments
        COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:OwlSandbox>
            -DEXPECTED_EXIT_CODE=2
            -DPROGRAM_ARGUMENT=--unknown
            -P ${PROJECT_SOURCE_DIR}/tests/cmake/ExpectExitCode.cmake
    )
endif()
