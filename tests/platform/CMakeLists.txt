add_executable(OwlPlatformTests)

target_sources(OwlPlatformTests PRIVATE
    PlatformTests.cpp
    WindowTests.cpp
)

target_link_libraries(OwlPlatformTests PRIVATE
    Catch2::Catch2WithMain
    OwlPlatform
)

owl_enable_warnings(OwlPlatformTests)
catch_discover_tests(OwlPlatformTests)
