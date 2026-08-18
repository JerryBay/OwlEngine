add_executable(OwlSandbox)

target_sources(OwlSandbox PRIVATE
    src/main.cpp
    src/SmokeSample.cpp
)

target_link_libraries(OwlSandbox PRIVATE
    OwlFoundation
    OwlPlatform
)

set_target_properties(OwlSandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

owl_enable_warnings(OwlSandbox)

if(OWL_BUILD_TESTS)
    add_test(
        NAME OwlSandbox.Help
        COMMAND $<TARGET_FILE:OwlSandbox> --help
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
