execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE OWL_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE OWL_GIT_REVISION_RESULT
)

if(NOT OWL_GIT_REVISION_RESULT EQUAL 0 OR OWL_GIT_REVISION STREQUAL "")
    set(OWL_GIT_REVISION "unknown")
endif()

if(CMAKE_GENERATOR_PLATFORM)
    set(OWL_TARGET_ARCHITECTURE "${CMAKE_GENERATOR_PLATFORM}")
else()
    set(OWL_TARGET_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(OWL_FOUNDATION_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

configure_file(
    src/BuildInfoConfig.h.in
    "${OWL_FOUNDATION_GENERATED_DIR}/owl/foundation/generated/BuildInfoConfig.h"
    @ONLY
)

find_package(spdlog CONFIG REQUIRED)

add_library(OwlFoundation STATIC)

target_sources(OwlFoundation PRIVATE
    src/Assert.cpp
    src/BuildInfo.cpp
    src/CommandLine.cpp
    src/Log.cpp
)

target_include_directories(OwlFoundation
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${OWL_FOUNDATION_GENERATED_DIR}"
)

target_compile_definitions(OwlFoundation
    PUBLIC
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:OWL_ENABLE_ASSERTS=1>
    PRIVATE
        OWL_BUILD_CONFIGURATION="$<CONFIG>"
)

target_link_libraries(OwlFoundation PRIVATE spdlog::spdlog)

owl_enable_warnings(OwlFoundation)
