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

owl_enable_warnings(OwlSandbox)

if(OWL_BUILD_TESTS)
    add_test(
        NAME OwlSandbox.Help
        COMMAND $<TARGET_FILE:OwlSandbox> --help
    )
    set_tests_properties(OwlSandbox.Help PROPERTIES
        PASS_REGULAR_EXPRESSION "--sample clear"
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
        NAME OwlSandbox.InvalidArguments
        COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:OwlSandbox>
            -DEXPECTED_EXIT_CODE=2
            -DPROGRAM_ARGUMENT=--unknown
            -P ${PROJECT_SOURCE_DIR}/tests/cmake/ExpectExitCode.cmake
    )
endif()
