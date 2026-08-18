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

add_executable(OwlUnitTests)

target_sources(OwlUnitTests PRIVATE
    ${OWL_FOUNDATION_TEST_SOURCES}
    ${OWL_PLATFORM_TEST_SOURCES}
)

source_group("foundation" FILES ${OWL_FOUNDATION_TEST_SOURCES})
source_group("platform" FILES ${OWL_PLATFORM_TEST_SOURCES})

target_link_libraries(OwlUnitTests PRIVATE
    Catch2::Catch2WithMain
    OwlFoundation
    OwlPlatform
)

owl_enable_warnings(OwlUnitTests)
catch_discover_tests(OwlUnitTests)
